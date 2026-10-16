/* fw2/cores/view_core.h -- Player 2 views (docs/features/PLAYER_V2_SPEC.md section 11, HELIOS_SPEC section 11). A view is { enter, exit, input, draw, invalidates }. There is ONE place a view is left,
 * vw_switch(): it calls the old view's exit, the new view's enter, then marks the Helios regions the new view repaints dirty (`invalidates`, a bit per region). Nothing else changes what is on screen
 * when a view changes, so a hand-written repaint flag cannot be forgotten at a call site (the B-349 class of bug). A view's input returns VW_STAY or the view to switch to (VW_BACK = the one before).
 * Pure: no drawing, no registers. Host-tested (sim/test_p2_views.py). */
#ifndef VIEW_CORE_H
#define VIEW_CORE_H
#include <stdint.h>
#include "helios_core.h"
#ifndef BUG
#define BUG 0
#endif
#define VW_STAY (-1)
#define VW_BACK (-2)
typedef struct {
    void (*enter)(void);                 /* the view is about to be shown: set up its state, clear what it must */
    void (*exit)(void);                  /* the view is being left (may be 0) */
    int  (*input)(uint32_t edge);        /* a key edge: VW_STAY, VW_BACK or the view to switch to */
    void (*draw)(void);                  /* once per loop pass: mark its regions dirty when something changed (may be 0) */
    uint32_t invalidates;                /* Helios regions repainted when this view is entered */
    void (*redraw)(void);                /* repaint everything WITHOUT changing the view's state (the colours changed); 0 = enter() */
} view_t;
typedef struct { const view_t *tab; uint8_t n, cur, prev, busy, pending; uint32_t switches; } vw_t;

static inline void vw_init(vw_t *v, const view_t *tab, uint32_t n, uint32_t first)
{
    v->tab = tab; v->n = (uint8_t)n; v->cur = (uint8_t)first; v->prev = (uint8_t)first; v->busy = 0; v->pending = 0xFFu; v->switches = 0;
}
/* Enters the first view (nothing to leave). */
static inline void vw_start(vw_t *v, hl_t *h)
{
    if (v->tab[v->cur].enter) v->tab[v->cur].enter();
    hl_dirty_mask(h, v->tab[v->cur].invalidates);
}
static inline void vw_switch(vw_t *v, hl_t *h, int to)
{
    if (to == VW_BACK) to = v->prev;
    if (to < 0 || to >= (int)v->n) return;
    if (v->busy) { v->pending = (uint8_t)to; return; }                             /* asked from inside an enter/exit: applied right after, never nested */
    if (to == (int)v->cur) return;
    v->busy = 1;
#if BUG != 1
    if (v->tab[v->cur].exit) v->tab[v->cur].exit();
#endif
    v->prev = v->cur; v->cur = (uint8_t)to;
#if BUG != 3
    hl_clear(h);                                                                    /* regions the old view still had pending (deferred by the beam) must not draw over the new view */
#endif
    if (v->tab[to].enter) v->tab[to].enter();
#if BUG != 2
    hl_dirty_mask(h, v->tab[to].invalidates);
#endif
    v->switches++;
    v->busy = 0;
    if (v->pending != 0xFFu) { uint8_t p = v->pending; v->pending = 0xFFu; vw_switch(v, h, p); }
}
/* The colours changed (or anything else that makes the whole picture stale): the current view repaints itself in place. */
static inline void vw_refresh(vw_t *v, hl_t *h)
{
    const view_t *w = &v->tab[v->cur];
    hl_clear(h);
    if (w->redraw) w->redraw(); else if (w->enter) w->enter();
    hl_dirty_mask(h, w->invalidates);
}
/* A key edge to the current view; a switch it asks for is made here. */
static inline void vw_input(vw_t *v, hl_t *h, uint32_t edge)
{
    if (!v->tab[v->cur].input) return;
    int r = v->tab[v->cur].input(edge);
    if (r != VW_STAY) vw_switch(v, h, r);
}
static inline void vw_draw(vw_t *v) { if (v->tab[v->cur].draw) v->tab[v->cur].draw(); }
#endif
