/* stdin: "O value" opens the picker; "K keys" sends keys (prints effect, value, channel). */
#include <stdio.h>
#include "colour_core.h"
int main(void)
{
    char c; unsigned a; cp_t p = { 0, 0, 0 };
    while (scanf(" %c %u", &c, &a) == 2) {
        if (c == 'O') cp_open(&p, (uint16_t)a);
        else { int e = cp_input(&p, a); printf("%d %u %u\n", e, p.v, p.ch); }
    }
    return 0;
}
