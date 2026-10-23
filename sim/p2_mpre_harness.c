/* Host check of the preset bookkeeping in fw2/ui/meters.c: the modules are stubs that record the last preset they were told. Prints lines "name value". */
#include <stdio.h>
#include <stdint.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#define P2_PACKS 1                                                     /* the pack-only meters are rows through their descriptors (argv[2..]) */
static uint8_t slot_mem[8][0x10000] __attribute__((aligned(4))), scratch_mem[0x1000];
#define PACK_SLOT_DATA(i) (slot_mem[i])
#define PACK_SCRATCH (scratch_mem)
#include "../fw2/ui/meters.c"
#include "p2_dyn_register.h"
uint32_t host_rd(uint32_t a) { (void)a; return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } uint32_t mp3play_degrade_now(void) { return 0; } caps_t caps;
static uint32_t last[METER_N + 1];
#define IDX_X(key, nm, id, lc, UC, np, cmp) idx_##key,
enum { METER_LIST(IDX_X) };
#define STUB_X(key, nm, id, lc, UC, np, cmp) \
    void meter_##key##_tick(const mtr_in_t *i) { (void)i; } uint16_t meter_##key##_mv[MPD_##UC##_N]; void meter_##key##_apply(void) { } \
    void meter_##key##_preset(uint32_t p) { last[idx_##key] = p; } const char *meter_##key##_preset_name(uint32_t p) { (void)p; return #key; }
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
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { (void)s; (void)c; (void)n; }
void gfx_cblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_bar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint16_t f, uint16_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_scroll_down(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfx_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_blit_abs(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_sblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
static uint8_t fbuf[8192];
int main(int argc, char **argv)
{
    meters_init();
    const uint32_t up = argc > 2 ? dyn_load_files(argc - 2, argv + 2) : 0u;
    if (argc > 1 && argv[1][0] != '-') {                               /* METR mode: load the file, then print every user preset of every meter: "id npre name v0 v1 ..." */
        FILE *f = fopen(argv[1], "rb"); uint32_t n = (uint32_t)fread(fbuf, 1, sizeof fbuf, f); fclose(f);
        printf("loaded %u\n", meters_metr_load(fbuf, n));
        for (uint32_t m = 0; m < meter_count; m++) {
            printf("meter %u npre %u boot %u\n", m, meters_npre(m), meters_preset_of(m));
            for (uint32_t p = 0; p < meters_npre(m); p++) {
                meters_select(m); meters_preset_select(m, p);
                printf("%s", meters_preset_name()); for (uint32_t i = 0; i < mdef(m)->np; i++) printf(" %u", mdef(m)->mv[i]); printf("\n");
            }
        }
        return 0;
    }
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 't') {          /* tuning pool mode: "-t" then the descriptors */
        uint32_t w[TUNE_SLOTS * TUNE_WORDS];
#define SLOT_METER(s) ({ uint32_t mt = 99, pr = 0; uint8_t ix[TUNE_MAX]; uint16_t vl[TUNE_MAX]; tune_unpack(&w[(s) * TUNE_WORDS], tune_max_of, &mt, &pr, ix, vl) ? mt : 99u; })
#define SHOW(tag) do { meters_tune_words(w); printf("%s A %u B %u newer %u\n", tag, SLOT_METER(0), SLOT_METER(1), meters_tune_newer()); } while (0)
#define EDIT(m, i) do { uint32_t np = 0; const uint16_t *mx = tune_max_of(m, &np); uint16_t *v = mdef(m)->mv; v[i] = v[i] < mx[i] ? (uint16_t)(v[i] + 1u) : (uint16_t)(v[i] - 1u); meters_params_changed(m); } while (0)
        SHOW("empty");
        EDIT(1, 0); SHOW("tuned 1");
        EDIT(2, 0); SHOW("tuned 2");
        EDIT(3, 0); SHOW("tuned 3 evicts the oldest");
        EDIT(2, 1); SHOW("tuned 2 again replaces its own slot");
        meters_preset_select(3, meters_preset_of(3)); SHOW("preset of 3 reselected drops its tuning");
        uint32_t saved[TUNE_SLOTS * TUNE_WORDS], newer = meters_tune_newer(), v2 = mdef(2)->mv[1]; meters_tune_words(saved);
        meters_preset_select(2, meters_preset_of(2)); meters_preset_select(1, meters_preset_of(1)); SHOW("both presets reselected");
        meters_tune_apply(saved, newer); SHOW("boot restore");
        printf("restored custom2 %u custom1 %u v2 %u\n", custom[2], custom[1], mdef(2)->mv[1] == v2);
        return 0;
    }
    printf("rows %u count %u\n", up, meter_count);
    meters_preset_select(3, 6); meters_preset_select(2, 9); meters_preset_select(9, 1);          /* 9 is past Chladni's 4 presets: the first; meter 9 does not exist */
    printf("of3 %u of2 %u\n", meters_preset_of(3), meters_preset_of(2));
    printf("mv3"); for (uint32_t i = 0; i < mdef(3)->np; i++) printf(" %u", mdef(3)->mv[i]); printf("\n");
    meters_select(2); for (int i = 0; i < 5; i++) meters_preset_next();
    printf("chladni_after5 %u name %s\n", meters_preset_of(2), meters_preset_name());
    printf("mv2"); for (uint32_t i = 0; i < mdef(2)->np; i++) printf(" %u", mdef(2)->mv[i]); printf("\n");
    meters_select(4); meters_preset_next(); meters_preset_next(); meters_preset_next();
    printf("vu_wrap %u\n", meters_preset_of(4));
    meters_select(0); meters_preset_next(); printf("bars_next %u last0 %u\n", meters_preset_of(0), last[0]);
    return 0;
}
