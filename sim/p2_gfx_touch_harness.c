/* Host check of fw2/cores/gfx_touch_core.h: reads "y h" pairs from stdin ("R" resets), prints the spans after each. */
#include <stdio.h>
#include "../fw2/cores/gfx_touch_core.h"
int main(void)
{
    gt_t t; gt_reset(&t); char c; unsigned y, h;
    while (scanf(" %c", &c) == 1) {
        if (c == 'R') gt_reset(&t); else if (c == 'N' && scanf("%u %u", &y, &h) == 2) gt_note(&t, y, h);
        printf("vis %u-%u stash %u-%u visible %d\n", t.y0, t.y1, t.s0, t.s1, gt_visible(&t));
    }
    return 0;
}
