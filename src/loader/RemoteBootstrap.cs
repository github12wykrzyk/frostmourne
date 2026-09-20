using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace FrostmourneGui {
    // Diagnostic only: one explicit x86 Windows load into the just-launched, fingerprint-gated Wow.exe.
    // No hooks, stealth, proxy DLL, anti-cheat intervention or process-elevation fallback.
    internal static class RemoteBootstrap {
        const uint Access = 0x0002 | 0x0400 | 0x0008 | 0x0010 | 0x0020;
        const uint MemCommitReserve = 0x3000, MemRelease = 0x8000, PageReadWrite = 0x04;
        const uint WaitObject0 = 0, WaitTimeout = 258, TimeoutMs = 15000;
        const uint Magic = 0xF1057A01, ErrorMagic = 0xF1057AEE, Nonce = 0x35A12340;
        const uint Abi = 0x00010000;

        [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protection);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint freeType);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool WriteProcessMemory(IntPtr process, IntPtr destination, byte[] source, UIntPtr size, out UIntPtr written);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool ReadProcessMemory(IntPtr process, IntPtr source, [Out] byte[] buffer, UIntPtr size, out UIntPtr read);
        [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stackSize, IntPtr entry, IntPtr argument, uint flags, out uint threadId);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle, uint timeout);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeThread(IntPtr thread, out uint code);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true, EntryPoint="LoadLibraryExW")]
        static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
        [DllImport("kernel32.dll", CharSet=CharSet.Ansi, SetLastError=true, ExactSpelling=true)]
        static extern IntPtr GetProcAddress(IntPtr module, string export);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool FreeLibrary(IntPtr module);

        static Win32Exception Win32(string stage) {
            return new Win32Exception(Marshal.GetLastWin32Error(), stage);
        }
        static uint Address(IntPtr address) { return unchecked((uint)address.ToInt32()); }
        static IntPtr Ptr(uint value) { return new IntPtr(unchecked((int)value)); }

        static ProcessModule ModuleAt(Process process, uint address) {
            foreach (ProcessModule m in process.Modules) {
                ulong baseAddress = Address(m.BaseAddress);
                if ((ulong)address >= baseAddress && (ulong)address < baseAddress + (ulong)m.ModuleMemorySize) return m;
            }
            throw new InvalidOperationException("Nie odnaleziono modulu zawierajacego wskaznik WinAPI");
        }

        static IntPtr RemoteOsFunction(Process target, string name) {
            IntPtr localKernel = LoadLibraryEx("kernel32.dll", IntPtr.Zero, 0);
            if (localKernel == IntPtr.Zero) throw Win32("LoadLibraryEx kernel32");
            try {
                IntPtr function = GetProcAddress(localKernel, name);
                if (function == IntPtr.Zero) throw Win32("GetProcAddress " + name);
                using (Process self = Process.GetCurrentProcess()) {
                    ProcessModule owner = ModuleAt(self, Address(function));
                    uint offset = checked(Address(function) - Address(owner.BaseAddress));
                    using (Process remote = Process.GetProcessById(target.Id)) {
                        foreach (ProcessModule m in remote.Modules) {
                            if (!String.Equals(Path.GetFullPath(m.FileName), Path.GetFullPath(owner.FileName),
                                               StringComparison.OrdinalIgnoreCase)) continue;
                            if ((ulong)offset >= (ulong)m.ModuleMemorySize)
                                throw new InvalidOperationException("Rozny rozmiar obrazow Windows DLL");
                            return Ptr(checked(Address(m.BaseAddress) + offset));
                        }
                    }
                    throw new InvalidOperationException("Nie znaleziono identycznej systemowej DLL " + owner.ModuleName + " w procesie Wow.exe");
                }
            } finally { FreeLibrary(localKernel); }
        }

        // LoadLibraryEx(DONT_RESOLVE_DLL_REFERENCES) maps an export directory locally without
        // running DllMain or importing/executing the DLL in the supervisor.
        static uint ExportRva(string dll, string name) {
            IntPtr mapping = LoadLibraryEx(dll, IntPtr.Zero, 1);
            if (mapping == IntPtr.Zero) throw Win32("Mapowanie eksportow " + name);
            try {
                IntPtr address = GetProcAddress(mapping, name);
                if (address == IntPtr.Zero) throw Win32("Brak eksportu " + name);
                long offset = (long)Address(address) - (long)Address(mapping);
                if (offset <= 0 || offset >= 16 * 1024 * 1024)
                    throw new InvalidDataException("Eksport poza oczekiwanym obrazem DLL " + name);
                return (uint)offset;
            } finally { FreeLibrary(mapping); }
        }

        static uint Invoke(IntPtr process, IntPtr entry, IntPtr argument, string stage, Action<string> log) {
            uint tid;
            IntPtr thread = CreateRemoteThread(process, IntPtr.Zero, UIntPtr.Zero, entry, argument, 0, out tid);
            if (thread == IntPtr.Zero) throw Win32("CreateRemoteThread " + stage);
            try {
                log("ETAP " + stage + " thread=" + tid + " WAIT");
                uint wait = WaitForSingleObject(thread, TimeoutMs);
                if (wait == WaitTimeout)
                    throw new TimeoutException(stage + ": timeout 15s; stan NIEZNANY, bez ponawiania");
                if (wait != WaitObject0) throw Win32("WaitForSingleObject " + stage + " status=" + wait);
                uint code;
                if (!GetExitCodeThread(thread, out code)) throw Win32("GetExitCodeThread " + stage);
                return code;
            } finally { CloseHandle(thread); }
        }

        static void Write(IntPtr process, IntPtr address, byte[] data, string stage) {
            UIntPtr written;
            if (!WriteProcessMemory(process, address, data, (UIntPtr)data.Length, out written) ||
                written.ToUInt64() != (ulong)data.Length)
                throw Win32("WriteProcessMemory " + stage);
        }
        static byte[] Read(IntPtr process, IntPtr address, int size, string stage) {
            byte[] data = new byte[size];
            UIntPtr received;
            if (!ReadProcessMemory(process, address, data, (UIntPtr)size, out received) ||
                received.ToUInt64() != (ulong)size)
                throw Win32("ReadProcessMemory " + stage);
            return data;
        }
        static IntPtr Allocate(IntPtr process, int size, string stage) {
            IntPtr pointer = VirtualAllocEx(process, IntPtr.Zero, (UIntPtr)size, MemCommitReserve, PageReadWrite);
            if (pointer == IntPtr.Zero) throw Win32("VirtualAllocEx " + stage);
            return pointer;
        }

        internal static string LoadAndInitialize(Process target, string expectedExe, string dll,
                                                 bool kickTrialEnabled, uint maxRemainingMs, Action<string> log) {
            if (IntPtr.Size != 4) throw new InvalidOperationException("Loader musi dzialac jako proces x86");
            if (target.HasExited) throw new InvalidOperationException("Wow.exe zakonczyl sie przed ladowaniem");
            if (!String.Equals(Path.GetFullPath(target.MainModule.FileName), Path.GetFullPath(expectedExe),
                               StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Inny obraz uruchomionego procesu niz wybrany Wow.exe");
            if (target.MainModule.ModuleMemorySize == 0) throw new InvalidOperationException("Brak obrazu uruchomionego klienta");
            IntPtr loadLibrary = RemoteOsFunction(target, "LoadLibraryW");
            IntPtr process = OpenProcess(Access, false, (uint)target.Id);
            if (process == IntPtr.Zero) throw Win32("OpenProcess PID=" + target.Id);
            IntPtr pathPointer = IntPtr.Zero, packetPointer = IntPtr.Zero, probePointer = IntPtr.Zero, kickPointer = IntPtr.Zero;
            bool pathSafe = true, packetSafe = true, probeSafe = true, kickSafe = true;
            try {
                byte[] pathData = Encoding.Unicode.GetBytes(Path.GetFullPath(dll) + "\0");
                pathPointer = Allocate(process, pathData.Length, "DLL path");
                Write(process, pathPointer, pathData, "DLL path");
                log("ETAP zaladowanie_DLL=START pid=" + target.Id + " api=LoadLibraryW");
                uint loadedBase;
                try {
                    loadedBase = Invoke(process, loadLibrary, pathPointer, "LoadLibraryW", log);
                } catch (TimeoutException) {
                    pathSafe = false; // Never release an argument buffer while a remote thread may still be using it.
                    throw;
                }
                if (loadedBase == 0)
                    throw new InvalidOperationException("LoadLibraryW w Wow.exe zwrocilo NULL; blad Win32 procesu docelowego niedostepny");
                ProcessModule module = null;
                using (Process inspect = Process.GetProcessById(target.Id)) {
                    foreach (ProcessModule candidate in inspect.Modules) {
                        if (Address(candidate.BaseAddress) != loadedBase) continue;
                        module = candidate;
                        break;
                    }
                }
                if (module == null || !String.Equals(Path.GetFullPath(module.FileName), Path.GetFullPath(dll),
                            StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Nie potwierdzono tozsamosci zdalnie zaladowanej DLL");
                log("ETAP zaladowanie_DLL=PASS pid=" + target.Id + " base=0x" + loadedBase.ToString("X8"));

                uint abi = ExportRva(dll, "_Frostmourne_GetAbi@4");
                uint init = ExportRva(dll, "_Frostmourne_Initialize@4");
                uint observedAbi = Invoke(process, Ptr(checked(loadedBase + abi)), IntPtr.Zero, "Frostmourne_GetAbi", log);
                if (observedAbi != Abi)
                    throw new InvalidOperationException("ABI niezgodne: otrzymano 0x" + observedAbi.ToString("X8"));
                log("ETAP inicjalizacja_ABI=PASS wersja=1.0 pid=" + target.Id);

                byte[] packet = new byte[32];
                Buffer.BlockCopy(BitConverter.GetBytes((uint)32), 0, packet, 0, 4);
                Buffer.BlockCopy(BitConverter.GetBytes((uint)1), 0, packet, 4, 4);
                Buffer.BlockCopy(BitConverter.GetBytes(Nonce), 0, packet, 8, 4);
                Buffer.BlockCopy(BitConverter.GetBytes((uint)target.Id), 0, packet, 12, 4);
                packetPointer = Allocate(process, packet.Length, "INIT packet");
                Write(process, packetPointer, packet, "INIT packet");
                uint status;
                try {
                    status = Invoke(process, Ptr(checked(loadedBase + init)), packetPointer, "Frostmourne_Initialize", log);
                } catch (TimeoutException) {
                    packetSafe = false;
                    throw;
                }
                byte[] answer = Read(process, packetPointer, packet.Length, "INIT packet");
                uint reportedResult = BitConverter.ToUInt32(answer, 16);
                uint win32 = BitConverter.ToUInt32(answer, 20);
                uint pid = BitConverter.ToUInt32(answer, 24);
                log("ETAP inicjalizacja wynik=0x" + status.ToString("X8") + " packet=0x" +
                    reportedResult.ToString("X8") + " win32=" + win32 + " observed_pid=" + pid);
                if (status == ErrorMagic || status != Magic || reportedResult != Magic || win32 != 0)
                    throw new InvalidOperationException("Frostmourne_Initialize FAIL: win32=" + win32);
                if (pid != (uint)target.Id)
                    throw new InvalidOperationException("PID w DLL rozni sie od uruchomionego Wow.exe");
                log("ETAP potwierdzenie_PID_wewnatrz_DLL=PASS pid=" + pid);
                uint apProbe = ExportRva(dll, "_Frostmourne_GetAutoPickpocketStatus@4");
                uint apStatus = Invoke(process, Ptr(checked(loadedBase + apProbe)), IntPtr.Zero,
                    "Frostmourne_GetAutoPickpocketStatus", log);
                if (apStatus != 0x41500002 && apStatus != 0x41500003)
                    throw new InvalidOperationException("Auto Pickpocket native adapter FAIL: 0x" + apStatus.ToString("X8"));
                log("ETAP auto_pickpocket_native=PASS status=" + (apStatus == 0x41500003 ? "READY" : "STARTING") +
                    " client_cast=IMPLEMENTED gameplay_result=NOT_TESTED_IN_GAME pid=" + pid);
                // Read-only binding probe: samples only known Lua registration pairs and
                // function prologues. It neither calls WoW functions nor reads live units.
                uint probeRva = ExportRva(dll, "_Frostmourne_ProbeInterruptBindings@4");
                byte[] probePacket = new byte[32];
                Buffer.BlockCopy(BitConverter.GetBytes((uint)32), 0, probePacket, 0, 4);
                Buffer.BlockCopy(BitConverter.GetBytes((uint)target.Id), 0, probePacket, 4, 4);
                probePointer = Allocate(process, probePacket.Length, "Interrupt probe packet");
                Write(process, probePointer, probePacket, "Interrupt probe packet");
                uint probeResult;
                try {
                    probeResult = Invoke(process, Ptr(checked(loadedBase + probeRva)), probePointer,
                                         "Frostmourne_ProbeInterruptBindings", log);
                } catch (TimeoutException) {
                    probeSafe = false;
                    throw;
                }
                byte[] observed = Read(process, probePointer, probePacket.Length, "Interrupt probe packet");
                uint packetResult = BitConverter.ToUInt32(observed, 8);
                uint probePid = BitConverter.ToUInt32(observed, 12);
                uint imageBase = BitConverter.ToUInt32(observed, 16);
                uint registrations = BitConverter.ToUInt32(observed, 20);
                uint prologues = BitConverter.ToUInt32(observed, 24);
                uint probeError = BitConverter.ToUInt32(observed, 28);
                log("ETAP interrupt_bindings pid=" + probePid + " image=0x" + imageBase.ToString("X8") +
                    " registrations=0x" + registrations.ToString("X2") +
                    " prologues=0x" + prologues.ToString("X2") + " win32=" + probeError +
                    " actions=WYLACZONE live_cast=NOT_READ");
                if (probeResult != 0xF17A1234 || packetResult != probeResult ||
                    probePid != (uint)target.Id || imageBase != 0x00400000 ||
                    registrations != 0xF || prologues != 0xF || probeError != 0)
                    throw new InvalidOperationException("READ-ONLY interrupt binding probe FAIL; check stage/bitmasks in log");
                log("ETAP interrupt_bindings=PASS pid=" + probePid +
                    " sampled=4_registration_pairs_and_4_function_prologues kick_trial_requested=" + kickTrialEnabled);
                if (maxRemainingMs < 200 || maxRemainingMs > 3000)
                    throw new InvalidDataException("Kick: czas maksymalny poza 200..3000 ms");
                uint kickRva = ExportRva(dll, "_Frostmourne_StartAutoKick@4");
                byte[] kickPacket = new byte[36];
                Buffer.BlockCopy(BitConverter.GetBytes((uint)36), 0, kickPacket, 0, 4);
                Buffer.BlockCopy(BitConverter.GetBytes((uint)target.Id), 0, kickPacket, 4, 4);
                Buffer.BlockCopy(BitConverter.GetBytes(kickTrialEnabled ? 1u : 0u), 0, kickPacket, 8, 4);
                Buffer.BlockCopy(BitConverter.GetBytes(maxRemainingMs), 0, kickPacket, 12, 4);
                Buffer.BlockCopy(BitConverter.GetBytes(150u), 0, kickPacket, 16, 4);
                kickPointer = Allocate(process, kickPacket.Length, "Kick packet");
                Write(process, kickPointer, kickPacket, "Kick packet");
                uint kickReturn;
                try {
                    kickReturn = Invoke(process, Ptr(checked(loadedBase + kickRva)), kickPointer,
                                        "Frostmourne_StartAutoKick", log);
                } catch (TimeoutException) { kickSafe = false; throw; }
                byte[] kickAnswer = Read(process, kickPointer, kickPacket.Length, "Kick packet");
                uint kickResult = BitConverter.ToUInt32(kickAnswer, 20);
                uint kickError = BitConverter.ToUInt32(kickAnswer, 24);
                uint kickPid = BitConverter.ToUInt32(kickAnswer, 28);
                uint hook = BitConverter.ToUInt32(kickAnswer, 32);
                log("ETAP kick_bridge result=0x" + kickReturn.ToString("X8") + " packet=0x" +
                    kickResult.ToString("X8") + " win32=" + kickError + " pid=" + kickPid +
                    " hook_installed=" + hook + " kick_trial_enabled=" + kickTrialEnabled);
                if (kickReturn != 0xF17AC176 || kickResult != kickReturn ||
                    kickError != 0 || kickPid != (uint)target.Id ||
                    hook != (kickTrialEnabled ? 1u : 0u))
                    throw new InvalidOperationException("Kick native bridge FAIL; zostaw gre, zachowaj logi.");
                return "PASS: DLL PID=" + pid + ", Kick " +
                    (kickTrialEnabled ? "HOOK ZAINSTALOWANY (SKUTECZNOSC NIEPOTWIERDZONA)" : "OFF") +
                    "; Auto Pickpocket adapter STARTING/READY";
            } finally {
                if (kickPointer != IntPtr.Zero && kickSafe)
                    VirtualFreeEx(process, kickPointer, UIntPtr.Zero, MemRelease);
                if (probePointer != IntPtr.Zero && probeSafe)
                    VirtualFreeEx(process, probePointer, UIntPtr.Zero, MemRelease);
                if (packetPointer != IntPtr.Zero && packetSafe)
                    VirtualFreeEx(process, packetPointer, UIntPtr.Zero, MemRelease);
                if (pathPointer != IntPtr.Zero && pathSafe)
                    VirtualFreeEx(process, pathPointer, UIntPtr.Zero, MemRelease);
                CloseHandle(process);
            }
        }
    }
}
