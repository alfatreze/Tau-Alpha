/* Host harness for fw2/cores/cellgrid_core.h. Commands: "g bx by bw bh cell maxc maxr" prints the fitted geometry; "z n" zeroes an n-byte store; "s i v" sets a nibble; "d" dumps the store as hex. */
#include <stdio.h>
#include "cellgrid_core.h"
int main(void)
{
    static uint8_t z[4096]; char c; unsigned a, b, d, e, f, g, h;
    while (scanf(" %c", &c) == 1) {
        if (c == 'g' && scanf("%u %u %u %u %u %u %u", &a, &b, &d, &e, &f, &g, &h) == 7) { cg_geom_t q; const int ok = cg_fit(&q, a, b, d, e, f, g, h); printf("%d %u %u %u %u %u %u w %u h %u\n", ok, q.x0, q.y0, q.cols, q.rows, q.pitch, q.cell, (unsigned)cg_w(&q), (unsigned)cg_h(&q)); }
        else if (c == 'z' && scanf("%u", &a) == 1) cg_zero(z, a);
        else if (c == 's' && scanf("%u %u", &a, &b) == 2) cg_set(z, a, b);
        else if (c == 'q' && scanf("%u", &a) == 1) printf("%u\n", (unsigned)cg_get(z, a));
        else if (c == 'd' && scanf("%u", &a) == 1) { for (unsigned i = 0; i < a; i++) printf("%02x", z[i]); printf("\n"); }
    }
    return 0;
}
