/* Host check of the per-meter draw-command budget in fw2/ui/meters.c (cores/meter_budget_core.h): the tick functions are stubs that "issue" a chosen number of engine commands by advancing the gfx_cmds
 * counter, the spectrum window counter is driven by the harness. Prints lines "name value". */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#include "../fw2/generated/meter_registry.h"
#include "../fw2/cores/meter_pack_core.h"
#include "../fw2/ui/meter.h"
#define P2_PACKS 1                                                     /* a second meter: Master VU as a row through its descriptor (argv[1]) run by a stub */
static uint8_t slot_mem[8][0x10000] __attribute__((aligned(4))), scratch_mem[0x1000];
#define PACK_SLOT_DATA(i) (slot_mem[i])
#define PACK_SCRATCH (scratch_mem)
#define PACK_CALL(e, in, api) pack_call((e), (in), (api))
#include "../fw2/cores/gfx_touch_core.h"
static uint32_t now, win, spend[METER_SLOTS], drawn[METER_SLOTS], scan, touch_y, touch_h, last_degrade_seen;
static gt_t gfx_touch; uint32_t gfx_cmds;
static uint32_t pack_call(uint32_t e, const void *in, const void *api) { (void)api; const uint32_t r = (e - MTR_PACK_SLOT_BASE) / MTR_PACK_SLOT_SIZE; drawn[r]++; last_degrade_seen = ((const mtr_in_t *)in)->degrade; gfx_cmds += spend[r]; gt_note(&gfx_touch, touch_y, touch_h); return 0; }
#include "../fw2/ui/meters.c"
#include "p2_dyn_register.h"
void gfx_touch_reset(void) { gt_reset(&gfx_touch); } uint32_t gfx_touch_rows(void) { return gt_visible(&gfx_touch) ? ((uint32_t)gfx_touch.y0 << 16) | gfx_touch.y1 : 0u; } uint32_t gfx_cur_buf(void) { return 0; } uint32_t gfx_front_buf(void) { return 0; }
uint32_t host_rd(uint32_t a) { if (a == R_CYCLES) return now; if (a == R_SPEC_ST) return win << 16; if (a == R_SCAN) return scan; return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } uint32_t degrade_level; uint32_t mp3play_degrade_now(void) { return degrade_level; } caps_t caps;
#define IDX_X(key, nm, id, lc, UC, np, cmp) idx_##key,
enum { METER_LIST(IDX_X) };
#define STUB_X(key, nm, id, lc, UC, np, cmp) \
    void meter_##key##_tick(const mtr_in_t *i) { drawn[idx_##key]++; last_degrade_seen = i->degrade; gfx_cmds += spend[idx_##key]; gt_note(&gfx_touch, touch_y, touch_h); } uint16_t meter_##key##_mv[MPD_##UC##_N]; void meter_##key##_apply(void) { } \
    void meter_##key##_preset(uint32_t p) { (void)p; } const char *meter_##key##_preset_name(uint32_t p) { (void)p; return #key; }
METER_LIST(STUB_X)
int gfx_compose_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; return 0; }
void gfx_compose_present(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t f, uint16_t b) { (void)x; (void)y; (void)s; (void)f; (void)b; return 0; }
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
int gfx_blend_ready(void) { return 0; } void gfx_blend_on(uint32_t a) { (void)a; } void gfx_blend_off(void) { }
void gfx_blend_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int gfx_mb_write(uint32_t a, uint32_t d) { (void)a; (void)d; return 0; } int gfx_mb_read(uint32_t a, uint32_t *d) { (void)a; *d = 0; return 0; }
void gfx_fence(void) { }
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { (void)s; (void)c; (void)n; }
void gfx_cblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_bar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint16_t f, uint16_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_scroll_down(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfx_scroll_left(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfx_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_blit_abs(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_sblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
static void tick(void) { win++; meters_tick(1); }
static uint32_t run(uint32_t n) { const uint32_t d0 = drawn[cur]; for (uint32_t i = 0; i < n; i++) tick(); return drawn[cur] - d0; }
int main(int argc, char **argv)
{
    caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50); dyn_load_files(argc - 1, argv + 1);
    const uint32_t B = meter_cmd_budget[idx_winamp_bars];
    printf("ceiling bars %u\n", B);
    spend[idx_winamp_bars] = B; tick();                          /* the forced first tick: not judged */
    printf("inside self %u over %u last %u\n", meters_self_degrade(0), meter_stats.over, meter_stats.cmds_last);
    run(4); printf("exactly at the ceiling: self %u over %u\n", meters_self_degrade(0), meter_stats.over);
    spend[idx_winamp_bars] = B + 1; run(1); printf("one over: self %u over %u\n", meters_self_degrade(0), meter_stats.over);
    printf("max %u\n", meter_stats.cmds_max);
    /* at level 1 every second window is drawn: 8 windows draw 4 pictures, each still over the ceiling, so the level rises to 2 (every fourth) */
    const uint32_t d1 = run(8); printf("over for 8 windows: drawn %u self %u\n", d1, meters_self_degrade(0));
    printf("the meter was told degrade %u\n", last_degrade_seen);
    printf("a forced repaint is not judged: "); spend[idx_winamp_bars] = B * 10u; meters_enter(0, 0, 100, 50); tick(); printf("over %u self %u\n", meter_stats.over, meters_self_degrade(0));
    spend[idx_winamp_bars] = 1; const uint32_t d2 = run(64); printf("back inside for 64 windows: drawn %u self %u\n", d2, meters_self_degrade(0));
    run(64); printf("and a while longer: self %u\n", meters_self_degrade(0));
    degrade_level = 1; spend[idx_winamp_bars] = B + 1; run(4); degrade_level = 0;
    printf("the governor's level wins when higher: told %u\n", last_degrade_seen);
    meters_select(4); spend[4] = 1000; tick(); tick(); printf("another meter has its own level: vu self %u bars self %u\n", meters_self_degrade(4), meters_self_degrade(idx_winamp_bars));
    /* beam exposure: rows 50..149 written; the beam (line = row + 4) is inside them, ahead of them, behind them, or the bitstream has no beam counter */
    meters_init(); meters_enter(0, 0, 100, 50); tick(); meters_select(idx_winamp_bars); touch_y = 50; touch_h = 100; spend[idx_winamp_bars] = 1; scan = 0; tick();
    uint32_t d0 = meter_stats.direct, e0 = meter_stats.exposed;
    scan = (1u << 9) | (4u + 100u); tick(); printf("beam inside: rows %u-%u direct %u exposed %u\n", meter_stats.rows >> 16, meter_stats.rows & 0xFFFFu, meter_stats.direct - d0, meter_stats.exposed - e0);
    d0 = meter_stats.direct; e0 = meter_stats.exposed;
    scan = (1u << 9) | (4u + 20u); tick(); printf("beam well ahead of the rows: direct %u exposed %u\n", meter_stats.direct - d0, meter_stats.exposed - e0);
    d0 = meter_stats.direct; e0 = meter_stats.exposed;
    scan = (1u << 9) | (4u + 200u); tick(); printf("beam already past: direct %u exposed %u\n", meter_stats.direct - d0, meter_stats.exposed - e0);
    d0 = meter_stats.direct; e0 = meter_stats.exposed;
    scan = (1u << 9) | 2u; tick(); printf("vertical blanking: direct %u exposed %u\n", meter_stats.direct - d0, meter_stats.exposed - e0);
    d0 = meter_stats.direct; e0 = meter_stats.exposed;
    scan = 4u + 100u; tick(); printf("no beam counter: direct %u exposed %u\n", meter_stats.direct - d0, meter_stats.exposed - e0);
    touch_h = 0; d0 = meter_stats.direct; scan = (1u << 9) | (4u + 100u); tick(); printf("nothing visible drawn: rows %u direct %u\n", meter_stats.rows, meter_stats.direct - d0);
    touch_y = 400; touch_h = 50; scan = (1u << 9) | (4u + 400u - 360u); d0 = meter_stats.direct; e0 = meter_stats.exposed; tick(); printf("stash rows only: direct %u exposed %u\n", meter_stats.direct - d0, meter_stats.exposed - e0);
    return 0;
}
