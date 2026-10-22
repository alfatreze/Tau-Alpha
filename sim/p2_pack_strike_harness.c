/* Host check of the pack strike rule in fw2/ui/meters.c (P2_PACKS, cores/pack_strike_core.h): a bundle from argv[1] with the Master VU pack; the pack call spends a chosen number of cycles (argv[2..] a list of costs, one per
 * tick, 'F' forces the tick). Prints, per tick, whether the pack or the built-in drew, the pack state and the counters. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#define P2_PACKS 1
static uint8_t slot_mem[8][0x10000], scratch_mem[0x1000];
#define PACK_SLOT_DATA(i) (slot_mem[i])
#define PACK_SCRATCH (scratch_mem)
#define PACK_CALL(e, in, api) pack_call((e), (in), (api))
static uint32_t pack_calls, now, spend, stub_text;
static uint32_t pack_call(uint32_t e, const void *in, const void *api);
#include "../fw2/ui/meters.c"
static uint32_t pack_call(uint32_t e, const void *in, const void *api) { (void)e; (void)in; (void)api; pack_calls++; now += spend; return 0; }
static uint32_t win, builtin[METER_N];
uint32_t host_rd(uint32_t a) { if (a == R_CYCLES) return now; if (a == R_SPEC_ST) return win << 16; return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } uint32_t mp3play_degrade_now(void) { return 0; } caps_t caps;
#define IDX_X(key, nm, id, lc, UC, np, cmp) idx_##key,
enum { METER_LIST(IDX_X) };
#define STUB_X(key, nm, id, lc, UC, np, cmp) \
    void meter_##key##_tick(const mtr_in_t *i) { (void)i; builtin[idx_##key]++; } uint16_t meter_##key##_mv[MPD_##UC##_N]; void meter_##key##_apply(void) { } \
    void meter_##key##_preset(uint32_t p) { (void)p; } const char *meter_##key##_preset_name(uint32_t p) { (void)p; return #key; }
METER_LIST(STUB_X)
int gfx_compose_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; return 0; }
void gfx_compose_present(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; }
uint32_t gfx_cmds; void gfx_touch_reset(void) { } uint32_t gfx_touch_rows(void) { return 0u; } uint32_t gfx_cur_buf(void) { return 0; } uint32_t gfx_front_buf(void) { return 0; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t f, uint16_t b) { (void)x; (void)y; (void)s; (void)f; (void)b; stub_text++; return 0; }
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
int gfx_blend_ready(void) { return 0; } void gfx_blend_on(uint32_t a) { (void)a; } void gfx_blend_off(void) { }
void gfx_blend_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int gfx_mb_write(uint32_t a, uint32_t d) { (void)a; (void)d; return 0; } int gfx_mb_read(uint32_t a, uint32_t *d) { (void)a; *d = 0; return 0; }
void gfx_fence(void) { }
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { (void)s; (void)c; (void)n; }
void gfx_cblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_bar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint16_t f, uint16_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_scroll_down(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfx_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_blit_abs(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_sblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
int main(int argc, char **argv)
{
    caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    FILE *f = fopen(argv[1], "rb"); static uint8_t buf[0x10000]; uint32_t n = f ? (uint32_t)fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f); meters_packs_load(buf, n);
    const uint32_t row = getenv("ROW") ? (uint32_t)atoi(getenv("ROW")) : (uint32_t)idx_winamp_bars;      /* a pack-only row falls back to its "pack not running" note (a forced picture draws it), the built-in meter to its built-in tick */
    meters_select(row);
    printf("limit %u\n", cyc_limit_of(row));
    for (int a = 2; a < argc; a++) {
        const int force = argv[a][0] == 'F'; spend = (uint32_t)strtoul(argv[a] + (force ? 1 : 0), 0, 10);
        if (force) force_next = 1;
        const uint32_t p0 = pack_calls, b0 = row < METER_N ? builtin[row] : stub_text; win++; meters_tick(1);
        printf("%s state %u struck %u overruns %u\n", pack_calls > p0 ? "pack" : (row < METER_N ? builtin[row] : stub_text) > b0 ? "built-in" : "none", meters_pack_state(row), meter_stats.struck, meter_stats.overruns);
    }
    return 0;
}
