#pragma GCC optimize ("Os")
#include "fullscreen_view.h"
#include "meter.h"
#include "theme.h"
#include "helios.h"
#include "regions.h"
#include "../gfx/gfx.h"
#include "../transport/mp3play.h"
#include "../cores/nowplaying_core.h"

#define FS_W 400u
#define FS_FIG_H 323u
#define FS_TXT_Y 328u
#define FS_PROG_X 8u
#define FS_PROG_W 384u
#define FS_PROG_Y 350u
static uint32_t shown_gen = 0xFFFFFFFFu, shown_px = 0xFFFFFFFFu, shown_col = 0xFFFFFFFFu;

void fullscreen_view_enter(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, th_role[TR_BG_BOTTOM]);
    meters_enter(0, 0, FS_W, FS_FIG_H);
    shown_gen = shown_px = shown_col = 0xFFFFFFFFu;
}
void fullscreen_view_mark(void) { helios_dirty_mask(RM(R_FS_FIG) | RM(R_FS_FOOT)); }
void fullscreen_draw_fig(void) { meters_tick(mp3play_np()->gov < 1u); }

/* "Title . Artist . Album", centred in the progress width; each part is clipped if the line would not fit. */
static void text_line(const np_info_t *p)
{
    const char *part[3] = { p->title, p->artist, p->album };
    const uint16_t col[3] = { UI_WHITE, UI_ACCENT, UI_DIM };
    uint32_t wt[3], total = 0, n = 0; char buf[3][44];
    for (uint32_t i = 0; i < 3u; i++) {
        uint32_t m = 0; while (part[i][m] && m + 1u < sizeof buf[i]) { buf[i][m] = part[i][m]; m++; } buf[i][m] = 0;
        if (m) n++;
    }
    const uint32_t sep = 20u, avail = FS_PROG_W - (n > 1u ? (n - 1u) * sep : 0u);
    for (uint32_t i = 0; i < 3u; i++) { wt[i] = gfx_text_width(buf[i]); total += wt[i]; }
    for (uint32_t i = 2; total > avail && i < 3u; i--) {                         /* clip from the album backwards, then the artist, then the title */
        uint32_t m = 0; while (buf[i][m]) m++;
        while (m && total > avail) { buf[i][--m] = 0; total -= wt[i]; wt[i] = gfx_text_width(buf[i]); total += wt[i]; }
        if (!i) break;
    }
    total += n > 1u ? (n - 1u) * sep : 0u;
    gfx_rect(0, FS_TXT_Y - 2u, FS_W, 20u, th_role[TR_BG_BOTTOM]);
    uint32_t x = FS_PROG_X + (total < FS_PROG_W ? (FS_PROG_W - total) / 2u : 0u), drawn = 0;
    for (uint32_t i = 0; i < 3u; i++) {
        if (!buf[i][0]) continue;
        if (drawn) { gfx_rect(x + 8u, FS_TXT_Y + 7u, 3u, 3u, UI_FAINT); x += sep; }
        gfx_text(x, FS_TXT_Y, buf[i], col[i], th_role[TR_BG_BOTTOM]); x += wt[i]; drawn++;
    }
}
void fullscreen_draw_foot(void)
{
    const np_info_t *p = mp3play_np();
    if (p->valid && p->gen != shown_gen) { shown_gen = p->gen; text_line(p); }
    const uint32_t px = p->valid ? np_bar(p->pos, p->total, FS_PROG_W) : 0u, col = (p->playing && !p->held) ? UI_ACCENT : UI_FS_RED;
    if (px == shown_px && col == shown_col) return;
    shown_px = px; shown_col = col;
    gfx_rect(FS_PROG_X - 2u, FS_PROG_Y - 4u, FS_PROG_W + 4u, 10u, th_role[TR_BG_BOTTOM]);        /* clear the line and the old tick */
    gfx_rect(FS_PROG_X, FS_PROG_Y, FS_PROG_W, 2u, UI_FS_TRACK);
    if (px) gfx_rect(FS_PROG_X, FS_PROG_Y, px, 2u, (uint16_t)col);
    gfx_rect(FS_PROG_X + px - (px ? 1u : 0u), FS_PROG_Y - 3u, 2u, 8u, (uint16_t)col);
}
