#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "browser.h"
#include "../platform/mmio.h"
#include "../cores/browse_core.h"
#include "../stream/library.h"
#include "mp3play.h"

static const br_ops_t ops = { library_br_count, library_br_child };
static br_t b; static uint8_t inited; static browser_view_t view;

static void cp(char *d, const char *s, uint32_t cap) { uint32_t i = 0; for (; s[i] && i + 1u < cap; i++) d[i] = s[i]; d[i] = 0; }

static void refresh(void)
{
    br_lv_t *l = br_cur(&b);
    static const char *const names[8] = { "LIBRARY", "ARTISTS", "ALBUMS", "ALBUMS", "TRACKS", "ALL TRACKS", "PLAYLISTS", "PLAYLIST" };
    cp(view.head, names[l->kind & 7u], sizeof view.head);
    view.sel = l->sel; view.top = l->top; view.count = l->count; view.depth = b.depth; view.n = 0;
    int trk = l->kind == BK_TRACKS_OF || l->kind == BK_TRACKS || l->kind == BK_TRACKS_LIST;
    for (uint32_t i = 0; i < BROWSER_ROWS && l->top + i < l->count; i++, view.n++) {
        library_br_text(l->kind, l->parent, l->top + i, view.rows[i], sizeof view.rows[i]); view.is_track[i] = (uint8_t)trk;
    }
    view.gen++;
}
void browser_enter(void)
{
    if (!inited) { br_init(&b, &ops, BROWSER_ROWS); inited = 1; }
    refresh();
}
int browser_key(uint32_t edge)
{
    int leave = 0;
    if (edge & KEY_DOWN) br_move(&b, 1);
    if (edge & KEY_UP) br_move(&b, -1);
    if (edge & KEY_RIGHT) br_page(&b, 1);
    if (edge & KEY_LEFT) br_page(&b, -1);
    if (edge & KEY_B) { if (!br_back(&b)) leave = 1; }
    if (edge & KEY_A) {
        br_result_t r = br_enter(&b, &ops);
        if (r.act == BR_PLAY || r.act == BR_SHUFFLE_ALL) {
            uint32_t start = 0, n; uint16_t *qb = library_queue_buf();
            if (r.act == BR_SHUFFLE_ALL) { uint32_t seed = cycles() | 1u; n = library_build_queue(LQ_ALL_SHUFFLED, 0, 0, qb, 16384u, &start, seed); mp3play_play_queue(n, 0, LQ_ALL_NATURAL, 0, 1, seed); }
            else { n = library_build_queue(r.kind, r.parent, r.pos, qb, 16384u, &start, 0); mp3play_play_queue(n, start, r.kind, r.parent, 0, 0); }
            leave = 1;
        }
    }
    refresh();
    return leave;
}
const browser_view_t *browser_view(void) { return &view; }
