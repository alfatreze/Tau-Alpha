#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../cores/spectrum_core.h"

#define R_SPEC_IDX  0x800000DCu
#define R_SPEC_DATA 0x800000E0u
_Static_assert(MTR_BANDS == SPEC_BANDS, "the meter inputs carry one value per hardware band");

extern void meter_winamp_bars_tick(const mtr_in_t *in);
extern void meter_winamp_scope_tick(const mtr_in_t *in);
extern void meter_vu_master_tick(const mtr_in_t *in);
extern void meter_chladni_tick(const mtr_in_t *in);
const meter_def_t meter_table[] = {
    { "WINAMP BARS", meter_winamp_bars_tick },
    { "WINAMP SCOPE", meter_winamp_scope_tick },
    { "CHLADNI", meter_chladni_tick },
    { "VU MASTER", meter_vu_master_tick },
};
const uint32_t meter_count = sizeof meter_table / sizeof meter_table[0];

static uint32_t cur, bx, by, bw, bh, win_seen = 0xFFFFFFFFu, frame, last_t, force_next;
static uint8_t lvl[MTR_BANDS];
extern uint32_t mp3play_paused_now(void);          /* mp3play.c: 1 when nothing is being heard */

void meters_init(void) { cur = 0; }
uint32_t meters_cur(void) { return cur; }
const char *meters_name(void) { return meter_table[cur].name; }

void meters_enter(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    bx = x; by = y; bw = w; bh = h; frame = 0; force_next = 1;
    win_seen = caps.spec ? (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu : 0xFFFFFFFFu;      /* the window in progress is skipped: its mean mixes in what played before the meter was shown */
    for (uint32_t b = 0; b < MTR_BANDS; b++) lvl[b] = 0;
    gfx_rect(x, y, w, h, th_role[TR_BG_BOTTOM]);
    last_t = cycles();
}
void meters_select(uint32_t idx) { cur = idx < meter_count ? idx : 0u; force_next = 1; }
void meters_next(int dir)
{
    cur = dir > 0 ? (cur + 1u) % meter_count : (cur + meter_count - 1u) % meter_count;
    force_next = 1;                                  /* the new meter repaints everything, the old picture is erased by its forced clear */
}

static int fetch(uint32_t *m)                        /* a new window, read coherently (the counter must not move while the 16 means are read) */
{
    uint32_t w0 = (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu;
    if (w0 == win_seen) return 0;
    for (uint32_t b = 0; b < MTR_BANDS; b++) { REG_WR(R_SPEC_IDX, b); m[b] = REG_RD(R_SPEC_DATA) & 0xFFFFFu; }
    if (((REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu) != w0) return 0;
    win_seen = w0;
    return 1;
}

void meters_tick(int allowed)
{
    if (!caps.spec || !allowed || !bw) return;
    uint32_t m[MTR_BANDS];
    if (!fetch(m)) return;
    for (uint32_t b = 0; b < MTR_BANDS; b++) lvl[b] = (uint8_t)spec_ballistics(lvl[b], spec_level(m[b], b));
    const uint32_t t = cycles(), dt = (t - last_t) / (CLK_HZ / 1000u); last_t = t;
    uint32_t pl = 0, pr = 0;
    if (caps.wave) { const uint32_t pk = REG_RD(R_WAVE_PK); pl = ((pk & 0xFFFFu) * 3u) / 4u; pr = ((pk >> 16) * 3u) / 4u; REG_WR(R_WAVE_CTL, 1u); }      /* peaks since the last tick, then cleared */
    mtr_in_t in = { lvl, frame++, (uint16_t)(dt > 200u ? 200u : dt), (uint16_t)bx, (uint16_t)by, (uint16_t)bw, (uint16_t)bh, th_role[TR_BG_BOTTOM], (uint8_t)force_next, (uint8_t)mp3play_paused_now(), pl, pr };
    force_next = 0;
    meter_table[cur].tick(&in);
}
