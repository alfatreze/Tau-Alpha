/* Host run of fw2/cores/marquee_core.h: a 30-character string in a box that holds 10 characters (10 px each); prints k at times. */
#include <stdio.h>
#ifndef BUG
#define BUG 0
#endif
#include "../fw2/cores/marquee_core.h"
int main(void)
{
    mq_t m; mq_reset(&m);
    const char *s = "abcdefghijklmnopqrstuvwxyz0123";   /* 30 chars */
    const uint32_t sig = mq_sig(s), len = 30u, box = 10u;
    uint32_t k = 0;
    for (uint32_t t = 0; t <= 9000u; t += 20u) {
        const int fits = (len - k) <= box;
        k = mq_update(&m, sig, t, fits);
        if (t % 100u == 0u) printf("%u %u\n", t, k);
    }
    const uint32_t s2 = mq_sig("another string that is long enough to scroll");
    k = mq_update(&m, s2, 9100u, 0);
    printf("new %u\n", k);
    return 0;
}
