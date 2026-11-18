#pragma GCC optimize ("Os")
#include "meterlist_view.h"
#include "../gfx/gfx.h"
#include "theme.h"
#include "meter.h"
#include "meters_toc.h"
#include "meterlist_thumb.h"

/* The METERS page (Settings > METERS, its own entry of the main menu since 0.7.5). Rows come from the TOC (meters_ui_*), never from a table: name, flags, and the 56 x 32 preview from the row's PSRAM preview block (only the seven rows in the window
 * are read; no card read, no buffer). A SETS the meter, X CONFIGURES the highlighted one (any meter that has parameters, not only the set one); a row that can be configured carries a small settings icon (a gear). Cold code.
 * THE RIGHT EDGE of a row, left to right: the name (from x 92, cut with a dot to end by x 300), the gear (11 x 11 at x 304 to 314), then the marks, which never start before x 316: the refusal "!" (ends at x 340),
 * the "N/A" of a greyed row (a greyed row has no gear: it cannot be configured) and the "*" of the meter on screen at x 346 or later. The three marks and the gear can all be on one row without touching. */
#define BG   UI_BG_SCREEN
#define FG   UI_WHITE
#define DIM  UI_DIM
#define OFFC UI_FAINT
#define ACC  UI_ACCENT
#define TOP_Y 34u
#define ROW_H 40u                                  /* tall enough for the 32 px preview */
#define VIS 7u                                     /* menupages_core.h MG_VIS_METER */
#define NAME_X 92u                                 /* the name starts right of the preview */
#define NAME_MAXW 208u                             /* ... and ends before x 300: the gear is at x 304 to 314, the refusal mark "!" starts at 324 or later, the star and "N/A" at 346 or later (any font) */
#define GEAR_X 304u                                /* the settings icon: 11 x 11, vertically centred on the row */
#define GEAR_N 11u
#define VALUE_MAXW 220u                            /* the Appearance row: the label METER on the left, the value right aligned */
static uint8_t ml_msg;                             /* the line under the rows: 0 the hints, 1 A on a greyed row, 2 X on a greyed row, 3 X on a meter without settings, 4 the meter could not be opened */
/* The settings icon (a gear), 11 x 11, one bit per pixel, most significant bit = leftmost: drawn as runs with gfx_rect, so it needs no icon sheet and takes the colour it is given. */
static const uint16_t gear_rows[GEAR_N] = { 0x070, 0x376, 0x3FE, 0x1DC, 0x78F, 0x78F, 0x78F, 0x1DC, 0x3FE, 0x376, 0x070 };
static void gear_draw(uint32_t x, uint32_t y, uint16_t c)
{
    for (uint32_t j = 0; j < GEAR_N; j++) {
        const uint32_t m = gear_rows[j]; uint32_t i = 0;
        while (i < GEAR_N) {
            if (!(m >> (GEAR_N - 1u - i) & 1u)) { i++; continue; }
            uint32_t k = i; while (k < GEAR_N && (m >> (GEAR_N - 1u - k) & 1u)) k++;
            gfx_rect(x + i, y + j, k - i, 1u, c); i = k;
        }
    }
}
static void put_num(char *o, uint32_t v)
{
    char t[8]; uint32_t k = 0, n = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) o[n++] = t[--k]; o[n] = 0;
}
/* A name wider than maxw (the widest font, 23 capitals) is cut from the end and ends with a dot: "WWWWWWWWWWWWWWWWWW." Whole when it fits. */
static void fit(char *s, uint32_t maxw)
{
    uint32_t n = 0; while (s[n]) n++;
    if (n < 2u || gfx_text_width(s) <= maxw) return;
    s[n - 1u] = '.';
    while (n > 1u && gfx_text_width(s) > maxw) { s[n - 2u] = '.'; s[n - 1u] = 0; n--; }
}
const char *meterlist_cur_name(char *o, uint32_t n) { meters_row_name(meters_cur(), o, n); fit(o, VALUE_MAXW); return o; }
uint32_t meterlist_msg(void) { return ml_msg; }
void meterlist_msg_clear(void) { ml_msg = 0u; }
uint32_t meterlist_guard(uint32_t i, uint32_t what)
{
    const uint32_t row = meters_ui_row(i);
    ml_msg = 0u;
    if (!(meters_row_flags(row) & MROW_SELECTABLE)) { ml_msg = what == MLG_X ? 2u : 1u; return MLG_REFUSED; }         /* a meter this firmware cannot run: nothing changes, one short line says why */
    if (what == MLG_A) { meters_select(row); return MLG_PICKED; }
    if (!meters_row_configurable(row)) { ml_msg = 3u; return MLG_REFUSED; }                                          /* it has no parameters: nothing to configure */
    if (meters_cfg_begin(row)) { ml_msg = 4u; return MLG_REFUSED; }                                                  /* its pack is loaded for the page; a damaged or refused pack stays in the list with a short line (the set meter is back) */
    return MLG_CONFIGURE;
}
void meterlist_draw(const char *title, uint32_t top, uint32_t sel, uint32_t n, uint32_t cur)
{
    gfx_rect(0, 0, GFX_W, TOP_Y, BG);
    gfx_text(20, 6, title, ACC, BG);
    { char pos[16]; put_num(pos, sel + 1u); uint32_t k = 0; while (pos[k]) k++; pos[k++] = ' '; pos[k++] = '/'; pos[k++] = ' '; put_num(pos + k, n); gfx_text(GFX_W - 20 - gfx_text_width(pos), 6, pos, DIM, BG); }     /* where you are: "12 / 64" */
    for (uint32_t k = 0; k < VIS; k++) {
        const uint32_t i = top + k, y = TOP_Y + k * ROW_H;
        if (i >= n) { gfx_rect(0, y, GFX_W, ROW_H, BG); continue; }                  /* a list shorter than seven rows: the rows below are cleared */
        const int on = i == sel; const uint16_t bg = on ? ACC : BG;
        const uint32_t row = meters_ui_row(i), fl = meters_row_flags(row); const int grey = !(fl & MROW_SELECTABLE);
        char nm[24];
        gfx_rect(0, y, GFX_W, ROW_H, bg);
        if (grey) { gfx_rect(20, y + 4u, METER_THUMB_W, METER_THUMB_H, OFFC); gfx_rect(21, y + 5u, METER_THUMB_W - 2u, METER_THUMB_H - 2u, bg); }       /* a meter this firmware cannot run: an empty frame, no preview */
        else meter_thumb_draw(row, 20, y + 4u);
        meters_row_name(row, nm, sizeof nm); fit(nm, NAME_MAXW);
        if (meters_row_configurable(row)) gear_draw(GEAR_X, y + (ROW_H - GEAR_N) / 2u, on ? UI_ON_ACCENT : DIM);       /* it can be configured (a greyed row cannot): the settings icon */
        gfx_text(NAME_X, y + 12, nm, on ? UI_ON_ACCENT : (grey ? OFFC : FG), bg);
        if (grey) gfx_text(GFX_W - 24 - gfx_text_width("N/A"), y + 12, "N/A", on ? UI_ON_ACCENT : DIM, bg);
        else if (fl & MROW_REFUSED) gfx_text(GFX_W - 60 - gfx_text_width("!"), y + 12, "!", on ? UI_ON_ACCENT : UI_RED, bg);        /* it crashed last time or was refused: picking it tries again */
        if (i == cur) gfx_text(GFX_W - 24 - gfx_text_width("*"), y + 12, "*", on ? UI_ON_ACCENT : DIM, bg);
    }
    { const uint32_t ye = TOP_Y + VIS * ROW_H; gfx_rect(0, ye, GFX_W, GFX_H - ye, BG); }       /* from the last row to the bottom edge: the gaps and the hint lines are drawn over this (a strip of the old screen stayed here, B-1044) */
    { static const char *const msg[5] = { "UP DOWN  A SET  X CONFIGURE", "NEEDS A NEWER TAU: CANNOT PICK", "NEEDS A NEWER TAU: NO SETTINGS", "THIS METER HAS NO SETTINGS", "CANNOT OPEN THIS METER" }; gfx_text(20, 322, msg[ml_msg < 5u ? ml_msg : 0u], ml_msg ? ACC : DIM, BG); }
    gfx_text(20, 340, "B BACK  START CLOSE", DIM, BG);
}
