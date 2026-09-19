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
