/* ================================================================== MEDIA LIBRARY (TAU_LIBRARY)
 * Browse and play from a host-built index instead of a playlist (spec: docs/MEDIA_LIBRARY_0.4_SPEC.md).
 *
 * The index (tau-library.tdb, data slot 5) is built on the host by tools/tau_library.py and read once at boot into PSRAM
 * by the portable core in library_core.h (tested on the host against the Python reference). Here: the boot loader with
 * its fail-safe, the full-screen browse overlay (same frame as the playlist), the play queue and the hooks that let
 * next / previous / auto-advance follow the queue.
 *
 * Any index failure switches the LIBRARY OFF (no Library screen, Info shows the code); single files and playlists keep
 * working exactly as without it. Tracks are opened by ABSOLUTE path (proven on hardware, B-033). */

#include "library_core.h"

#define LIB_SLOT_ID   5u
/* Rows are the Settings menu's: 36 px, 7 visible (see the drawing section). */
#define LIB_ROW_H    36u
#define LIB_ROWS     7u
#define LIB_PS_BASE   0xA4000000u                  /* PSRAM CPU window */
#define LIB_IMG       ((volatile uint8_t *)(uintptr_t)(LIB_PS_BASE + 0x010000u))    /* index image, 4 MiB cap */
#define LIB_IMG_CAP   0x400000u
#define LIB_Q         ((volatile uint16_t *)(uintptr_t)(LIB_PS_BASE + 0x510000u))   /* play queue, 32 KiB     */
#define LIB_DEAD      ((volatile uint32_t *)(uintptr_t)(LIB_PS_BASE + 0x520000u))   /* dead-track bitmap      */
#define LIB_DEAD_WORDS (LIB_MAX_TRACKS / 32u)

static lib_t    lib;
static uint8_t  lib_err;            /* E-code when OFF                             */
static uint32_t lib_ms;             /* boot load time                              */

/* Browse stack: HOME > (ARTISTS > ALBUMS_OF > TRACKS_OF) | ALBUMS > TRACKS_OF | TRACKS | LISTS > TRACKS_LIST */
enum { LV_HOME = 0, LV_ARTISTS, LV_ALBUMS, LV_ALBUMS_OF, LV_TRACKS_OF, LV_TRACKS, LV_LISTS, LV_TRACKS_LIST };
typedef struct { uint8_t kind; uint16_t parent, sel, top, count; } lib_lv_t;
static lib_lv_t lib_stk[4];
static uint8_t  lib_depth;
#define LIB_CUR (lib_stk[lib_depth])

static uint32_t lib_rep_at;
static uint16_t lib_mq_sel = 0xFFFFu, lib_mq_off;
static uint32_t lib_mq_next;

/* Play request (set by the overlay, executed by the main loop at the same point as a playlist pick). */
enum { LP_NONE = 0, LP_RANGE, LP_ALLTITLE, LP_SHUFFLE, LP_LIST };
static uint8_t  lib_play_req;
static uint16_t lib_req_first, lib_req_n, lib_req_pos, lib_req_hid;
static uint8_t  lib_req_hk;                       /* history kind: 1 album, 2 artist, 3 playlist, 4 all tracks A-Z, 5 Shuffle All */

/* The queue: u16 track ids in PSRAM, a position, and whether it came from Shuffle All (re-shuffled on wrap). */
static uint8_t  lib_q_shuffle;
static uint32_t lib_seed;

/* ---------------------------------------------------------------- boot load and proof */
/* The index lives in the PSRAM window, which does not exist on an older bitstream: prove it before any store
 * (same rule as the art buffer, KB-039). */
COLD_FN static int lib_psram_prove(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG(0x80000088u) != 0x50535231u) return 0;
    if (!((REG(0x800000A0u) >> 16) & 1u)) return 0;
    volatile uint32_t *w[4] = { (volatile uint32_t *)LIB_IMG, (volatile uint32_t *)(LIB_IMG + LIB_IMG_CAP - 4u),
                                (volatile uint32_t *)LIB_Q, (volatile uint32_t *)(LIB_DEAD + LIB_DEAD_WORDS - 1u) };
    for (uint32_t k = 0; k < 2u; k++)
        for (uint32_t i = 0; i < 4u; i++) {
            *w[i] = pat[k] ^ i;
            if (*w[i] != (pat[k] ^ i)) return 0;
        }
    return 1;
}

static void settings_mark_dirty(void);

COLD_FN static int lib_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;                       /* the window IS the tag buffer */
    return target_read_slot(LIB_SLOT_ID, off, TAG_OFF, len);
}

COLD_FN static void lib_boot_load(void)
{
    lib_state = LIB_ST_NONE; lib_err = 0; lib_depth = 0;
    if (lib_disabled) return;                    /* switched off in Menu > Settings > Library: legacy mode runs as if no index existed */
    /* No index file: not an error, and nothing else is touched. The host fails a read past the end, and a
     * present file always serves its first byte. */
    if (!target_read_slot(LIB_SLOT_ID, 0u, TAG_OFF, 1u)) return;
    if (TAG_SIZE < LIB_WIN) { lib_state = LIB_ST_OFF; lib_err = LIB_E_COUNT; return; }   /* window must fit the tag buffer */
    uint32_t t0 = cycles();
    int e;
    if (!lib_psram_prove()) e = LIB_E_PSRAM;
    else e = lib_load(&lib, lib_rd, 0, tagbuf, LIB_IMG, LIB_IMG_CAP);
    lib_ms = (cycles() - t0) / (CLK_HZ / 1000u);
    if (e) { lib_state = LIB_ST_OFF; lib_err = (uint8_t)e; return; }
    for (uint32_t i = 0; i < LIB_DEAD_WORDS; i++) LIB_DEAD[i] = 0u;
    lib_state = LIB_ST_OK;
    lib_stk[0].kind = LV_HOME;
}

static int  lib_is_dead(uint32_t id) { return (LIB_DEAD[id >> 5] >> (id & 31u)) & 1u; }
static void lib_mark_dead(uint32_t id) { LIB_DEAD[id >> 5] |= 1u << (id & 31u); }

/* ---------------------------------------------------------------- rows */
static uint32_t lib_home_n(void) { return 4u + (lib.n_lists ? 1u : 0u); }

static uint32_t lib_view_of(uint8_t kind)       /* jump-table view, or 3 when the list is not A-Z sorted */
{
    return kind == LV_ARTISTS ? LIB_V_ARTISTS : kind == LV_ALBUMS ? LIB_V_ALBUMS : kind == LV_TRACKS ? LIB_V_TRACKS : 3u;
}

COLD_FN static uint32_t lib_row_track(const lib_lv_t *v, uint32_t pos)
{
    if (v->kind == LV_TRACKS) return lib_track_by_title(&lib, pos);
    if (v->kind == LV_TRACKS_LIST) return lib_list_item(&lib, v->parent, pos);
    lib_album_t a; lib_album(&lib, v->parent, &a);
    return (uint32_t)a.first_track + pos;
}

COLD_FN static uint32_t lib_row_album(const lib_lv_t *v, uint32_t pos)
{
    if (v->kind == LV_ALBUMS) return lib_album_by_title(&lib, pos);
    lib_artist_t r; lib_artist(&lib, v->parent, &r);
    return (uint32_t)r.first_album + pos;
}

/* Label, optional track-number tile (-1 none) and optional year of one row. */
COLD_FN static void lib_row_data(const lib_lv_t *v, uint32_t pos, char *lbl, uint32_t cap, int32_t *tile, uint32_t *year)
{
    *tile = -1; *year = 0; lbl[0] = 0;
    switch (v->kind) {
    case LV_HOME: {
        static const char *const h[5] = { "ARTISTS", "ALBUMS", "TRACKS", "SHUFFLE ALL", "PLAYLISTS" };
        const char *s = h[pos < 5u ? pos : 4u];
        uint32_t n = 0;
        while (s[n] && n + 1u < cap) { lbl[n] = s[n]; n++; }
        lbl[n] = 0;
        break; }
    case LV_ARTISTS: { lib_artist_t r; lib_artist(&lib, pos, &r); lib_str(&lib, r.name, lbl, cap); break; }
    case LV_ALBUMS: case LV_ALBUMS_OF: {
        lib_album_t a; lib_album(&lib, lib_row_album(v, pos), &a);
        lib_str(&lib, a.title, lbl, cap); *year = a.year; break; }
    case LV_LISTS: { lib_list_t p; lib_list(&lib, pos, &p); lib_str(&lib, p.name, lbl, cap); *year = p.n; break; }   /* right-hand number = length */
    case LV_TRACKS_LIST: {                           /* the tile numbers the position in the list */
        lib_track_t t; lib_track(&lib, lib_row_track(v, pos), &t);
        lib_str(&lib, t.title, lbl, cap);
        *tile = (int32_t)(pos + 1u);
        break; }
    default: {
        lib_track_t t; lib_track(&lib, lib_row_track(v, pos), &t);
        lib_str(&lib, t.title, lbl, cap);
        *tile = (int32_t)(t.tno & 1023u);
        break; }
    }
}

COLD_FN static void lib_push(uint8_t kind, uint16_t parent)
{
    if (lib_depth >= 3u) return;
    lib_lv_t *v = &lib_stk[++lib_depth];
    v->kind = kind; v->parent = parent; v->sel = 0; v->top = 0;
    switch (kind) {
    case LV_ARTISTS: v->count = lib.n_artists; break;
    case LV_ALBUMS:  v->count = lib.n_albums;  break;
    case LV_TRACKS:  v->count = lib.n_tracks;  break;
    case LV_ALBUMS_OF: { lib_artist_t r; lib_artist(&lib, parent, &r); v->count = r.n_albums; break; }
    case LV_LISTS:   v->count = lib.n_lists;   break;
    case LV_TRACKS_LIST: { lib_list_t p; lib_list(&lib, parent, &p); v->count = p.n; break; }
    default:         { lib_album_t a; lib_album(&lib, parent, &a); v->count = a.n_tracks; break; }
    }
    lib_ui_dirty = 1u;
}

COLD_FN static void lib_follow(void)
{
    lib_lv_t *v = &LIB_CUR;
    if (v->sel < v->top) v->top = v->sel;
    else if (v->sel >= v->top + LIB_ROWS) v->top = (uint16_t)(v->sel - LIB_ROWS + 1u);
    if (v->count > LIB_ROWS && v->top > v->count - LIB_ROWS) v->top = (uint16_t)(v->count - LIB_ROWS);
    if (v->count <= LIB_ROWS) v->top = 0;
}

/* ---------------------------------------------------------------- drawing */
/* Same look as the Settings menu: 36 px rows (SET_MENU_ROW_H), a rounded accent bar 4 px shorter than the row,
 * text 8 px down, and a ">" at the right of rows that open a deeper level. 7 rows fit. */
#define LIB_TILE_W   28u
#define LIB_TILE_H   18u

static char *lib_cat(char *q, const char *s) { while (*s) *q++ = *s++; return q; }

/* Does activating this row open a deeper level (shown with the ">" like a settings group)? */
static int lib_row_opens(uint8_t kind)
{
    return kind == LV_HOME || kind == LV_ARTISTS || kind == LV_ALBUMS || kind == LV_ALBUMS_OF || kind == LV_LISTS;
}

/* Track-number tile: theme colour, black number, always two digits (the owner's rule: 01, 07, 12). A selected row
 * is the same theme colour, so the tile gets a panel-coloured rim to stay visible. */
COLD_FN static void lib_tile(uint32_t x, uint32_t y, uint32_t n, int sel)
{
    if (n > 99u) n = 99u;
    char d[3] = { (char)('0' + n / 10u), (char)('0' + n % 10u), 0 };
    if (sel) fb_round_rect_on(x - 1u, y - 1u, LIB_TILE_W + 2u, LIB_TILE_H + 2u, 4u, UI_PANEL, ui_accent);
    fb_round_rect_on(x, y, LIB_TILE_W, LIB_TILE_H, 3u, ui_accent, UI_PANEL);
    fb_set_color(0x0000u, ui_accent);
    uint32_t w = fb_text_width(d, TS_1X);
    fb_text_clipped(x + (LIB_TILE_W - w) / 2u, y + (LIB_TILE_H - FB_CELL(TS_1X)) / 2u, d, TS_1X, TS_1X, w + 2u);
}

COLD_FN static void lib_row_body(uint32_t i)
{
    const lib_lv_t *v = &LIB_CUR;
    uint32_t y = PL_UI_LIST_Y + i * LIB_ROW_H;
    uint32_t pos = (uint32_t)v->top + i;
    uint32_t rw = PL_UI_W - 8u - (v->count > LIB_ROWS ? 12u : 0u);     /* stop short of the scroll bar */
    if (pos >= v->count) { fb_rect(PL_UI_X + 4u, y, rw, LIB_ROW_H, UI_PANEL); return; }

    int sel = (pos == v->sel);
    uint16_t bg = sel ? ui_accent : UI_PANEL;
    fb_rect(PL_UI_X + 4u, y, rw, LIB_ROW_H, UI_PANEL);                 /* the whole slot, then the bar on it */
    if (sel) fb_round_rect_on(PL_UI_X + 4u, y, rw, LIB_ROW_H - 4u, 5u, bg, UI_PANEL);

    char nm[64];
    int32_t tile; uint32_t year;
    lib_row_data(v, pos, nm, sizeof(nm), &tile, &year);

    uint32_t ty = y + 8u;
    uint32_t tx = PL_UI_TEXT_X, right = PL_UI_X + PL_UI_W - 16u - (v->count > LIB_ROWS ? 12u : 0u);
    if (lib_row_opens(v->kind)) {                                       /* ">" like a settings group */
        right -= 12u;
        fb_set_color(sel ? UI_PANEL : UI_DIM, bg);
        fb_text_clipped(right, ty, ">", TS_1X, TS_1X, 12u);
        right -= 8u;
    }
    if (tile >= 0) { lib_tile(PL_UI_X + 10u, y + (LIB_ROW_H - 4u - LIB_TILE_H) / 2u, (uint32_t)tile, sel); tx = PL_UI_X + 10u + LIB_TILE_W + 8u; }
    if (year) {
        char yb[8], *q = yb;
        q = ui_dec(q, year); *q = 0;
        uint32_t w = fb_text_width(yb, TS_1X);
        fb_set_color(sel ? UI_PANEL : UI_DIM, bg);
        fb_text_clipped(right - w, ty, yb, TS_1X, TS_1X, w + 2u);
        right -= w + 8u;
    }
    const char *txt = nm;
    if (sel && lib_mq_off) {
        uint32_t len = 0;
        while (nm[len]) len++;
        txt = nm + (lib_mq_off < len ? lib_mq_off : 0u);
    }
    fb_set_color(sel ? UI_PANEL : UI_WHITE, bg);
    fb_text_boxed(tx, ty, txt, TS_1X, TS_1X, right - tx, right);
}

COLD_FN static void lib_ui_draw_body(void)
{
    lib_lv_t *v = &LIB_CUR;
    if (v->kind == LV_HOME) v->count = (uint16_t)lib_home_n();
    if (v->sel >= v->count) v->sel = (uint16_t)(v->count ? v->count - 1u : 0u);
    lib_follow();

    char title[40], right[24], *q = title;
    switch (v->kind) {
    case LV_HOME:    q = lib_cat(q, "LIBRARY"); break;
    case LV_ARTISTS: q = lib_cat(q, "ARTISTS"); break;
    case LV_ALBUMS:  q = lib_cat(q, "ALBUMS");  break;
    case LV_TRACKS:  q = lib_cat(q, "TRACKS");  break;
    case LV_LISTS:   q = lib_cat(q, "PLAYLISTS"); break;
    case LV_TRACKS_LIST: { lib_list_t p; lib_list(&lib, v->parent, &p); q += lib_str(&lib, p.name, q, 34); break; }
    case LV_ALBUMS_OF: { lib_artist_t r; lib_artist(&lib, v->parent, &r); q += lib_str(&lib, r.name, q, 34); break; }
    default:           { lib_album_t a; lib_album(&lib, v->parent, &a); q += lib_str(&lib, a.title, q, 34); break; }
    }
    *q = 0;
    q = right;
    if (v->kind != LV_HOME) { q = ui_dec(q, (uint32_t)v->sel + 1u); q = lib_cat(q, " / "); q = ui_dec(q, v->count); }
    *q = 0;
    const char *hint = v->kind == LV_HOME ? "A OPEN   B CLOSE"
                     : v->kind == LV_ARTISTS ? "A OPEN  X PLAY  L R A-Z"
                     : (v->kind == LV_ALBUMS || v->kind == LV_ALBUMS_OF || v->kind == LV_LISTS) ? "A OPEN  X PLAY  B BACK"
                     : "A PLAY   B BACK";
    ov_frame(title, right, hint);

    if (v->count > LIB_ROWS) {
        uint32_t track_x = PL_UI_X + PL_UI_W - 11u, track_y = PL_UI_LIST_Y, track_h = LIB_ROWS * LIB_ROW_H - 4u;
        fb_rect(track_x, track_y, 3u, track_h, ui_mix(UI_PANEL, UI_DIM, 1u, 3u));
        uint32_t span = v->count - LIB_ROWS;
        uint32_t th = track_h * LIB_ROWS / v->count;
        if (th < 8u) th = 8u;
        fb_rect(track_x, track_y + (track_h - th) * v->top / span, 3u, th, ui_accent);
    }
    for (uint32_t i = 0; i < LIB_ROWS; i++) lib_row_body(i);
}

COLD_FN static void lib_ui_row(uint32_t i)
{
    uint8_t o = ov_draw; ov_draw = 1u;
    lib_row_body(i);
    ov_draw = o;
}

COLD_FN static void lib_ui_draw(void)
{
    uint8_t o = ov_draw; ov_draw = 1u;
    lib_ui_draw_body();
    ov_draw = o;
}

/* Selected row scrolls when its label does not fit (same rule as the playlist). */
COLD_FN static void lib_ui_marquee(void)
{
    lib_lv_t *v = &LIB_CUR;
    if (!v->count) return;
    if (lib_mq_sel != v->sel) { lib_mq_sel = v->sel; lib_mq_off = 0; lib_mq_next = cycles() + CLK_HZ; return; }
    if ((int32_t)(cycles() - lib_mq_next) < 0) return;
    char nm[64]; int32_t tile; uint32_t year;
    lib_row_data(v, v->sel, nm, sizeof(nm), &tile, &year);
    uint32_t avail = PL_UI_W - 40u - (tile >= 0 ? LIB_TILE_W + 2u : 0u) - (year ? 56u : 0u) - (lib_row_opens(v->kind) ? 20u : 0u);
    if (fb_text_width(nm, TS_1X) <= avail) { lib_mq_next = cycles() + CLK_HZ; return; }
    uint32_t len = 0;
    while (nm[len]) len++;
    lib_mq_next = cycles() + CLK_HZ / 3u;
    if (++lib_mq_off >= len) { lib_mq_off = 0; lib_mq_next = cycles() + CLK_HZ; }
    if (v->sel >= v->top && v->sel < v->top + LIB_ROWS) lib_ui_row(v->sel - v->top);
}

/* ---------------------------------------------------------------- playing */
/* Same walk as pl_play_span() over the queue: first entry at or after `pos` that opens, bounded by `span`, with dead
 * tracks remembered. The command path being fatal stops the walk. */
COLD_FN static int lib_play_span(uint32_t pos, int dir, uint32_t span)
{
    if (!lib_qn || !span) return 0;
    if (span > lib_qn) span = lib_qn;
    uint16_t prev_pos = lib_qpos;
    uint8_t  prev_src = lib_src;
    uint32_t leaving_id = cur_file_id;

    sw_have_prev = 0;
    if (target_read_slot(MP3_SLOT_ID, 0, TAG_OFF, 16u)) {
        for (uint32_t i = 0; i < sizeof(sw_prev_head); i++) sw_prev_head[i] = tagbuf[i];
        sw_have_prev = 1;
    }
    uint32_t at = pos % lib_qn, nmiss = 0, nfail = 0;
    char path[LIB_MAX_PATH + 8u];
    for (uint32_t n = 0; n < span; n++) {
        uint32_t id = LIB_Q[at];
        if (lib_is_dead(id)) {
            nmiss++;
        } else if (!lib_path(&lib, id, path, sizeof(path))) {
            nfail++;
        } else {
            lib_qpos = (uint16_t)at; lib_src = 1u;
            if (pl_open_name(path)) {
                track_from_pl = 0u;                 /* the resume saver is for playlists */
                if (lib_h) { lib_h = (lib_h & 0x3FFFu) | ((uint32_t)lib_qpos << 14); settings_mark_dirty(); }   /* remember the position */
                pl_arm_load(leaving_id);
                if (nmiss)      ui_toast_set("SKIPPED", nmiss, " MISSING");
                else if (nfail) ui_toast_set("SKIPPED", nfail, " UNREADABLE");
                return 1;
            }
            switch (pl_classify_failure()) {
            case PLF_FATAL: lib_qpos = prev_pos; lib_src = prev_src; pl_toast_open_fail(); return 0;
            case PLF_DEAD:  lib_mark_dead(id); nmiss++; break;
            default:        nfail++; break;
            }
        }
        at = dir > 0 ? (at + 1u) % lib_qn : (at + lib_qn - 1u) % lib_qn;
    }
    lib_qpos = prev_pos; lib_src = prev_src;
    if (nmiss) ui_toast_set("NO TRACKS:", nmiss, " MISSING");
    else       pl_toast_open_fail();
    return 0;
}

COLD_FN static int lib_skip(int delta)
{
    if (!lib_qn) return 0;
    uint32_t next = (uint32_t)(lib_qpos + lib_qn + (delta > 0 ? 1u : lib_qn - 1u)) % lib_qn;
    return lib_play_span(next, delta, lib_qn);
}

COLD_FN static int lib_advance_auto(void)
{
    if (rep_mode == REP_ONE || !lib_qn) return 0;
    if ((uint32_t)lib_qpos + 1u >= lib_qn) {
        if (rep_mode != REP_ALL) return 0;
        if (lib_q_shuffle) { lib_seed = (lib_seed * 1664525u + cycles()) & 0x7FFFFFFFu; lib_hs = lib_seed; lib_queue_shuffle_all(&lib, LIB_Q, lib_seed); settings_mark_dirty(); }
        return lib_play_span(0u, +1, lib_qn);
    }
    if (rep_mode == REP_ALL) return lib_play_span((uint32_t)lib_qpos + 1u, +1, lib_qn);
    return lib_play_span((uint32_t)lib_qpos + 1u, +1, lib_qn - ((uint32_t)lib_qpos + 1u));
}

/* Executed by the main loop, like a playlist pick: build the queue, then play from `pos`. */
COLD_FN static int lib_play_start(void)
{
    uint32_t n = 0;
    lib_q_shuffle = 0;
    switch (lib_play_req) {
    case LP_RANGE:
        n = lib_req_n;
        for (uint32_t i = 0; i < n; i++) LIB_Q[i] = (uint16_t)(lib_req_first + i);
        break;
    case LP_ALLTITLE:
        n = lib.n_tracks;
        for (uint32_t i = 0; i < n; i++) LIB_Q[i] = (uint16_t)lib_track_by_title(&lib, i);
        break;
    case LP_LIST:
        n = lib_queue_list(&lib, lib_req_first, LIB_Q);
        break;
    case LP_SHUFFLE:
        lib_seed = (cycles() ^ 0x2545F491u) & 0x7FFFFFFFu;
        n = lib_queue_shuffle_all(&lib, LIB_Q, lib_seed);
        lib_q_shuffle = 1u;
        break;
    default: return 0;
    }
    lib_play_req = LP_NONE;
    if (!n) return 0;
    lib_h = (uint32_t)lib_req_hk | ((uint32_t)lib_req_hid << 3) | ((uint32_t)lib_req_pos << 14);   /* what is being played, for the next start */
    lib_hb = lib.build_id & 0x7FFFFFFFu;
    if (lib_q_shuffle) lib_hs = lib_seed;
    settings_mark_dirty();
    uint16_t old_n = lib_qn;
    lib_qn = (uint16_t)n;
    pcm_flush();
    refill_drain();
    if (lib_play_span(lib_req_pos, +1, n)) { ring_fill = 0; ring_rd = 0; return 1; }
    lib_qn = old_n;                               /* nothing opened: keep whatever was playing */
    return 0;
}

/* At boot: open (load, do not start) what the user was last playing: the album, artist, playlist, all-tracks order or
 * Shuffle All (same seed, so the same order), at the same queue position. With no history, or an index that was rebuilt
 * since, the first track of the first album. Returns 1 when a track was opened. */
COLD_FN static int lib_boot_restore(void)
{
    uint32_t hk = lib_h & 7u, hid = (lib_h >> 3) & 0x7FFu, pos = (lib_h >> 14) & 0x3FFFu, n = 0;
    lib_q_shuffle = 0;
    if (!lib_h || lib_hb != (lib.build_id & 0x7FFFFFFFu)) hk = 0;
    switch (hk) {
    case 1:
        if (hid < lib.n_albums) { lib_album_t a; lib_album(&lib, hid, &a); n = a.n_tracks;
            for (uint32_t i = 0; i < n; i++) LIB_Q[i] = (uint16_t)(a.first_track + i); }
        break;
    case 2:
        if (hid < lib.n_artists) {
            lib_artist_t r; lib_artist(&lib, hid, &r);
            if (r.n_albums) {
                lib_album_t a0, a1;
                lib_album(&lib, r.first_album, &a0); lib_album(&lib, r.first_album + r.n_albums - 1u, &a1);
                n = (uint32_t)a1.first_track + a1.n_tracks - a0.first_track;
                for (uint32_t i = 0; i < n; i++) LIB_Q[i] = (uint16_t)(a0.first_track + i);
            }
        }
        break;
    case 3: if (hid < lib.n_lists) n = lib_queue_list(&lib, hid, LIB_Q); break;
    case 4:
        n = lib.n_tracks;
        for (uint32_t i = 0; i < n; i++) LIB_Q[i] = (uint16_t)lib_track_by_title(&lib, i);
        break;
    case 5: lib_seed = lib_hs; n = lib_queue_shuffle_all(&lib, LIB_Q, lib_seed); lib_q_shuffle = 1u; break;
    default: break;
    }
    if (!n) {                                        /* first run or unusable history */
        lib_album_t a; lib_album(&lib, 0, &a);
        n = a.n_tracks; hk = 1u; hid = 0u; pos = 0u; lib_q_shuffle = 0;
        for (uint32_t i = 0; i < n; i++) LIB_Q[i] = (uint16_t)(a.first_track + i);
    }
    if (pos >= n) pos = 0u;
    lib_h = hk | (hid << 3) | (pos << 14);
    lib_hb = lib.build_id & 0x7FFFFFFFu;
    lib_qn = (uint16_t)n;
    hold_paused = 1u;                                /* the load ends paused: open it, do not start it */
    if (lib_play_span(pos, +1, n)) return 1;
    hold_paused = 0u; lib_qn = 0u;
    return 0;
}

/* Title, album and (when the tag has none) artist from the index, so the card can be drawn as soon as the head is read. */
COLD_FN static void lib_meta_apply(void)
{
    if (!lib_qn) return;
    lib_track_t t; lib_album_t a;
    lib_track(&lib, LIB_Q[lib_qpos], &t);
    lib_album(&lib, t.album, &a);
    lib_str(&lib, t.title, track_title, sizeof(track_title));
    lib_str(&lib, a.title, track_album, sizeof(track_album));
    if (!track_artist[0]) { lib_artist_t r; lib_artist(&lib, a.artist, &r); lib_str(&lib, r.name, track_artist, sizeof(track_artist)); }
}

/* ---------------------------------------------------------------- input */
COLD_FN static void lib_ui_close(void) { lib_ui_open = 0u; pl_ui_restore = 1u; }

COLD_FN static void lib_ui_enter(void)
{
    lib_ui_open = 1u; lib_ui_dirty = 1u; lib_mq_sel = 0xFFFFu;
    if (lib_depth == 0u) lib_stk[0].kind = LV_HOME;
}

COLD_FN static void lib_request(uint8_t kind, uint16_t first, uint16_t n, uint16_t pos, uint8_t hk, uint16_t hid)
{
    lib_play_req = kind; lib_req_first = first; lib_req_n = n; lib_req_pos = pos; lib_req_hk = hk; lib_req_hid = hid;
    lib_ui_close();
}

/* Called after set_input(); consumes the pad while the library overlay is up. */
COLD_FN static void lib_ui_input(uint32_t *edge_p, uint32_t *fall_p, uint32_t *keys_p)
{
    uint32_t edge = *edge_p, keys = *keys_p;
    /* B-145: Start closes the library outright, from any depth, the same shortcut set_input()
     * already gives Settings -- browsing Artists -> an artist -> its Albums used to mean three
     * presses of B (one per level) just to get back to the player. Left as one press with A/B/L/R
     * still fully working underneath. Matches lib_request()'s own precedent of NOT resetting
     * lib_depth on close (browse position is remembered for the next open), so this behaves like
     * "I'm done, not I made a mistake" -- a fresh B-by-B backout still resets depth to 0 as before. */
    if (edge & KEY_START) {
        lib_ui_close();
        *edge_p = edge & KEY_SELECT; *fall_p &= KEY_SELECT; *keys_p = keys & KEY_SELECT;
        return;
    }
    lib_lv_t *v = &LIB_CUR;
    if (v->kind == LV_HOME) v->count = (uint16_t)lib_home_n();
    uint32_t n = v->count ? v->count : 1u;      /* never divide by zero on an empty list */

    if (edge & KEY_UP)   { v->sel = v->sel ? (uint16_t)(v->sel - 1u) : (uint16_t)(n - 1u); lib_follow(); lib_ui_dirty = 1u; }
    if (edge & KEY_DOWN) { v->sel = (uint16_t)((v->sel + 1u) % n); lib_follow(); lib_ui_dirty = 1u; }
    if (edge & (KEY_UP | KEY_DOWN)) lib_rep_at = cycles() + CLK_HZ / 1000u * PL_HOLD_MS;
    if ((keys & (KEY_UP | KEY_DOWN)) && (int32_t)(cycles() - lib_rep_at) >= 0) {
        lib_rep_at = cycles() + CLK_HZ / 16u;
        if (keys & KEY_UP) v->sel = v->sel ? (uint16_t)(v->sel - 1u) : (uint16_t)(n - 1u);
        else               v->sel = (uint16_t)((v->sel + 1u) % n);
        lib_follow(); lib_ui_dirty = 1u;
    }
    /* Left and Right are Back and Forward, as in the menus (they used to page, and both jumped to an end of a short list). Long
     * lists: hold Up/Down, or the letter jump on L1/R1. */
    if (edge & KEY_RIGHT) edge |= KEY_A;
    if (edge & KEY_LEFT)  edge |= KEY_B;

    uint32_t view = lib_view_of(v->kind);
    if (view < 3u && (edge & (KEY_L1 | KEY_R1))) {
        v->sel = (uint16_t)lib_jump(&lib, view, n, v->sel, (edge & KEY_R1) ? +1 : -1);
        lib_follow(); lib_ui_dirty = 1u;
    }

    if ((edge & KEY_A) && v->count) {
        switch (v->kind) {
        case LV_HOME:
            if (v->sel == 0u) lib_push(LV_ARTISTS, 0);
            else if (v->sel == 1u) lib_push(LV_ALBUMS, 0);
            else if (v->sel == 2u) lib_push(LV_TRACKS, 0);
            else if (v->sel == 3u) lib_request(LP_SHUFFLE, 0, 0, 0, 5u, 0);
            else lib_push(LV_LISTS, 0);
            break;
        case LV_LISTS:     lib_push(LV_TRACKS_LIST, v->sel); break;
        case LV_TRACKS_LIST: lib_request(LP_LIST, v->parent, 0, v->sel, 3u, v->parent); break;
        case LV_ARTISTS:   lib_push(LV_ALBUMS_OF, v->sel); break;
        case LV_ALBUMS: case LV_ALBUMS_OF: lib_push(LV_TRACKS_OF, (uint16_t)lib_row_album(v, v->sel)); break;
        case LV_TRACKS_OF: {
            lib_album_t a; lib_album(&lib, v->parent, &a);
            lib_request(LP_RANGE, a.first_track, a.n_tracks, v->sel, 1u, v->parent);
            break; }
        default: lib_request(LP_ALLTITLE, 0, 0, v->sel, 4u, 0); break;
        }
    }
    if ((edge & KEY_X) && (v->kind == LV_ARTISTS || v->kind == LV_ALBUMS || v->kind == LV_ALBUMS_OF || v->kind == LV_LISTS)) {
        if (v->kind == LV_LISTS) {
            lib_request(LP_LIST, v->sel, 0, 0, 3u, v->sel);
        } else if (v->kind == LV_ARTISTS) {                /* all of an artist: its albums' tracks are contiguous */
            lib_artist_t r; lib_artist(&lib, v->sel, &r);
            lib_album_t a0, a1;
            lib_album(&lib, r.first_album, &a0); lib_album(&lib, r.first_album + r.n_albums - 1u, &a1);
            lib_request(LP_RANGE, a0.first_track, (uint16_t)(a1.first_track + a1.n_tracks - a0.first_track), 0, 2u, v->sel);
        } else {
            lib_album_t a; lib_album(&lib, lib_row_album(v, v->sel), &a);
            lib_request(LP_RANGE, a.first_track, a.n_tracks, 0, 1u, (uint16_t)lib_row_album(v, v->sel));
        }
    }
    if (edge & KEY_B) {
        if (lib_depth) { lib_depth--; lib_ui_dirty = 1u; lib_mq_sel = 0xFFFFu; }
        else lib_ui_close();
    }
    *edge_p = edge & KEY_SELECT;
    *fall_p &= KEY_SELECT;
    *keys_p = keys & KEY_SELECT;
}

/* Value for the Info page's LIBRARY row. */
COLD_FN static char *lib_info_text(char *q)
{
    if (lib_disabled) {
        q = lib_cat(q, "OFF (SETTING)");
    } else if (lib_state == LIB_ST_OK) {
        q = ui_dec(q, lib.n_tracks); q = lib_cat(q, " TRK ");
        q = ui_dec(q, lib_ms / 1000u); *q++ = '.'; q = ui_dec(q, (lib_ms % 1000u) / 100u); q = lib_cat(q, " S ");
        q = lib_cat(q, lib_boot_ok ? "R1" : "R0");   /* B-080: did the boot restore something (R1) or not (R0) */
    } else if (lib_state == LIB_ST_OFF) {
        q = lib_cat(q, "OFF E"); q = ui_dec(q, lib_err);
    } else {
        q = lib_cat(q, "NONE");
    }
    return q;
}
