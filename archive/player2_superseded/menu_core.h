/* fw2/cores/menu_core.h -- Player 2 settings menu logic (spec section 11): a list of items, each a number in a range or a choice from a short list, a selection that wraps, and Left/Right to change the
 * selected value (clamped for a range, wrapping for a choice of few). Pure: the values live with the caller, the labels with the view. Host-tested (sim/test_p2_menu.py). */
#ifndef MENU_CORE_H
#define MENU_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint8_t kind; uint16_t min, max, step; } mi_t;          /* kind 0 = range (clamped), 1 = choice (wraps) */
typedef struct { uint8_t n, sel; uint8_t top, rows; } menu_t;
static inline void menu_init(menu_t *m, uint32_t n, uint32_t rows) { m->n = (uint8_t)n; m->sel = 0; m->top = 0; m->rows = (uint8_t)rows; }
static inline void menu_fix(menu_t *m)
{
    if (m->sel < m->top) m->top = m->sel;
    if (m->sel >= m->top + m->rows) m->top = (uint8_t)(m->sel + 1u - m->rows);
    uint32_t maxtop = m->n > m->rows ? m->n - m->rows : 0u;
    if (m->top > maxtop) m->top = (uint8_t)maxtop;
}
static inline void menu_move(menu_t *m, int d)
{
    if (!m->n) return;
    if (d > 0) m->sel = (uint8_t)(m->sel + 1u >= m->n ? 0u : m->sel + 1u);
    else       m->sel = (uint8_t)(m->sel == 0u ? m->n - 1u : m->sel - 1u);
    menu_fix(m);
}
/* Change the selected item's value by one step in direction d. Returns 1 when it changed. */
static inline int menu_adjust(const menu_t *m, const mi_t *items, uint32_t *vals, int d)
{
    const mi_t *it = &items[m->sel]; uint32_t v = vals[m->sel], nv = v;
    if (it->kind == 1u) {                                                    /* a choice: wraps */
        uint32_t span = (uint32_t)it->max - it->min + 1u;
        nv = d > 0 ? (v + 1u > it->max ? it->min : v + 1u) : (v <= it->min ? it->max : v - 1u);
        (void)span;
    } else {
        uint32_t st = it->step ? it->step : 1u;
#if BUG == 1
        nv = d > 0 ? v + st : v - st;                                        /* no clamp */
#else
        if (d > 0) nv = v + st > it->max ? it->max : v + st;
        else       nv = v < (uint32_t)it->min + st ? it->min : v - st;
#endif
    }
#if BUG == 2
    vals[m->sel] = nv; return 1;                                             /* reports a change that did not happen */
#else
    if (nv == v) return 0;
    vals[m->sel] = nv; return 1;
#endif
}
#endif
