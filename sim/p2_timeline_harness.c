/* Host harness for fw2/cores/timeline_core.h: reads operations on stdin, prints the answer of each query (sim/test_p2_timeline.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "timeline_core.h"
int main(void)
{
    static tl_t t; char op[16];
    while (scanf("%15s", op) == 1) {
        if (!strcmp(op, "INIT")) { unsigned c; scanf("%u", &c); tl_init(&t, c); }
        else if (!strcmp(op, "OPEN")) { unsigned gen, src, exp, tol, num, den, fl; scanf("%u %u %u %u %u %u %u", &gen, &src, &exp, &tol, &num, &den, &fl); printf("O %d\n", tl_open(&t, gen, src, exp, (uint16_t)tol, (uint16_t)num, (uint16_t)den, (uint8_t)fl) ? 1 : 0); }
        else if (!strcmp(op, "PUT")) { unsigned n; scanf("%u", &n); tl_put(&t, n); }
        else if (!strcmp(op, "CLOSE")) { printf("C %d\n", tl_close(&t)); }
        else if (!strcmp(op, "FLUSH")) { unsigned c; scanf("%u", &c); tl_flush(&t, c); }
        else if (!strcmp(op, "Q")) {
            unsigned h; scanf("%u", &h);
            uint32_t sw = tl_advance(&t, h); const tl_seg_t *g = tl_current(&t, h);
            printf("Q %d %u %u %u %u %u %d %u\n", g ? (int)g->gen : -1, g ? tl_pos(g, h) : 0u, sw, t.swaps, t.n, t.acct_err, (int)t.last_delta, t.put_total);
        }
    }
    return 0;
}
