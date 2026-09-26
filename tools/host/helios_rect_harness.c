/* Host CLI wrapper for fw/helios_rect.h, driven by sim/test_helios_rect.py. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fw/helios_rect.h"

int main(int argc, char **argv)
{
    if (argc != 10 || strcmp(argv[1], "subtract")) { fprintf(stderr, "usage: subtract rx ry rw rh hx hy hw hh\n"); return 2; }
    int32_t rx = atoi(argv[2]), ry = atoi(argv[3]), rw = atoi(argv[4]), rh = atoi(argv[5]);
    int32_t hx = atoi(argv[6]), hy = atoi(argv[7]), hw = atoi(argv[8]), hh = atoi(argv[9]);
    helios_rect_t out[4];
    int n = helios_rect_subtract(rx, ry, rw, rh, hx, hy, hw, hh, out);
    for (int i = 0; i < n; i++)
        printf("%d %d %d %d\n", out[i].x, out[i].y, out[i].w, out[i].h);
    return 0;
}
