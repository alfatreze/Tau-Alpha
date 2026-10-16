#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "check_page.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../transport/mp3play.h"
#include "../ui/theme.h"

#define BG UI_BG_SCREEN
#define DIM UI_DIM
#define FG UI_WHITE
#define ACC UI_ACCENT
#define ROW_H 24u
#define TOP_Y 40u

/* The tests. Steady ones (AUDIO, the six rates) fail on any FIFO stall; TRACKS fails when a skip does not land or the reader falls behind data it holds. Rates are MP3 only (skipped on FLAC). */
enum { T_AUDIO = 0, T_TRACKS, T_ST110, T_ST150, T_ST200, T_PI110, T_PI150, T_PI200, T_N };
static const char *const names[T_N] = { "AUDIO 20 S", "TRACK CHANGES X6", "STRETCH 1.10X", "STRETCH 1.50X", "STRETCH 2.00X", "PITCH 1.10X", "PITCH 1.50X", "PITCH 2.00X" };
static const uint8_t mode_of[T_N] = { 0, 0, 2, 2, 2, 1, 1, 1 }, sel_of[T_N] = { 0, 0, 0, 2, 4, 2, 5, 7 };    /* Stretch list 1.10 1.25 1.50 1.75 2.00; Pitch list 0.85 0.95 1.10 1.20 1.30 1.50 1.75 2.00 */
uint32_t check_mask, check_under;
static uint8_t res[T_N]; static uint32_t val[T_N];
static uint8_t state, cur, step, dirty = 1;               /* state 0 ready, 1 running, 2 done, 3 refused */
static uint32_t t0, u0, gen0;

static uint32_t stalls(void) { return aout_stats.under_ring + aout_stats.dry; }
static void publish(void) { uint32_t m = 0; for (uint32_t i = 0; i < T_N; i++) m |= (uint32_t)res[i] << (2u * i); check_mask = m; }
static void normal(void) { mp3play_pref_set(PREF_STRETCH, 0u); }
static void finish(uint8_t st) { normal(); state = st; dirty = 1; publish(); }
static void begin_test(uint32_t i)
{
    cur = (uint8_t)i; step = 0; t0 = cycles(); u0 = stalls(); gen0 = mp.gen; dirty = 1;
    if (i >= T_ST110) {
        if (mp3play_is_flac()) { res[i] = 3; return; }                    /* skipped: no rate change for FLAC */
        mp3play_pref_set(PREF_STRETCH, mode_of[i]); mp3play_pref_set(PREF_SPEED, sel_of[i]);
    }
}
static void next_test(void) { publish(); if (cur + 1u >= T_N) finish(2u); else begin_test(cur + 1u); }
static uint32_t secs(void) { return (uint32_t)(cycles() - t0) / CLK_HZ; }

void check_page_repaint(void) { dirty = 1; gfx_rect(0, 0, GFX_W, GFX_H, BG); }
void check_page_enter(void) { for (uint32_t i = 0; i < T_N; i++) { res[i] = 0; val[i] = 0; } state = 0; dirty = 1; publish(); gfx_rect(0, 0, GFX_W, GFX_H, BG); }
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
    if (!mp.playing || mp.held) { res[cur] = 2; val[cur] = 0xFFFFu; next_test(); return; }      /* playback stopped or paused: the test did not run to its end */
    if (cur >= T_ST110 && res[cur] == 3u) { next_test(); return; }
    switch (cur) {
    case T_AUDIO:
        if (secs() >= 20u) { val[cur] = stalls() - u0; check_under += val[cur]; res[cur] = val[cur] ? 2u : 1u; next_test(); }
        break;
    case T_TRACKS: {
        const uint32_t el = (uint32_t)(cycles() - t0);
        if (step == 0u) { mp3play_act(MPA_NEXT, 0); gen0 = mp.gen; t0 = cycles(); step = 1u; }                          /* skip */
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
    gfx_text(10, GFX_H - 40, "FIFO STALLS PER TEST; RATES MP3 ONLY", DIM, BG);
    gfx_text(10, GFX_H - 20, state == 1u ? "B STOP" : "A RUN  B BACK", DIM, BG);
    dirty = 0;
}
uint32_t check_page_dirty(void) { return dirty; }
