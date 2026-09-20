#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include "bootstrap.h"
#include "../auto_pickpocket/auto_pickpocket.h"
#include "../auto_pickpocket/native_adapter.h"

#ifndef FM_VERSION
#define FM_VERSION L"0.1.0-test1"
#endif
#ifdef FM_DEBUG_BUILD
#define FM_BUILD_FLAVOR L"debug"
#else
#define FM_BUILD_FLAVOR L"release"
#endif
static volatile LONG g_state = 0; /* 0=attached, 1=initializing, 2=initialized, 3=stopped */
static DWORD g_pid = 0;
static FILETIME g_attached_at;
static FM_AP_ENGINE g_auto_pickpocket; /* Native bridge starts separately after ABI initialization. */

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
    if (swprintf_s(line, sizeof(line)/sizeof(line[0]), L"%04u-%02u-%02uT%02u:%02u:%02uZ event=INITIALIZED module=FrostmourneBootstrap version=%ls build=%ls abi=%lu.%lu pid=%lu attach_filetime=%08lx%08lx initialization_ms=%lu auto_pickpocket_core=READY adapter=INITIALIZING gameplay_actions=PENDING\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, FM_VERSION, FM_BUILD_FLAVOR,
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
    fm_ap_init(&g_auto_pickpocket); /* Native adapter starts after packet/ABI validation. */
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
    if (!fm_ap_native_start(&g_auto_pickpocket)) {
        packet->result = FM_INIT_ERROR;
        packet->win32_error = ERROR_NOT_ENOUGH_MEMORY;
        InterlockedExchange(&g_state, 0);
        return FM_INIT_ERROR;
    }
    return FM_INIT_MAGIC;
}

DWORD WINAPI Frostmourne_Shutdown(LPVOID unused) {
    (void)unused;
    if (InterlockedCompareExchange(&g_state, 3, 2) != 2) return FM_INIT_ERROR;
    fm_ap_native_stop();
    return FM_INIT_MAGIC;
}

/* STARTING is not gameplay-ready. READY means a live in-game snapshot
 * and native cast entry gate passed; it does not confirm successful loot. */
DWORD WINAPI Frostmourne_GetAutoPickpocketStatus(LPVOID unused) {
    (void)unused;
    if (InterlockedCompareExchange(&g_state, 2, 2) != 2) return 0;
    return fm_ap_native_status();
}

/* No execution of private WoW functions: only 8-byte registration entries and
 * 12-byte function prologues from the exact audited 12340 image are sampled.
 * ReadProcessMemory fails cleanly for an inaccessible/modified address. */
typedef struct FM_PROBE_EXPECTED {
    DWORD table_va, name_va, function_va;
    BYTE first_bytes[12];
} FM_PROBE_EXPECTED;
static const FM_PROBE_EXPECTED g_probe_expected[4] = {
    {0x00AD2560u, 0x00A1EED8u, 0x00611DF0u,
     {0x55,0x8B,0xEC,0x81,0xEC,0xC8,0x02,0x00,0x00,0x57,0x8B,0x7D}},
    {0x00AD2568u, 0x00A1EEC8u, 0x00612090u,
     {0x55,0x8B,0xEC,0x81,0xEC,0xBC,0x02,0x00,0x00,0x56,0x8B,0x75}},
    {0x00AD22D0u, 0x00A1F450u, 0x0060E630u,
     {0x55,0x8B,0xEC,0x83,0xEC,0x54,0x57,0x8B,0x7D,0x08,0x6A,0x01}},
    {0x00ACCDF0u, 0x00A0AC08u, 0x0053E060u,
     {0x55,0x8B,0xEC,0x81,0xEC,0xD0,0x02,0x00,0x00,0x56,0x8B,0x75}}
};
static int fm_read_exact(DWORD va, void *out, SIZE_T bytes) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(ULONG_PTR)va,
                             out, bytes, &got) && got == bytes;
}
DWORD WINAPI Frostmourne_ProbeInterruptBindings(LPVOID data) {
    FM_INTERRUPT_PROBE_PACKET *p = (FM_INTERRUPT_PROBE_PACKET *)data;
    DWORD i;
    if (!p) return FM_INIT_ERROR;
    p->result = FM_INIT_ERROR;
    p->observed_pid = GetCurrentProcessId();
    p->image_base = (DWORD)(ULONG_PTR)GetModuleHandleW(NULL);
    p->registration_mask = 0;
    p->prologue_mask = 0;
    p->win32_error = ERROR_INVALID_PARAMETER;
    if (p->size != sizeof(*p) || p->target_pid != g_pid ||
        p->observed_pid != g_pid ||
        InterlockedCompareExchange(&g_state, 2, 2) != 2 ||
        p->image_base != 0x00400000u) return FM_INIT_ERROR;
    p->win32_error = ERROR_SUCCESS;
    for (i=0; i<4; ++i) {
        DWORD pair[2];
        BYTE prologue[12];
        if (fm_read_exact(g_probe_expected[i].table_va, pair, sizeof(pair)) &&
            pair[0] == g_probe_expected[i].name_va &&
            pair[1] == g_probe_expected[i].function_va)
            p->registration_mask |= (1u << i);
        if (fm_read_exact(g_probe_expected[i].function_va, prologue, sizeof(prologue)) &&
            memcmp(prologue, g_probe_expected[i].first_bytes, sizeof(prologue)) == 0)
            p->prologue_mask |= (1u << i);
    }
    if (p->registration_mask != FM_INTERRUPT_PROBE_MASK ||
        p->prologue_mask != FM_INTERRUPT_PROBE_MASK) {
        p->win32_error = ERROR_INVALID_DATA;
        return FM_INIT_ERROR;
    }
    p->result = FM_INTERRUPT_PROBE_MAGIC;
    return FM_INTERRUPT_PROBE_MAGIC;
}
