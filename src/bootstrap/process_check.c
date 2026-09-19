#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

/* Read-only observation. No remote threads, executable memory access, DLL loading or escalation. */
#define EXPECTED_SIZE 7704216LL
#define EXPECTED_SHA L"edba72ae4188bda717eec73b733aab9cb2f4ab7d4a1e22b44e60d81743648ebd"
#define REPORT_CAP 8192

static void add(WCHAR *out, size_t cap, const WCHAR *fmt, ...) {
    WCHAR part[1200];
    va_list ap;
    va_start(ap, fmt);
    if (SUCCEEDED(StringCchVPrintfW(part, ARRAYSIZE(part), fmt, ap)))
        StringCchCatW(out, cap, part);
    va_end(ap);
}
static DWORD hash_file(const WCHAR *path, WCHAR digest[65], LONGLONG *size_out) {
    HANDLE f = INVALID_HANDLE_VALUE;
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    BYTE buffer[65536], value[32], mz[2];
    DWORD size = sizeof(value), read = 0, err = ERROR_SUCCESS, i;
    LARGE_INTEGER length;
    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return GetLastError();
    if (!GetFileSizeEx(f, &length)) { err = GetLastError(); goto cleanup; }
    *size_out = length.QuadPart;
    if (!ReadFile(f, mz, sizeof(mz), &read, NULL)) { err = GetLastError(); goto cleanup; }
    if (read != sizeof(mz) || mz[0] != 'M' || mz[1] != 'Z') {
        err = ERROR_BAD_EXE_FORMAT; goto cleanup;
    }
    if (SetFilePointer(f, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
        GetLastError() != ERROR_SUCCESS) { err = GetLastError(); goto cleanup; }
    if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        err = GetLastError(); goto cleanup;
    }
    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        err = GetLastError(); goto cleanup;
    }
    for (;;) {
        if (!ReadFile(f, buffer, sizeof(buffer), &read, NULL)) {
            err = GetLastError(); goto cleanup;
        }
        if (!read) break;
        if (!CryptHashData(hash, buffer, read, 0)) {
            err = GetLastError(); goto cleanup;
        }
    }
    if (!CryptGetHashParam(hash, HP_HASHVAL, value, &size, 0)) {
        err = GetLastError(); goto cleanup;
    }
    if (size != 32) { err = ERROR_INVALID_DATA; goto cleanup; }
    for (i = 0; i < size; ++i)
        swprintf_s(digest + i * 2, 65 - i * 2, L"%02x", (unsigned)value[i]);
cleanup:
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    CloseHandle(f);
    return err;
}
static DWORD check(DWORD *matched_pid, WCHAR *out, size_t cap) {
    HANDLE snap = INVALID_HANDLE_VALUE, proc = NULL;
    PROCESSENTRY32W entry;
    MODULEENTRY32W module;
    WCHAR path[32768], digest[65] = L"";
    DWORD pid = 0, count = 0, len, err, module_err = ERROR_SUCCESS;
    BOOL local_wow64 = FALSE, remote_wow64 = FALSE, present = FALSE;
    LONGLONG bytes = 0;
    *matched_pid = 0;
    add(out, cap, L"FROSTMOURNE - odczytowa diagnostyka procesu Wow.exe\r\n");
    add(out, cap, L"Program nie wstrzykuje DLL i nie uruchamia kodu w grze.\r\n\r\n");
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        add(out, cap, L"BLAD: lista procesow, Win32=%lu.\r\n", GetLastError());
        return 10;
    }
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snap, &entry)) {
        err = GetLastError();
        CloseHandle(snap);
        add(out, cap, L"BLAD: odczyt listy procesow, Win32=%lu.\r\n", err);
        return 11;
    }
    do {
        if (_wcsicmp(entry.szExeFile, L"Wow.exe") == 0) {
            ++count;
            pid = entry.th32ProcessID;
            add(out, cap, L"Znaleziony Wow.exe: PID=%lu\r\n", pid);
        }
    } while (Process32NextW(snap, &entry));
    err = GetLastError();
    CloseHandle(snap);
    if (err != ERROR_NO_MORE_FILES) {
        add(out, cap, L"BLAD: enumeracja procesow, Win32=%lu.\r\n", err);
        return 12;
    }
    if (!count) {
        add(out, cap, L"ETAP 2: BRAK - proces Wow.exe nie jest uruchomiony.\r\n");
        return 20;
    }
    if (count != 1) {
        add(out, cap, L"ETAP 2: NIEJEDNOZNACZNY - %lu procesow Wow.exe. "
            L"Nie wybieram procesu automatycznie.\r\n", count);
        return 21;
    }
    *matched_pid = pid;
    proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!proc) {
        add(out, cap, L"BLAD: OpenProcess PID=%lu, Win32=%lu; brak eskalacji uprawnien.\r\n",
            pid, GetLastError());
        return 22;
    }
    if (WaitForSingleObject(proc, 0) != WAIT_TIMEOUT) {
        add(out, cap, L"BLAD: proces PID=%lu zakonczyl sie przed odczytem.\r\n", pid);
        CloseHandle(proc);
        return 23;
    }
    len = ARRAYSIZE(path);
    if (!QueryFullProcessImageNameW(proc, 0, path, &len)) {
        err = GetLastError();
        add(out, cap, L"BLAD: QueryFullProcessImageNameW, Win32=%lu.\r\n", err);
        CloseHandle(proc);
        return 24;
    }
    add(out, cap, L"Pelna sciezka: %ls\r\n", path);
    if (!IsWow64Process(GetCurrentProcess(), &local_wow64) ||
        !IsWow64Process(proc, &remote_wow64)) {
        err = GetLastError();
        add(out, cap, L"BLAD: IsWow64Process, Win32=%lu.\r\n", err);
        CloseHandle(proc);
        return 25;
    }
    add(out, cap, L"Architektura: tester=x86 (WOW64=%u); proces WOW64=%u.\r\n",
        (unsigned)local_wow64, (unsigned)remote_wow64);
    if (local_wow64 != remote_wow64) {
        add(out, cap, L"BLAD: niezgodna architektura procesu i testera.\r\n");
        CloseHandle(proc);
        return 26;
    }
    err = hash_file(path, digest, &bytes);
    if (err) {
        add(out, cap, L"BLAD: odczyt SHA256 obrazu na dysku, Win32=%lu.\r\n", err);
        CloseHandle(proc);
        return 27;
    }
    add(out, cap, L"Plik na dysku: rozmiar=%I64d, SHA256=%ls\r\n", bytes, digest);
    if (bytes != EXPECTED_SIZE || _wcsicmp(digest, EXPECTED_SHA) != 0) {
        add(out, cap, L"ETAP 2: ODRZUCONY - fingerprint nie odpowiada referencji 3.3.5a/12340.\r\n");
        CloseHandle(proc);
        return 28;
    }
    add(out, cap, L"Fingerprint na dysku: zgodny z referencja klienta x86 3.3.5a/12340.\r\n");
    /* Never open the target with PROCESS_VM_WRITE, PROCESS_VM_OPERATION or PROCESS_CREATE_THREAD. */
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        module_err = GetLastError();
        add(out, cap, L"Lista modulow: BLAD Win32=%lu (nie oznacza braku DLL).\r\n", module_err);
    } else {
        ZeroMemory(&module, sizeof(module));
        module.dwSize = sizeof(module);
        if (!Module32FirstW(snap, &module)) {
            module_err = GetLastError();
            add(out, cap, L"Lista modulow: BLAD Win32=%lu.\r\n", module_err);
        } else {
            do {
                if (_wcsicmp(module.szModule, L"FrostmourneBootstrap.dll") == 0) {
                    present = TRUE;
                    add(out, cap, L"WYKRYTO FrostmourneBootstrap.dll: %ls\r\n", module.szExePath);
                }
            } while (Module32NextW(snap, &module));
            module_err = GetLastError();
            if (module_err != ERROR_NO_MORE_FILES)
                add(out, cap, L"BLAD podczas enumeracji modulow: Win32=%lu.\r\n", module_err);
        }
        CloseHandle(snap);
    }
    if (WaitForSingleObject(proc, 0) != WAIT_TIMEOUT) {
        add(out, cap, L"BLAD: proces zakonczyl sie w czasie diagnostyki; wynik niewazny.\r\n");
        CloseHandle(proc);
        return 29;
    }
    CloseHandle(proc);
    add(out, cap, L"\r\nETAP 2: POTWIERDZONY PID=%lu i fingerprint pliku obrazu na dysku.\r\n", pid);
    if (present)
        add(out, cap, L"ETAP 3: DLL jest na liscie modulow, lecz wykonanie ABI NIEPOTWIERDZONE.\r\n");
    else if (module_err == ERROR_NO_MORE_FILES)
        add(out, cap, L"ETAP 3: brak DLL na liscie modulow; inicjalizacja NIEPOTWIERDZONA.\r\n");
    else
        add(out, cap, L"ETAP 3: obecnosc DLL nieustalona; inicjalizacja NIEPOTWIERDZONA.\r\n");
    add(out, cap, L"Nie znaleziono autoryzowanego interfejsu rozszerzen klienta; "
        L"zdalne ladowanie nie zostalo podjete.\r\n");
    return 0;
}
static DWORD save_log(const WCHAR *message, DWORD pid, WCHAR *destination, size_t chars) {
    WCHAR base[MAX_PATH], dir[MAX_PATH];
    DWORD len, written;
    HANDLE file;
    WORD bom = 0xFEFF;
    len = GetEnvironmentVariableW(L"LOCALAPPDATA", base, ARRAYSIZE(base));
    if (!len || len >= ARRAYSIZE(base)) return ERROR_PATH_NOT_FOUND;
    if (FAILED(StringCchPrintfW(dir, ARRAYSIZE(dir), L"%ls\\Frostmourne", base)))
        return ERROR_BUFFER_OVERFLOW;
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return GetLastError();
    if (FAILED(StringCchPrintfW(base, ARRAYSIZE(base), L"%ls\\logs", dir)))
        return ERROR_BUFFER_OVERFLOW;
    if (!CreateDirectoryW(base, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return GetLastError();
    if (FAILED(StringCchPrintfW(destination, chars,
          L"%ls\\process-check-%lu-%lu.log", base, pid, GetTickCount())))
        return ERROR_BUFFER_OVERFLOW;
    file = CreateFileW(destination, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    if (!WriteFile(file, &bom, sizeof(bom), &written, NULL) || written != sizeof(bom) ||
        !WriteFile(file, message, (DWORD)(wcslen(message) * sizeof(WCHAR)), &written, NULL) ||
        written != (DWORD)(wcslen(message) * sizeof(WCHAR))) {
        DWORD err = GetLastError();
        CloseHandle(file);
        return err ? err : ERROR_WRITE_FAULT;
    }
    if (!FlushFileBuffers(file)) {
        DWORD err = GetLastError();
        CloseHandle(file);
        return err;
    }
    CloseHandle(file);
    return ERROR_SUCCESS;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    WCHAR message[REPORT_CAP] = L"", path[MAX_PATH] = L"", ci[8];
    DWORD pid = 0, status, log_error;
    BOOL noninteractive;
    (void)instance; (void)previous; (void)command; (void)show;
    status = check(&pid, message, ARRAYSIZE(message));
    add(message, ARRAYSIZE(message), L"\r\nKod diagnostyczny: %lu.\r\n", status);
    log_error = save_log(message, pid, path, ARRAYSIZE(path));
    if (log_error)
        add(message, ARRAYSIZE(message), L"BLAD zapisu logu: Win32=%lu.\r\n", log_error);
    else
        add(message, ARRAYSIZE(message), L"Log UTF-16: %ls\r\n", path);
    noninteractive = GetEnvironmentVariableW(L"CI", ci, ARRAYSIZE(ci)) > 0;
    if (!noninteractive)
        MessageBoxW(NULL, message, L"FROSTMOURNE - diagnostyka Wow.exe",
                    MB_OK | (status == 0 ? MB_ICONINFORMATION : MB_ICONWARNING) | MB_TOPMOST);
    /* CI passes only if no Wow.exe exists: verifies the expected no-target error branch. */
    if (noninteractive && status == 20 && !log_error) return 0;
    return status ? (int)status : (log_error ? 30 : 0);
}
