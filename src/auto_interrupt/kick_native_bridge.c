#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../bootstrap/bootstrap.h"

/* Exact audited Whitemane WoW 3.3.5a build 12340 image only. All of the native
 * operations in this file run in the application's own UnitCastingInfo call,
 * NOT on the loader's remote bootstrap thread. UnitCastingInfo is a read-only
 * Lua API invoked by the paired addon on the WoW UI thread.
 *
 * TRIAL BUILD: arm the feature only for the registered client and a user
 * enabled GUI option. Casting is an ATTEMPT, never server-confirmed success. */
#define FM_UNIT_CAST_INFO 0x00611DF0u
#define FM_LUA_STRING_ARG 0x0084E0E0u
#define FM_UNIT_POINTER 0x0060C1F0u
#define FM_UNIT_GUID 0x0060ABF0u
#define FM_CLIENT_CLOCK 0x0086AE20u
#define FM_CAST_SPELL 0x0080DA40u
#define FM_TRAMP_SIZE 9u
#define FM_MARKER "FMKICK12340|"
#define FM_MARKER_LEN 12u

typedef int (__cdecl *FM_LUA_CFUNC)(void *state);
typedef const char *(__cdecl *FM_LUA_STR)(void *state, int index, unsigned *length);
typedef void *(__cdecl *FM_FIND_UNIT)(const char *unit);
typedef int (__cdecl *FM_FIND_GUID)(const char *unit, unsigned __int64 *out_guid, int flags);
typedef unsigned (__cdecl *FM_TIME_NOW)(void);
typedef void (__cdecl *FM_NATIVE_CAST)(unsigned spell, unsigned arg2,
                                     unsigned guid_lo, unsigned guid_hi, unsigned flags);

static volatile LONG g_installed = 0, g_in_hook = 0;
static BYTE g_original[FM_TRAMP_SIZE];
static FM_LUA_CFUNC g_original_func = NULL;
static volatile DWORD g_max_remaining = 800, g_safety = 150;
static unsigned __int64 g_last_guid = 0;
static DWORD g_last_start = 0, g_last_end = 0, g_last_spell = 0;
static WCHAR g_log_file[MAX_PATH];

static int safe_read(const void *address, void *output, SIZE_T length) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, length, &got) && got == length;
}
static int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return (int)(ch-'0');
    if (ch >= 'A' && ch <= 'F') return (int)(ch-'A'+10);
    if (ch >= 'a' && ch <= 'f') return (int)(ch-'a'+10);
    return -1;
}
static int number(const char **cursor, DWORD *out, char separator) {
    const char *p = *cursor;
    unsigned __int64 value = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 10) {
        value = value * 10 + (unsigned)(*p - '0');
        if (value > 0xFFFFFFFFu) return 0;
        ++digits; ++p;
    }
    if (!digits || (separator ? *p != separator : *p != 0)) return 0;
    if (separator) ++p;
    *cursor = p; *out = (DWORD)value; return 1;
}
static int parse_request(const char *msg, unsigned __int64 *guid,
                         DWORD *start, DWORD *end, DWORD *kick_spell) {
    const char *p = msg;
    unsigned __int64 value = 0;
    int digits = 0, x;
    if (!p || strncmp(p, FM_MARKER, FM_MARKER_LEN)) return 0;
    p += FM_MARKER_LEN;
    if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) return 0;
    p += 2;
    while ((x = hex_digit(*p)) >= 0 && digits < 16) {
        value = (value << 4) | (unsigned)x;
        ++digits; ++p;
    }
    if (!digits || *p != '|') return 0;
    ++p;
    if (!number(&p, start, '|') || !number(&p, end, '|') ||
        !number(&p, kick_spell, 0) || !value || *end <= *start ||
        *kick_spell != 1766u) return 0;
    *guid = value;
    return 1;
}
static void log_attempt(const char *event, DWORD spell, DWORD cast_spell,
                        unsigned __int64 guid, DWORD left) {
    WCHAR line[260];
    DWORD n, written;
    HANDLE f;
    SYSTEMTIME t;
    if (!g_log_file[0]) return;
    GetSystemTime(&t);
    n = (DWORD)_snwprintf_s(line, sizeof(line)/sizeof(line[0]), _TRUNCATE,
        L"%04u-%02u-%02uT%02u:%02u:%02uZ event=%hs guid=%08lX%08lX kick_id=%lu enemy_spell=%lu remaining_ms=%lu\r\n",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,event,
        (DWORD)(guid >> 32),(DWORD)guid,spell,cast_spell,left);
    if (!n || n >= sizeof(line)/sizeof(line[0])) return;
    f = CreateFileW(g_log_file, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f,line,n*sizeof(WCHAR),&written,NULL);
    CloseHandle(f);
}
static int process_request(const char *marker) {
    unsigned __int64 expected_guid=0, live_guid=0;
    DWORD expected_start=0, expected_end=0, kick_spell=0;
    DWORD live[7]; /* spell ID at +A6C, cast time fields at +A78, +A7C */
    DWORD now, remaining;
    BYTE *object;
    if (!parse_request(marker,&expected_guid,&expected_start,&expected_end,&kick_spell) ||
        InterlockedCompareExchange(&g_installed,0,0) != 1) return 0;
    /* On the same WoW Lua/UI call thread, resolve target identity again,
     * then read a fresh independent cast snapshot before issuing a cast. */
    if (!((FM_FIND_GUID)(ULONG_PTR)FM_UNIT_GUID)("target",&live_guid,0) ||
        live_guid != expected_guid) return 0;
    object = (BYTE *)((FM_FIND_UNIT)(ULONG_PTR)FM_UNIT_POINTER)("target");
    if (!object ||
        !safe_read(object+0xA6C, &live[0], sizeof(DWORD)) ||
        !safe_read(object+0xA78, &live[1], sizeof(DWORD)) ||
        !safe_read(object+0xA7C, &live[2], sizeof(DWORD))) return 0;
    now = ((FM_TIME_NOW)(ULONG_PTR)FM_CLIENT_CLOCK)();
    if (!live[0] || live[1] != expected_start || live[2] != expected_end ||
        live[2] <= now || now < live[1]) return 0;
    remaining = live[2] - now;
    if (remaining > g_max_remaining || remaining <= g_safety) return 0;
    if (g_last_guid==live_guid && g_last_start==live[1] &&
        g_last_end==live[2] && g_last_spell==live[0]) return 0;
    /* Revalidate target GUID immediately before action. Casting another unit's
     * spell or a new cast after a target switch is deliberately refused. */
    if (!((FM_FIND_GUID)(ULONG_PTR)FM_UNIT_GUID)("target",&expected_guid,0) ||
        expected_guid != live_guid) return 0;
    g_last_guid=live_guid; g_last_start=live[1];
    g_last_end=live[2]; g_last_spell=live[0];
    ((FM_NATIVE_CAST)(ULONG_PTR)FM_CAST_SPELL)(kick_spell, 0,
                      (DWORD)live_guid, (DWORD)(live_guid >> 32), 0);
    log_attempt("KICK_REQUESTED_NOT_CONFIRMED",kick_spell,live[0],live_guid,remaining);
    return 1;
}
static int __cdecl kick_hook(void *lua) {
    const char *arg;
    if (!g_original_func) return 0;
    arg = ((FM_LUA_STR)(ULONG_PTR)FM_LUA_STRING_ARG)(lua,1,NULL);
    if (!arg || strncmp(arg,FM_MARKER,FM_MARKER_LEN))
        return g_original_func(lua);
    /* Never execute game action recursively. An arbitrary addon cannot
     * request a Kick without a valid, fresh, independently checked cast. */
    if (InterlockedCompareExchange(&g_in_hook,1,0)==0) {
        log_attempt("KICK_MARKER_RECEIVED",1766u,0,0,0);
        if (!process_request(arg))
            log_attempt("KICK_REJECTED_PRECHECK",1766u,0,0,0);
        InterlockedExchange(&g_in_hook,0);
    }
    return 0; /* Intentional no-return Lua diagnostics marker, not a unit. */
}
static int install_hook(void) {
    BYTE *site=(BYTE *)(ULONG_PTR)FM_UNIT_CAST_INFO;
    BYTE *tramp;
    DWORD old, ignore;
    LONG relative;
    static const BYTE prefix[FM_TRAMP_SIZE]={0x55,0x8B,0xEC,0x81,0xEC,0xC8,0x02,0x00,0x00};
    if (!safe_read(site,g_original,sizeof(g_original)) ||
        memcmp(g_original,prefix,sizeof(prefix))) return 0;
    tramp=(BYTE *)VirtualAlloc(NULL,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    if (!tramp) return 0;
    memcpy(tramp,g_original,FM_TRAMP_SIZE);
    tramp[FM_TRAMP_SIZE]=0xE9;
    relative=(LONG)((ULONG_PTR)(site+FM_TRAMP_SIZE) -
                    (ULONG_PTR)(tramp+FM_TRAMP_SIZE+5));
    memcpy(tramp+FM_TRAMP_SIZE+1,&relative,sizeof(relative));
    g_original_func=(FM_LUA_CFUNC)tramp;
    FlushInstructionCache(GetCurrentProcess(),tramp,FM_TRAMP_SIZE+5);
    if (!VirtualProtect(site,FM_TRAMP_SIZE,PAGE_EXECUTE_READWRITE,&old)) {
        g_original_func=NULL; VirtualFree(tramp,0,MEM_RELEASE); return 0;
    }
    relative=(LONG)((ULONG_PTR)(void *)&kick_hook-(ULONG_PTR)(site+5));
    site[0]=0xE9;
    memcpy(site+1,&relative,sizeof(relative));
    memset(site+5,0x90,FM_TRAMP_SIZE-5);
    FlushInstructionCache(GetCurrentProcess(),site,FM_TRAMP_SIZE);
    if (!VirtualProtect(site,FM_TRAMP_SIZE,old,&ignore)) return 0;
    InterlockedExchange(&g_installed,1);
    return 1;
}
static void init_log_path(void) {
    WCHAR folder[MAX_PATH],dir[MAX_PATH];
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",folder,MAX_PATH);
    if (!n || n>=MAX_PATH || n>MAX_PATH-90) return;
    if (_snwprintf_s(dir,MAX_PATH,_TRUNCATE,L"%ls\\Frostmourne",folder)<0) return;
    CreateDirectoryW(dir,NULL);
    if (_snwprintf_s(dir,MAX_PATH,_TRUNCATE,L"%ls\\Frostmourne\\logs",folder)<0) return;
    CreateDirectoryW(dir,NULL);
    _snwprintf_s(g_log_file,MAX_PATH,_TRUNCATE,L"%ls\\kick-%lu.log",dir,GetCurrentProcessId());
}
DWORD WINAPI Frostmourne_StartAutoKick(LPVOID data) {
    FM_KICK_PACKET *p=(FM_KICK_PACKET *)data;
    FM_INTERRUPT_PROBE_PACKET probe;
    if (!p) return FM_INIT_ERROR;
    p->result=FM_INIT_ERROR;
    p->win32_error=ERROR_INVALID_PARAMETER;
    p->observed_pid=GetCurrentProcessId();
    p->hook_installed=0;
    if (p->size!=sizeof(*p) || p->target_pid!=p->observed_pid ||
        p->max_remaining_ms<200 || p->max_remaining_ms>3000 ||
        p->safety_margin_ms<60 || p->safety_margin_ms>=p->max_remaining_ms ||
        InterlockedCompareExchange(&g_installed,0,0)) return FM_INIT_ERROR;
    memset(&probe,0,sizeof(probe));
    probe.size=sizeof(probe); probe.target_pid=p->target_pid;
    if (Frostmourne_ProbeInterruptBindings(&probe)!=FM_INTERRUPT_PROBE_MAGIC) {
        p->win32_error=ERROR_REVISION_MISMATCH;
        return FM_INIT_ERROR;
    }
    if (p->enabled) {
        g_max_remaining=p->max_remaining_ms;
        g_safety=p->safety_margin_ms;
        init_log_path();
        if (!install_hook()) {
            p->win32_error=ERROR_INVALID_DATA;
            return FM_INIT_ERROR;
        }
        p->hook_installed=1;
    }
    p->win32_error=ERROR_SUCCESS;
    p->result=FM_KICK_START_MAGIC;
    return FM_KICK_START_MAGIC;
}
