/* Native test harness for fw/vu_master_core.h (the MASTER VU meter's portable logic). Prints results for
 * sim/test_vu_master.py to check against an independent Python reference -- same pattern as
 * tools/host/chladni_harness.c for fw/chladni_core.h.
 *
 * Usage:
 *   segments <peak>              -- vum_peak_to_segments(peak, vu_db_to_segment, VU_SEGMENT_TABLE_N)
 *   coalesce <lit>                -- vum_coalesce(lit, ...): prints "n z0 s0 c0 z1 s1 c1 z2 s2 c2" (unused slots 0 0 0)
 *   zone <seg>                    -- vum_zone_of(seg)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fw/vu_master_core.h"
#include "../../fw/vu_segment_table.h"

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "segments") && argc == 3) {
        uint32_t peak = (uint32_t)strtoul(argv[2], 0, 10);
        printf("%u\n", vum_peak_to_segments(peak, vu_db_to_segment, VU_SEGMENT_TABLE_N));
        return 0;
    }
    if (!strcmp(argv[1], "coalesce") && argc == 3) {
        uint32_t lit = (uint32_t)strtoul(argv[2], 0, 10);
        uint32_t zone[3] = {0,0,0}, start[3] = {0,0,0}, count[3] = {0,0,0};
        uint32_t n = vum_coalesce(lit, zone, start, count);
        printf("%u %u %u %u %u %u %u %u %u %u\n", n,
               zone[0], start[0], count[0], zone[1], start[1], count[1], zone[2], start[2], count[2]);
        return 0;
    }
    if (!strcmp(argv[1], "zone") && argc == 3) {
        uint32_t seg = (uint32_t)strtoul(argv[2], 0, 10);
        printf("%u\n", vum_zone_of(seg));
        return 0;
    }
    return 2;
}
