/* Host driver for fw2/aout/aout_rate.h: prints the increment for rate/speed pairs. */
#include <stdio.h>
#include <stdlib.h>
#include "aout/aout_rate.h"
int main(int argc, char **argv)
{
    for (int i = 1; i + 2 < argc; i += 3)
        printf("%u\n", aout_rate_inc(66666667u, (uint32_t)atoi(argv[i]), (uint32_t)atoi(argv[i + 1]), (uint32_t)atoi(argv[i + 2])));
    return 0;
}
