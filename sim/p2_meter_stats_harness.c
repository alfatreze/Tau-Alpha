/* Host check of the draw-cost measurement in fw2/ui/meters.c (meter_stats): the tick functions are stubs that spend a chosen number of cycles on a fake counter; the spectrum window counter is driven by
 * the harness. Prints lines "name value". */
#include <stdio.h>
#include <stdint.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#include "../fw2/ui/meters.c"
static uint32_t now, win, spend[METER_N], comp_cost, sum_dt[METER_N], comp_calls;
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
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void show(const char *tag)
{
    printf("%s ticks %u shed %u last %u compose %u max", tag, meter_stats.ticks, meter_stats.shed, meter_stats.last_cyc, meter_stats.compose_cyc);
    for (uint32_t m = 0; m < METER_N; m++) printf(" %u", meter_stats.max_cyc[m]);
    printf("\n");
}
int main(void)
{
    caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    for (uint32_t m = 0; m < METER_N; m++) spend[m] = 100u * (m + 1u);
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
    /* Cadence and the compose gate of the Waterfall (meter 5): mode SPECTROGRAM (0), 100 ms a row, a window every 30 ms for 1.2 s: twelve pictures with the remainder carried (a clock reset each time would give ten),
       each called with exactly the periods that elapsed, and composed; in mode ZONES (1) every window draws and nothing is composed. */
    for (uint32_t m = 0; m < METER_N; m++) { spend[m] = 0; sum_dt[m] = 0; }
    meters_select(idx_waterfall_grid); meter_waterfall_grid_mv[MPD_WF_N > 1u ? 1 : 0] = 100u; meter_waterfall_grid_mv[0] = 0u;
    win++; meters_tick(1); degrade_level = 0;                                                    /* the forced first draw */
    t0 = meter_stats.ticks; comp_calls = 0; sum_dt[idx_waterfall_grid] = 0;
    for (int i = 0; i < 40; i++) { now += 30u * (CLK_HZ / 1000u); win++; meters_tick(1); }
    printf("cad drawn %u sumdt %u composed %u\n", meter_stats.ticks - t0, sum_dt[idx_waterfall_grid], comp_calls);
    meter_waterfall_grid_mv[0] = 1u; t0 = meter_stats.ticks; comp_calls = 0; sum_dt[idx_waterfall_grid] = 0;
    for (int i = 0; i < 10; i++) { now += 30u * (CLK_HZ / 1000u); win++; meters_tick(1); }
    printf("zones drawn %u composed %u\n", meter_stats.ticks - t0, comp_calls);
    return 0;
}
