/* Host check of fw2/cores/mpreset_core.h (B-966): reads a file and prints what the firmware would make of it.
 *   h idx FILE METER_ID                      -> "idx CODE COUNT" then one line per entry "id flags name"
 *   h pre FILE METER_ID ID PARAMS_FILE       -> "pre CODE HIT NAME" then the applied values on one line (PARAMS_FILE: one "key_hash min max default" per line, the meter's parameters)
 *   h find FILE ID                           -> "find POSITION" (an index file already checked) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/cores/mpreset_core.h"
static uint8_t buf[8192];
static uint32_t slurp(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return 0; uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f); fclose(f); return n; }
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    const uint32_t n = slurp(argv[2]);
    if (!strcmp(argv[1], "idx")) {
        const int e = mp_index_check(buf, n, (uint32_t)atoi(argv[3]));
        printf("idx %d %u\n", e, e ? 0u : mp_index_count(buf));
        if (!e) for (uint32_t i = 0; i < mp_index_count(buf); i++) { uint8_t id, fl; char nm[16]; mp_index_entry(buf, i, &id, &fl, nm); printf("%u %u %s\n", id, fl, nm); }
    } else if (!strcmp(argv[1], "pre") && argc >= 6) {
        const int e = mp_preset_check(buf, n, (uint32_t)atoi(argv[3]), (uint32_t)atoi(argv[4]));
        mp_param_t ps[64]; uint16_t v[64]; uint32_t np = 0;
        FILE *f = fopen(argv[5], "r"); unsigned k, lo, hi, d; while (f && np < 64 && fscanf(f, "%u %u %u %u", &k, &lo, &hi, &d) == 4) { ps[np].key = (uint16_t)k; ps[np].min = (uint16_t)lo; ps[np].max = (uint16_t)hi; ps[np].def = (uint16_t)d; np++; } if (f) fclose(f);
        char nm[16] = ""; uint32_t hit = 0;
        if (!e) { mp_preset_name(buf, nm); hit = mp_preset_apply(buf, ps, np, v); }
        printf("pre %d %u %s\n", e, hit, nm);
        for (uint32_t i = 0; i < np && !e; i++) printf("%u ", v[i]);
        printf("\n");
    } else if (!strcmp(argv[1], "find")) {
        printf("find %d\n", mp_index_find(buf, (uint32_t)atoi(argv[3])));
    }
    return 0;
}
