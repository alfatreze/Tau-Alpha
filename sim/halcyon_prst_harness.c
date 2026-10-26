/* B-641: drives fw/halcyon_prst.h. argv[1] = the PRST section file. Prints "code count" then per entry "type name controls|stages pre coefs...". */
#include <stdio.h>
#include <stdlib.h>
#include "halcyon_prst.h"
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    static uint8_t buf[65536]; uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f); fclose(f);
    uint16_t offs[8]; uint8_t cnt = 0;
    int e = hp_check(buf, n, offs, &cnt);
    printf("%d %u\n", e, e ? 0u : (unsigned)cnt);
    if (e) return 0;
    for (uint32_t k = 0; k < cnt; k++) {
        const uint8_t *en = buf + offs[k];
        hal_ctl_t c; int32_t coef[50], pre = 0;
        uint32_t ns = hp_entry(en, &c, coef, &pre);
        printf("%u %s ", en[0], hp_name(en));
        if (!ns) printf("%d %d %d %d %d %d\n", c.warmth, c.bass, c.vocal, c.punch, c.sibilance, c.air);
        else { printf("%u %d", ns, (int)pre); for (uint32_t i = 0; i < ns * 5u; i++) printf(" %d", (int)coef[i]); printf("\n"); }
    }
    int a1, a2; /* stability probe: stdin pairs */
    while (scanf("%d %d", &a1, &a2) == 2) printf("S %d\n", hp_stable(a1, a2));
    return 0;
}
