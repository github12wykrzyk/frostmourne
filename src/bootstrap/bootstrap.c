#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "bootstrap.h"

#ifndef FM_VERSION\n#define FM_VERSION L"0.1.0-test1"\n#endif\n#ifdef FM_DEBUG_BUILD\n#define FM_BUILD_FLAVOR L"debug"\n#else\n#define FM_BUILD_FLAVOR L"release"\n#endif
static volatile LONG g_state = 0; /* 0=attached, 1=initializing, 2=initialized, 3=stopped */
static DWORD g_pid = 0;
static FILETIME g_attached_at;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    (void)instance;
    if (reason == DLL_PROCESS_ATTACH) {
        g_pid = GetCurrentProcessId();
        GetSystemTimeAsFileTime(&g_attached_at);
        /* Preserve thread notifications for statically linked CRT. */
    }
    return TRUE;
}

/* All directory creation, file I/O and formatting occur OUTSIDE DllMain. */
static DWORD write_diagnostic(DWORD elapsed_ms) {
    WCHAR dir[MAX_PATH], sub[MAX_PATH], file[MAX_PATH], line[512];
    WCHAR *base;
    SYSTEMTIME now;
    HANDLE out;
    DWORD written, len, n;
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return ERROR_PATH_NOT_FOUND;
    if (lstrlenW(dir) > MAX_PATH - 80) return ERROR_BUFFER_OVERFLOW;
    base = dir + lstrlenW(dir);
    if (wcscpy_s(base, MAX_PATH - (size_t)(base - dir), L"\\Frostmourne")) return ERROR_BUFFER_OVERFLOW;
    if (!CreateDirectoryW(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return GetLastError();
    if (swprintf_s(sub, MAX_PATH, L"%ls\\logs", dir) < 0) return ERROR_BUFFER_OVERFLOW;
    if (!CreateDirectoryW(sub, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return GetLastError();
    if (swprintf_s(file, MAX_PATH, L"%ls\\bootstrap-%lu.log", sub, (unsigned long)g_pid) < 0) return ERROR_BUFFER_OVERFLOW;
    GetSystemTime(&now);
    if (swprintf_s(line, sizeof(line)/sizeof(line[0]), L"%04u-%02u-%02uT%02u:%02u:%02uZ event=INITIALIZED module=FrostmourneBootstrap version=%ls build=%ls abi=%lu.%lu pid=%lu attach_filetime=%08lx%08lx initialization_ms=%lu\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, FM_VERSION,
        (unsigned long)FM_ABI_MAJOR, (unsigned long)FM_ABI_MINOR, (unsigned long)g_pid,
        (unsigned long)g_attached_at.dwHighDateTime, (unsigned long)g_attached_at.dwLowDateTime,
        (unsigned long)elapsed_ms) < 0) return ERROR_BUFFER_OVERFLOW;
    out = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE) return GetLastError();
    /* ASCII diagnostic text encoded as UTF-16LE, without an implicit BOM. */
    len = (DWORD)(lstrlenW(line) * sizeof(WCHAR));
    if (!WriteFile(out, line, len, &written, NULL) || written != len) {
        DWORD error = GetLastError();
        CloseHandle(out);
        return error ? error : ERROR_WRITE_FAULT;
    }
    if (!FlushFileBuffers(out)) { DWORD error = GetLastError(); CloseHandle(out); return error; }
    CloseHandle(out);
    return ERROR_SUCCESS;
}

DWORD WINAPI Frostmourne_GetAbi(LPVOID unused) {
    (void)unused;
    return (FM_ABI_MAJOR << 16) | FM_ABI_MINOR;
}

DWORD WINAPI Frostmourne_Initialize(LPVOID data) {
    FM_INIT_PACKET *packet = (FM_INIT_PACKET *)data;
    DWORD start, error;
    if (!packet) return FM_INIT_ERROR;
    packet->result = FM_INIT_ERROR;
    packet->win32_error = ERROR_INVALID_PARAMETER;
    packet->observed_pid = GetCurrentProcessId();
    packet->elapsed_ms = 0;
    if (packet->size != sizeof(*packet) || packet->abi_major != FM_ABI_MAJOR ||
        packet->nonce != FM_PACKET_NONCE || packet->target_pid != g_pid) return FM_INIT_ERROR;
    if (InterlockedCompareExchange(&g_state, 1, 0) != 0) {
        packet->win32_error = ERROR_ALREADY_EXISTS;
        return FM_INIT_ERROR;
    }
    start = GetTickCount();
    error = write_diagnostic(GetTickCount() - start);
    packet->elapsed_ms = GetTickCount() - start;
    if (error != ERROR_SUCCESS) {
        packet->win32_error = error;
        InterlockedExchange(&g_state, 0);
        return FM_INIT_ERROR;
    }
    packet->win32_error = ERROR_SUCCESS;
    packet->result = FM_INIT_MAGIC;
    InterlockedExchange(&g_state, 2);
    return FM_INIT_MAGIC;
}

DWORD WINAPI Frostmourne_Shutdown(LPVOID unused) {
    (void)unused;
    if (InterlockedCompareExchange(&g_state, 3, 2) != 2) return FM_INIT_ERROR;
    return FM_INIT_MAGIC;
}
