/* Host harness of the window loader and the TOC boot path of fw2/ui/meters.c (P2_PACKS, plan P2.2): the real meters.c is compiled with host buffers for the PSRAM copy of the TOC and previews, the 64 KiB window and the 4 KB
 * scratch, a bin reader over files, a logged fence.i and a logged call into the pack. Commands on stdin, one per line (the answers go to stdout):
 *   user PATH | core PATH | nouser | nocore     the two bin files the boot path may find (data slot 10 and data slot 9); a PATH that does not exist = no file
 *   boot                                         meters_init + the boot path of main_player.c (the user bin, else the core bin; the order main uses)
 *   select ROW | next | tick [CYCLES] | swap N  select a row, X, one drawn tick (the pack spends CYCLES), N selections walking the rows
 *   state                                        every counter and the row table
 * The log lines "F" (fence.i) and "C <fnv>" (a call into the window, fnv = FNV-1a of its first 256 bytes) show the order of the load sequence: the bytes are written, THEN F, THEN C. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#define P2_PACKS 1
#define MTR_STATE
static uint32_t toc_mem[(40u + 256u * 64u) / 4u + 4u], pv_mem[0x10000u / 4u + 4u];
static uint8_t win_mem[0x10000] __attribute__((aligned(4))), scratch_mem[0x1000] __attribute__((aligned(4)));
#define MTR_TOC_BASE ((volatile uint32_t *)toc_mem)
#define MTR_PV_BASE ((volatile uint32_t *)pv_mem)
#define PACK_WINDOW_DATA (win_mem)
#define PACK_SCRATCH (scratch_mem)
#define PACK_CALL(e, in, api) pack_call((e), (in), (api))
#define MTR_FLUSH() win_flush()
static uint32_t pack_call(uint32_t e, const void *in, const void *api);
static void win_flush(void);
#include "../fw2/ui/meters.c"
static uint32_t now, win, spend, flushes, calls;
static uint32_t fnv(const uint8_t *p, uint32_t n) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
static void win_flush(void) { flushes++; printf("F\n"); }
static uint32_t pack_call(uint32_t e, const void *in, const void *api) { (void)in; (void)api; calls++; now += spend; printf("C %08x entry %x\n", fnv(win_mem, 256u), e); return 0; }
static uint32_t builtin;
uint32_t host_rd(uint32_t a) { if (a == R_CYCLES) return now; if (a == R_SPEC_ST) return win << 16; return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } uint32_t mp3play_degrade_now(void) { return 0; } caps_t caps;
#define IDX_X(key, nm, id, lc, UC, np, cmp) idx_##key,
enum { METER_LIST(IDX_X) };
#define STUB_X(key, nm, id, lc, UC, np, cmp) \
    void meter_##key##_tick(const mtr_in_t *i) { (void)i; builtin++; printf("B\n"); } uint16_t meter_##key##_mv[MPD_##UC##_N]; void meter_##key##_apply(void) { } \
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
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { (void)s; (void)c; (void)n; }
void gfx_cblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_bar(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint16_t f, uint16_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
void gfx_scroll_down(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfx_scroll_left(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void gfx_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_blit_abs(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void gfx_sblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
/* persistence: fw2/ui/appearance.c (layout 2 of the persisted settings) on the real settings words */
uint8_t th_theme, th_pol, th_pal; uint32_t font_cur;
void theme_set(uint32_t t, uint32_t p, uint32_t c) { th_theme = (uint8_t)t; th_pol = (uint8_t)p; th_pal = (uint8_t)c; }
int font_select(uint32_t i) { font_cur = i; return 0; }
#include "../fw2/ui/appearance.c"
static uint32_t saved_words[SF_WORDS];
/* the two data slots: a file each, read in pieces, a read past its end fails */
static uint8_t *fb[2]; static uint32_t fl[2];
static int rd_slot(int s, uint32_t off, uint8_t *dst, uint32_t len) { if (!fb[s] || (uint64_t)off + len > fl[s]) return 0; memcpy(dst, fb[s] + off, len); return 1; }
static int rd_user(void *c, uint32_t off, uint8_t *dst, uint32_t len) { (void)c; return rd_slot(0, off, dst, len); }
static int rd_core(void *c, uint32_t off, uint8_t *dst, uint32_t len) { (void)c; return rd_slot(1, off, dst, len); }
static void setfile(int s, const char *p)
{
    free(fb[s]); fb[s] = 0; fl[s] = 0;
    FILE *f = fopen(p, "rb"); if (!f) return;
    fseek(f, 0, SEEK_END); fl[s] = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET); fb[s] = malloc(fl[s] ? fl[s] : 1u); if (fread(fb[s], 1, fl[s], f) != fl[s]) fl[s] = 0; fclose(f);
}
static void state(void)
{
    uint32_t r, e, fe, sn; meters_pack_info(&r, &e, &fe, &sn);
    printf("state cur %u count %u src %u mtoc_err %u mtoc_count %u loads %u last_cyc %u max_cyc %u last_hash %08x err %u strikes %u overruns %u struck %u present", meters_cur(), meter_count, meters_pack_source(), mtoc_err, mtoc_count, mwin_loads, mwin_last_cyc, mwin_max_cyc, mwin_last_hash, mwin_err, mwin_strikes, meter_stats.overruns, meter_stats.struck);
    for (uint32_t m = 0; m < meter_count; m++) printf(" %d", meters_present(m));
    printf(" pstate");
    for (uint32_t m = 0; m < meter_count; m++) printf(" %u", meters_pack_state(m));
    printf(" info run %x err %u file %u seen %u\n", r, e, fe, sn);
}
int main(void)
{
    char line[512]; caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    while (fgets(line, sizeof line, stdin)) {
        char a[16], b[400]; b[0] = 0; const int n = sscanf(line, "%15s %399s", a, b); if (n < 1) continue;
        if (!strcmp(a, "user")) setfile(0, b); else if (!strcmp(a, "core")) setfile(1, b);
        else if (!strcmp(a, "nouser")) setfile(0, "/nonexistent"); else if (!strcmp(a, "nocore")) setfile(1, "/nonexistent");
        else if (!strcmp(a, "boot")) { meters_init(); meters_enter(0, 0, 100, 50); int ok = meters_packs_load_file(rd_user, (void *)1); printf("boot user %d", ok); if (!ok) { ok = meters_packs_load_file(rd_core, (void *)2); printf(" core %d", ok); } printf("\n"); }
        else if (!strcmp(a, "select")) { meters_select((uint32_t)atoi(b)); printf("cur %u\n", meters_cur()); }
        else if (!strcmp(a, "next")) { meters_next(1); printf("cur %u\n", meters_cur()); }
        else if (!strcmp(a, "tick")) { spend = (uint32_t)atoi(b); win++; meters_tick(1); printf("tick done cur %u\n", meters_cur()); }
        else if (!strcmp(a, "swap")) { const int k = atoi(b); for (int i = 0; i < k; i++) { meters_select((uint32_t)(i % (int)meter_count)); win++; spend = 0; meters_tick(1); } printf("swapped %d\n", k); }
        else if (!strcmp(a, "def")) {
            const uint32_t m = meters_cur(); const meter_def_t *d = meters_def(m); char buf[40];
            printf("def row %u name %s np %u npre %u cmp %u cmd %u cyc %u\n", m, d->name, d->np, meters_npre(m), d->cmp, cmd_limit_of(m), cyc_limit_of(m));
            for (uint32_t i = 0; i < d->np; i++) printf("p %u %s|type %u|min %u|max %u|nval %u\n", i, meters_param_label(m, i, buf, sizeof buf), d->pd[i].type, d->pd[i].min, d->pd[i].max, d->pd[i].nval);
        }
        else if (!strcmp(a, "thumb")) {
            const uint32_t m = (uint32_t)atoi(b); uint8_t v; uint16_t f; uint8_t x, y, t, n;
            printf("thumb row %u len %u rle", m, meters_thumb_len(m)); for (uint32_t i = 0; i < 6u && meters_thumb_rle(m, i, &v); i++) printf(" %u", v);
            printf(" col0"); if (meters_thumb_col(m, 0u, &f, &x, &y, &t, &n)) printf(" %u %u %u %u %u", f, x, y, t, n); printf("\n");
        }
        else if (!strcmp(a, "save")) { uint32_t sv[SF_COUNT]; for (uint32_t i = 0; i < SF_COUNT; i++) sv[i] = sf_table[i].def; appearance_get(sv); sf_pack(saved_words, sv); printf("saved hash %08x pos %u id %u\n", sv[SF_METER_HASH], sv[SF_METER_PRE_POS], sv[SF_METER_PRE_ID]); }
        else if (!strcmp(a, "restart")) {                                  /* Quit and start again: the player boots (the bins), then the saved register file is applied as main_player.c does */
            meters_init(); meters_enter(0, 0, 100, 50); if (!meters_packs_load_file(rd_user, (void *)1)) meters_packs_load_file(rd_core, (void *)2);
            uint32_t sv[SF_COUNT]; sf_unpack(saved_words, sv); appearance_set(sv); printf("restarted cur %u\n", meters_cur());
        }
        else if (!strcmp(a, "edit")) { const uint32_t i = (uint32_t)atoi(b); const meter_def_t *d = meters_def(meters_cur()); d->mv[i] = (uint16_t)(d->mv[i] + 1u); meters_params_changed(meters_cur()); }
        else if (!strcmp(a, "show")) { const meter_def_t *d = meters_def(meters_cur()); printf("show cur %u custom %u mv", meters_cur(), meters_custom(meters_cur())); for (uint32_t i = 0; i < 3u && i < d->np; i++) printf(" %u", d->mv[i]); printf("\n"); }
        else if (!strcmp(a, "state")) state();
    }
    return 0;
}
