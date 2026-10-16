#include <stdio.h>
#include "halcyon_sel_core.h"
int main(void)
{
    char c; unsigned a, b; int d;
    while (scanf(" %c", &c) == 1) {
        if (c == 'Y' && scanf("%u %u", &a, &b) == 2) printf("%u\n", hs_cycle(a, b));
        else if (c == 'S' && scanf("%u %u %d", &a, &b, &d) == 3) printf("%u\n", hs_step(a, b, d));
        else if (c == 'R' && scanf("%u %u", &a, &b) == 2) printf("%u\n", hs_restore(a, b));
        else if (c == 'V' && scanf("%u %u", &a, &b) == 2) printf("%u\n", hs_saved(a, b));
        else if (c == 'W' && scanf("%u %d", &a, &d) == 2) printf("%u\n", hs_row(a, d));
        else if (c == 'C' && scanf("%u %d", &a, &d) == 2) printf("%d\n", hs_ctl_clamp(a, d));
    }
    return 0;
}
