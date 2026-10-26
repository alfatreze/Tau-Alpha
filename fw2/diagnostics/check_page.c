#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "check_page.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../transport/mp3play.h"
#include "../ui/theme.h"
#include "../gfx/font.h"
#include "../generated/font_data.h"
#include "../cores/font_core.h"
#include "../gfx/icon.h"
#include "../cores/icon_core.h"
#include "blit_check.h"

#define BG UI_BG_SCREEN
#define DIM UI_DIM
#define FG UI_WHITE
#define ACC UI_ACCENT
#define ROW_H 22u
#define TOP_Y 40u

/* The tests. Steady ones (AUDIO, the six rates) fail on any FIFO stall; TRACKS fails when a skip does not land or the reader falls behind data it holds. Rates are MP3 only (skipped on FLAC). */
enum { T_FONT = 0, T_ICONS, T_AUDIO, T_TRACKS, T_ST110, T_ST150, T_ST200, T_PI110, T_PI150, T_PI200, T_GFX, T_BLIT, T_N };   /* T_GFX last: the bits of the others in CHECK MASK keep their places */
static const char *const names[T_N] = { "FONT MEMORY", "ICONS", "AUDIO 20 S", "TRACK CHANGES X6", "STRETCH 1.10X", "STRETCH 1.50X", "STRETCH 2.00X", "PITCH 1.10X", "PITCH 1.50X", "PITCH 2.00X", "GFX ISSUE", "BLIT OPS" };
static const uint8_t mode_of[T_N] = { 0, 0, 0, 0, 2, 2, 2, 1, 1, 1, 0, 0 }, sel_of[T_N] = { 0, 0, 0, 0, 0, 2, 4, 2, 5, 7, 0, 0 };    /* Stretch list 1.10 1.25 1.50 1.75 2.00; Pitch list 0.85 0.95 1.10 1.20 1.30 1.50 1.75 2.00 */
uint32_t check_mask, check_under;
static uint8_t res[T_N]; static uint32_t val[T_N];
static uint8_t state, cur, step, dirty = 1;               /* state 0 ready, 1 running, 2 done, 3 refused */
static uint32_t t0, u0, gen0;

static uint32_t stalls(void) { return aout_stats.under_ring + aout_stats.dry; }
static void publish(void) { uint32_t m = 0; for (uint32_t i = 0; i < T_N; i++) m |= (uint32_t)res[i] << (2u * i); check_mask = m; }
static void normal(void) { mp3play_pref_set(PREF_STRETCH, 0u); }
static void finish(uint8_t st) { normal(); state = st; dirty = 1; publish(); }
/* The font memory test (needs no audio): draw glyphs through the engine in white on black into the stash, read the pixels back, and compare them with what the loaded font's own atlas predicts through the
 * engine's weight table. Proves the load (every word, in order), the 8-bit glyph code and the second range on the real hardware. The glyphs: A (ASCII), e-acute and y-diaeresis (the 0xA0 range, bit 7 on bit 15). */
static uint32_t font_check(void)
{
    static const uint16_t w16[16] = { 0, 4, 6, 7, 8, 10, 10, 11, 12, 13, 13, 14, 15, 15, 16, 16 };       /* the engine's dark-polarity coverage weights (mp3_fb.sv cov_weight) */
    static const uint8_t codes[3] = { 0x41, 0xE9, 0xFF };
    ft_t f; if (ft_parse(font_recs[font_cur < FONT_COUNT ? font_cur : 0u], font_rec_len, &f)) return 0xFFFFu;
    uint32_t bad = 0;
    for (uint32_t k = 0; k < 3u; k++) {
        const uint32_t g = codes[k], idx = g < 0x80u ? g - 0x20u : g - 0xA0u + 95u, x0 = 8u + k * 24u, y0 = 540u;
        gfx_rect_stash(x0, y0, 16, 16, 0x0001u);                                        /* a known background first: no stale pixel can pass */
        gfx_char_stash(x0, y0, g, 0xFFFFu, 0x0000u);
        gfx_fence();
        for (uint32_t row = 0; row < 16u; row++)
            for (uint32_t c = 0; c < 16u; c += 2u) {
                uint32_t r = 0;
                if (gfx_mb_read((y0 + row) * GFX_STRIDE + x0 + c, &r)) { bad++; continue; }
                for (uint32_t h = 0; h < 2u; h++) {
                    const uint32_t cov = (f.words[idx * 32u + row * 2u + (c + h) / 8u] >> (4u * ((c + h) % 8u))) & 15u, w = w16[cov];
                    const uint32_t want = (((31u * w) >> 4) & 31u) << 11 | (((63u * w) >> 4) & 63u) << 5 | (((31u * w) >> 4) & 31u);
                    const uint32_t got = h ? (r >> 16) & 0xFFFFu : r & 0xFFFFu;           /* either half may hold the pixel: the mailbox word's two halves are the two pixels of the pair */
                    if (got != want) bad++;
                }
            }
    }
    return bad;
}
/* The icons test: draw three icons into the stash (white on black, through the palette blit), read the pixels back and compare them with the record's own coverage through the same colour table. Proves the
 * plane build, the colour-table load and OP_CBLIT on the real hardware. The icons: play, shuffle (24 wide) and vol3 (26 wide). */
static uint32_t icon_check(void)
{
    static const uint8_t ids[3] = { ICON_PLAY, ICON_SHUFFLE, ICON_VOL3 };
    uint32_t bad = 0;
    for (uint32_t k = 0; k < 3u; k++) {
        ti_icon_t ic; if (ti_find(icon_rec, icon_rec_len, ids[k], &ic)) return 0xFFFFu;
        const uint32_t x0 = 200u, y0 = 600u;                         /* off the live icon planes (stash rows 560..587, columns 0..488): until B-870 this drew over the Select and Start planes at (300, 560) and left them as colours, so every icon drawn after a Check showed garbage */
        if (k) gfx_rect_stash(x0, y0, 32, 28, 0x0001u);                                 /* a known background: nothing stale can pass */
        if (icon_draw_stash(ids[k], x0, y0, 0xFFFFu, 0x0000u)) return 0xFFFFu;
        gfx_fence();
        for (uint32_t row = 0; row < ic.h; row++)
            for (uint32_t col = 0; col < ic.w; col += 2u) {
                uint32_t r = 0;
                if (gfx_mb_read((y0 + row) * GFX_STRIDE + x0 + col, &r)) { bad++; continue; }
                for (uint32_t h = 0; h < 2u && col + h < ic.w; h++) {
                    const uint32_t want = ti_colour(0x0000u, 0xFFFFu, ti_cov(&ic, col + h, row)), got = h ? (r >> 16) & 0xFFFFu : r & 0xFFFFu;
                    if (got != want) bad++;
                }
            }
    }
    return bad;
}
/* The GFX ISSUE test (T0 of the Talos plan, needs no audio): how the CPU spends its time feeding the draw engine. Issues N one-row rectangles into the stash (a free stretch of the font test's rows, never
 * visible) and times (a) the issue loop, which includes every wait for a free FIFO slot, and (b) the fence that drains what is left. Two widths: 1 pixel (the engine does almost nothing, so the issue time is the CPU's
 * cost per command: MMIO writes and the FIFO-full poll) and 100 pixels (a 100-word burst per command: if the cycles per command rise the engine, not the CPU, is the limit). Cycles per command go to Info
 * (GFX B1 ISSUE / DRAIN, GFX B100 ISSUE / DRAIN); the row itself always passes: it measures. */
uint32_t gfx_bench[4], blit_check_mask;                  /* blit_check_mask: the failed tests of the last BLIT OPS run (bits: blit_check.h) */
static void gfx_issue_bench(void)
{
    static const uint8_t wd[2] = { 1u, 100u };
    for (uint32_t k = 0; k < 2u; k++) {
        gfx_fence();
        const uint32_t N = 1000u, t = cycles();
        for (uint32_t i = 0; i < N; i++) gfx_rect_stash(120u, 548u, wd[k], 1u, 0x0001u);
        const uint32_t t1 = cycles();
        gfx_fence();
        gfx_bench[2u * k] = (uint32_t)(t1 - t) / N; gfx_bench[2u * k + 1u] = (uint32_t)(cycles() - t1) / N;
    }
}
static void begin_test(uint32_t i)
{
    if (i == T_BLIT) { blit_check_mask = blit_check_run(); res[i] = blit_check_mask ? 2u : 1u; val[i] = blit_check_mask; cur = (uint8_t)i; step = 0; t0 = cycles(); dirty = 1; return; }
    if (i == T_GFX) { gfx_issue_bench(); res[i] = 1u; val[i] = 0; cur = (uint8_t)i; step = 0; t0 = cycles(); dirty = 1; return; }
    if (i == T_ICONS) { val[i] = icon_check(); res[i] = val[i] ? 2u : 1u; cur = (uint8_t)i; step = 0; t0 = cycles(); dirty = 1; return; }
    if (i == T_FONT) { val[i] = font_check(); res[i] = val[i] ? 2u : 1u; cur = (uint8_t)i; step = 0; t0 = cycles(); dirty = 1; return; }
    cur = (uint8_t)i; step = 0; t0 = cycles(); u0 = stalls(); gen0 = mp.gen; dirty = 1;
    if (i >= T_ST110) {
        if (mp3play_is_flac()) { res[i] = 3; return; }                    /* skipped: no rate change for FLAC */
        mp3play_pref_set(PREF_STRETCH, mode_of[i]); mp3play_pref_set(PREF_SPEED, sel_of[i]);
    }
}
static void next_test(void) { publish(); if (cur + 1u >= T_N) finish(2u); else begin_test(cur + 1u); }
static uint32_t secs(void) { return (uint32_t)(cycles() - t0) / CLK_HZ; }

void check_page_repaint(void) { dirty = 1; gfx_rect(0, 0, GFX_W, GFX_H, BG); }
void check_page_enter(void) { dirty = 1; gfx_rect(0, 0, GFX_W, GFX_H, BG); }   /* the results of the last run stay (page, Info CHECK MASK, BLIT BAD) until the next run starts: DEV 187 wiped them on entry (B-867) */
void check_page_exit(void) { if (state == 1u) finish(2u); }
int check_page_key(uint32_t edge)
{
    if (edge & KEY_B) { if (state == 1u) { finish(2u); return 0; } return 1; }
    if ((edge & KEY_A) && state != 1u) {
        if (!mp.playing || mp.held) { state = 3; dirty = 1; return 0; }  /* a track must be playing */
        for (uint32_t i = 0; i < T_N; i++) { res[i] = 0; val[i] = 0; }
        check_under = 0; state = 1; begin_test(0);
    }
    return 0;
}
void check_page_tick(void)
{
    if (state != 1u) return;
    if (cur == T_FONT || cur == T_ICONS || cur == T_GFX || cur == T_BLIT) { next_test(); return; }                          /* the font memory test ran in begin_test and needs no audio */
    if (!mp.playing || mp.held) { res[cur] = 2; val[cur] = 0xFFFFu; next_test(); return; }      /* playback stopped or paused: the test did not run to its end */
    if (cur >= T_ST110 && res[cur] == 3u) { next_test(); return; }
    switch (cur) {
    case T_AUDIO:
        if (secs() >= 20u) { val[cur] = stalls() - u0; check_under += val[cur]; res[cur] = val[cur] ? 2u : 1u; next_test(); }
        break;
    case T_TRACKS: {
        const uint32_t el = (uint32_t)(cycles() - t0);
        if (step == 0u) { gen0 = mp.gen; mp3play_act(MPA_NEXT, 0); t0 = cycles(); step = 1u; }                          /* skip: the generation is read BEFORE the act, which bumps it inside the call (B-864) */
        else if ((mp.gen != gen0 && el > CLK_HZ * 2u) || el > CLK_HZ * 5u) {                                             /* the new track runs for 2 s, or no skip landed in 5 s */
            if (mp.gen == gen0) { res[cur] = 2u; next_test(); break; }
            val[cur]++; t0 = cycles(); step = 0u;
            if (val[cur] >= 6u) { res[cur] = 1u; next_test(); }
        }
        break; }
    default:
        if (step == 0u && secs() >= 2u) { step = 1u; t0 = cycles(); u0 = stalls(); }                                      /* 2 s to settle (the stretcher restarts), then 4 s measured */
        else if (step == 1u && secs() >= 4u) { val[cur] = stalls() - u0; check_under += val[cur]; res[cur] = val[cur] ? 2u : 1u; next_test(); }
    }
}
void check_page_draw(void)
{
    gfx_rect(0, 0, GFX_W, 36, BG);
    gfx_text(10, 6, state == 1u ? "CHECK  RUNNING" : state == 2u ? "CHECK  DONE" : state == 3u ? "CHECK  PLAY A TRACK FIRST" : "CHECK  READY", ACC, BG);
    for (uint32_t i = 0; i < T_N; i++) {
        const uint32_t y = TOP_Y + i * ROW_H; char v[12]; uint32_t n = 0;
        const char *r = res[i] == 1u ? "PASS" : res[i] == 2u ? "FAIL" : res[i] == 3u ? "SKIP" : (state == 1u && cur == i) ? "RUN" : "-";
        for (const char *p = r; *p; p++) v[n++] = *p; v[n] = 0;
        gfx_rect(0, y, GFX_W, ROW_H, BG);
        gfx_text(10, y + 4, names[i], DIM, BG);
        gfx_text(GFX_W - 10 - gfx_text_width(v), y + 4, v, res[i] == 2u ? ACC : FG, BG);
    }
    gfx_rect(0, GFX_H - 44, GFX_W, 44, BG);
    gfx_text(10, GFX_H - 40, "FONT, ICONS, STALLS, GFX ISSUE, BLIT OPS", DIM, BG);
    gfx_text(10, GFX_H - 20, state == 1u ? "B STOP" : "A RUN  B BACK", DIM, BG);
    dirty = 0;
}
uint32_t check_page_dirty(void) { return dirty; }
