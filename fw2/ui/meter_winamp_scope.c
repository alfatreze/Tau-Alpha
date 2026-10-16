#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../generated/meter_presets.h"

/* Winamp scope (meters/winamp_scope/meter.json): the hardware capture block (tau_wave_meter.sv) records the mono mix as 256 columns, each the min..max envelope over two samples, started at a rising zero
 * crossing. The module draws 64 columns (four hardware columns each, so one frame is at most 64 x 2 = 128 rectangles plus the clear and the centre line: the 256-column draw measured 21 times the cost of the
 * bars and caused audible jitter in the old player), normalised to the PREVIOUS frame's peak, with temporal smoothing. The capture is re-armed after every draw and read next time round; while it is still
 * running the last picture stays. */
#define COLS 64u
#define HW_COLS 256u
static uint8_t smooth = 35u, trail = 30u;                         /* temporal smoothing 0..90, and the percent of the old trace kept each frame 0..80 (the FLUID preset) */
/* The trail: instead of wiping the box each frame, the box is blended toward the background by (100 - trail) percent (a source weight of that many 256ths) and the new trace is drawn on top, so an old trace fades
 * over a few frames. The background is a strip of the box's rows in the stash (columns 0..111, rows 600..), blended in chunks as wide as the strip. Without the blend in the bitstream, or at trail 0, the box is wiped. */
#define STRIP_X 0u
#define STRIP_Y 600u
#define STRIP_W 112u
static uint16_t strip_bg; static uint16_t strip_h;
const char *meter_winamp_scope_preset_name(uint32_t i) { return mpp_scope_name[i < MPP_SCOPE_PRESETS ? i : 0u]; }
uint16_t meter_winamp_scope_mv[MPP_SCOPE_N];
void meter_winamp_scope_apply(void) { smooth = (uint8_t)meter_winamp_scope_mv[MPP_SCOPE_SCOPE_SMOOTH]; trail = (uint8_t)meter_winamp_scope_mv[MPP_SCOPE_SCOPE_TRAIL]; }
void meter_winamp_scope_preset(uint32_t i)
{
    for (uint32_t k = 0; k < MPP_SCOPE_N; k++) meter_winamp_scope_mv[k] = mpp_scope[i < MPP_SCOPE_PRESETS ? i : 0u][k];
    meter_winamp_scope_apply();
}
static uint32_t sc_pk = 1u; static uint8_t armed, init;
static int16_t sc_mid[COLS], sc_half[COLS];                       /* the picture's own state: the smoothed centre and half height of each column */

void meter_winamp_scope_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const int32_t ey = (int32_t)(h / 2u) - 1;
    const uint32_t cy = y + h / 2u;
    if (in->force) { init = 0; armed = 0; gfx_rect(x0, y, w, h, in->bg); strip_h = 0; }
    if (!armed) { REG_WR(R_WAVE_CTL, 2u | (1u << 8)); armed = 1u; return; }          /* nothing captured yet */
    if (in->paused) { init = 0; return; }
    if (REG_RD(R_WAVE_ST) & 2u) return;                                              /* still capturing: the last picture stays */
    if (trail && gfx_blend_ready()) {
        if (strip_h != h || strip_bg != in->bg) { gfx_rect_stash(STRIP_X, STRIP_Y, STRIP_W, h, in->bg); strip_h = (uint16_t)h; strip_bg = in->bg; }
        uint32_t a = (100u - trail) * 256u / 100u; if (a < 1u) a = 1u; if (a > 255u) a = 255u;
        gfx_blend_on(a); gfx_blend_blit(STRIP_X, STRIP_Y, x0, y, w, h); gfx_blend_off();
    } else gfx_rect(x0, y, w, h, in->bg);
    gfx_rect(x0, cy, w, 1u, UI_TRACK);
    uint32_t npk = 1u;
    for (uint32_t c = 0; c < COLS; c++) {
        int32_t mn = 32767, mx = -32768;
        for (uint32_t k = 0; k < HW_COLS / COLS; k++) {                              /* the envelope of four hardware columns */
            REG_WR(R_WAVE_IDX, c * (HW_COLS / COLS) + k);
            const uint32_t d = REG_RD(R_WAVE_DATA);
            const int32_t a = (int16_t)(d >> 16), b = (int16_t)(d & 0xFFFFu);
            if (a < mn) mn = a;
            if (b > mx) mx = b;
        }
        const uint32_t am = (uint32_t)(mn < 0 ? -mn : mn), ax = (uint32_t)(mx < 0 ? -mx : mx);
        if (am > npk) npk = am;
        if (ax > npk) npk = ax;
        int32_t lo = (mn * ey) / (int32_t)sc_pk, hi = (mx * ey) / (int32_t)sc_pk;
        if (lo < -ey) lo = -ey; else if (lo > ey) lo = ey;                           /* both bounds, both variables: a DC-biased window must not escape the box */
        if (hi < -ey) hi = -ey; else if (hi > ey) hi = ey;
        if (lo > hi) lo = hi;
        int32_t mid = (lo + hi) / 2, half = (hi - lo) / 2;
        if (!init) sc_mid[c] = (int16_t)mid;
        sc_mid[c] = (int16_t)(sc_mid[c] + (((mid - sc_mid[c]) * (100 - (int32_t)smooth)) / 100));
        mid = sc_mid[c]; sc_half[c] = (int16_t)half;
        const uint32_t cx = x0 + (c * w) / COLS, cxn = x0 + ((c + 1u) * w) / COLS, cwid = cxn > cx ? cxn - cx : 1u;
        int32_t s_top = (int32_t)cy - (mid + half), s_bot = s_top + 2 * half + 2;      /* clamped in signed space before any cast: never a wrapped address */
        if (s_top < (int32_t)y) s_top = (int32_t)y;
        if (s_bot > (int32_t)(y + h)) s_bot = (int32_t)(y + h);
        if (s_bot < s_top) s_bot = s_top;
        gfx_rect(cx, (uint32_t)s_top, cwid, (uint32_t)(s_bot - s_top), UI_ACCENT);
    }
    sc_pk = npk; init = 1u;
    REG_WR(R_WAVE_CTL, 2u | (1u << 8));                                              /* arm the next capture: two samples per column */
}
