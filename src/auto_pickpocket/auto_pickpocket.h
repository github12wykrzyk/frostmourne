#ifndef FM_AUTO_PICKPOCKET_H
#define FM_AUTO_PICKPOCKET_H

/* Client-independent decision engine. This header does NOT define a WoW memory layout. */
#include <stddef.h>
#include <stdint.h>

#define FM_AP_MAX_UNITS 128u
#define FM_AP_MAX_HISTORY 256u
#define FM_AP_MAX_BLACKLIST 128u
#define FM_AP_TARGET   0x01u
#define FM_AP_FOCUS    0x02u
#define FM_AP_MOUSEOVER 0x04u
#define FM_AP_NEARBY   0x08u

typedef enum FM_AP_STATUS {
    FM_AP_NONE = 0,
    FM_AP_PENDING = 1,
    FM_AP_SUCCESS = 2,
    FM_AP_FAILED = 3,
    FM_AP_UNKNOWN = 4
} FM_AP_STATUS;

typedef enum FM_AP_CAST_RESULT {
    FM_AP_NOT_ISSUED = 0,
    FM_AP_ISSUED = 1
} FM_AP_CAST_RESULT;

typedef struct FM_AP_CONFIG {
    int enabled;                  /* OFF by default, even in a diagnostic build. */
    unsigned source_mask;         /* Bitwise OR of FM_AP_TARGET etc. */
    int require_stealth;
    int block_combat;
    double max_range_yards;       /* Additional scan limit, never extends spell range. */
    uint32_t scan_interval_ms;
    uint32_t action_delay_ms;
    uint32_t failed_retry_ms;
    uint32_t confirmation_timeout_ms;
    uint32_t max_opener_hold_ms;
} FM_AP_CONFIG;

typedef struct FM_AP_UNIT {
    uint64_t guid;                 /* Stable within world/instance epoch, not a pointer. */
    uint32_t npc_entry;
    unsigned source_mask;
    int is_npc;
    int hostile;
    int alive;
    int eligible_known;           /* Unknown eligibility is ALWAYS skipped. */
    int pickpocketable;
    int in_spell_range;           /* Verified native spell range, not inferred from scan radius. */
    int line_of_sight;
    int native_can_cast;
    double distance_yards;
} FM_AP_UNIT;

typedef struct FM_AP_FRAME {
    uint64_t world_epoch;         /* Adapter changes this on zone/session/instance change. */
    int game_ready;
    int is_rogue;
    int player_alive;
    int stealthed;
    int in_combat;
    int player_busy;
    int pickpocket_available;
    int adapter_verified;         /* Never enable action with an unverified client bridge. */
    const FM_AP_UNIT *units;
    size_t unit_count;
} FM_AP_FRAME;

typedef struct FM_AP_HISTORY {
    uint64_t guid;
    FM_AP_STATUS status;
    uint64_t next_retry_at_ms;
    uint64_t updated_at_ms;
} FM_AP_HISTORY;

typedef struct FM_AP_ENGINE {
    FM_AP_CONFIG config;
    uint64_t world_epoch;
    uint64_t next_scan_at_ms;
    uint64_t next_action_at_ms;
    uint64_t pending_guid;
    uint64_t pending_deadline_at_ms;
    uint64_t opener_deadline_at_ms;
    FM_AP_HISTORY history[FM_AP_MAX_HISTORY];
    size_t history_count;
    uint64_t blocked_guids[FM_AP_MAX_BLACKLIST];
    uint32_t blocked_npc_entries[FM_AP_MAX_BLACKLIST];
    size_t blocked_guid_count;
    size_t blocked_entry_count;
} FM_AP_ENGINE;

/* Must run on the verified game's designated action thread. No pointers are retained. */
typedef FM_AP_CAST_RESULT (*FM_AP_CAST_GUID)(void *context, uint64_t guid);

void fm_ap_init(FM_AP_ENGINE *engine);
int fm_ap_validate_config(const FM_AP_CONFIG *config);
int fm_ap_set_config(FM_AP_ENGINE *engine, const FM_AP_CONFIG *config);
int fm_ap_blacklist_guid(FM_AP_ENGINE *engine, uint64_t guid);
int fm_ap_blacklist_entry(FM_AP_ENGINE *engine, uint32_t npc_entry);
void fm_ap_reset_world(FM_AP_ENGINE *engine, uint64_t world_epoch);
/* Returns 1 only if a cast was actually issued by the adapter, otherwise 0. */
int fm_ap_tick(FM_AP_ENGINE *engine, const FM_AP_FRAME *frame,
               uint64_t now_ms, FM_AP_CAST_GUID cast_guid, void *context);
/* Accept only verified game/server result for the currently pending GUID. */
int fm_ap_report(FM_AP_ENGINE *engine, uint64_t guid, FM_AP_STATUS result,
                 uint64_t now_ms);
/* True only while a cast is pending, and NEVER past the configured hold deadline. */
int fm_ap_hold_opener(const FM_AP_ENGINE *engine, uint64_t now_ms);
FM_AP_STATUS fm_ap_status(const FM_AP_ENGINE *engine, uint64_t guid);
#endif
