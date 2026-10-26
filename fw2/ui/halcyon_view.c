#pragma GCC optimize ("Os")
#include "halcyon_view.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../aout/halcyon.h"
#include "../cores/halcyon_sel_core.h"
#include "../cores/view_core.h"
#include "helios.h"
#include "regions.h"
#include "theme.h"

#define BG    UI_BG_SCREEN
#define FG    UI_WHITE
#define DIM   UI_DIM
#define FAINT UI_FAINT
#define ACC   UI_ACCENT
#define GRID  UI_TRACK
#define AXIS  UI_FAINT
#define BAND  UI_PANEL
#define TRACK UI_TRACK
#define HP_X 20u
#define HP_W 360u
#define HP_Y 38u
#define HP_H 120u
#define ROWS_Y 166u
#define ROW_H 20u
#define FOOT_Y 290u
static const uint16_t band_x[6] = { 125u, 84u, 234u, 269u, 301u, 324u };       /* the plot x of each control's stage (20 Hz to 20 kHz, log, 360 px) */
static const char *const ctl_name[6] = { "WARMTH", "BASS", "VOCAL", "PUNCH", "SIBILANCE", "AIR" };
static uint8_t sel, dirty;

static void dec(char *o, uint32_t v) { char t[8]; uint32_t k = 0, n = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (k) o[n++] = t[--k]; o[n] = 0; }

void halcyon_view_enter(void)
{
    sel = 1u; halcyon_page_open(); dirty = 1;
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
}
void halcyon_view_redraw(void) { gfx_rect(0, 0, GFX_W, GFX_H, BG); dirty = 1; }
int halcyon_view_input(uint32_t edge)
{
    enum { UP = 1u << 0, DOWN = 1u << 1, LEFT = 1u << 2, RIGHT = 1u << 3, A = 1u << 4, B = 1u << 5, X = 1u << 6, Y = 1u << 7, L1 = 1u << 8, R1 = 1u << 9 };
    (void)A;
    if (edge & B) return VW_BACK;
    if (!halcyon_present) return VW_STAY;
    if (edge & (UP | DOWN)) sel = (uint8_t)hs_row(sel, (edge & UP) != 0);
    if (edge & (LEFT | RIGHT)) {
        const int d = (edge & RIGHT) ? 1 : -1;
        if (!sel) halcyon_step(d); else halcyon_ctl_set(sel - 1u, halcyon_ctl(sel - 1u) + d);
    }
    if (edge & R1) halcyon_step(1);
    if (edge & L1) halcyon_step(-1);
    if (edge & X) halcyon_compare();
    if (edge & Y) halcyon_reset();
    dirty = 1;
    return VW_STAY;
}
void halcyon_view_mark(void) { if (dirty) helios_dirty(R_HAL); }

static void plot(const int16_t *db)
{
    const int32_t cy = (int32_t)HP_Y + (int32_t)HP_H / 2;
    gfx_rect(HP_X - 4u, HP_Y - 4u, HP_W + 8u, HP_H + 8u, BG);
    if (sel) {                                                        /* the lit band of the selected control */
        int32_t bx = (int32_t)band_x[sel - 1u] - 22, bw = 44;
        if (bx < 0) { bw += bx; bx = 0; }
        if (bx + bw > (int32_t)HP_W) bw = (int32_t)HP_W - bx;
        gfx_rect(HP_X + (uint32_t)bx, HP_Y, (uint32_t)bw, HP_H, BAND);
    }
    static const uint16_t gx[3] = { 84u, 204u, 324u };                 /* 100 Hz, 1 kHz, 10 kHz */
    for (uint32_t i = 0; i < 3u; i++) gfx_rect(HP_X + gx[i], HP_Y, 1u, HP_H, GRID);
    gfx_rect(HP_X, (uint32_t)cy, HP_W, 1u, AXIS);
    for (int32_t d = 24; d <= 48; d += 24) { gfx_rect(HP_X, (uint32_t)(cy - d), HP_W, 1u, GRID); gfx_rect(HP_X, (uint32_t)(cy + d), HP_W, 1u, GRID); }
    gfx_text(HP_X + 2u, HP_Y + 1u, "+14", FAINT, BG);
    gfx_text(HP_X + 2u, (uint32_t)cy - 14, "0", FAINT, BG);
    gfx_text(HP_X + 2u, HP_Y + HP_H - 16u, "-14", FAINT, BG);
    gfx_text(HP_X + gx[0] + 3u, HP_Y + HP_H - 16u, "100", FAINT, BG);
    gfx_text(HP_X + gx[1] + 3u, HP_Y + HP_H - 16u, "1K", FAINT, BG);
    gfx_text(HP_X + gx[2] + 3u, HP_Y + HP_H - 16u, "10K", FAINT, BG);
    int32_t prev = 0;                                                  /* the curve: 4 px per dB (db is 1/64 dB), 3-pixel columns joined to the previous one */
    for (uint32_t x = 0; x < HP_W; x += 3u) {
        const uint32_t k = (x * (HALCYON_NGRID - 1u)) / (HP_W - 1u), k1 = (k + 1u < HALCYON_NGRID) ? k + 1u : k;
        const int32_t x0 = (int32_t)((k * (HP_W - 1u)) / (HALCYON_NGRID - 1u)), x1 = (int32_t)((k1 * (HP_W - 1u)) / (HALCYON_NGRID - 1u));
        int32_t v = db[k];
        if (x1 > x0) v += (int32_t)(db[k1] - db[k]) * ((int32_t)x - x0) / (x1 - x0);
        int32_t y = cy - v / 16;
        if (y < (int32_t)HP_Y) y = (int32_t)HP_Y;
        if (y > (int32_t)(HP_Y + HP_H - 2u)) y = (int32_t)(HP_Y + HP_H - 2u);
        int32_t top = y, h = 3;
        if (x) { int32_t lo = prev < y ? prev : y, hi = prev < y ? y : prev; top = lo; h = hi - lo + 3; }
        if (top + h > (int32_t)(HP_Y + HP_H)) h = (int32_t)(HP_Y + HP_H) - top;
        gfx_rect(HP_X + x, (uint32_t)top, 3u, (uint32_t)h, halcyon_cmp ? DIM : ACC);
        prev = y;
    }
}

static void row(uint32_t i, int on)
{
    const uint32_t y = ROWS_Y + i * ROW_H; const uint16_t bg = on ? ACC : BG;
    gfx_rect(0, y, GFX_W, ROW_H, bg);
    gfx_text(HP_X, y + 1u, ctl_name[i], on ? UI_ON_ACCENT : DIM, bg);
    const int32_t v = halcyon_ctl(i);
    const uint32_t bx = HP_X + 112u, bw = 200u, by = y + 7u;
    gfx_rect(bx, by, bw, 6u, on ? UI_TRACK : UI_PANEL);
    const uint16_t fill = on ? UI_ON_ACCENT : DIM;
    if (i == 4u) { if (v > 0) gfx_rect(bx, by, (uint32_t)v * 40u, 6u, fill); }                       /* sibilance 0..5: a dip depth from the left */
    else {
        gfx_rect(bx + bw / 2u, by - 2u, 1u, 10u, fill);
        if (v > 0) gfx_rect(bx + bw / 2u, by, (uint32_t)v * 20u, 6u, fill);
        else if (v < 0) gfx_rect(bx + bw / 2u - (uint32_t)(-v) * 20u, by, (uint32_t)(-v) * 20u, 6u, fill);
    }
    char t[8], *q = t;
    if (v < 0) { *q++ = '-'; dec(q, (uint32_t)(-v)); } else { if (v) *q++ = '+'; dec(q, (uint32_t)v); }
    gfx_text(GFX_W - 20u - gfx_text_width(t), y + 1u, t, on ? UI_ON_ACCENT : FG, bg);
}

void halcyon_view_draw(void)
{
    if (!dirty) return;
    dirty = 0;
    gfx_rect(0, 0, GFX_W, HP_Y - 4u, BG);
    gfx_text(20, 6, "HALCYON EQ", ACC, BG);
    const char *nm = halcyon_name(); const uint32_t w = gfx_text_width(nm);
    if (sel == 0u && halcyon_present) { gfx_rect(GFX_W - 28u - w, 3u, w + 16u, 22u, ACC); gfx_text(GFX_W - 20u - w, 6, nm, UI_ON_ACCENT, ACC); }
    else gfx_text(GFX_W - 20u - w, 6, nm, DIM, BG);
    gfx_rect(0, 306, GFX_W, 54, BG);
    if (!halcyon_present) {
        gfx_text(HP_X, HP_Y + 20u, "NO UNIT", DIM, BG);
        gfx_text(HP_X, HP_Y + 40u, "THIS BITSTREAM HAS NO HALCYON ENGINE", DIM, BG);
        gfx_text(20, 340, "B BACK", DIM, BG);
        return;
    }
    int16_t db[HALCYON_NGRID];
    const int32_t atten = halcyon_curve_get(db);
    plot(db);
    for (uint32_t i = 0; i < 6u; i++) row(i, i + 1u == sel);
    char t[44], *q = t; const uint32_t tenths = ((uint32_t)atten * 10u) / 8u;
    const char *a = "PREAMP -"; while (*a) *q++ = *a++;
    dec(q, tenths / 10u); while (*q) q++; *q++ = '.'; dec(q, tenths % 10u); while (*q) q++;
    const char *b = (atten || halcyon_ctl(0) || halcyon_ctl(1) || halcyon_ctl(2) || halcyon_ctl(3) || halcyon_ctl(4) || halcyon_ctl(5)) ? " DB  17 HZ HIGH-PASS ON" : " DB  FLAT: BYPASS";
    while (*b) *q++ = *b++; *q = 0;
    gfx_text(HP_X, FOOT_Y + 2u, t, FAINT, BG);
    gfx_text(20, 322, sel ? "UP DOWN ROW  L R ADJUST  L1 R1 PRESET" : "UP DOWN ROW  L R PRESET", DIM, BG);
    gfx_text(20, 340, "X A/B  Y RESET  B BACK", DIM, BG);
}
