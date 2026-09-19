/* Single-attempt x86 diagnostic loader for an authorized, isolated local Wow.exe.
   Reuses the existing fingerprint + process/module diagnostics verbatim. */
#define FM_LOCAL_INPROCESS 1
#define wWinMain FM_ReadOnlyEntryUnused
#include "process_check.c"
#undef wWinMain
#include "bootstrap.h"
#include "frostmourne_loader_hash.h"
#include <stdint.h>

#define FM_WAIT_MS 10000u
#define FM_PROCESS_RIGHTS (PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD | \
    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE)

static DWORD fm_find_module(DWORD pid, const WCHAR *name, WCHAR *path,
                            size_t path_cap, HMODULE *base, BOOL *found) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W m;
    DWORD err;
    *found = FALSE;
    *base = NULL;
    if (snap == INVALID_HANDLE_VALUE) return GetLastError();
    ZeroMemory(&m, sizeof(m));
    m.dwSize = sizeof(m);
    if (!Module32FirstW(snap, &m)) {
        err = GetLastError();
        CloseHandle(snap);
        return err;
    }
    do {
        if (_wcsicmp(m.szModule, name) == 0) {
            *found = TRUE;
            *base = m.hModule;
            if (path && FAILED(StringCchCopyW(path, path_cap, m.szExePath))) {
                CloseHandle(snap);
                return ERROR_BUFFER_OVERFLOW;
            }
            break;
        }
    } while (Module32NextW(snap, &m));
    err = GetLastError();
    CloseHandle(snap);
    if (*found || err == ERROR_NO_MORE_FILES) return ERROR_SUCCESS;
    return err;
}

static DWORD fm_execute(HANDLE process, LPTHREAD_START_ROUTINE entry, LPVOID data,
                        DWORD *exit_code, BOOL *completed) {
    HANDLE thread;
    DWORD wait, err;
    *completed = FALSE;
    thread = CreateRemoteThread(process, NULL, 0, entry, data, 0, NULL);
    if (!thread) return GetLastError();
    wait = WaitForSingleObject(thread, FM_WAIT_MS);
    if (wait != WAIT_OBJECT_0) {
        err = wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();
        CloseHandle(thread);
        return err;
    }
    *completed = TRUE;
    if (!GetExitCodeThread(thread, exit_code)) {
        err = GetLastError();
        CloseHandle(thread);
        return err;
    }
    CloseHandle(thread);
    return ERROR_SUCCESS;
}

/* Resolve a callable address in an equivalent module of the target process.
   This also handles Windows forwarding LoadLibraryW to KERNELBASE.dll. */
static DWORD fm_remote_loadlibrary(DWORD pid, LPTHREAD_START_ROUTINE *address) {
    HMODULE owner = NULL, remote = NULL;
    WCHAR path[MAX_PATH], *name;
    BOOL present;
    uintptr_t delta;
    DWORD err;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)(void *)LoadLibraryW, &owner)) return GetLastError();
    if (!GetModuleFileNameW(owner, path, ARRAYSIZE(path))) return GetLastError();
    name = wcsrchr(path, L'\\');
    if (!name) return ERROR_INVALID_DATA;
    delta = (uintptr_t)(void *)LoadLibraryW - (uintptr_t)owner;
    err = fm_find_module(pid, name + 1, NULL, 0, &remote, &present);
    if (err) return err;
    if (!present) return ERROR_MOD_NOT_FOUND;
    *address = (LPTHREAD_START_ROUTINE)((uintptr_t)remote + delta);
    return ERROR_SUCCESS;
}

static DWORD fm_dll_path(WCHAR path[MAX_PATH]) {
    WCHAR *name;
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!len || len >= MAX_PATH) return ERROR_BUFFER_OVERFLOW;
    name = wcsrchr(path, L'\\');
    if (!name) return ERROR_INVALID_DATA;
    if (FAILED(StringCchCopyW(name + 1, MAX_PATH - (size_t)(name + 1 - path),
                               L"FrostmourneBootstrap.dll"))) return ERROR_BUFFER_OVERFLOW;
    return ERROR_SUCCESS;
}

static DWORD fm_verify_target(HANDLE process, WCHAR *out, size_t cap) {
    WCHAR path[32768], digest[65];
    DWORD n = ARRAYSIZE(path), err;
    LONGLONG size = 0;
    BOOL local32 = FALSE, target32 = FALSE;
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) return ERROR_PROCESS_ABORTED;
    if (!QueryFullProcessImageNameW(process, 0, path, &n)) return GetLastError();
    if (!IsWow64Process(GetCurrentProcess(), &local32) ||
        !IsWow64Process(process, &target32)) return GetLastError();
    if (local32 != target32) return ERROR_BAD_EXE_FORMAT;
    err = hash_file(path, digest, &size);
    if (err) return err;
    if (size != EXPECTED_SIZE || _wcsicmp(digest, EXPECTED_SHA)) return ERROR_INVALID_IMAGE_HASH;
    add(out, cap, L"Ponowna kontrola uchwytu procesu i SHA256 obrazu: PASS.\r\n");
    return ERROR_SUCCESS;
}

static int fm_finish(DWORD code, DWORD pid, WCHAR *out) {
    WCHAR path[MAX_PATH] = L"", ci[8] = L"";
    DWORD log_error;
    add(out, REPORT_CAP, L"\r\nKod diagnostyczny: %lu.\r\n", code);
    log_error = save_log(out, pid, path, ARRAYSIZE(path));
    if (log_error) add(out, REPORT_CAP, L"BLAD zapisu logu Win32=%lu.\r\n", log_error);
    else add(out, REPORT_CAP, L"Log kontrolera: %ls\r\n", path);
    if (!GetEnvironmentVariableW(L"CI", ci, ARRAYSIZE(ci)))
        MessageBoxW(NULL, out, L"FROSTMOURNE - lokalny test DLL w Wow.exe",
                    MB_OK | MB_TOPMOST | (code ? MB_ICONWARNING : MB_ICONINFORMATION));
    /* CI validates only the no-game branch, not in-process operation. */
    if (GetEnvironmentVariableW(L"CI", ci, ARRAYSIZE(ci)) && code == 20 && !log_error) return 0;
    return code ? (int)code : (log_error ? 90 : 0);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    WCHAR out[REPORT_CAP] = L"", dll[MAX_PATH] = L"", digest[65] = L"";
    WCHAR existing_path[MAX_PATH] = L"", remote_path[MAX_PATH] = L"";
    DWORD pid = 0, code, err, thread_exit = 0, written = 0;
    DWORD init_rva;
    BOOL existing = FALSE, completed = FALSE, remote_found = FALSE;
    BOOL init_completed = FALSE;
    LONGLONG dll_size = 0;
    HANDLE process = NULL;
    HMODULE remote_module = NULL, local_module = NULL, verify_module = NULL;
    LPTHREAD_START_ROUTINE loadlibrary_address = NULL;
    LPVOID remote_string = NULL, remote_packet = NULL;
    FARPROC init_export = NULL, abi_export = NULL;
    FM_INIT_PACKET packet;
    (void)instance; (void)previous; (void)command; (void)show;
    add(out, REPORT_CAP, L"FROSTMOURNE: jednorazowy lokalny test in-process x86.\r\n");
    code = check(&pid, out, ARRAYSIZE(out));
    if (code) return fm_finish(code, pid, out);
    /* The read-only prerequisite alone is never a success result for this tool. */
    code = 40;
    err = fm_dll_path(dll);
    if (err) { add(out, REPORT_CAP, L"BLAD: sciezka DLL Win32=%lu.\r\n", err); goto finish; }
    err = hash_file(dll, digest, &dll_size);
    if (err) { add(out, REPORT_CAP, L"BLAD: odczyt DLL Win32=%lu.\r\n", err); goto finish; }
    if (dll_size != FM_EXPECTED_DLL_SIZE || _wcsicmp(digest, FM_EXPECTED_DLL_SHA256)) {
        add(out, REPORT_CAP, L"BLAD: SHA256/rozmiar DLL nie zgadza sie z kompilacja x86.\r\n");
        goto finish;
    }
    add(out, REPORT_CAP, L"Integralnosc DLL: PASS SHA256=%ls.\r\n", digest);
    err = fm_find_module(pid, L"FrostmourneBootstrap.dll", existing_path,
                         ARRAYSIZE(existing_path), &remote_module, &existing);
    if (err) { add(out, REPORT_CAP, L"BLAD: lista modulow przed testem Win32=%lu.\r\n", err); goto finish; }
    if (existing) {
        add(out, REPORT_CAP, L"STOP: DLL juz istnieje w procesie: %ls. Brak drugiej proby.\r\n", existing_path);
        code = 41; goto finish;
    }
    if (!GetEnvironmentVariableW(L"CI", existing_path, ARRAYSIZE(existing_path))) {
        if (MessageBoxW(NULL, L"Tylko dla kontrolowanej, lokalnej sesji klienta x86.\n"
                L"Nie uruchamiaj na publicznym serwerze ani przy aktywnych zabezpieczeniach, "
                L"ktorych mialoby to wymagac obejscia.\n\n"
                L"Wykonac JEDNA probe zaladowania diagnostycznej DLL do Wow.exe?",
                L"FROSTMOURNE - potwierdzenie testu", MB_YESNO | MB_ICONWARNING | MB_TOPMOST) != IDYES) {
            add(out, REPORT_CAP, L"Test anulowany. Proces gry nie zostal zmieniony.\r\n");
            code = 42; goto finish;
        }
    }
    process = OpenProcess(FM_PROCESS_RIGHTS, FALSE, pid);
    if (!process) {
        add(out, REPORT_CAP, L"BLAD: dostep do procesu Win32=%lu; bez eskalacji uprawnien.\r\n",
            GetLastError()); code = 43; goto finish;
    }
    err = fm_verify_target(process, out, ARRAYSIZE(out));
    if (err) { add(out, REPORT_CAP, L"BLAD: ponowna weryfikacja procesu Win32=%lu.\r\n", err);
               code = 44; goto finish; }
    err = fm_remote_loadlibrary(pid, &loadlibrary_address);
    if (err) { add(out, REPORT_CAP, L"BLAD: adres loadera Windows Win32=%lu.\r\n", err);
               code = 45; goto finish; }
    local_module = LoadLibraryExW(dll, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!local_module) {
        add(out, REPORT_CAP, L"BLAD: odczyt lokalnej tablicy eksportow Win32=%lu.\r\n", GetLastError());
        code = 46; goto finish;
    }
    init_export = GetProcAddress(local_module, "_Frostmourne_Initialize@4");
    abi_export = GetProcAddress(local_module, "_Frostmourne_GetAbi@4");
    if (!init_export || !abi_export) {
        add(out, REPORT_CAP, L"BLAD: brak wymaganych eksportow ABI.\r\n");
        code = 47; goto finish;
    }
    init_rva = (DWORD)((uintptr_t)init_export - (uintptr_t)local_module);
    remote_string = VirtualAllocEx(process, NULL, (wcslen(dll) + 1) * sizeof(WCHAR),
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_string) {
        add(out, REPORT_CAP, L"BLAD: argument DLL Win32=%lu.\r\n", GetLastError());
        code = 48; goto finish;
    }
    if (!WriteProcessMemory(process, remote_string, dll,
            (wcslen(dll) + 1) * sizeof(WCHAR), NULL)) {
        add(out, REPORT_CAP, L"BLAD: zapis argumentu sciezki Win32=%lu.\r\n", GetLastError());
        code = 49; goto finish;
    }
    add(out, REPORT_CAP, L"Rozpoczynam pojedyncza probe LoadLibraryW w PID=%lu.\r\n", pid);
    err = fm_execute(process, loadlibrary_address, remote_string, &thread_exit, &completed);
    if (!completed) remote_string = NULL; /* Never free memory a remote thread might still use. */
    if (err || !thread_exit) {
        add(out, REPORT_CAP, L"BLAD: LoadLibraryW Win32=%lu result=0x%08lx (0 = brak modulu).\r\n",
            err, thread_exit); code = 50; goto finish;
    }
    err = fm_find_module(pid, L"FrostmourneBootstrap.dll", remote_path,
                         ARRAYSIZE(remote_path), &verify_module, &remote_found);
    if (err || !remote_found || verify_module != (HMODULE)(uintptr_t)thread_exit ||
        _wcsicmp(dll, remote_path)) {
        add(out, REPORT_CAP, L"BLAD: brak zweryfikowanego modulu w Wow.exe Win32=%lu; "
            L"module=0x%08lx thread=0x%08lx path=%ls.\r\n",
            err, (DWORD)(uintptr_t)verify_module, thread_exit, remote_path);
        code = 51; goto finish;
    }
    add(out, REPORT_CAP, L"DLL jest zaladowana w Wow.exe: PID=%lu base=0x%08lx.\r\n",
        pid, thread_exit);
    ZeroMemory(&packet, sizeof(packet));
    packet.size = sizeof(packet);
    packet.abi_major = FM_ABI_MAJOR;
    packet.nonce = FM_PACKET_NONCE;
    packet.target_pid = pid;
    remote_packet = VirtualAllocEx(process, NULL, sizeof(packet),
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_packet || !WriteProcessMemory(process, remote_packet, &packet,
                                              sizeof(packet), NULL)) {
        add(out, REPORT_CAP, L"BLAD: bufor ABI Win32=%lu.\r\n", GetLastError());
        code = 52; goto finish;
    }
    err = fm_execute(process, (LPTHREAD_START_ROUTINE)((uintptr_t)verify_module + init_rva),
                     remote_packet, &thread_exit, &init_completed);
    if (!init_completed) remote_packet = NULL;
    if (err || !init_completed) {
        add(out, REPORT_CAP, L"BLAD: wywolanie ABI Win32=%lu (brak automatycznej ponownej proby).\r\n", err);
        code = 53; goto finish;
    }
    if (!ReadProcessMemory(process, remote_packet, &packet, sizeof(packet), NULL)) {
        add(out, REPORT_CAP, L"BLAD: odczyt odpowiedzi ABI Win32=%lu.\r\n", GetLastError());
        code = 54; goto finish;
    }
    add(out, REPORT_CAP, L"ABI: thread=0x%08lx result=0x%08lx pid=%lu error=%lu elapsed_ms=%lu.\r\n",
        thread_exit, packet.result, packet.observed_pid, packet.win32_error, packet.elapsed_ms);
    if (thread_exit != FM_INIT_MAGIC || packet.result != FM_INIT_MAGIC ||
        packet.win32_error != ERROR_SUCCESS || packet.observed_pid != pid) {
        add(out, REPORT_CAP, L"BLAD: odpowiedz ABI nie potwierdza inicjalizacji.\r\n");
        code = 55; goto finish;
    }
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        add(out, REPORT_CAP, L"BLAD: Wow.exe zakonczyl sie w trakcie testu.\r\n");
        code = 56; goto finish;
    }
    add(out, REPORT_CAP, L"ETAP 3: PASS - DLL w procesie Wow.exe i wykonanie "
                         L"Frostmourne_Initialize potwierdzone. Sprawdz stabilnosc gry.\r\n");
    add(out, REPORT_CAP, L"Log samej DLL: %%LOCALAPPDATA%%\\Frostmourne\\logs\\bootstrap-%lu.log\r\n", pid);
    code = 0;
finish:
    if (remote_packet && process) VirtualFreeEx(process, remote_packet, 0, MEM_RELEASE);
    if (remote_string && process) VirtualFreeEx(process, remote_string, 0, MEM_RELEASE);
    if (local_module) FreeLibrary(local_module);
    if (process) CloseHandle(process);
    return fm_finish(code, pid, out);
}
