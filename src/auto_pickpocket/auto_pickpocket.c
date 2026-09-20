#include "auto_pickpocket.h"
#include <string.h>

static uint64_t plus_ms(uint64_t now, uint32_t delay) {
    return UINT64_MAX - now < delay ? UINT64_MAX : now + delay;
}

void fm_ap_init(FM_AP_ENGINE *engine) {
    if (!engine) return;
    memset(engine, 0, sizeof(*engine));
    engine->config.source_mask = FM_AP_TARGET | FM_AP_FOCUS | FM_AP_MOUSEOVER | FM_AP_NEARBY;
    engine->config.require_stealth = 1;
    engine->config.block_combat = 1;
    engine->config.max_range_yards = 5.0;
    engine->config.scan_interval_ms = 100;
    engine->config.action_delay_ms = 350;
    engine->config.failed_retry_ms = 10000;
    engine->config.confirmation_timeout_ms = 1000;
    engine->config.max_opener_hold_ms = 500;
}

int fm_ap_validate_config(const FM_AP_CONFIG *c) {
    return c && !(c->source_mask & ~(FM_AP_TARGET | FM_AP_FOCUS | FM_AP_MOUSEOVER | FM_AP_NEARBY)) &&
        c->source_mask && c->max_range_yards > 0.0 && c->max_range_yards <= 5.0 &&
        c->scan_interval_ms >= 50 && c->scan_interval_ms <= 5000 &&
        c->action_delay_ms >= 100 && c->action_delay_ms <= 10000 &&
        c->failed_retry_ms >= 500 && c->failed_retry_ms <= 3600000 &&
        c->confirmation_timeout_ms >= 100 && c->confirmation_timeout_ms <= 10000 &&
        c->max_opener_hold_ms <= 5000;
}

int fm_ap_set_config(FM_AP_ENGINE *engine, const FM_AP_CONFIG *config) {
    if (!engine || !fm_ap_validate_config(config)) return 0;
    engine->config = *config;
    if (!config->enabled) engine->opener_deadline_at_ms = 0;
    return 1;
}

int fm_ap_blacklist_guid(FM_AP_ENGINE *e, uint64_t guid) {
    size_t i;
    if (!e || !guid) return 0;
    for (i = 0; i < e->blocked_guid_count; ++i) if (e->blocked_guids[i] == guid) return 1;
    if (e->blocked_guid_count == FM_AP_MAX_BLACKLIST) return 0;
    e->blocked_guids[e->blocked_guid_count++] = guid;
    return 1;
}

int fm_ap_blacklist_entry(FM_AP_ENGINE *e, uint32_t entry) {
    size_t i;
    if (!e || !entry) return 0;
    for (i = 0; i < e->blocked_entry_count; ++i) if (e->blocked_npc_entries[i] == entry) return 1;
    if (e->blocked_entry_count == FM_AP_MAX_BLACKLIST) return 0;
    e->blocked_npc_entries[e->blocked_entry_count++] = entry;
    return 1;
}

void fm_ap_reset_world(FM_AP_ENGINE *e, uint64_t world_epoch) {
    if (!e) return;
    e->world_epoch = world_epoch;
    e->history_count = 0;
    e->pending_guid = 0;
    e->pending_deadline_at_ms = 0;
    e->opener_deadline_at_ms = 0;
    e->next_scan_at_ms = 0;
    e->next_action_at_ms = 0;
}

static FM_AP_HISTORY *find_history(FM_AP_ENGINE *e, uint64_t guid) {
    size_t i;
    for (i = 0; i < e->history_count; ++i)
        if (e->history[i].guid == guid) return &e->history[i];
    return NULL;
}

static FM_AP_HISTORY *slot(FM_AP_ENGINE *e, uint64_t guid) {
    FM_AP_HISTORY *h = find_history(e, guid);
    if (h) return h;
    /* Fail closed rather than evicting a successful GUID and pocketing it again. */
    if (e->history_count == FM_AP_MAX_HISTORY) return NULL;
    h = &e->history[e->history_count++];
    memset(h, 0, sizeof(*h));
    h->guid = guid;
    return h;
}

FM_AP_STATUS fm_ap_status(const FM_AP_ENGINE *e, uint64_t guid) {
    size_t i;
    if (!e || !guid) return FM_AP_NONE;
    for (i = 0; i < e->history_count; ++i)
        if (e->history[i].guid == guid) return e->history[i].status;
    return FM_AP_NONE;
}

static int blocked(const FM_AP_ENGINE *e, const FM_AP_UNIT *u) {
    size_t i;
    for (i = 0; i < e->blocked_guid_count; ++i) if (e->blocked_guids[i] == u->guid) return 1;
    for (i = 0; i < e->blocked_entry_count; ++i) if (e->blocked_npc_entries[i] == u->npc_entry) return 1;
    return 0;
}

int fm_ap_report(FM_AP_ENGINE *e, uint64_t guid, FM_AP_STATUS result, uint64_t now_ms) {
    FM_AP_HISTORY *h;
    if (!e || !guid || e->pending_guid != guid ||
        (result != FM_AP_SUCCESS && result != FM_AP_FAILED && result != FM_AP_UNKNOWN)) return 0;
    h = find_history(e, guid);
    if (!h || h->status != FM_AP_PENDING) return 0;
    h->status = result;
    h->updated_at_ms = now_ms;
    h->next_retry_at_ms = (result == FM_AP_FAILED) ? plus_ms(now_ms, e->config.failed_retry_ms) : UINT64_MAX;
    e->pending_guid = 0;
    e->pending_deadline_at_ms = 0;
    e->opener_deadline_at_ms = 0;
    return 1;
}

int fm_ap_hold_opener(const FM_AP_ENGINE *e, uint64_t now_ms) {
    return e && e->config.enabled && e->pending_guid &&
        e->opener_deadline_at_ms && now_ms < e->opener_deadline_at_ms;
}

int fm_ap_tick(FM_AP_ENGINE *e, const FM_AP_FRAME *f, uint64_t now_ms,
               FM_AP_CAST_GUID cast_guid, void *context) {
    size_t i, count;
    if (!e || !f) return 0;
    if (e->world_epoch != f->world_epoch) fm_ap_reset_world(e, f->world_epoch);
    if (e->pending_guid && now_ms >= e->pending_deadline_at_ms)
        fm_ap_report(e, e->pending_guid, FM_AP_UNKNOWN, now_ms);
    if (!e->config.enabled || !f->game_ready || !f->adapter_verified ||
        !f->is_rogue || !f->player_alive || (e->config.require_stealth && !f->stealthed) ||
        (e->config.block_combat && f->in_combat) || f->player_busy ||
        !f->pickpocket_available || !cast_guid || !f->units) {
        /* Interrupted pending action never implies a successful pocket. */
        if (e->pending_guid && (!e->config.enabled || !f->game_ready || !f->player_alive ||
            (e->config.require_stealth && !f->stealthed) || (e->config.block_combat && f->in_combat)))
            fm_ap_report(e, e->pending_guid, FM_AP_UNKNOWN, now_ms);
        return 0;
    }
    if (e->pending_guid || now_ms < e->next_scan_at_ms || now_ms < e->next_action_at_ms) return 0;
    e->next_scan_at_ms = plus_ms(now_ms, e->config.scan_interval_ms);
    count = f->unit_count < FM_AP_MAX_UNITS ? f->unit_count : FM_AP_MAX_UNITS;
    for (i = 0; i < count; ++i) {
        const FM_AP_UNIT *u = &f->units[i];
        FM_AP_HISTORY *h;
        if (!u->guid || !(u->source_mask & e->config.source_mask) || !u->is_npc ||
            !u->hostile || !u->alive || !u->eligible_known || !u->pickpocketable ||
            !u->in_spell_range || !u->line_of_sight || !u->native_can_cast ||
            !(u->distance_yards >= 0.0 && u->distance_yards <= e->config.max_range_yards) ||
            blocked(e, u)) continue;
        h = find_history(e, u->guid);
        if (h && h->status != FM_AP_NONE &&
            (h->status != FM_AP_FAILED || now_ms < h->next_retry_at_ms)) continue;
        h = slot(e, u->guid);
        if (!h) return 0;
        /* Do not mark attempted if the adapter declined to execute a cast. */
        if (cast_guid(context, u->guid) != FM_AP_ISSUED) continue;
        h->status = FM_AP_PENDING;
        h->updated_at_ms = now_ms;
        h->next_retry_at_ms = UINT64_MAX;
        e->pending_guid = u->guid;
        e->pending_deadline_at_ms = plus_ms(now_ms, e->config.confirmation_timeout_ms);
        e->opener_deadline_at_ms = plus_ms(now_ms, e->config.max_opener_hold_ms);
        e->next_action_at_ms = plus_ms(now_ms, e->config.action_delay_ms);
        return 1;
    }
    return 0;
}
