/* fw2/cores/configure_core.h -- the meter Configure page's logic (A on a colour row opens the picker, X exports the config) (no drawing, no registers; host-tested by sim/test_p2_configure.py). The page lists, for the current meter: a PRESET row, every visible parameter of its
 * manifest, and a RESET row. A parameter is visible unless it is hidden (not drawn in Player 2) or its `when` condition (another parameter must have a value) is not met. Up/Down move the selection (wrapping),
 * Left/Right change the value (a number steps and clamps, a choice wraps, an on/off toggles; PRESET steps through the presets); A on RESET re-applies the preset. Editing never touches a value that is not
 * the selected row's, and a row that becomes hidden takes the selection to its neighbour. */
#ifndef CONFIGURE_CORE_H
#define CONFIGURE_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define CF_MAX_ROWS 40u
#define CF_PRESET 0xFEu                 /* row ids: a parameter index, or one of these */
#define CF_RESET  0xFDu
typedef struct { const char *label; uint8_t type, hidden; uint16_t min, max, step; uint8_t nval; const char *const *vals; uint8_t when_idx; uint16_t when_val; } cf_pd_t;     /* = mpd_t (ui/meter.h) */
typedef struct { uint8_t rows[CF_MAX_ROWS]; uint8_t n, sel, top; } cf_t;
enum { CFE_NONE = 0, CFE_PARAM, CFE_PRESET, CFE_RESET, CFE_CLOSE, CFE_PICK, CFE_EXPORT };      /* PICK: A on a colour row opens the picker; EXPORT: X shows the tuned config as a report grid */
typedef struct { uint8_t kind; int8_t dir; } cf_effect_t;
#define CF_VIS 8u
static inline int cf_visible(const cf_pd_t *pd, uint32_t np, const uint16_t *mv, uint32_t i)
{
    if (pd[i].hidden) return 0;
    if (pd[i].when_idx != 255u && pd[i].when_idx < np && mv[pd[i].when_idx] != pd[i].when_val) return 0;
    return 1;
}
/* Rebuild the row list; the selection stays on the same row id when it is still there, else it moves to the nearest row. */
static inline void cf_build(cf_t *c, const cf_pd_t *pd, uint32_t np, const uint16_t *mv)
{
    const uint8_t keep = c->n ? c->rows[c->sel] : CF_PRESET; uint32_t n = 0, sel = 0;
    c->rows[n++] = CF_PRESET;
    for (uint32_t i = 0; i < np && n + 1u < CF_MAX_ROWS; i++) if (cf_visible(pd, np, mv, i)) c->rows[n++] = (uint8_t)i;
    c->rows[n++] = CF_RESET;
    for (uint32_t k = 0; k < n; k++) if (c->rows[k] == keep) sel = k;
    if (keep != CF_PRESET && keep != CF_RESET && c->rows[sel] != keep) {          /* the row went away: stay at the same place in the list */
        sel = c->sel < n ? c->sel : n - 1u;
    }
    c->n = (uint8_t)n; c->sel = (uint8_t)sel;
    if (c->sel < c->top) c->top = c->sel;
    if (c->sel >= c->top + CF_VIS) c->top = (uint8_t)(c->sel + 1u - CF_VIS);
    if (c->n <= CF_VIS) c->top = 0;
}
static inline uint32_t cf_step_value(const cf_pd_t *p, uint32_t v, int dir)
{
    if (p->type == 3u) return v;                                                           /* a colour is edited by the picker, not stepped */
    if (p->type == 2u) return v ? 0u : 1u;
    if (p->type == 1u) { const uint32_t n = p->nval ? p->nval : 1u; return dir > 0 ? (v + 1u) % n : (v + n - 1u) % n; }
    int32_t nv = (int32_t)v + dir * (int32_t)p->step;
#if BUG == 1
    if (nv > (int32_t)p->max) nv = p->min;                                                 /* mutant: wraps instead of clamping */
#else
    if (nv > (int32_t)p->max) nv = p->max;
#endif
    if (nv < (int32_t)p->min) nv = p->min;
    return (uint32_t)nv;
}
#define CF_UP 1u
#define CF_DOWN 2u
#define CF_LEFT 4u
#define CF_RIGHT 8u
#define CF_A 16u
#define CF_B 32u
#define CF_X 64u
static inline cf_effect_t cf_input(cf_t *c, const cf_pd_t *pd, uint32_t np, uint16_t *mv, uint32_t keys)
{
    cf_effect_t e = { CFE_NONE, 0 };
    if (keys & CF_B) { e.kind = CFE_CLOSE; return e; }
    if (keys & (CF_UP | CF_DOWN)) {
        c->sel = (uint8_t)((keys & CF_DOWN) ? (c->sel + 1u) % c->n : (c->sel + c->n - 1u) % c->n);
        if (c->sel < c->top) c->top = c->sel;
        if (c->sel >= c->top + CF_VIS) c->top = (uint8_t)(c->sel + 1u - CF_VIS);
        return e;
    }
    const uint8_t id = c->rows[c->sel];
    if (keys & (CF_LEFT | CF_RIGHT)) {
        const int dir = (keys & CF_RIGHT) ? 1 : -1;
        if (id == CF_PRESET) { e.kind = CFE_PRESET; e.dir = (int8_t)dir; return e; }
        if (id == CF_RESET) return e;
        if (pd[id].type == 3u) return e;                                                   /* a colour: A opens the picker */
        mv[id] = (uint16_t)cf_step_value(&pd[id], mv[id], dir); e.kind = CFE_PARAM;
        cf_build(c, pd, np, mv);
        return e;
    }
    if (keys & CF_X) { e.kind = CFE_EXPORT; return e; }
    if ((keys & CF_A) && id == CF_RESET) { e.kind = CFE_RESET; return e; }
    if ((keys & CF_A) && id < CF_MAX_ROWS && pd[id].type == 3u) { e.kind = CFE_PICK; return e; }
    return e;
}
#endif
