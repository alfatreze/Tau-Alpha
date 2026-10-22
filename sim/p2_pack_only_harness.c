/* Host check of the pack-only rows of fw2/ui/meters.c (P2_PACKS, B-812): a bundle read from argv[1] (made by tools/player2/pack_bundle.py; none = no packs) is installed through the real loader; the pack-only meter
 * (the Waterfall, row METER_N) must then be a row whose name, Configure descriptors, presets, compose code and limits come from the descriptor its pack carries. Prints what the host sees. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#define P2_PACKS 1
static uint8_t slot_mem[8][0x10000] __attribute__((aligned(4))), scratch_mem[0x1000];
#define PACK_SLOT_DATA(i) (slot_mem[i])
#define PACK_SCRATCH (scratch_mem)
#define PACK_CALL(e, in, api) pack_call((e), (in), (api))
static uint32_t pack_calls[8], stub_text, now, win, spend;
static uint32_t pack_call(uint32_t e, const void *in, const void *api);
#include "../fw2/ui/meters.c"
static uint32_t pack_call(uint32_t e, const void *in, const void *api) { (void)in; (void)api; pack_calls[(e - MTR_PACK_SLOT_BASE) / MTR_PACK_SLOT_SIZE]++; now += spend; return 0; }
static uint32_t builtin[METER_N];
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

static void tick(void) { win++; meters_tick(1); }
int main(int argc, char **argv)
{
    caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    if (argc > 1) { FILE *f = fopen(argv[1], "rb"); static uint8_t buf[0x10000]; uint32_t n = f ? (uint32_t)fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f); meters_packs_load(buf, n); }
    const uint32_t R = getenv("ROW") ? (uint32_t)atoi(getenv("ROW")) : (uint32_t)METER_N;      /* the pack-only row under test */
    printf("present %d count %u list %u state %u\n", meters_present(R), meter_count, meters_list_count(), meters_pack_state(R));
    meters_select(R); printf("select row %u -> cur %u\n", R, meters_cur());
    meters_select(0);
    for (int i = 0; i < 6; i++) { meters_next(1); printf("next -> %u\n", meters_cur()); }
    meters_select(0); meters_next(-1); printf("prev from 0 -> %u\n", meters_cur());
    meters_preset_select(R, 3u); printf("preset of absent/present row after select 3: %u\n", meters_preset_of(R));
    if (!meters_present(R)) return 0;
    const meter_def_t *d = meters_def(R);
    printf("name %s np %u npre %u cmp %u\n", d->name, d->np, meters_npre(R), d->cmp);
    for (uint32_t i = 0; i < d->np; i++) { const mpd_t *p = &d->pd[i]; printf("pd %s %u %u %u %u %u %u %u %u", p->label, p->type, p->hidden, p->min, p->max, p->step, p->nval, p->when_idx, p->when_val); for (uint32_t j = 0; j < p->nval; j++) printf(" [%s]", p->vals[j]); printf("\n"); }
    for (uint32_t k = 0; k < meters_npre(R); k++) { meters_preset_select(R, k); printf("preset %u", k); meters_select(R); printf(" %s", meters_preset_name()); for (uint32_t i = 0; i < d->np; i++) printf(" %u", d->mv[i]); printf("\n"); }
    meters_select(R); win++; meters_tick(1); win++; meters_tick(1);
    printf("ticks pack %u builtin-text %u\n", pack_calls[R], stub_text);
    uint8_t pl[96]; meters_preset_select(R, 0u); const uint32_t pn = meters_payload(R, pl); printf("payload id %u n %u\n", pl[0], pn);
    printf("thumb len %u\n", meters_thumb_len(R));
    return 0;
}
