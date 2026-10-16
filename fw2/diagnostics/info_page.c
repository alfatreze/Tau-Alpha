#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "info_page.h"
#include "info_counters.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../ui/theme.h"
#include "../cores/helios_core.h"

#define BG   UI_BG_SCREEN
#define DIM  UI_DIM
#define FG   UI_WHITE
#define ACC  UI_ACCENT
#define ROW_H 19u
#define TOP_Y 30u
#define VIS 16u

static uint32_t top;                       /* first visible row */

void info_page_init(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(10, 6, "INFO", ACC, BG);
    gfx_text(10, GFX_H - 20, "UP DOWN SCROLL  B BACK  SELECT+START REPORT", DIM, BG);
}

void info_page_key(uint32_t edge)
{
    uint32_t n = counters_count;
    uint32_t maxtop = n > VIS ? n - VIS : 0;
    if (edge & KEY_DOWN) top = hl_wrap(top, maxtop + 1u, 0);                 /* the one wrap rule (Helios) */
    if (edge & KEY_UP)   top = hl_wrap(top, maxtop + 1u, 1);
}

void info_page_draw(void)
{
    char v[CNT_VALUE_MAX + 1];
    for (uint32_t k = 0; k < VIS && top + k < counters_count; k++) {
        const cnt_t *c = &counters_table[top + k];
        uint32_t y = TOP_Y + k * ROW_H;
        char *e = cnt_fmt(c, v); *e = 0;
        uint32_t w = gfx_text_width(v);
        gfx_rect(0, y, GFX_W, ROW_H, BG);                          /* values change length: clear the row */
        gfx_text(10, y, c->name, DIM, BG);
        gfx_text(GFX_W - 10 - w, y, v, FG, BG);
    }
}
