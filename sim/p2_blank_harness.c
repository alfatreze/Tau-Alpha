/* stdin: "S sel" selects; "T now edge hz" -> one bl_step; prints "ret blanked sec". */
#include <stdio.h>
#include "blank_core.h"
int main(void)
{
    char c; unsigned a, b, h; bl_t bl = { 0 };
    while (scanf(" %c", &c) == 1) {
        if (c == 'S') { if (scanf("%u", &a) == 1) bl.sel = a; }
        else if (c == 'T') { if (scanf("%u %u %u", &a, &b, &h) == 3) { int r = bl_step(&bl, a, h, b); printf("%d %u %u\n", r, bl.blanked, bl.sec); } }
    }
    return 0;
}
