#include <stdio.h>
#include "spectrum_core.h"
int main(void)
{
    char c; unsigned a, b;
    while (scanf(" %c %u %u", &c, &a, &b) == 3) {
        if (c == 'l') printf("%u\n", spec_log(a));
        else if (c == 'v') printf("%u\n", spec_level(a, b));
        else printf("%u\n", spec_ballistics(a, b));
    }
    return 0;
}
