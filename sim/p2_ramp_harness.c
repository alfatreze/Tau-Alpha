/* Host check of fw2/cores/ramp_core.h: reads "a b t n" lines (decimal) from stdin and prints the ramp colour of each. */
#include <stdio.h>
#include "../fw2/cores/ramp_core.h"
int main(void)
{
    unsigned a, b, t, n;
    while (scanf("%u %u %u %u", &a, &b, &t, &n) == 4) printf("%u\n", (unsigned)rc_ramp((uint16_t)a, (uint16_t)b, t, n));
    return 0;
}
