#include <stdio.h>
#include "theme_core.h"
int main(void)
{
    char c; unsigned a, b; int d;
    while (scanf(" %c", &c) == 1) {
        if (c == 'A' && scanf("%u %u", &a, &b) == 2) printf("%u\n", tc_accent((uint16_t)a, b));
        else if (c == 'P' && scanf("%u %d", &a, &d) == 2) printf("%u\n", tc_pick_step(a, d));
        else if (c == 'V' && scanf("%u %u %u", &a, &b, (unsigned *)&d) == 3) printf("%u\n", tc_valid(a, b, (uint32_t)d));
    }
    return 0;
}
