#pragma GCC optimize ("Os")
#include "meterlist_view.h"
#include "../gfx/gfx.h"
#include "theme.h"
#include "meter.h"
#include "meters_toc.h"
#include "meterlist_thumb.h"

/* The METER list page. Rows come from the TOC (meters_ui_*), never from a table: name, flags, and the 56 x 32 preview from the row's PSRAM preview block (only the seven rows in the window are read; no card read, no buffer). Cold code. */
#define BG   UI_BG_SCREEN
#define FG   UI_WHITE
#define DIM  UI_DIM
#define OFFC UI_FAINT
#define ACC  UI_ACCENT
#define TOP_Y 34u
#define ROW_H 40u                                  /* tall enough for the 32 px preview */
#define VIS 7u                                     /* menupages_core.h MG_VIS_METER */
#define NAME_X 92u                                 /* the name starts right of the preview */
#define NAME_MAXW 232u                             /* ... and ends before x 324: the refusal mark "!" starts at 324 or later, the star and "N/A" at 346 or later (any font) */
#define VALUE_MAXW 220u                            /* the Appearance row: the label METER on the left, the value right aligned */
static uint8_t ml_msg;
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
    if (!(meters_row_flags(row) & MROW_SELECTABLE)) { ml_msg = 1u; return MLG_REFUSED; }
    ml_msg = 0u;
    meters_select(row);
    return what == MLG_X ? MLG_CONFIGURE : MLG_PICKED;
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
        gfx_text(NAME_X, y + 12, nm, on ? UI_ON_ACCENT : (grey ? OFFC : FG), bg);
        if (grey) gfx_text(GFX_W - 24 - gfx_text_width("N/A"), y + 12, "N/A", on ? UI_ON_ACCENT : DIM, bg);
        else if (fl & MROW_REFUSED) gfx_text(GFX_W - 60 - gfx_text_width("!"), y + 12, "!", on ? UI_ON_ACCENT : UI_RED, bg);        /* it crashed last time or was refused: picking it tries again */
        if (i == cur) gfx_text(GFX_W - 24 - gfx_text_width("*"), y + 12, "*", on ? UI_ON_ACCENT : DIM, bg);
    }
    { const uint32_t ye = TOP_Y + VIS * ROW_H; gfx_rect(0, ye, GFX_W, GFX_H - ye, BG); }       /* from the last row to the bottom edge: the gaps and the hint lines are drawn over this (a strip of the old screen stayed here, B-1044) */
    gfx_text(20, 322, ml_msg ? "NEEDS A NEWER TAU: CANNOT PICK" : "UP DOWN  A PICK  X CONFIGURE", ml_msg ? ACC : DIM, BG);
    gfx_text(20, 340, "B BACK  START CLOSE", DIM, BG);
}
