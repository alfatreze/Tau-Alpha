/* Native harness for fw/rc_lut.h (sim/test_rc_lut.py). Not part of any firmware build.
 *   cuts r                       prints rc_lut_cut(r, dy) for dy=0..15, one per line
 *   loads r1 r2 r3 ...           runs rc_lut_prepare() across a sequence of radii starting from an
 *                                 unset cache, prints one line per call: "<r> <loaded 0|1>" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fw/rc_lut.h"

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "cuts")) {
        uint32_t r = (uint32_t)atoi(argv[2]);
        for (uint32_t dy = 0u; dy < RC_LUT_N; dy++) printf("%u\n", rc_lut_cut(r, dy));
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "loads")) {
        uint8_t cache = RC_LUT_RADIUS_NONE;
        uint8_t cuts[RC_LUT_N];
        for (int i = 2; i < argc; i++) {
            uint32_t r = (uint32_t)atoi(argv[i]);
            int loaded = rc_lut_prepare(r, &cache, cuts);
            printf("%u %d\n", r, loaded);
        }
        return 0;
    }
    fprintf(stderr, "usage: cuts r | loads r1 r2 ...\n");
    return 2;
}
