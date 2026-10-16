#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "browser_view.h"
#include "../gfx/gfx.h"
#include "../transport/browser.h"
#define BG   RGB565(0, 0, 0)
#define FG   RGB565(255, 255, 255)
#define DIM  RGB565(140, 146, 160)
#define ACC  RGB565(255, 160, 40)
#define SELBG RGB565(255, 160, 40)
#define ROW_H 30u
#define TOP_Y 34u
static uint32_t drawn_gen = 0xFFFFFFFFu;

void browser_view_enter(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(20, 322, "UP DOWN MOVE  LEFT RIGHT PAGE", DIM, BG);
    gfx_text(20, 340, "A OPEN OR PLAY  B BACK", DIM, BG);
    drawn_gen = 0xFFFFFFFFu;
}
void browser_view_draw(void)
{
    const browser_view_t *v = browser_view();
    if (v->gen == drawn_gen) return;
    drawn_gen = v->gen;
    gfx_rect(0, 4, GFX_W, 24, BG);
    gfx_text(20, 6, v->head, ACC, BG);
    char pos[20]; uint32_t n = 0, a = v->count ? v->sel + 1u : 0u, c = v->count; char t[12]; uint32_t k;
    k = 0; do { t[k++] = (char)('0' + a % 10u); a /= 10u; } while (a);
    while (k) pos[n++] = t[--k]; pos[n++] = '/';
    k = 0; do { t[k++] = (char)('0' + c % 10u); c /= 10u; } while (c);
    while (k) pos[n++] = t[--k]; pos[n] = 0;
    gfx_text(GFX_W - 20 - gfx_text_width(pos), 6, pos, DIM, BG);
    for (uint32_t i = 0; i < BROWSER_ROWS; i++) {
        uint32_t y = TOP_Y + i * ROW_H;
        int on = i < v->n && v->top + i == v->sel;
        gfx_rect(0, y, GFX_W, ROW_H, on ? SELBG : BG);
        if (i >= v->n) continue;
        char buf[44]; uint32_t m = 0; for (; v->rows[i][m] && m + 1u < sizeof buf; m++) buf[m] = v->rows[i][m]; buf[m] = 0;
        while (m && gfx_text_width(buf) > GFX_W - 60u) buf[--m] = 0;
        gfx_text(20, y + 7, buf, on ? BG : FG, on ? SELBG : BG);
    }
}
