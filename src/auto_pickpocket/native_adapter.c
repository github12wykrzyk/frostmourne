/*
 * FROSTMOURNE / WoW 3.3.5a 12340 x86: one selected-NPC experiment.
 * External reference: AzDeltaQQ/WotLKRotations game_actions.cpp and offsets.h.
 * This adapter does not claim server-confirmed pickpocket or nearby enumeration.
 * The window-thread dispatch intentionally avoids casting on a loader-created thread.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "native_adapter.h"

#define FM_WM_TICK (WM_APP + 0x52Au)
#define FM_CAST_VA 0x0080DA40u
/* WoW 3.3.5a 12340: check the exact client and the live object chain
 * before requesting a cast. Candidate flags below are EXPERIMENTAL,
 * not evidence of loot, hostile reaction, spell range or clear LOS. */
#define FM_CONN_VA 0x00C79CE0u
#define FM_MANAGER_OFFSET 0x2ED0u
#define FM_FIRST_OFFSET 0xACu
#define FM_PLAYER_GUID_OFFSET 0xC0u
#define FM_NEXT_OFFSET 0x3Cu
#define FM_GUID_OFFSET 0x30u
#define FM_TYPE_OFFSET 0x14u
#define FM_FIELDS_OFFSET 0x08u
#define FM_X_OFFSET 0x798u
#define FM_Y_OFFSET 0x79Cu
#define FM_Z_OFFSET 0x7A0u
#define FM_SCAN_LIMIT 2048u
#define FM_LUA_STATE_VA 0x00D3F78Cu
#define FM_LUA_LOAD_VA 0x0084F860u
#define FM_LUA_PCALL_VA 0x0084EC50u
#define FM_LUA_GETTOP_VA 0x0084DBD0u
#define FM_LUA_SETTOP_VA 0x0084DBF0u
#define FM_LUA_TOSTRING_VA 0x0084E0E0u

typedef struct FM_LUA_STATE FM_LUA_STATE;
typedef int (__cdecl *FM_LUA_LOAD)(FM_LUA_STATE*, const char*, size_t, const char*);
typedef int (__cdecl *FM_LUA_PCALL)(FM_LUA_STATE*, int, int, int);
typedef int (__cdecl *FM_LUA_GETTOP)(FM_LUA_STATE*);
typedef void (__cdecl *FM_LUA_SETTOP)(FM_LUA_STATE*, int);
typedef const char *(__cdecl *FM_LUA_TOSTRING)(FM_LUA_STATE*, int, size_t*);
typedef char (__cdecl *FM_CAST)(int, int, uint64_t, char);

static FM_AP_ENGINE *g_engine;
static volatile LONG g_stop;
static volatile LONG g_status = FM_AP_NATIVE_STARTING;
static volatile LONG g_busy;
static volatile LONG g_tick_posted;
static HWND g_window;
static WNDPROC g_previous;
static HANDLE g_thread;
static uint64_t g_world_epoch = 1u;
static uint64_t g_player_guid;
static unsigned long g_map_id;
static DWORD g_last_snapshot;
static uint64_t g_last_pending;
static DWORD g_action_thread_id;
static volatile LONG g_post_count;
static volatile LONG g_dispatch_count;
static DWORD g_last_worker_diag;
static DWORD g_last_lua_diag;
static DWORD g_last_frame_diag;
static DWORD g_last_scan_diag;

/* From tools/audit_interrupt_bridge.py run 35478822571, exact registered image.
 * x86 uint64_t uses two stack slots: four C parameters occupy twenty bytes. */
static const unsigned char g_cast_prologue[] = {
    0x55,0x8B,0xEC,0xE8,0x48,0x5D,0xCC,0xFF,
    0x68,0xA0,0x00,0x00,0x00,0x68,0x40,0x23
};

static void fm_log(const char *message) {
    char dir[MAX_PATH], logs[MAX_PATH], path[MAX_PATH], line[512];
    DWORD size, count;
    HANDLE file;
    size = GetEnvironmentVariableA("LOCALAPPDATA", dir, MAX_PATH);
    if (!size || size >= MAX_PATH || strlen(dir) > MAX_PATH - 100) return;
    if (sprintf_s(logs, sizeof(logs), "%s\\Frostmourne", dir) < 0) return;
    CreateDirectoryA(logs, NULL);
    if (strcat_s(logs, sizeof(logs), "\\logs")) return;
    CreateDirectoryA(logs, NULL);
    if (sprintf_s(path, sizeof(path), "%s\\auto-pickpocket-%lu.log",
                  logs, (unsigned long)GetCurrentProcessId()) < 0) return;
    if (sprintf_s(line, sizeof(line), "tick=%lu thread=%lu %s\r\n",
                  (unsigned long)GetTickCount(),
                  (unsigned long)GetCurrentThreadId(), message) < 0) return;
    file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    WriteFile(file, line, (DWORD)strlen(line), &count, NULL);
    CloseHandle(file);
    OutputDebugStringA(line);
}

static void fm_log_periodic(const char *message, DWORD *last) {
    DWORD now = GetTickCount();
    if (!*last || now - *last >= 5000u) {
        *last = now;
        fm_log(message);
    }
}

static int fm_safe_read(uintptr_t address, void *buffer, size_t bytes) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)address,
                             buffer, bytes, &copied) && copied == bytes;
}

static int fm_executable(uintptr_t address) {
    MEMORY_BASIC_INFORMATION m;
    DWORD access;
    if (!VirtualQuery((LPCVOID)address, &m, sizeof(m)) || m.State != MEM_COMMIT)
        return 0;
    access = m.Protect & 0xffu;
    return access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
}

static int fm_exact_cast_gate(void) {
    unsigned char actual[sizeof(g_cast_prologue)];
    return (uintptr_t)GetModuleHandleW(NULL) == 0x00400000u &&
           fm_executable(FM_CAST_VA) &&
           fm_safe_read(FM_CAST_VA, actual, sizeof(actual)) &&
           memcmp(actual, g_cast_prologue, sizeof(actual)) == 0;
}

/* Read-only state and an unprotected preflight. No Lua spell cast command.
 * 3.3.5 uses the localized spell name for IsSpellInRange. */
static const char g_preflight[] =
    "local _,class=UnitClass('player');"
    "if class~='ROGUE' or not UnitGUID('player') then return 'OFF' end;"
    "local id=UnitGUID('player');"
    "local map=GetCurrentMapAreaID and GetCurrentMapAreaID() or 0;"
    "local prefix=tostring(id)..'|'..tostring(map)..'|';"
    "if UnitIsDeadOrGhost('player') or UnitAffectingCombat('player') "
    "or UnitCastingInfo('player') or UnitChannelInfo('player') "
    "or not IsStealthed() then return 'IDLE|'..prefix end;"
    "local spell=GetSpellInfo(921);"
    "if not spell or not IsUsableSpell(921) then return 'IDLE|'..prefix end;"
    "if not UnitExists('target') or UnitIsPlayer('target') "
    "or UnitIsDeadOrGhost('target') or not UnitCanAttack('player','target') "
    "or not UnitIsVisible('target') then return 'SCAN|'..prefix end;"
    "local typ=UnitCreatureType('target');"
    "if typ~='Humanoid' and typ~='Undead' then return 'SCAN|'..prefix end;"
    "if IsSpellInRange(spell,'target')~=1 then return 'SCAN|'..prefix end;"
    "local guid=UnitGUID('target');"
    "if not guid then return 'SCAN|'..prefix end;"
    "return 'OK|'..prefix..tostring(guid);";

/* All Lua state calls occur on the owning game window thread, never in worker.
 * Any access violation disables the feature for the entire process. */
static int fm_lua_query(const char *script, char *result, size_t capacity) {
    FM_LUA_STATE *state = NULL;
    FM_LUA_GETTOP gettop = (FM_LUA_GETTOP)FM_LUA_GETTOP_VA;
    FM_LUA_LOAD load = (FM_LUA_LOAD)FM_LUA_LOAD_VA;
    FM_LUA_PCALL pcall = (FM_LUA_PCALL)FM_LUA_PCALL_VA;
    FM_LUA_TOSTRING tostring = (FM_LUA_TOSTRING)FM_LUA_TOSTRING_VA;
    FM_LUA_SETTOP settop = (FM_LUA_SETTOP)FM_LUA_SETTOP_VA;
    int top, status, ok = 0;
    size_t length = 0;
    const char *value;
    result[0] = '\0';
    if (!fm_safe_read(FM_LUA_STATE_VA, &state, sizeof(state)) || !state ||
        !fm_executable(FM_LUA_LOAD_VA) || !fm_executable(FM_LUA_PCALL_VA) ||
        !fm_executable(FM_LUA_GETTOP_VA) || !fm_executable(FM_LUA_SETTOP_VA) ||
        !fm_executable(FM_LUA_TOSTRING_VA)) {
        fm_log_periodic("event=LUA_PREFLIGHT_BLOCKED reason=state_pointer_or_function_gate", &g_last_lua_diag);
        return 0;
    }
    __try {
        top = gettop(state);
        if (top < 0 || top > 2048) {
            fm_log_periodic("event=LUA_PREFLIGHT_BLOCKED reason=stack_depth", &g_last_lua_diag);
            return 0;
        }
        status = load(state, script, strlen(script), "=FrostmourneAP");
        if (status != 0)
            fm_log_periodic("event=LUA_PREFLIGHT_BLOCKED reason=load_error", &g_last_lua_diag);
        if (status == 0) {
            status = pcall(state, 0, 1, 0);
            if (status != 0)
                fm_log_periodic("event=LUA_PREFLIGHT_BLOCKED reason=runtime_error", &g_last_lua_diag);
        }
        if (status == 0 && gettop(state) == top + 1) {
            value = tostring(state, -1, &length);
            if (value && length && length < capacity) {
                memcpy(result, value, length);
                result[length] = '\0';
                ok = 1;
            }
        }
        if (!ok && status == 0)
            fm_log_periodic("event=LUA_PREFLIGHT_BLOCKED reason=non_string_or_stack_result", &g_last_lua_diag);
        settop(state, top);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        fm_log("event=LUA_EXCEPTION adapter=FAILED gameplay_actions=DISABLED");
        InterlockedExchange(&g_status, FM_AP_NATIVE_FAILED);
        ok = 0;
    }
    return ok;
}

static int fm_parse_guid(const char *str, uint64_t *guid) {
    char *end;
    unsigned __int64 parsed;
    if (!str || !*str) return 0;
    parsed = _strtoui64(str, &end, 0);
    if (!parsed || !end || *end) return 0;
    *guid = (uint64_t)parsed;
    return 1;
}

static FM_AP_CAST_RESULT fm_native_cast(void *ctx, uint64_t guid) {
    char result;
    char logline[180];
    (void)ctx;
    if (!guid || !fm_exact_cast_gate() ||
        GetCurrentThreadId() != g_action_thread_id ||
        InterlockedCompareExchange(&g_stop, 0, 0)) return FM_AP_NOT_ISSUED;
    __try {
        result = ((FM_CAST)FM_CAST_VA)(921, 0, guid, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        fm_log("event=CAST_EXCEPTION adapter=FAILED gameplay_actions=DISABLED");
        InterlockedExchange(&g_status, FM_AP_NATIVE_FAILED);
        return FM_AP_NOT_ISSUED;
    }
    if (sprintf_s(logline, sizeof(logline),
                  "event=PICKPOCKET_CAST_REQUEST spell=921 guid=0x%I64X native_return=%d outcome=UNCONFIRMED",
                  (unsigned __int64)guid, (int)(unsigned char)result) >= 0) fm_log(logline);
    /* Native return indicates local dispatch at most, NEVER successful loot. */
    return FM_AP_ISSUED;
}


/* Enumerate current-object GUIDs without retaining object pointers across
 * ticks. ReadProcessMemory bounds every dereference; an invalid/cyclic list
 * disables this snapshot rather than extending a scan indefinitely. */
static int fm_scan_nearby(uint64_t player_guid, FM_AP_UNIT *out,
                          size_t capacity, size_t *count) {
    uint32_t connection, manager, first, cur, player_obj = 0;
    uint64_t manager_guid, object_guid;
    uint32_t player_type = 0, player_fields = 0, player_faction = 0;
    float px = 0.0f, py = 0.0f, pz = 0.0f;
    unsigned pass, steps;
    *count = 0;
    if (!fm_safe_read(FM_CONN_VA, &connection, sizeof(connection)) ||
        connection < 0x10000u || connection > 0x7FFF0000u ||
        !fm_safe_read((uintptr_t)connection + FM_MANAGER_OFFSET, &manager, sizeof(manager)) ||
        manager < 0x10000u || manager > 0x7FFF0000u ||
        !fm_safe_read((uintptr_t)manager + FM_PLAYER_GUID_OFFSET, &manager_guid, sizeof(manager_guid)) ||
        manager_guid != player_guid ||
        !fm_safe_read((uintptr_t)manager + FM_FIRST_OFFSET, &first, sizeof(first)))
        return 0;
    /* Pass 0 finds the real player and gets the authoritative position.
       Pass 1 collects nearby NPC objects from the same manager. */
    for (pass = 0; pass < 2; ++pass) {
        cur = first;
        for (steps = 0; cur && steps < FM_SCAN_LIMIT; ++steps) {
            uint32_t next, kind, fields, health, faction, flags, entry;
            float x, y, z;
            double dx, dy, dz, distance_squared;
            FM_AP_UNIT unit;
            size_t j;
            if (cur < 0x10000u || cur > 0x7FFF0000u || (cur & 3u) ||
                !fm_safe_read((uintptr_t)cur + FM_NEXT_OFFSET, &next, sizeof(next)) ||
                !fm_safe_read((uintptr_t)cur + FM_GUID_OFFSET, &object_guid, sizeof(object_guid)) ||
                next == cur) return 0;
            if (pass == 0) {
                if (object_guid == player_guid) player_obj = cur;
                cur = next;
                continue;
            }
            if (object_guid == player_guid ||
                (object_guid >> 48) != 0xF130u ||
                !fm_safe_read((uintptr_t)cur + FM_TYPE_OFFSET, &kind, sizeof(kind)) ||
                kind != 3u ||
                !fm_safe_read((uintptr_t)cur + FM_FIELDS_OFFSET, &fields, sizeof(fields)) ||
                fields < 0x10000u || fields > 0x7FFF0000u ||
                !fm_safe_read((uintptr_t)fields + 24u*4u, &health, sizeof(health)) || !health ||
                !fm_safe_read((uintptr_t)fields + 55u*4u, &faction, sizeof(faction)) ||
                !faction || faction == player_faction ||
                !fm_safe_read((uintptr_t)fields + 59u*4u, &flags, sizeof(flags)) ||
                (flags & 0x00000002u) ||
                !fm_safe_read((uintptr_t)fields + 3u*4u, &entry, sizeof(entry)) || !entry ||
                !fm_safe_read((uintptr_t)cur + FM_X_OFFSET, &x, sizeof(x)) ||
                !fm_safe_read((uintptr_t)cur + FM_Y_OFFSET, &y, sizeof(y)) ||
                !fm_safe_read((uintptr_t)cur + FM_Z_OFFSET, &z, sizeof(z))) {
                cur = next;
                continue;
            }
            dx = (double)x - px;
            dy = (double)y - py;
            dz = (double)z - pz;
            distance_squared = dx*dx + dy*dy + dz*dz;
            if (!(distance_squared >= 0.0 && distance_squared <= 25.0)) {
                cur = next;
                continue;
            }
            memset(&unit, 0, sizeof(unit));
            unit.guid = object_guid;
            unit.npc_entry = entry;
            unit.source_mask = FM_AP_NEARBY;
            unit.is_npc = unit.alive = 1;
            /* EXPERIMENT ONLY: these are candidates, not a native eligibility
             * verdict. The game/server rejects invalid casts. */
            unit.hostile = unit.eligible_known = unit.pickpocketable = 1;
            unit.in_spell_range = unit.line_of_sight = unit.native_can_cast = 1;
            unit.distance_yards = sqrt(distance_squared);
            for (j = 0; j < *count; ++j)
                if (out[j].guid == object_guid) break;
            if (j == *count && *count < capacity) {
                j = (*count)++;
                while (j && out[j-1].distance_yards > unit.distance_yards) {
                    out[j] = out[j-1];
                    --j;
                }
                out[j] = unit;
            }
            cur = next;
        }
        if (cur || (pass == 0 && !player_obj)) return 0;
        if (pass == 0) {
            if (!fm_safe_read((uintptr_t)player_obj + FM_TYPE_OFFSET, &player_type, sizeof(player_type)) ||
                player_type != 4u ||
                !fm_safe_read((uintptr_t)player_obj + FM_FIELDS_OFFSET, &player_fields, sizeof(player_fields)) ||
                player_fields < 0x10000u || player_fields > 0x7FFF0000u ||
                !fm_safe_read((uintptr_t)player_fields + 55u*4u, &player_faction, sizeof(player_faction)) ||
                !fm_safe_read((uintptr_t)player_obj + FM_X_OFFSET, &px, sizeof(px)) ||
                !fm_safe_read((uintptr_t)player_obj + FM_Y_OFFSET, &py, sizeof(py)) ||
                !fm_safe_read((uintptr_t)player_obj + FM_Z_OFFSET, &pz, sizeof(pz)) ||
                !(px > -25000.0f && px < 25000.0f &&
                  py > -25000.0f && py < 25000.0f &&
                  pz > -25000.0f && pz < 25000.0f)) return 0;
        }
    }
    return 1;
}

static void fm_tick_on_window_thread(void) {
    char answer[192], banner[64], *parts[4] = {NULL, NULL, NULL, NULL};
    char *context = NULL, *item;
    uint64_t player, target;
    unsigned long area;
    char *end;
    FM_AP_UNIT units[FM_AP_MAX_UNITS];
    size_t unit_count = 0;
    FM_AP_FRAME f;
    DWORD now = GetTickCount();
    int i;
    if (InterlockedCompareExchange(&g_stop, 0, 0) ||
        InterlockedCompareExchange(&g_status, 0, 0) == FM_AP_NATIVE_FAILED) return;
    if (!g_engine) {
        fm_log_periodic("event=TICK_BLOCKED reason=engine_null action=SKIPPED", &g_last_frame_diag);
        return;
    }
    if (!fm_exact_cast_gate()) {
        fm_log_periodic("event=TICK_BLOCKED reason=cast_prologue_or_image_changed action=SKIPPED", &g_last_frame_diag);
        return;
    }
    if (!fm_lua_query(g_preflight, answer, sizeof(answer))) {
        if (g_last_snapshot && now - g_last_snapshot > 2000) {
            fm_ap_reset_world(g_engine, ++g_world_epoch);
            g_last_snapshot = 0;
            InterlockedExchange(&g_status, FM_AP_NATIVE_STARTING);
        }
        return;
    }
    if (strcmp(answer, "OFF") == 0) {
        fm_log_periodic("event=PREFLIGHT_OFF reason=not_logged_in_or_not_rogue", &g_last_frame_diag);
        if (g_last_snapshot) {
            fm_ap_reset_world(g_engine, ++g_world_epoch);
            g_last_snapshot = 0;
            InterlockedExchange(&g_status, FM_AP_NATIVE_STARTING);
        }
        return;
    }
    item = strtok_s(answer, "|", &context);
    for (i = 0; i < 4 && item; ++i) {
        parts[i] = item;
        item = strtok_s(NULL, "|", &context);
    }
    if (!parts[0] || !parts[1] || !parts[2]) {
        fm_log_periodic("event=PREFLIGHT_PARSE_FAILED reason=missing_fields action=SKIPPED", &g_last_frame_diag);
        return;
    }
    if (!fm_parse_guid(parts[1], &player)) {
        fm_log_periodic("event=PREFLIGHT_PARSE_FAILED reason=player_guid action=SKIPPED", &g_last_frame_diag);
        return;
    }
    if (strcmp(parts[0], "IDLE") == 0)
        fm_log_periodic("event=PREFLIGHT_IDLE reason=stealth_combat_busy_or_spell_unavailable", &g_last_frame_diag);
    area = strtoul(parts[2], &end, 10);
    if (!end || *end) {
        fm_log_periodic("event=PREFLIGHT_PARSE_FAILED reason=map_id action=SKIPPED", &g_last_frame_diag);
        return;
    }
    if (g_player_guid != player || g_map_id != area) {
        g_player_guid = player;
        g_map_id = area;
        fm_ap_reset_world(g_engine, ++g_world_epoch);
    }
    g_last_snapshot = now;
    if (InterlockedCompareExchange(&g_status, FM_AP_NATIVE_READY,
                                    FM_AP_NATIVE_STARTING) == FM_AP_NATIVE_STARTING) {
        FM_AP_CONFIG config = g_engine->config;
        config.enabled = 1;
        config.source_mask = FM_AP_TARGET | FM_AP_NEARBY;
        fm_ap_set_config(g_engine, &config);
        fm_log("event=ADAPTER_READY adapter=WINDOW_THREAD nearby_scan=EXPERIMENTAL auto_enabled=1");
        /* Message is printed only after a successful in-game snapshot. */
        (void)fm_lua_query("DEFAULT_CHAT_FRAME:AddMessage('FROSTMOURNE Auto Pickpocket: EXPERIMENTAL NEARBY SCAN') return 'OK'",
                           banner, sizeof(banner));
    }
    if (g_last_pending && g_engine->pending_guid == 0) {
        fm_log("event=PICKPOCKET_RESULT outcome=UNKNOWN reason=no_correlated_server_loot_event");
        g_last_pending = 0;
    }
    memset(&f, 0, sizeof(f));
    f.world_epoch = g_world_epoch;
    f.game_ready = 1;
    f.adapter_verified = 1; /* Exact running image, cast prologue, Lua snapshot, window-thread gate. */
    f.is_rogue = 1;
    f.player_alive = 1;
    f.stealthed = strcmp(parts[0], "OK") == 0 || strcmp(parts[0], "SCAN") == 0;
    f.in_combat = 0;
    f.player_busy = 0;
    f.pickpocket_available = 1;
    /* Fail closed until the exact-client object manager and local GUID agree. */
    if (f.stealthed && !fm_scan_nearby(player, units, FM_AP_MAX_UNITS, &unit_count)) {
        fm_log_periodic("event=NEARBY_SCAN_NOT_READY action=SKIPPED reason=object_manager_or_layout", &g_last_scan_diag);
        return;
    }
    if (strcmp(parts[0], "OK") == 0 && parts[3] &&
        fm_parse_guid(parts[3], &target) && target != player) {
        size_t j;
        for (j = 0; j < unit_count; ++j)
            if (units[j].guid == target) {
                units[j].source_mask |= FM_AP_TARGET;
                break;
            }
    }
    if (f.stealthed && !unit_count)
        fm_log_periodic("event=NEARBY_SCAN_EMPTY action=WAITING", &g_last_scan_diag);
    f.units = units;
    f.unit_count = unit_count;
    if (fm_ap_tick(g_engine, &f, (uint64_t)now, fm_native_cast, NULL))
        g_last_pending = g_engine->pending_guid;
}

static LRESULT CALLBACK fm_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WNDPROC previous = g_previous;
    if (message == FM_WM_TICK && hwnd == g_window) {
        if (InterlockedIncrement(&g_dispatch_count) == 1)
            fm_log("event=WINDOW_TICK_RECEIVED phase=PREFLIGHT");
        InterlockedExchange(&g_tick_posted, 0);
        if (InterlockedCompareExchange(&g_busy, 1, 0) == 0) {
            if (!g_action_thread_id) g_action_thread_id = GetCurrentThreadId();
            if (InterlockedCompareExchange(&g_dispatch_count, 0, 0) == 1)
                fm_log("event=WINDOW_TICK_ENTER adapter=RUNNING");
            fm_tick_on_window_thread();
            InterlockedExchange(&g_busy, 0);
        }
        return 0;
    }
    return previous ? CallWindowProcW(previous, hwnd, message, wparam, lparam)
                    : DefWindowProcW(hwnd, message, wparam, lparam);
}

typedef struct FM_FIND_WINDOW { DWORD pid; HWND hwnd; } FM_FIND_WINDOW;
static BOOL CALLBACK fm_find_window(HWND hwnd, LPARAM parameter) {
    FM_FIND_WINDOW *found = (FM_FIND_WINDOW*)parameter;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == found->pid && IsWindowVisible(hwnd) &&
        GetWindow(hwnd, GW_OWNER) == NULL) {
        found->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

/* The game can destroy its startup/login HWND while Wow.exe stays alive.
 * The worker must discover the replacement; it must not keep posting to the
 * stale HWND or permanently disable the module on a normal window transition.
 * Only rebind after the previous HWND is no longer a valid window: never
 * overwrite a still-live foreign WndProc. */
static DWORD WINAPI fm_native_worker(LPVOID parameter) {
    FM_FIND_WINDOW window;
    LONG_PTR old;
    DWORD attempts = 0;
    int had_window = 0;
    (void)parameter;
    fm_log("event=ADAPTER_START nearby_scan=EXPERIMENTAL phase=WAITING_FOR_WINDOW");
    if (!fm_exact_cast_gate()) {
        fm_log("event=CAST_GATE_FAIL expected_prologue_or_image_mismatch gameplay_actions=DISABLED");
        InterlockedExchange(&g_status, FM_AP_NATIVE_FAILED);
        return 0;
    }
    while (!InterlockedCompareExchange(&g_stop, 0, 0) &&
           InterlockedCompareExchange(&g_status, 0, 0) != FM_AP_NATIVE_FAILED) {
        DWORD pid = 0;
        if (g_window && IsWindow(g_window)) {
            char pulse[240];
            GetWindowThreadProcessId(g_window, &pid);
            if (!g_last_worker_diag || GetTickCount() - g_last_worker_diag >= 5000u) {
                if (sprintf_s(pulse, sizeof(pulse),
                    "event=DISPATCH_HEARTBEAT posted=%ld delivered=%ld pending=%ld status=%ld",
                    (long)InterlockedCompareExchange(&g_post_count, 0, 0),
                    (long)InterlockedCompareExchange(&g_dispatch_count, 0, 0),
                    (long)InterlockedCompareExchange(&g_tick_posted, 0, 0),
                    (long)InterlockedCompareExchange(&g_status, 0, 0)) >= 0)
                    fm_log(pulse);
                g_last_worker_diag = GetTickCount();
            }
            if (pid != GetCurrentProcessId()) {
                fm_log("event=WINDOW_HANDLE_REUSED adapter=FAILED gameplay_actions=DISABLED");
                InterlockedExchange(&g_status, FM_AP_NATIVE_FAILED);
                break;
            }
            if (InterlockedCompareExchange(&g_tick_posted, 1, 0) == 0) {
                if (PostMessageW(g_window, FM_WM_TICK, 0, 0))
                    InterlockedIncrement(&g_post_count);
                else
                    InterlockedExchange(&g_tick_posted, 0);
            }
            Sleep(100);
            continue;
        }
        if (g_window) {
            fm_log("event=GAME_WINDOW_REPLACED action=REACQUIRE gameplay_actions=PAUSED");
            /* The old HWND is destroyed. Wait until the in-flight tick has
             * returned before publishing a different HWND/WndProc pair. */
            while (InterlockedCompareExchange(&g_busy, 0, 0) &&
                   !InterlockedCompareExchange(&g_stop, 0, 0)) Sleep(10);
            if (InterlockedCompareExchange(&g_stop, 0, 0)) break;
            if (g_engine) {
                g_engine->config.enabled = 0;
                fm_ap_reset_world(g_engine, ++g_world_epoch);
            }
            g_last_snapshot = 0;
            g_last_pending = 0;
            g_action_thread_id = 0;
            g_window = NULL;
            g_previous = NULL;
            InterlockedExchange(&g_tick_posted, 0);
            InterlockedExchange(&g_status, FM_AP_NATIVE_STARTING);
            attempts = 0;
        }
        window.pid = GetCurrentProcessId();
        window.hwnd = NULL;
        EnumWindows(fm_find_window, (LPARAM)&window);
        if (window.hwnd) {
            SetLastError(0);
            old = SetWindowLongPtrW(window.hwnd, GWLP_WNDPROC, (LONG_PTR)fm_window_proc);
            if (old) {
                char info[256], cls[80] = {0}, title[96] = {0};
                DWORD window_thread = GetWindowThreadProcessId(window.hwnd, NULL);
                (void)GetClassNameA(window.hwnd, cls, sizeof(cls));
                (void)GetWindowTextA(window.hwnd, title, sizeof(title));
                if (sprintf_s(info, sizeof(info),
                    "event=WINDOW_SELECTED thread=%lu class=%s title=%s",
                    (unsigned long)window_thread, cls, title) >= 0)
                    fm_log(info);
                g_previous = (WNDPROC)old;
                g_window = window.hwnd;
                attempts = 0;
                had_window = 1;
                fm_log("event=WINDOW_THREAD_DISPATCH_INSTALLED phase=WAITING_FOR_ROGUE_LOGIN");
            }
        }
        if (!g_window && ++attempts >= 240u) {
            fm_log(had_window ?
                "event=WINDOW_REACQUIRE_TIMEOUT gameplay_actions=DISABLED" :
                "event=WINDOW_NOT_FOUND adapter=FAILED gameplay_actions=DISABLED");
            InterlockedExchange(&g_status, FM_AP_NATIVE_FAILED);
            break;
        }
        Sleep(250);
    }
    /* The DLL stays mapped until the process exits: never unload a live callback. */
    return 0;
}

int fm_ap_native_start(FM_AP_ENGINE *engine) {
    if (!engine || g_thread) return 0;
    g_engine = engine;
    g_stop = g_busy = g_tick_posted = 0;
    g_post_count = g_dispatch_count = 0;
    g_last_worker_diag = g_last_lua_diag = g_last_frame_diag = g_last_scan_diag = 0;
    g_status = FM_AP_NATIVE_STARTING;
    g_thread = CreateThread(NULL, 0, fm_native_worker, NULL, 0, NULL);
    return g_thread != NULL;
}

void fm_ap_native_stop(void) {
    InterlockedExchange(&g_stop, 1);
    if (g_engine) g_engine->config.enabled = 0;
    /* Do not free the DLL's callback while it could be running. */
}

unsigned long fm_ap_native_status(void) {
    return (unsigned long)InterlockedCompareExchange(&g_status, 0, 0);
}
