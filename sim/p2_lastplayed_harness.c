/* Host harness for fw2/cores/lastplayed_core.h. Commands: "p valid kind parent id" prints the three words; "u w0 w1 w2" prints the unpacked record ("-" when refused); "f id n i0 i1 ..." prints the position. */
#include <stdio.h>
#include "lastplayed_core.h"
int main(void)
{
    char c;
    while (scanf(" %c", &c) == 1) {
        if (c == 'p') { lastp_t l; unsigned a, b, d, e; if (scanf("%u %u %u %u", &a, &b, &d, &e) != 4) return 1; l.valid = a; l.kind = b; l.parent = d; l.id = e; uint32_t w[2]; lastp_pack(&l, w); printf("%u %u\n", w[0], w[1]); }
        else if (c == 'u') { uint32_t w[2]; lastp_t l; if (scanf("%u %u", &w[0], &w[1]) != 2) return 1; if (lastp_unpack(w, &l)) printf("%u %u %u\n", l.kind, l.parent, l.id); else printf("-\n"); }
        else if (c == 'f') { unsigned id, n; uint16_t ids[64]; if (scanf("%u %u", &id, &n) != 2) return 1; for (unsigned i = 0; i < n; i++) { unsigned v; if (scanf("%u", &v) != 1) return 1; ids[i] = (uint16_t)v; } printf("%d\n", (int)lastp_find(ids, n, id)); }
    }
    return 0;
}
