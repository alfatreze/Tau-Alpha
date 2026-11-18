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
#define MTR_OWN_QUIESCE 1
#define MPH_REAL 1                                                              /* the preset files are modelled below (fake_n files for every meter), not read from a card */
static uint32_t toc_mem[(40u + 256u * 64u) / 4u + 4u], pv_mem[0x10000u / 4u + 4u];
static uint8_t win_mem[0x10000] __attribute__((aligned(4))), scratch_mem[0x1000] __attribute__((aligned(4)));
#define MTR_TOC_BASE ((volatile uint32_t *)toc_mem)
#define MTR_PV_BASE ((volatile uint32_t *)pv_mem)
#define PACK_WINDOW_DATA (win_mem)
#define PACK_SCRATCH (scratch_mem)
#define PACK_CALL(e, in, api) pack_call((e), (in), (api))
#define MTR_FLUSH() win_flush()
static uint32_t pack_call(uint32_t e, const void *in, const void *api);
static uint32_t fake_n;                                                         /* preset files of every meter (not counting DEFAULT); the command `mph N` sets it */
uint32_t mph_enter(uint32_t hash, uint32_t mid) { (void)hash; (void)mid; return fake_n; }
void mph_forget(uint32_t hash) { (void)hash; }
void mph_name(uint32_t pos, char *out) { if (pos) sprintf(out, "FILE %u", pos); else strcpy(out, "DEFAULT"); }
uint32_t mph_id1(uint32_t pos) { return pos ? 50u + pos : 0u; }
int mph_find(uint32_t id1) { return id1 > 50u && id1 - 50u <= fake_n ? (int)(id1 - 50u) : -1; }
static void win_flush(void);
#include "../fw2/ui/meters.c"
int mph_values(uint32_t hash, uint32_t pos, const mp_param_t *ps, uint32_t np, uint16_t *vals)            /* DEFAULT = every parameter's default; file n = the default of parameter 1 plus n (cut at its maximum) */
{
    (void)hash; for (uint32_t i = 0; i < np; i++) vals[i] = (uint16_t)ps[i].def;
    if (pos && np > 1u) { uint32_t v = ps[1].def + pos; if (v > ps[1].max) v = ps[1].max; vals[1] = (uint16_t)v; }
    return 1;
}
static uint32_t now, win, spend, flushes, calls;
uint32_t wd_host_cut = 1u;                                                      /* WD_HOST_CUT builds: 1 = the watchdog cuts every pack tick (a hang), 0 = it runs; the command `cut` flips it */
static uint32_t fnv(const uint8_t *p, uint32_t n) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
static void win_flush(void) { flushes++; printf("W %08x\n", fnv(win_mem, win_info.size)); printf("F\n"); }       /* the whole image as the window holds it at the flush */
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
/* A recording screen for the METER list page (fw2/ui/meterlist_view.c): every gfx_rect / gfx_text / gfx_cblit stamps the pixels it covers with the current serial, so `draw` can say which pixels of the 400 x 360 page the frame did not touch (they would keep the previous screen). */
#define SCR_W 400u
#define SCR_H 360u
static uint16_t screen_stamp[SCR_H][SCR_W]; static uint32_t shot_serial = 1u, adv_tab[95]; static int have_adv, rec_log;
static void stamp(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { for (uint32_t j = y; j < y + h && j < SCR_H; j++) for (uint32_t i = x; i < x + w && i < SCR_W; i++) screen_stamp[j][i] = (uint16_t)shot_serial; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { stamp(x, y, w, h); if (rec_log) printf("r %u %u %u %u %u\n", x, y, w, h, c); }
uint32_t gfx_text_width(const char *s) { uint32_t w = 0; for (; *s; s++) w += have_adv ? adv_tab[(unsigned char)*s >= 0x20 && (unsigned char)*s <= 0x7E ? (unsigned char)*s - 0x20u : 0u] : 8u; return w; }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t f, uint16_t b) { (void)f; (void)b; const uint32_t w = gfx_text_width(s); stamp(x, y, w, 16u); if (rec_log) printf("t %u %u %u %s\n", x, y, w, s); return x + w; }
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void gfx_clut_begin(uint32_t s) { (void)s; } void gfx_clut_put(uint16_t c) { (void)c; }
int gfx_blend_ready(void) { return 0; } void gfx_blend_on(uint32_t a) { (void)a; } void gfx_blend_off(void) { }
void gfx_blend_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int gfx_mb_write(uint32_t a, uint32_t d) { (void)a; (void)d; return 0; } int gfx_mb_read(uint32_t a, uint32_t *d) { (void)a; *d = 0; return 0; }
void gfx_fence(void) { }
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { (void)s; (void)c; (void)n; }
void gfx_cblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; stamp(c, d, e, f); if (rec_log) printf("b %u %u %u %u\n", c, d, e, f); }
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
#include "../fw2/ui/meterlist_thumb.c"
#include "../fw2/ui/meterlist_view.c"
static uint32_t saved_words[SF_WORDS];
/* the two data slots: a file each, read in pieces, a read past its end fails */
static uint8_t *fb[2]; static uint32_t fl[2];
/* The music stream: while a read of it is in flight (stream_inflight, set when a selection or the boot starts and after every tick) ANY card read collides with it on the one slot driver and the shared tag zone: the read FAILS and is logged "X".
 * Only mp3play_io_quiesce() lets the stream's command finish (clears the flag). Log: "Q" a quiesce, "R off" a card read (before the data is served), "X" a collision. failread N: the Nth card read from now on fails (a failing card). */
static int stream_inflight, fail_in = -1, collisions, reads_total;
void mp3play_io_quiesce(void) { printf("Q\n"); stream_inflight = 0; now += 1000u; }
static int rd_slot(int s, uint32_t off, uint8_t *dst, uint32_t len)
{
    printf("R %u\n", off); reads_total++;
    if (stream_inflight) { printf("X\n"); collisions++; memset(dst, 0xA5, len); return 0; }
    if (fail_in > 0 && --fail_in == 0) { fail_in = -1; now += 750000u; return 0; }
    if (!fb[s] || (uint64_t)off + len > fl[s]) return 0; memcpy(dst, fb[s] + off, len); return 1;
}
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
    printf("state saved %08x pend %08x clean %u commits %u aborts %u rows %u skipped %u notune %u curnotune %u ", msel_saved, msel_pend_hash, msel_clean_s, msel_commits, msel_aborts, msel_rows, msel_skipped, msel_notune, msel_cur_notune);
    printf("cur %u count %u src %u mtoc_err %u mtoc_count %u loads %u last_cyc %u max_cyc %u last_hash %08x err %u strikes %u overruns %u struck %u readfail %u rfcyc %u qlast %u qmax %u lastrow %u collisions %u present", meters_cur(), meter_count, meters_pack_source(), mtoc_err, mtoc_count, mwin_loads, mwin_last_cyc, mwin_max_cyc, mwin_last_hash, mwin_err, mwin_strikes, meter_stats.overruns, meter_stats.struck, mwin_readfail, mwin_readfail_cyc, mwin_q_last, mwin_q_max, mwin_last_row, collisions);
    for (uint32_t m = 0; m < meter_count; m++) printf(" %d", meters_present(m));
    printf(" pstate");
    for (uint32_t m = 0; m < meter_count; m++) printf(" %u", meters_pack_state(m));
    printf(" info run %x err %u file %u seen %u\n", r, e, fe, sn);
}
static void cstat(void)       /* what a Configure page of another row may and may not change */
{
    uint32_t r, e, fe, sn; meters_pack_info(&r, &e, &fe, &sn);
    printf("cur %u active %u set %u saved %08x pend %u pendhash %08x commits %u aborts %u loads %u refused %u strikes %u collisions %u preset %u custom %u\n", meters_cur(), meters_cfg_active(), meters_cfg_set(), msel_saved, (unsigned)pend_on, msel_pend_hash, msel_commits, msel_aborts, mwin_loads, mwin_refused, mwin_strikes, collisions, meters_preset_of(meters_cur()), meters_custom(meters_cur()));
}
int main(void)
{
    char line[512]; caps.spec = 1; meters_init(); meters_enter(0, 0, 100, 50);
    while (fgets(line, sizeof line, stdin)) {
        char a[16], b[400]; b[0] = 0; const int n = sscanf(line, "%15s %399s", a, b); if (n < 1) continue;
        if (!strcmp(a, "user")) setfile(0, b); else if (!strcmp(a, "core")) setfile(1, b);
        else if (!strcmp(a, "nouser")) setfile(0, "/nonexistent"); else if (!strcmp(a, "nocore")) setfile(1, "/nonexistent");
        else if (!strcmp(a, "failread")) fail_in = atoi(b);
        else if (!strcmp(a, "mph")) fake_n = (uint32_t)atoi(b);
        else if (!strcmp(a, "boot")) { stream_inflight = 1; meters_init(); meters_enter(0, 0, 100, 50); int ok = meters_packs_load_file(rd_user, (void *)1); printf("boot user %d", ok); if (!ok) { ok = meters_packs_load_file(rd_core, (void *)2); printf(" core %d", ok); } printf("\n"); }
        else if (!strcmp(a, "select")) { stream_inflight = 1; meters_select((uint32_t)atoi(b)); printf("cur %u\n", meters_cur()); }
        else if (!strcmp(a, "next")) { stream_inflight = 1; meters_next(1); printf("cur %u\n", meters_cur()); }
        else if (!strcmp(a, "prev")) { stream_inflight = 1; meters_next(-1); printf("cur %u\n", meters_cur()); }
        else if (!strcmp(a, "walk")) { const int k = atoi(b); uint32_t rows[256]; for (int i = 0; i < k && i < 256; i++) { stream_inflight = 1; meters_next(1); rows[i] = meters_cur(); } printf("walk"); for (int i = 0; i < k && i < 256; i++) printf(" %u", rows[i]); printf("\n"); }       /* X, k times: the rows visited, in order */
        else if (!strcmp(a, "mid")) printf("mid row %u manifest %u hash %08x\n", meters_cur(), mid_of(meters_cur()), act.hash);
        else if (!strcmp(a, "tune")) { uint32_t w[8]; meters_tune_words(w); printf("tune"); for (int i = 0; i < 8; i++) printf(" %u", w[i]); printf("\n"); }
        else if (!strcmp(a, "maxes")) { uint32_t np = 0; const uint16_t *m = meters_shipped_max_of((uint32_t)atoi(b), &np); printf("maxes %u", m ? np : 0u); for (uint32_t i = 0; m && i < np; i++) printf(" %u", m[i]); printf("\n"); }
        else if (!strcmp(a, "tick")) { stream_inflight = 1; spend = (uint32_t)atoi(b); win++; meters_tick(1); printf("tick done cur %u\n", meters_cur()); }
        else if (!strcmp(a, "swap")) { const int k = atoi(b); for (int i = 0; i < k; i++) { stream_inflight = 1; meters_select((uint32_t)(i % (int)meter_count)); win++; spend = 0; meters_tick(1); } printf("swapped %d\n", k); }
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
        else if (!strcmp(a, "restart")) { stream_inflight = 1;                                  /* Quit and start again: the player boots (the bins), then the saved register file is applied as main_player.c does */
            meters_init(); meters_enter(0, 0, 100, 50); if (!meters_packs_load_file(rd_user, (void *)1)) meters_packs_load_file(rd_core, (void *)2);
            uint32_t sv[SF_COUNT]; sf_unpack(saved_words, sv); appearance_set(sv); printf("restarted cur %u\n", meters_cur());
        }
        else if (!strcmp(a, "edit")) { const uint32_t i = (uint32_t)atoi(b); const meter_def_t *d = meters_def(meters_cur()); d->mv[i] = (uint16_t)(d->mv[i] + 1u); meters_params_changed(meters_cur()); }
        else if (!strcmp(a, "show")) { const meter_def_t *d = meters_def(meters_cur()); printf("show cur %u custom %u mv", meters_cur(), meters_custom(meters_cur())); for (uint32_t i = 0; i < 3u && i < d->np; i++) printf(" %u", d->mv[i]); printf("\n"); }
        else if (!strcmp(a, "advance")) { now += (uint32_t)atoi(b) * CLK_HZ; }                      /* SECONDS of the 66.667 MHz counter pass */
        else if (!strcmp(a, "cut")) wd_host_cut = (uint32_t)atoi(b);
        else if (!strcmp(a, "flags")) { const uint32_t m = (uint32_t)atoi(b); char nm[40]; printf("flags row %u %u name %s\n", m, meters_row_flags(m), meters_row_name(m, nm, sizeof nm)); }
        else if (!strcmp(a, "list")) { printf("list %u:", meters_list_count()); for (uint32_t i = 0; i < meters_list_count(); i++) printf(" %u", meters_list_row(i)); printf("\n"); }
        else if (!strcmp(a, "adv")) { char *p = line + 3; for (int i = 0; i < 95; i++) adv_tab[i] = (uint32_t)strtoul(p, &p, 10); have_adv = 1; }       /* the advances of 0x20..0x7E of the font under test */
        else if (!strcmp(a, "uilist")) { printf("uilist %u:", meters_ui_count()); for (uint32_t i = 0; i < meters_ui_count(); i++) printf(" %u/%u", meters_ui_row(i), meters_row_flags(meters_ui_row(i))); printf("\n"); }
        else if (!strcmp(a, "uiindex")) { const uint32_t r = (uint32_t)atoi(b); printf("uiindex row %u -> %u\n", r, meters_ui_index(r)); }
        else if (!strcmp(a, "step")) { const int d = atoi(b); const uint32_t r = meters_ui_step(meters_cur(), d); printf("step %d from %u -> %u\n", d, meters_cur(), r); }
        else if (!strcmp(a, "draw")) {                                                         /* draw TOP SEL: the page of the METER list with the previous screen = serial 0 */
            unsigned top = 0, sel = 0; sscanf(line, "%*s %u %u", &top, &sel); const uint32_t n = meters_ui_count();
            memset(screen_stamp, 0, sizeof screen_stamp); shot_serial = 1u; rec_log = 1; const uint32_t r0 = reads_total, q0 = 0; (void)q0;
            meterlist_draw("METER", top, sel, n, meters_ui_index(meters_cur()));
            rec_log = 0; uint32_t un = 0, firstrow = 9999u; for (uint32_t y = 0; y < SCR_H; y++) for (uint32_t x = 0; x < SCR_W; x++) if (screen_stamp[y][x] != 1u) { un++; if (y < firstrow) firstrow = y; }
            printf("draw top %u sel %u uncovered %u firstrow %u reads %u\n", top, sel, un, firstrow, reads_total - r0);
        }
        else if (!strcmp(a, "pick")) { const uint32_t i = (uint32_t)atoi(b); stream_inflight = 1; const uint32_t g = meterlist_guard(i, MLG_A); printf("pick %u guard %u cur %u msg %u\n", i, g, meters_cur(), meterlist_msg()); }
        else if (!strcmp(a, "cfg")) { const uint32_t i = (uint32_t)atoi(b); stream_inflight = 1; const uint32_t g = meterlist_guard(i, MLG_X); printf("cfg %u guard %u cur %u msg %u\n", i, g, meters_cur(), meterlist_msg()); }
        else if (!strcmp(a, "keyclear")) { meterlist_msg_clear(); printf("msg %u\n", meterlist_msg()); }
        else if (!strcmp(a, "curname")) { char nm[32]; meterlist_cur_name(nm, sizeof nm); printf("curname %s\n", nm); }
        else if (!strcmp(a, "cbegin")) { stream_inflight = 1; const uint32_t r = (uint32_t)atoi(b); const int e = meters_cfg_begin(r); printf("cbegin %u code %d ", r, e); cstat(); }       /* Configure of a row opens: the row is loaded for the page, the set meter is remembered */
        else if (!strcmp(a, "cend")) { stream_inflight = 1; meters_cfg_end(); printf("cend "); cstat(); }                    /* the Configure page is left: the set meter comes back */
        else if (!strcmp(a, "cstat")) { printf("cstat "); cstat(); }
        else if (!strcmp(a, "cable")) { printf("cable"); for (uint32_t m = 0; m < meter_count; m++) if (meters_row_configurable(m)) printf(" %u", m); printf("\n"); }       /* every row that can be configured */
        else if (!strcmp(a, "pre")) { meters_preset_select(meters_cur(), (uint32_t)atoi(b)); printf("pre cur %u preset %u of %u custom %u\n", meters_cur(), meters_preset_of(meters_cur()), meters_npre(meters_cur()), meters_custom(meters_cur())); }
        else if (!strcmp(a, "sput")) {                                                          /* sput HASH REV ANCHOR N i v i v ...: the settings interface, max 255 for every parameter, 40 parameters */
            unsigned h, rv, an, n; char *p = line + 4; h = (unsigned)strtoul(p, &p, 16); rv = (unsigned)strtoul(p, &p, 10); an = (unsigned)strtoul(p, &p, 10); n = (unsigned)strtoul(p, &p, 10);
            meter_settings_t st; memset(&st, 0, sizeof st); st.preset = (uint8_t)an; st.n = (uint8_t)n;
            for (unsigned i = 0; i < n && i < MS_MAX_ENTRIES; i++) { st.idx[i] = (uint8_t)strtoul(p, &p, 10); st.val[i] = (uint16_t)strtoul(p, &p, 10); }
            uint16_t mx[40]; for (int i = 0; i < 40; i++) mx[i] = 255;
            printf("sput stored %d\n", meter_settings_put(h, rv, &st, mx, 40u));
        }
        else if (!strcmp(a, "sget")) {
            unsigned h, rv; char *p = line + 4; h = (unsigned)strtoul(p, &p, 16); rv = (unsigned)strtoul(p, &p, 10);
            uint16_t mx[40]; for (int i = 0; i < 40; i++) mx[i] = 255;
            meter_settings_t st; memset(&st, 0, sizeof st); const int f = meter_settings_get(h, rv, mx, 40u, &st);
            printf("sget found %d preset %u n %u", f, f ? st.preset : 0u, f ? st.n : 0u); for (unsigned i = 0; f && i < st.n; i++) printf(" %u=%u", st.idx[i], st.val[i]); printf("\n");
        }
        else if (!strcmp(a, "sdrop")) { unsigned h, rv; char *p = line + 5; h = (unsigned)strtoul(p, &p, 16); rv = (unsigned)strtoul(p, &p, 10); meter_settings_drop(h, rv); printf("sdrop\n"); }
        else if (!strcmp(a, "state")) state();
    }
    return 0;
}
