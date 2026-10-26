#include <stdio.h>
#include "monoclock_core.h"
int main(void)
{
    unsigned hz, start, step, n; static mc_t m;
    if (scanf("%u %u %u %u", &hz, &start, &step, &n) != 4) return 2;
    uint32_t now = start;
    for (unsigned i = 0; i < n; i++) { now += step; printf("%u\n", mc_update(&m, now, hz)); }
    return 0;
}
