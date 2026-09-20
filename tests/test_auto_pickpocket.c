#include "../src/auto_pickpocket/auto_pickpocket.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int casts;
static uint64_t casted[1024];
static int accept = 1;
static FM_AP_CAST_RESULT cast(void *unused, uint64_t guid) {
    (void)unused;
    if (!accept) return FM_AP_NOT_ISSUED;
    casted[casts++] = guid;
    return FM_AP_ISSUED;
}
static FM_AP_UNIT unit(uint64_t guid) {
    FM_AP_UNIT u;
    memset(&u, 0, sizeof(u));
    u.guid = guid; u.npc_entry = 7; u.source_mask = FM_AP_NEARBY;
    u.is_npc = u.hostile = u.alive = u.eligible_known = u.pickpocketable = 1;
    u.in_spell_range = u.line_of_sight = u.native_can_cast = 1;
    u.distance_yards = 3.0;
    return u;
}
static FM_AP_FRAME frame(FM_AP_UNIT *u, size_t count) {
    FM_AP_FRAME f;
    memset(&f, 0, sizeof(f));
    f.world_epoch = 1; f.game_ready = f.is_rogue = f.player_alive = 1;
    f.stealthed = f.pickpocket_available = f.adapter_verified = 1;
    f.units = u; f.unit_count = count;
    return f;
}
static void enable(FM_AP_ENGINE *e) {
    FM_AP_CONFIG c = e->config;
    c.enabled = 1; assert(fm_ap_set_config(e, &c));
}
static void clear_casts(void) { casts = 0; accept = 1; }

static void test_disabled_and_verified_gate(void) {
    FM_AP_ENGINE e; FM_AP_UNIT u = unit(1); FM_AP_FRAME f = frame(&u, 1);
    fm_ap_init(&e); clear_casts();
    assert(!fm_ap_tick(&e, &f, 100, cast, NULL));
    enable(&e); f.adapter_verified = 0;
    assert(!fm_ap_tick(&e, &f, 100, cast, NULL));
    f.adapter_verified = 1;
    assert(fm_ap_tick(&e, &f, 100, cast, NULL));
    assert(casts == 1 && casted[0] == 1);
    assert(fm_ap_status(&e, 1) == FM_AP_PENDING);
    assert(fm_ap_hold_opener(&e, 200));
    assert(!fm_ap_hold_opener(&e, 600));
    assert(fm_ap_report(&e, 1, FM_AP_SUCCESS, 250));
    assert(!fm_ap_hold_opener(&e, 251));
    assert(!fm_ap_tick(&e, &f, 500, cast, NULL));
    assert(casts == 1);
}
static void test_many_guid_and_unconfirmed(void) {
    FM_AP_ENGINE e; FM_AP_UNIT us[3] = {unit(11), unit(12), unit(13)};
    FM_AP_FRAME f = frame(us, 3);
    fm_ap_init(&e); enable(&e); clear_casts();
    assert(fm_ap_tick(&e, &f, 100, cast, NULL));
    assert(!fm_ap_tick(&e, &f, 200, cast, NULL));
    assert(fm_ap_report(&e, 11, FM_AP_SUCCESS, 250));
    assert(fm_ap_tick(&e, &f, 500, cast, NULL));
    assert(fm_ap_report(&e, 12, FM_AP_UNKNOWN, 530));
    assert(fm_ap_tick(&e, &f, 900, cast, NULL));
    assert(fm_ap_report(&e, 13, FM_AP_SUCCESS, 930));
    assert(!fm_ap_tick(&e, &f, 1500, cast, NULL));
    assert(casts == 3 && casted[0] == 11 && casted[1] == 12 && casted[2] == 13);
}
static void test_timeout_and_retry(void) {
    FM_AP_ENGINE e; FM_AP_UNIT u = unit(21); FM_AP_FRAME f = frame(&u, 1);
    fm_ap_init(&e); enable(&e); clear_casts();
    assert(fm_ap_tick(&e, &f, 100, cast, NULL));
    assert(!fm_ap_tick(&e, &f, 1101, cast, NULL));
    assert(fm_ap_status(&e, 21) == FM_AP_UNKNOWN);
    assert(!fm_ap_report(&e, 21, FM_AP_SUCCESS, 1200));
    assert(!fm_ap_tick(&e, &f, 2000, cast, NULL));
    assert(casts == 1);
    fm_ap_reset_world(&e, 2); f.world_epoch = 2;
    assert(fm_ap_tick(&e, &f, 2500, cast, NULL));
    assert(fm_ap_report(&e, 21, FM_AP_FAILED, 2600));
    assert(!fm_ap_tick(&e, &f, 5000, cast, NULL));
    assert(fm_ap_tick(&e, &f, 12600, cast, NULL));
    assert(casts == 3);
}
static void test_fail_closed_filters(void) {
    FM_AP_ENGINE e; FM_AP_UNIT u = unit(31); FM_AP_FRAME f = frame(&u, 1);
    fm_ap_init(&e); enable(&e); clear_casts();
    f.stealthed = 0; assert(!fm_ap_tick(&e, &f, 100, cast, NULL)); f.stealthed = 1;
    f.in_combat = 1; assert(!fm_ap_tick(&e, &f, 100, cast, NULL)); f.in_combat = 0;
    f.player_busy = 1; assert(!fm_ap_tick(&e, &f, 100, cast, NULL)); f.player_busy = 0;
    u.eligible_known = 0; assert(!fm_ap_tick(&e, &f, 100, cast, NULL)); u.eligible_known = 1;
    u.is_npc = 0; assert(!fm_ap_tick(&e, &f, 200, cast, NULL)); u.is_npc = 1;
    u.distance_yards = 6.0; assert(!fm_ap_tick(&e, &f, 300, cast, NULL)); u.distance_yards = 3.0;
    u.in_spell_range = 0; assert(!fm_ap_tick(&e, &f, 400, cast, NULL)); u.in_spell_range = 1;
    u.line_of_sight = 0; assert(!fm_ap_tick(&e, &f, 500, cast, NULL)); u.line_of_sight = 1;
    u.native_can_cast = 0; assert(!fm_ap_tick(&e, &f, 600, cast, NULL)); u.native_can_cast = 1;
    accept = 0; assert(!fm_ap_tick(&e, &f, 700, cast, NULL));
    assert(fm_ap_status(&e, 31) == FM_AP_NONE);
    accept = 1; assert(fm_ap_tick(&e, &f, 800, cast, NULL));
    assert(fm_ap_report(&e, 31, FM_AP_SUCCESS, 850));
    assert(!fm_ap_tick(&e, &f, 2000, cast, NULL));
    assert(casts == 1);
}
static void test_blacklist_world_and_interruption(void) {
    FM_AP_ENGINE e; FM_AP_UNIT us[2] = {unit(41), unit(42)}; FM_AP_FRAME f = frame(us, 2);
    fm_ap_init(&e); enable(&e); clear_casts();
    assert(fm_ap_blacklist_guid(&e, 41));
    assert(fm_ap_tick(&e, &f, 100, cast, NULL) && casted[0] == 42);
    f.stealthed = 0;
    assert(!fm_ap_tick(&e, &f, 200, cast, NULL));
    assert(fm_ap_status(&e, 42) == FM_AP_UNKNOWN);
    f.stealthed = 1; f.world_epoch = 2;
    assert(fm_ap_tick(&e, &f, 500, cast, NULL) && casted[1] == 42);
    assert(!fm_ap_report(&e, 9999, FM_AP_SUCCESS, 501));
    assert(fm_ap_report(&e, 42, FM_AP_FAILED, 502));
    fm_ap_reset_world(&e, 3); f.world_epoch = 3;
    assert(fm_ap_blacklist_entry(&e, 7));
    assert(!fm_ap_tick(&e, &f, 900, cast, NULL));
}
static void test_invalid_config(void) {
    FM_AP_ENGINE e; FM_AP_CONFIG c;
    fm_ap_init(&e); c = e.config;
    c.max_range_yards = 100; assert(!fm_ap_set_config(&e, &c));
    c.max_range_yards = 5.0; c.scan_interval_ms = 1;
    assert(!fm_ap_set_config(&e, &c));
    assert(e.config.enabled == 0);
}
int main(void) {
    test_disabled_and_verified_gate();
    test_many_guid_and_unconfirmed();
    test_timeout_and_retry();
    test_fail_closed_filters();
    test_blacklist_world_and_interruption();
    test_invalid_config();
    puts("AUTO PICKPOCKET CORE: PASS (six no-game suites; no WoW-native adapter)");
    return 0;
}
