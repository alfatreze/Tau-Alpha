/* fw2/cores/browse_core.h -- Player 2 library browser navigation (spec section 11): a stack of list levels (Home > Artists > Albums of > Tracks of ..., Home > Albums > Tracks of, Home > Tracks,
 * Home > Playlists > Tracks of) with a selection and a scroll window per level. Pure: the library is reached through two callbacks, so the same code is host-tested against a synthetic
 * library (sim/test_p2_browse.py) and drives the real one on the device. Invariants (checked): the selection is always inside the visible window, the window never runs past the end,
 * Back restores the parent's selection exactly, up/down wrap. */
#ifndef BROWSE_CORE_H
#define BROWSE_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
enum { BK_HOME = 0, BK_ARTISTS, BK_ALBUMS, BK_ALBUMS_OF, BK_TRACKS_OF, BK_TRACKS, BK_LISTS, BK_TRACKS_LIST };
typedef struct {
    uint32_t (*count)(uint8_t kind, uint32_t parent);                   /* rows of a level */
    uint32_t (*child)(uint8_t kind, uint32_t parent, uint32_t pos);     /* the id behind row `pos`: artist, album, track or playlist id */
} br_ops_t;
typedef struct { uint8_t kind; uint32_t parent; uint32_t sel, top, count; } br_lv_t;
#define BR_DEPTH 4u
typedef struct { br_lv_t st[BR_DEPTH]; uint8_t depth; uint8_t rows; } br_t;
/* What Enter did. BR_PLAY carries the level it was pressed on (kind/parent) and the row, for the caller to build a queue from. BR_SHUFFLE_ALL is the Home action row. */
typedef enum { BR_NONE = 0, BR_PUSHED, BR_PLAY, BR_SHUFFLE_ALL } br_act_t;
typedef struct { br_act_t act; uint8_t kind; uint32_t parent, pos, id; } br_result_t;

static inline br_lv_t *br_cur(br_t *b) { return &b->st[b->depth]; }
static inline void br_fix(br_t *b)                                      /* keep the window valid around the selection */
{
    br_lv_t *l = br_cur(b);
    if (!l->count) { l->sel = l->top = 0; return; }
    if (l->sel >= l->count) l->sel = l->count - 1u;
#if BUG != 1
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel >= l->top + b->rows) l->top = l->sel + 1u - b->rows;
#endif
    uint32_t maxtop = l->count > b->rows ? l->count - b->rows : 0u;
    if (l->top > maxtop) l->top = maxtop;
}
static inline void br_open(br_t *b, const br_ops_t *o, uint8_t kind, uint32_t parent)
{
    br_lv_t *l = &b->st[b->depth];
    l->kind = kind; l->parent = parent; l->sel = l->top = 0; l->count = o->count(kind, parent);
}
static inline void br_init(br_t *b, const br_ops_t *o, uint32_t rows)
{
    b->depth = 0; b->rows = (uint8_t)rows; br_open(b, o, BK_HOME, 0);
}
static inline void br_move(br_t *b, int delta)                          /* wraps both ways */
{
    br_lv_t *l = br_cur(b);
    if (!l->count) return;
    if (delta > 0) l->sel = (l->sel + 1u >= l->count) ? 0u : l->sel + 1u;
    else           l->sel = l->sel == 0u ? l->count - 1u : l->sel - 1u;
    br_fix(b);
}
static inline void br_page(br_t *b, int dir)                            /* a page at a time, stopping at the ends (no wrap) */
{
    br_lv_t *l = br_cur(b);
    if (!l->count) return;
    if (dir > 0) l->sel = (l->sel + b->rows >= l->count) ? l->count - 1u : l->sel + b->rows;
    else         l->sel = l->sel < b->rows ? 0u : l->sel - b->rows;
    br_fix(b);
}
/* 0 when already at Home (the caller leaves the browser). Back restores the parent's selection and window as they were. */
static inline int br_back(br_t *b)
{
    if (!b->depth) return 0;
#if BUG == 2
    b->depth--; br_cur(b)->sel = 0; br_cur(b)->top = 0;                 /* forgets where you were */
#else
    b->depth--;
#endif
    return 1;
}
static inline br_result_t br_enter(br_t *b, const br_ops_t *o)
{
    br_result_t r = { BR_NONE, 0, 0, 0, 0 };
    br_lv_t *l = br_cur(b);
    if (!l->count) return r;
    r.kind = l->kind; r.parent = l->parent; r.pos = l->sel;
    uint8_t next = 0xFF; uint32_t par = 0;
    switch (l->kind) {
    case BK_HOME:
        if (l->sel == 0u) next = BK_ARTISTS; else if (l->sel == 1u) next = BK_ALBUMS; else if (l->sel == 2u) next = BK_TRACKS;
        else if (l->sel == 3u) { r.act = BR_SHUFFLE_ALL; return r; }
        else next = BK_LISTS;
        break;
    case BK_ARTISTS:    next = BK_ALBUMS_OF;  par = o->child(l->kind, l->parent, l->sel); break;
    case BK_ALBUMS:
    case BK_ALBUMS_OF:  next = BK_TRACKS_OF;  par = o->child(l->kind, l->parent, l->sel); break;
    case BK_LISTS:      next = BK_TRACKS_LIST; par = o->child(l->kind, l->parent, l->sel); break;
    default:            r.act = BR_PLAY; r.id = o->child(l->kind, l->parent, l->sel); return r;      /* a track row */
    }
    if (b->depth + 1u >= BR_DEPTH) return r;
    b->depth++;
    br_open(b, o, next, par);
    r.act = BR_PUSHED; r.kind = next; r.parent = par;
    return r;
}
#endif
