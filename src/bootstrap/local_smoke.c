#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "bootstrap.h"

/* Local-only diagnostic: DLL is loaded into THIS tester, never another process. */
int wmain(void) {
    wchar_t path[MAX_PATH], *slash;
    HMODULE module;
    DWORD (WINAPI *initialize)(LPVOID);
    DWORD (WINAPI *shutdown)(LPVOID);
    DWORD (WINAPI *abi)(LPVOID);
    FM_INIT_PACKET packet;
    DWORD result;
    if (!GetModuleFileNameW(NULL,path,MAX_PATH)) return 10;
    slash=wcsrchr(path,L'\\');
    if (!slash || ((size_t)(slash-path)+sizeof(L"FrostmourneBootstrap.dll")/sizeof(wchar_t))>=MAX_PATH) return 11;
    wcscpy_s(slash+1,MAX_PATH-(slash+1-path),L"FrostmourneBootstrap.dll");
    module=LoadLibraryW(path);
    if (!module) {wprintf(L"LOCAL_LOAD_FAILED win32=%lu\n",GetLastError());return 12;}
    initialize=(DWORD(WINAPI *)(LPVOID))GetProcAddress(module,"_Frostmourne_Initialize@4");
    shutdown=(DWORD(WINAPI *)(LPVOID))GetProcAddress(module,"_Frostmourne_Shutdown@4");
    abi=(DWORD(WINAPI *)(LPVOID))GetProcAddress(module,"_Frostmourne_GetAbi@4");
    if (!initialize || !shutdown || !abi) {puts("ABI_EXPORT_MISSING");FreeLibrary(module);return 13;}
    if (abi(NULL)!=((FM_ABI_MAJOR<<16)|FM_ABI_MINOR)) {puts("ABI_MISMATCH");FreeLibrary(module);return 14;}
    ZeroMemory(&packet,sizeof(packet));
    packet.size=sizeof(packet);
    packet.abi_major=FM_ABI_MAJOR;
    packet.nonce=FM_PACKET_NONCE;
    packet.target_pid=GetCurrentProcessId();
    result=initialize(&packet);
    if (result!=FM_INIT_MAGIC || packet.result!=FM_INIT_MAGIC ||
        packet.win32_error!=ERROR_SUCCESS || packet.observed_pid!=GetCurrentProcessId() ||
        GetModuleHandleW(L"FrostmourneBootstrap.dll")!=module) {
        printf("LOCAL_INIT_FAILED result=%lu packet=%lu win32=%lu\n",
            (unsigned long)result,(unsigned long)packet.result,(unsigned long)packet.win32_error);
        FreeLibrary(module);return 15;
    }
    if(shutdown(NULL)!=FM_INIT_MAGIC) {puts("LOCAL_SHUTDOWN_FAILED");FreeLibrary(module);return 16;}
    puts("LOCAL_BOOTSTRAP_SMOKE_PASS; no other process was accessed");
    FreeLibrary(module);
    return 0;
}
