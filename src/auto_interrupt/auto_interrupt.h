#ifndef FM_AUTO_INTERRUPT_H
#define FM_AUTO_INTERRUPT_H
/* Decision engine only. No WoW structs, pointers, function calls, hooks or loading.
 * The native game adapter is deliberately NOT provided until independently verified. */
#include <stdint.h>
#include <stddef.h>
#define FM_AI_TARGET 0u
#define FM_AI_FOCUS 1u
#define FM_AI_HISTORY_MAX 64u
#define FM_AI_SPELL_BLACKLIST_MAX 16u
typedef enum {
    FM_AI_DISABLED, FM_AI_ADAPTER_UNVERIFIED, FM_AI_NO_CAST, FM_AI_STALE,
    FM_AI_NOT_HOSTILE, FM_AI_NOT_INTERRUPTIBLE, FM_AI_OUT_OF_WINDOW,
    FM_AI_OUT_OF_RANGE, FM_AI_NOT_READY, FM_AI_DUPLICATE,
    FM_AI_CAST_CHANGED, FM_AI_ACTION_REJECTED, FM_AI_ATTEMPTED
} FM_AI_RESULT;
typedef struct {
    uint64_t guid, cast_token, world_epoch;
    uint64_t observed_at_ms, started_at_ms, ends_at_ms;
    uint32_t spell_id, class_mask;
    uint32_t distance_cm, energy;
    int exists, hostile, player, casting, channelling, instant, interruptible;
    int kick_ready, can_act, in_spell_range;
} FM_AI_UNIT;
typedef struct {
    int enabled, kick_enabled, target_enabled, focus_enabled, pvp_enabled, pve_enabled;
    uint32_t reaction_delay_ms, max_remaining_ms, safety_margin_ms, max_sample_age_ms;
    uint32_t min_energy, class_blacklist;
    uint32_t spell_blacklist[FM_AI_SPELL_BLACKLIST_MAX];
    uint32_t spell_blacklist_count;
} FM_AI_CONFIG;
typedef struct {
    uint64_t world_epoch;
    int game_ready, adapter_verified, is_rogue, player_alive;
} FM_AI_FRAME;
typedef struct {
    uint64_t guid, cast_token, world_epoch;
    uint32_t spell_id;
} FM_AI_KEY;
typedef struct {
    FM_AI_CONFIG config;
    FM_AI_KEY attempted[FM_AI_HISTORY_MAX];
    size_t attempt_count;
    uint64_t last_world_epoch;
} FM_AI_ENGINE;
/* Read current target/focus twice independently; use GUID, never a retained unit pointer. */
typedef int (*FM_AI_READ)(void *context, unsigned slot, FM_AI_UNIT *out);
/* Must revalidate GUID+cast token+spell ID atomically on a verified game action thread.
 * Return 1 only when Kick was issued; a request is NOT proof of an interrupt. */
typedef int (*FM_AI_KICK)(void *context, uint64_t guid, uint64_t token, uint32_t spell_id);
void fm_ai_init(FM_AI_ENGINE *engine);
void fm_ai_reset_world(FM_AI_ENGINE *engine, uint64_t epoch);
int fm_ai_valid_config(const FM_AI_CONFIG *config);
FM_AI_RESULT fm_ai_tick(FM_AI_ENGINE *engine, const FM_AI_FRAME *frame,
                        uint64_t now_ms, FM_AI_READ read, FM_AI_KICK kick, void *context);
const char *fm_ai_result_name(FM_AI_RESULT result);
#endif
