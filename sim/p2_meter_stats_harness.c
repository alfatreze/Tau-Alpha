/* Host check of the draw-cost measurement in fw2/ui/meters.c (meter_stats): the tick functions are stubs that spend a chosen number of cycles on a fake counter; the spectrum window counter is driven by
 * the harness. Prints lines "name value". */
#include <stdio.h>
#include <stdint.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#include "../fw2/ui/meters.c"
static uint32_t now, win, spend[METER_N], comp_cost;
uint32_t host_rd(uint32_t a)
{
    if (a == R_CYCLES) return now;
    if (a == R_SPEC_ST) return win << 16;
    return 0;
}
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } caps_t caps;
#define IDX_X(key, nm, id, lc, UC, np, cmp) idx_##key,
enum { METER_LIST(IDX_X) };
#define STUB_X(key, nm, id, lc, UC, np, cmp) \
    void meter_##key##_tick(const mtr_in_t *i) { (void)i; now += spend[idx_##key]; } uint16_t meter_##key##_mv[MPD_##UC##_N]; void meter_##key##_apply(void) { } \
    void meter_##key##_preset(uint32_t p) { (void)p; } const char *meter_##key##_preset_name(uint32_t p) { (void)p; return #key; }
METER_LIST(STUB_X)
int gfx_compose_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; return 1; }
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
    return 0;
}
