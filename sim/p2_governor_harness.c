/* Host harness for fw2/cores/governor_core.h: `usable` then fills on stdin; prints the mode after each update. */
#include <stdio.h>
#include "governor_core.h"
int main(void)
{
    unsigned cap, f; static gov_t g;
    if (scanf("%u", &cap) != 1) return 2;
    gov_init(&g, cap);
    while (scanf("%u", &f) == 1) {
        uint32_t mode, deg;
        if (f == 0xFFFFFFFFu) { mode = 7u; deg = 7u; gov_publish(&g, 0, 0u, &mode, &deg); printf("%u 0 %u\n", mode, deg); continue; }      /* idle: nothing decoding */
        gov_publish(&g, 1, f, &mode, &deg); printf("%u %d %u\n", mode, gov_decode_wanted(&g, f), deg);
    }
    return 0;
}
