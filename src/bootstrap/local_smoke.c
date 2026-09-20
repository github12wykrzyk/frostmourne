#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "bootstrap.h"
#pragma comment(lib, "user32.lib")

/* Local-only diagnostic: this process is the sole LoadLibraryW target.
   GitHub Actions sets CI=true, so automation prints and exits without a dialog. */
static int finish(int code, const wchar_t *message) {
    wchar_t line[768], ci[8];
    if (swprintf_s(line, sizeof(line)/sizeof(line[0]),
        L"%ls\n\nKod wyniku: %d\n\nLog DLL (jesli inicjalizacja sie powiodla):\n"
        L"%%LOCALAPPDATA%%\\Frostmourne\\logs\\bootstrap-<PID>.log",
        message, code) < 0) return code ? code : 99;
    wprintf(L"%ls\n", line);
    fflush(stdout);
    /* Show an explicit result on double-click rather than closing instantly.
       Do not block noninteractive builds. */
    if (GetEnvironmentVariableW(L"CI", ci, sizeof(ci)/sizeof(ci[0])) == 0) {
        MessageBoxW(NULL, line, L"FROSTMOURNE - lokalny test DLL",
                    MB_OK | (code == 0 ? MB_ICONINFORMATION : MB_ICONERROR) | MB_TOPMOST);
    }
    return code;
}

int wmain(void) {
    wchar_t path[MAX_PATH], *slash, message[512];
    HMODULE module;
    DWORD (WINAPI *initialize)(LPVOID);
    DWORD (WINAPI *shutdown)(LPVOID);
    DWORD (WINAPI *abi)(LPVOID);
    DWORD (WINAPI *ap_status)(LPVOID);
    FM_INIT_PACKET packet;
    DWORD result, error, n;
    n = GetModuleFileNameW(NULL,path,MAX_PATH);
    if (!n || n >= MAX_PATH) {
        error = GetLastError();
        swprintf_s(message, sizeof(message)/sizeof(message[0]),
            L"BLAD: nie mozna odczytac sciezki testera. Win32=%lu", (unsigned long)error);
        return finish(10, message);
    }
    slash = wcsrchr(path,L'\\');
    if (!slash || (size_t)(slash-path)+
        sizeof(L"FrostmourneBootstrap.dll")/sizeof(wchar_t) > MAX_PATH)
        return finish(11, L"BLAD: nie mozna wyznaczyc sciezki biblioteki DLL.");
    if (wcscpy_s(slash+1, MAX_PATH-(size_t)(slash+1-path),
                 L"FrostmourneBootstrap.dll") != 0)
        return finish(11, L"BLAD: sciezka biblioteki DLL jest zbyt dluga.");
    module = LoadLibraryW(path);
    if (!module) {
        error = GetLastError();
        swprintf_s(message, sizeof(message)/sizeof(message[0]),
            L"BLAD: nie udalo sie zaladowac FrostmourneBootstrap.dll. Win32=%lu.\n"
            L"Sprawdz, czy DLL jest w tym samym katalogu co EXE.",
            (unsigned long)error);
        return finish(12, message);
    }
    initialize = (DWORD(WINAPI *)(LPVOID))GetProcAddress(module,"_Frostmourne_Initialize@4");
    shutdown = (DWORD(WINAPI *)(LPVOID))GetProcAddress(module,"_Frostmourne_Shutdown@4");
    abi = (DWORD(WINAPI *)(LPVOID))GetProcAddress(module,"_Frostmourne_GetAbi@4");
    ap_status = (DWORD(WINAPI *)(LPVOID))GetProcAddress(module,"_Frostmourne_GetAutoPickpocketStatus@4");
    if (!initialize || !shutdown || !abi || !ap_status) {
        FreeLibrary(module);
        return finish(13, L"BLAD: biblioteka nie udostepnia wymaganego ABI.");
    }
    if (abi(NULL) != ((FM_ABI_MAJOR<<16)|FM_ABI_MINOR)) {
        FreeLibrary(module);
        return finish(14, L"BLAD: wersja ABI biblioteki jest niezgodna z testerem.");
    }
    ZeroMemory(&packet,sizeof(packet));
    packet.size=sizeof(packet);
    packet.abi_major=FM_ABI_MAJOR;
    packet.nonce=FM_PACKET_NONCE;
    packet.target_pid=GetCurrentProcessId();
    result=initialize(&packet);
    if (result!=FM_INIT_MAGIC || packet.result!=FM_INIT_MAGIC ||
        packet.win32_error!=ERROR_SUCCESS || packet.observed_pid!=GetCurrentProcessId() ||
        GetModuleHandleW(L"FrostmourneBootstrap.dll")!=module) {
        swprintf_s(message, sizeof(message)/sizeof(message[0]),
            L"BLAD: nie udalo sie potwierdzic inicjalizacji DLL.\n"
            L"Result=%lu packet=%lu Win32=%lu",
            (unsigned long)result,(unsigned long)packet.result,
            (unsigned long)packet.win32_error);
        FreeLibrary(module);
        return finish(15, message);
    }
    if (ap_status(NULL)!=FM_AP_CORE_INERT) {
        FreeLibrary(module);
        return finish(17, L"BLAD: Auto Pickpocket core nie jest zainicjalizowany lub jest aktywny bez adaptera.");
    }
    if (shutdown(NULL)!=FM_INIT_MAGIC) {
        FreeLibrary(module);
        return finish(16, L"BLAD: diagnostyczne zamkniecie DLL nie powiodlo sie.");
    }
    FreeLibrary(module);
    return finish(0,
        L"SUKCES: FrostmourneBootstrap.dll zostala zaladowana i uruchomiona\n"
        L"w procesie lokalnego testera Windows x86.\n"
        L"ABI, inicjalizacja, obecnosc DLL i zamkniecie: OK.\n"
        L"Nie wykonywano iniekcji do procesu Wow.exe.");
}
