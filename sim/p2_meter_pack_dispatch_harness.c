/* Host check of the pack dispatch in fw2/ui/meters.c (P2_PACKS): which handler draws each meter, for a bundle read from argv[1] (made by tools/player2/pack_bundle.py). The built-in ticks and the pack entry
 * are stubs that count their calls; the slots, scratch and call are host buffers (PACK_SLOT_DATA, PACK_SCRATCH, PACK_CALL). Prints, per meter: built-in calls, pack calls, the pack's params pointer check,
 * and at the end the Info summary. */
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
static uint32_t pack_calls[8], activations;
static const void *last_params;
static uint32_t pack_call(uint32_t e, const void *in, const void *api);
#include "../fw2/ui/meters.c"
static uint32_t pack_call(uint32_t e, const void *in, const void *api) { (void)in; pack_calls[(e - MTR_PACK_SLOT_BASE) / MTR_PACK_SLOT_SIZE]++; last_params = ((const mtr_host_api_t *)api)->params; return 0; }
static uint32_t now, win, builtin[METER_N];
uint32_t host_rd(uint32_t a) { if (a == R_CYCLES) return now; if (a == R_SPEC_ST) return win << 16; return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } uint32_t degrade_level; uint32_t mp3play_degrade_now(void) { return degrade_level; } caps_t caps;
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
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t f, uint16_t b) { (void)x; (void)y; (void)s; (void)f; (void)b; return 0; }
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
int gfx_blend_ready(void) { return 0; } void gfx_blend_on(uint32_t a) { (void)a; } void gfx_blend_off(void) { }
void gfx_blend_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int gfx_mb_write(uint32_t a, uint32_t d) { (void)a; (void)d; return 0; } int gfx_mb_read(uint32_t a, uint32_t *d) { (void)a; *d = 0; return 0; }
void gfx_fence(void) { }
void gfx_bar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint16_t f, uint16_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_scroll_down(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfx_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_blit_abs(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_sblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
int main(int argc, char **argv)
{
    caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    if (argc > 1) { FILE *f = fopen(argv[1], "rb"); static uint8_t buf[0x10000]; uint32_t n = f ? (uint32_t)fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f); meters_packs_load(buf, n); }
    for (uint32_t pass = 0; pass < 2; pass++)                                   /* two passes: back to a meter after the others (the pack state is activated again) */
        for (uint32_t m = 0; m < METER_N; m++) { meters_select(m); win++; meters_tick(1); }
    for (uint32_t m = 0; m < METER_N; m++) printf("m%u builtin %u pack %u state %u\n", m, builtin[m], m < 8 ? pack_calls[m] : 0u, meters_pack_state(m));
    uint32_t r, e, fe, sn; meters_pack_info(&r, &e, &fe, &sn);
    printf("info run %x err %u file %u seen %u\n", r, e, fe, sn);
    return 0;
}
