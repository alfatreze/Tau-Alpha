#pragma GCC optimize ("Os")
#include "help_view.h"
#include "../gfx/gfx.h"
#include "../cores/help_core.h"
#include "../cores/view_core.h"
#include "../platform/mmio.h"
#include "helios.h"
#include "regions.h"
#include "theme.h"

#define BG  UI_BG_SCREEN
#define FG  UI_WHITE
#define DIM UI_DIM
#define ACC UI_ACCENT
#define LINE_H 22u
#define TOP_Y 40u
static hp_t hp; static uint8_t dirty;

void help_view_enter(void) { hp_open(&hp); dirty = 1; gfx_rect(0, 0, GFX_W, GFX_H, BG); }
void help_view_redraw(void) { gfx_rect(0, 0, GFX_W, GFX_H, BG); dirty = 1; }
int help_view_input(uint32_t edge)
{
    const int back = hp_input(&hp, edge);
    dirty = 1;
    return back ? VW_BACK : VW_STAY;
}
void help_view_mark(void) { if (dirty) helios_dirty(R_HELP); }
void help_view_draw(void)
{
    if (!dirty) return;
    dirty = 0;
    const hp_page_t *pg = &hp_pages[hp.page];
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    char t[40]; uint32_t n = 0;
    const char *h = "HELP ";
    while (*h) t[n++] = *h++;
    t[n++] = (char)('1' + hp.page); t[n++] = '/'; t[n++] = (char)('0' + HP_PAGES); t[n++] = ' '; t[n++] = ' ';
    for (const char *s = pg->title; *s && n < 39u; s++) t[n++] = *s;
    t[n] = 0;
    gfx_text(20, 6, t, ACC, BG);
    for (uint32_t i = 0; i < pg->n; i++) {
        const hp_line_t *l = &pg->lines[i];
        if (l->style == HS_GAP) continue;
        gfx_text(20, TOP_Y + i * LINE_H, l->s, l->style == HS_ACCENT ? ACC : l->style == HS_DIM ? DIM : FG, BG);
    }
    gfx_text(20, 340, "UP DOWN PAGE   B BACK", DIM, BG);
}
