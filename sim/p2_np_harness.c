#include <stdio.h>
#include "nowplaying_core.h"
int main(void)
{
    char c; unsigned a, b, d; char o[32];
    while (scanf(" %c %u %u %u", &c, &a, &b, &d) == 4) {
        if (c == 't') { np_time(o, a, b); printf("%s\n", o); }
        else if (c == 'b') printf("%u\n", np_bar(a, b, d));
        else if (c == 's') { np_speed(o, a); printf("%s\n", o); }
        else { np_khz(o, a); printf("%s\n", o); }
    }
    return 0;
}
