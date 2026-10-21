/* Host harness for fw2/cores/governor_core.h: `usable` then fills on stdin; prints the mode after each update. */
#include <stdio.h>
#include "governor_core.h"
int main(void)
{
    unsigned cap, f; static gov_t g;
    if (scanf("%u", &cap) != 1) return 2;
    gov_init(&g, cap);
    while (scanf("%u", &f) == 1) { gov_update(&g, f); printf("%d %d %u\n", (int)g.mode, gov_decode_wanted(&g, f), gov_degrade(&g, g.mode, f)); }
    return 0;
}
