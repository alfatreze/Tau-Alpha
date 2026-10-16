/* Host harness for fw2/cores/view_core.h: three fake views that log their calls. Commands: S to (switch), K edge (input), D (draw), B (a view whose enter asks for another switch). Prints the log. */
#include <stdio.h>
#include "view_core.h"
static int lg[256], ln; static vw_t vw; static hl_t hl; static int redraw_hits[4];
static void rd0(void) { redraw_hits[0]++; } static void rd1(void) { redraw_hits[1]++; } static void rd2(void) { redraw_hits[2]++; }
static vw_t *vp = &vw; static hl_t *hp = &hl;
static void e0(void) { lg[ln++] = 10; } static void x0(void) { lg[ln++] = 11; } static int i0(uint32_t e) { lg[ln++] = 12; return e == 1 ? 1 : e == 2 ? VW_BACK : VW_STAY; } static void d0(void) { lg[ln++] = 13; }
static void e1(void) { lg[ln++] = 20; } static void x1(void) { lg[ln++] = 21; } static int i1(uint32_t e) { lg[ln++] = 22; return e == 1 ? 2 : e == 2 ? VW_BACK : VW_STAY; }
static void e2(void) { lg[ln++] = 30; vw_switch(vp, hp, 0); }            /* a view that immediately asks to go to view 0 from inside its enter */
static void x2(void) { lg[ln++] = 31; }
static const view_t tab[3] = { { e0, x0, i0, d0, 0x1u }, { e1, x1, i1, 0, 0x6u }, { e2, x2, 0, 0, 0x0u } };
int main(void)
{
    char c; unsigned a;
    hl_add(&hl, rd0, 0, HL_WHOLE); hl_add(&hl, rd1, 0, HL_WHOLE); hl_add(&hl, rd2, 0, HL_WHOLE);
    vw_init(&vw, tab, 3, 0); vw_start(&vw, &hl);
    while (scanf(" %c", &c) == 1) {
        if (c == 'S') { if (scanf("%u", &a) == 1) vw_switch(&vw, &hl, (int)a); }
        else if (c == 'K') { if (scanf("%u", &a) == 1) vw_input(&vw, &hl, a); }
        else if (c == 'D') vw_draw(&vw);
        else if (c == 'F') { ln += 0; hl_flush(&hl, 0, 0); }
    }
    for (int i = 0; i < ln; i++) printf("%d ", lg[i]);
    printf("| cur=%d prev=%d switches=%u dirty=%d%d%d\n", vw.cur, vw.prev, vw.switches, hl.r[0].dirty, hl.r[1].dirty, hl.r[2].dirty);
    return 0;
}
