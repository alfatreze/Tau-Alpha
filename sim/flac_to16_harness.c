/* B-619: drives fw/flac.c's to16() (built with -DFLAC_TEST_EXPOSE). Prints "bps v out" for the values on stdin. */
#include <stdint.h>
#include <stdio.h>
#include "flac.h"
int16_t flac_test_to16(int32_t v, uint32_t bps);
int main(void)
{
    long bps, v;
    while (scanf("%ld %ld", &bps, &v) == 2) printf("%d\n", (int)flac_test_to16((int32_t)v, (uint32_t)bps));
    return 0;
}
