/* items: 0 range 0..100 step 1; 1 choice 0..2; 2 choice 0..7; 3 range 0..100 step 10; 4 choice 0..1; 5 range 5..60 step 5.  Commands: init ROWS | up | down | left | right ; prints sel top changed vals */
#include <stdio.h>
#include <string.h>
#include "menu_core.h"
static const mi_t items[6] = { {0,0,100,1}, {1,0,2,1}, {1,0,7,1}, {0,0,100,10}, {1,0,1,1}, {0,5,60,5} };
int main(void)
{
    static menu_t m; uint32_t vals[6] = { 94, 0, 2, 50, 0, 30 }; char c[16]; unsigned a;
    while (scanf("%15s", c) == 1) {
        int ch = 0;
        if (!strcmp(c, "init")) { scanf("%u", &a); menu_init(&m, 6, a); }
        else if (!strcmp(c, "up")) menu_move(&m, -1);
        else if (!strcmp(c, "down")) menu_move(&m, 1);
        else if (!strcmp(c, "left")) ch = menu_adjust(&m, items, vals, -1);
        else if (!strcmp(c, "right")) ch = menu_adjust(&m, items, vals, 1);
        printf("%u %u %d %u %u %u %u %u %u\n", m.sel, m.top, ch, vals[0], vals[1], vals[2], vals[3], vals[4], vals[5]);
    }
    return 0;
}
