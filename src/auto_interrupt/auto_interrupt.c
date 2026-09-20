#include "auto_interrupt.h"
#include <string.h>
void fm_ai_init(FM_AI_ENGINE *e) {
    if (!e) return;
    memset(e, 0, sizeof(*e));
    e->config.target_enabled = 1;
    e->config.focus_enabled = 1;
    e->config.pvp_enabled = 1;
    e->config.pve_enabled = 1;
    e->config.max_remaining_ms = 800;
    e->config.safety_margin_ms = 150;
    e->config.max_sample_age_ms = 100;
    e->config.min_energy = 25;
    /* OFF by default. Never enable automatically in a diagnostic package. */
}
void fm_ai_reset_world(FM_AI_ENGINE *e, uint64_t epoch) {
    if (!e) return;
    memset(e->attempted, 0, sizeof(e->attempted));
    e->attempt_count = 0;
    e->last_world_epoch = epoch;
}
int fm_ai_valid_config(const FM_AI_CONFIG *c) {
    return c && c->max_remaining_ms && c->max_remaining_ms <= 60000 &&
        c->safety_margin_ms < c->max_remaining_ms &&
        c->reaction_delay_ms <= 60000 &&
        c->max_sample_age_ms >= 1 && c->max_sample_age_ms <= 1000 &&
        c->spell_blacklist_count <= FM_AI_SPELL_BLACKLIST_MAX;
}
static FM_AI_RESULT check(const FM_AI_CONFIG *c, const FM_AI_FRAME *f,
                          const FM_AI_UNIT *u, uint64_t now) {
    uint64_t remaining;
    uint32_t i;
    if (!u->exists || !u->guid || !u->cast_token || !u->spell_id ||
        (!u->casting && !u->channelling) || u->instant ||
        u->world_epoch != f->world_epoch || !u->started_at_ms ||
        u->ends_at_ms <= u->started_at_ms) return FM_AI_NO_CAST;
    if (u->observed_at_ms > now || now - u->observed_at_ms > c->max_sample_age_ms)
        return FM_AI_STALE;
    if (!u->hostile || (u->player ? !c->pvp_enabled : !c->pve_enabled) ||
        (u->class_mask & c->class_blacklist)) return FM_AI_NOT_HOSTILE;
    if (!u->interruptible) return FM_AI_NOT_INTERRUPTIBLE;
    for (i = 0; i < c->spell_blacklist_count; ++i)
        if (u->spell_id == c->spell_blacklist[i]) return FM_AI_NOT_INTERRUPTIBLE;
    if (now < u->started_at_ms || now - u->started_at_ms < c->reaction_delay_ms ||
        now >= u->ends_at_ms) return FM_AI_OUT_OF_WINDOW;
    remaining = u->ends_at_ms - now;
    if (remaining > c->max_remaining_ms || remaining <= c->safety_margin_ms)
        return FM_AI_OUT_OF_WINDOW;
    if (!u->in_spell_range) return FM_AI_OUT_OF_RANGE;
    if (!u->kick_ready || !u->can_act || u->energy < c->min_energy)
        return FM_AI_NOT_READY;
    return FM_AI_ATTEMPTED; /* eligible, NOT an action yet */
}
static int same_cast(const FM_AI_UNIT *a, const FM_AI_UNIT *b) {
    return a->guid == b->guid && a->cast_token == b->cast_token &&
        a->world_epoch == b->world_epoch && a->spell_id == b->spell_id &&
        a->started_at_ms == b->started_at_ms && a->ends_at_ms == b->ends_at_ms &&
        a->casting == b->casting && a->channelling == b->channelling;
}
static int was_attempted(const FM_AI_ENGINE *e, const FM_AI_UNIT *u) {
    size_t i;
    for (i = 0; i < e->attempt_count; ++i)
        if (e->attempted[i].guid == u->guid &&
            e->attempted[i].cast_token == u->cast_token &&
            e->attempted[i].world_epoch == u->world_epoch &&
            e->attempted[i].spell_id == u->spell_id) return 1;
    return 0;
}
FM_AI_RESULT fm_ai_tick(FM_AI_ENGINE *e, const FM_AI_FRAME *f,
                        uint64_t now, FM_AI_READ read, FM_AI_KICK kick, void *ctx) {
    FM_AI_RESULT last = FM_AI_NO_CAST;
    unsigned slot;
    if (!e || !f || !fm_ai_valid_config(&e->config) ||
        !e->config.enabled || !e->config.kick_enabled || !f->game_ready ||
        !f->is_rogue || !f->player_alive) return FM_AI_DISABLED;
    if (!f->adapter_verified || !read || !kick) return FM_AI_ADAPTER_UNVERIFIED;
    if (e->last_world_epoch != f->world_epoch)
        fm_ai_reset_world(e, f->world_epoch);
    for (slot = FM_AI_TARGET; slot <= FM_AI_FOCUS; ++slot) {
        FM_AI_UNIT first, fresh;
        FM_AI_RESULT result;
        if ((slot == FM_AI_TARGET && !e->config.target_enabled) ||
            (slot == FM_AI_FOCUS && !e->config.focus_enabled)) continue;
        memset(&first, 0, sizeof(first));
        if (!read(ctx, slot, &first)) { last = FM_AI_STALE; continue; }
        result = check(&e->config, f, &first, now);
        if (result != FM_AI_ATTEMPTED) { last = result; continue; }
        if (was_attempted(e, &first)) { last = FM_AI_DUPLICATE; continue; }
        memset(&fresh, 0, sizeof(fresh));
        if (!read(ctx, slot, &fresh) || !same_cast(&first, &fresh)) {
            last = FM_AI_CAST_CHANGED; continue;
        }
        result = check(&e->config, f, &fresh, now);
        if (result != FM_AI_ATTEMPTED) { last = result; continue; }
        if (was_attempted(e, &fresh)) { last = FM_AI_DUPLICATE; continue; }
        /* History exhaustion stops actions: never forget a prior attempt during a fight. */
        if (e->attempt_count == FM_AI_HISTORY_MAX) return FM_AI_DISABLED;
        e->attempted[e->attempt_count].guid = fresh.guid;
        e->attempted[e->attempt_count].cast_token = fresh.cast_token;
        e->attempted[e->attempt_count].world_epoch = fresh.world_epoch;
        e->attempted[e->attempt_count].spell_id = fresh.spell_id;
        ++e->attempt_count; /* BEFORE callback, so even a rejected request won't spam. */
        return kick(ctx, fresh.guid, fresh.cast_token, fresh.spell_id)
            ? FM_AI_ATTEMPTED : FM_AI_ACTION_REJECTED;
    }
    return last;
}
const char *fm_ai_result_name(FM_AI_RESULT r) {
    static const char *const names[] = {
        "DISABLED","ADAPTER_UNVERIFIED","NO_CAST","STALE","NOT_HOSTILE",
        "NOT_INTERRUPTIBLE","OUT_OF_WINDOW","OUT_OF_RANGE","NOT_READY",
        "DUPLICATE","CAST_CHANGED","ACTION_REJECTED","ATTEMPTED"
    };
    return (unsigned)r < sizeof(names)/sizeof(names[0]) ? names[r] : "UNKNOWN";
}
