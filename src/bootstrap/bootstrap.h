#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define FM_ABI_MAJOR 1u
#define FM_ABI_MINOR 0u
#define FM_INIT_MAGIC 0xF1057A01u
#define FM_INIT_ERROR 0xF1057AEEu
#define FM_PACKET_NONCE 0x35A12340u

/* Versioned wire ABI; caller-owned memory allocated inside the target process. */
typedef struct FM_INIT_PACKET {
    DWORD size;
    DWORD abi_major;
    DWORD nonce;
    DWORD target_pid;
    DWORD result;
    DWORD win32_error;
    DWORD observed_pid;
    DWORD elapsed_ms;
} FM_INIT_PACKET;

/* Exports are stdcall; one LPVOID argument, DWORD exit status, no CRT objects. */
__declspec(dllexport) DWORD WINAPI Frostmourne_Initialize(LPVOID packet);
__declspec(dllexport) DWORD WINAPI Frostmourne_Shutdown(LPVOID unused);
__declspec(dllexport) DWORD WINAPI Frostmourne_GetAbi(LPVOID unused);

/* 0x41500002=bridge starting, 0x41500003=verified local ready, 0x41500004=bridge failed. */
#define FM_AP_CORE_INERT 0x41500001u
__declspec(dllexport) DWORD WINAPI Frostmourne_GetAutoPickpocketStatus(LPVOID unused);

/* Read-only exact-image in-process check; does NOT query live unit/cast data. */
#define FM_INTERRUPT_PROBE_MAGIC 0xF17A1234u
#define FM_INTERRUPT_PROBE_MASK 0x0000000Fu
typedef struct FM_INTERRUPT_PROBE_PACKET {
    DWORD size, target_pid, result, observed_pid;
    DWORD image_base, registration_mask, prologue_mask, win32_error;
} FM_INTERRUPT_PROBE_PACKET;
__declspec(dllexport) DWORD WINAPI Frostmourne_ProbeInterruptBindings(LPVOID packet);

/* Experimental target-only native Kick bridge, called once after the verified
 * bootstrap has initialized. This is NOT a gameplay success confirmation.
 * No arbitrary Wow.exe version or alternate in-process source is supported. */
#define FM_KICK_START_MAGIC 0xF17AC176u
typedef struct FM_KICK_PACKET {
    DWORD size, target_pid, enabled, max_remaining_ms, safety_margin_ms;
    DWORD result, win32_error, observed_pid, hook_installed;
} FM_KICK_PACKET;
__declspec(dllexport) DWORD WINAPI Frostmourne_StartAutoKick(LPVOID packet);
