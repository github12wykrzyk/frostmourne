#include "../src/auto_interrupt/auto_interrupt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { FM_AI_UNIT u; unsigned reads, kicks; int changed, rejected; } MOCK;
static int read_unit(void *context, unsigned slot, FM_AI_UNIT *out) {
    MOCK *m = (MOCK *)context;
    if (slot != FM_AI_TARGET) return 0;
    ++m->reads;
    *out = m->u;
    if (m->changed && m->reads == 2) ++out->cast_token;
    return 1;
}
static int kick(void *context, uint64_t guid, uint64_t token, uint32_t spell) {
    MOCK *m = (MOCK *)context;
    assert(guid == m->u.guid && token == m->u.cast_token && spell == m->u.spell_id);
    ++m->kicks;
    return !m->rejected;
}
static void setup(FM_AI_ENGINE *e, FM_AI_FRAME *f, MOCK *m) {
    fm_ai_init(e);
    memset(f, 0, sizeof(*f)); memset(m, 0, sizeof(*m));
    e->config.enabled = e->config.kick_enabled = 1;
    e->config.focus_enabled = 0;
    f->world_epoch = 1; f->game_ready = f->adapter_verified = 1;
    f->is_rogue = f->player_alive = 1;
    m->u.world_epoch = 1; m->u.guid = 12; m->u.cast_token = 34;
    m->u.spell_id = 56; m->u.started_at_ms = 500;
    m->u.ends_at_ms = 1400; m->u.observed_at_ms = 1000;
    m->u.exists = m->u.hostile = m->u.casting = 1;
    m->u.interruptible = m->u.kick_ready = m->u.can_act = m->u.in_spell_range = 1;
    m->u.energy = 50;
}
int main(void) {
    FM_AI_ENGINE e; FM_AI_FRAME f; MOCK m;
    setup(&e,&f,&m);
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_ATTEMPTED);
    assert(m.kicks == 1 && m.reads == 2);
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_DUPLICATE && m.kicks == 1);
    setup(&e,&f,&m); m.u.interruptible = 0;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_NOT_INTERRUPTIBLE && !m.kicks);
    setup(&e,&f,&m); m.u.casting = 0;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_NO_CAST && !m.kicks);
    setup(&e,&f,&m); m.u.observed_at_ms = 800;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_STALE && !m.kicks);
    setup(&e,&f,&m); m.u.ends_at_ms = 1100;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_OUT_OF_WINDOW && !m.kicks);
    setup(&e,&f,&m); m.u.in_spell_range = 0;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_OUT_OF_RANGE && !m.kicks);
    setup(&e,&f,&m); m.u.energy = 0;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_NOT_READY && !m.kicks);
    setup(&e,&f,&m); m.changed = 1;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_CAST_CHANGED && !m.kicks);
    setup(&e,&f,&m); m.rejected = 1;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_ACTION_REJECTED && m.kicks == 1);
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_DUPLICATE && m.kicks == 1);
    setup(&e,&f,&m); f.adapter_verified = 0;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_ADAPTER_UNVERIFIED && !m.kicks);
    setup(&e,&f,&m); e.config.enabled = 0;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_DISABLED && !m.kicks);
    setup(&e,&f,&m); f.world_epoch = 2; m.u.world_epoch = 2;
    assert(fm_ai_tick(&e,&f,1000,read_unit,kick,&m) == FM_AI_ATTEMPTED && m.kicks == 1);
    puts("AUTO INTERRUPT NATIVE TESTS: PASS (decision engine only; no game adapter)");
    return 0;
}
