#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "info_page.h"
#include "info_counters.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"

#define BG   RGB565(0, 0, 0)
#define DIM  RGB565(140, 146, 160)
#define FG   RGB565(255, 255, 255)
#define ACC  RGB565(255, 160, 40)
#define ROW_H 19u
#define TOP_Y 30u
#define VIS 16u

static uint32_t top;                       /* first visible row */

void info_page_init(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(10, 6, "PLAYER 2  S3 AUDIO", ACC, BG);
    gfx_text(10, GFX_H - 20, "A PLAY B HOLD X FLUSH Y RATE START REPORT", DIM, BG);
}

void info_page_key(uint32_t edge)
{
    uint32_t n = counters_count;
    uint32_t maxtop = n > VIS ? n - VIS : 0;
    if (edge & KEY_DOWN) top = top >= maxtop ? 0 : top + 1;                 /* wraps, as every list does (helios_wrap_index) */
    if (edge & KEY_UP)   top = top == 0 ? maxtop : top - 1;
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
