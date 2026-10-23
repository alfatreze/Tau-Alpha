/* Host check of the draw-cost measurement in fw2/ui/meters.c (meter_stats): the tick functions are stubs that spend a chosen number of cycles on a fake counter; the spectrum window counter is driven by
 * the harness. Prints lines "name value". */
#include <stdio.h>
#include <stdint.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#include "../fw2/generated/meter_registry.h"
#define P2_PACKS 1                                                     /* the Waterfall is pack-only: it is a row only through its pack's descriptor (argv[1]), run by a stub */
static uint8_t slot_mem[8][0x10000] __attribute__((aligned(4))), scratch_mem[0x1000];
#define PACK_SLOT_DATA(i) (slot_mem[i])
#define PACK_SCRATCH (scratch_mem)
#define PACK_CALL(e, in, api) pack_call((e), (in), (api))
static uint32_t now, win, spend[METER_SLOTS + 1], comp_cost, sum_dt[METER_SLOTS + 1], comp_calls;
static uint32_t pack_call(uint32_t e, const void *in, const void *api);
#include "../fw2/ui/meters.c"
#include "p2_dyn_register.h"
static uint32_t pack_call(uint32_t e, const void *in, const void *api) { (void)api; sum_dt[(e - MTR_PACK_SLOT_BASE) / MTR_PACK_SLOT_SIZE] += ((const mtr_in_t *)in)->dt_ms; now += spend[(e - MTR_PACK_SLOT_BASE) / MTR_PACK_SLOT_SIZE]; return 0; }
uint32_t host_rd(uint32_t a)
{
    if (a == R_CYCLES) return now;
    if (a == R_SPEC_ST) return win << 16;
    return 0;
}
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } uint32_t degrade_level; uint32_t mp3play_degrade_now(void) { return degrade_level; } caps_t caps;
#define IDX_X(key, nm, id, lc, UC, np, cmp) idx_##key,
enum { METER_LIST(IDX_X) };
#define STUB_X(key, nm, id, lc, UC, np, cmp) \
    void meter_##key##_tick(const mtr_in_t *i) { sum_dt[idx_##key] += i->dt_ms; now += spend[idx_##key]; } uint16_t meter_##key##_mv[MPD_##UC##_N]; void meter_##key##_apply(void) { } \
    void meter_##key##_preset(uint32_t p) { (void)p; } const char *meter_##key##_preset_name(uint32_t p) { (void)p; return #key; }
METER_LIST(STUB_X)
int gfx_compose_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; comp_calls++; return 1; }
void gfx_compose_present(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; now += comp_cost; }
uint32_t gfx_cmds; void gfx_touch_reset(void) { } uint32_t gfx_touch_rows(void) { return 0u; } uint32_t gfx_cur_buf(void) { return 0; } uint32_t gfx_front_buf(void) { return 0; }
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
static void show(const char *tag)
{
    printf("%s ticks %u shed %u last %u compose %u max", tag, meter_stats.ticks, meter_stats.shed, meter_stats.last_cyc, meter_stats.compose_cyc);
    for (uint32_t m = 0; m < METER_SLOTS; m++) printf(" %u", meter_stats.max_cyc[m]);
    printf("\n");
}
int main(int argc, char **argv)
{
    caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    printf("rows %u count %u\n", dyn_load_files(argc - 1, argv + 1), meter_count);
    for (uint32_t m = 0; m < METER_SLOTS; m++) spend[m] = 100u * (m + 1u);
    comp_cost = 40;
    win = 1; meters_tick(1);                                  /* meter 0: 100 cycles, no compose in the table (compose flag 0) */
    show("a");
    meters_select(2); win = 2; meters_tick(1);                /* meter 2 (Chladni): 300 + compose 40 */
    show("b");
    win = 3; meters_tick(0); meters_tick(0); meters_tick(0);  /* shed three times for one window: counted once */
    show("c");
    meters_tick(1);                                           /* the pending window is drawn now: a tick, no second shed */
    show("d");
    meters_tick(1); meters_tick(0);                           /* no new window: nothing counts */
    show("e");
    win = 4; spend[2] = 900; meters_tick(1);                  /* a slower tick raises the maximum */
    show("f");
    win = 5; spend[2] = 50; meters_tick(1);                   /* a faster one does not lower it */
    show("g");
    uint32_t t0 = meter_stats.ticks, d0 = meter_stats.degraded;
    degrade_level = 1; for (int i = 0; i < 8; i++) { win++; meters_tick(1); }                  /* level 1: every second window is drawn */
    printf("deg1 drawn %u skipped %u\n", meter_stats.ticks - t0, meter_stats.degraded - d0);
    t0 = meter_stats.ticks; d0 = meter_stats.degraded;
    degrade_level = 2; for (int i = 0; i < 8; i++) { win++; meters_tick(1); }                  /* level 2: every fourth */
    printf("deg2 drawn %u skipped %u\n", meter_stats.ticks - t0, meter_stats.degraded - d0);
    t0 = meter_stats.ticks; d0 = meter_stats.degraded;
    meters_select(1); win++; meters_tick(1);                                                    /* a forced repaint is never skipped */
    printf("force drawn %u skipped %u\n", meter_stats.ticks - t0, meter_stats.degraded - d0);
    degrade_level = 0; t0 = meter_stats.ticks; for (int i = 0; i < 3; i++) { win++; meters_tick(1); }
    printf("deg0 drawn %u\n", meter_stats.ticks - t0);
    /* The compose gate of the Waterfall (meter 5): composed in mode SPECTROGRAM (0), not in ZONES (1). */
    meters_select(5); dyn_mv[4][0] = 0u; win++; meters_tick(1);
    comp_calls = 0; for (int i = 0; i < 4; i++) { now += 30u * (CLK_HZ / 1000u); win++; meters_tick(1); }
    printf("spectrogram composed %u\n", comp_calls);
    dyn_mv[4][0] = 1u; comp_calls = 0; for (int i = 0; i < 4; i++) { now += 30u * (CLK_HZ / 1000u); win++; meters_tick(1); }
    printf("zones composed %u\n", comp_calls);
    return 0;
}
