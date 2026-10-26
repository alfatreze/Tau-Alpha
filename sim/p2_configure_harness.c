/* stdin: "P np" then np lines "type hidden min max step nval when_idx when_val" define the parameters; "V v0 v1 .." sets the values; "K keys" sends keys; "D" dumps rows: prints "rows n sel top | ids... | values..." */
#include <stdio.h>
#include <string.h>
#include "configure_core.h"
static cf_pd_t pd[32]; static uint16_t mv[32]; static uint32_t np; static cf_t cf;
int main(void)
{
    char c; unsigned a, b, d, e, f, g, h, i;
    while (scanf(" %c", &c) == 1) {
        if (c == 'P') { scanf("%u", &np); for (uint32_t k = 0; k < np; k++) { scanf("%u %u %u %u %u %u %u %u", &a, &b, &d, &e, &f, &g, &h, &i); pd[k] = (cf_pd_t){ "X", (uint8_t)a, (uint8_t)b, (uint16_t)d, (uint16_t)e, (uint16_t)f, (uint8_t)g, 0, (uint8_t)h, (uint16_t)i }; } memset(&cf, 0, sizeof cf); }
        else if (c == 'V') { for (uint32_t k = 0; k < np; k++) { scanf("%u", &a); mv[k] = (uint16_t)a; } cf_build(&cf, pd, np, mv); }
        else if (c == 'K') { scanf("%u", &a); cf_effect_t ef = cf_input(&cf, pd, np, mv, a); printf("eff %u %d\n", ef.kind, ef.dir); }
        else if (c == 'D') { printf("rows %u sel %u top %u |", cf.n, cf.sel, cf.top); for (uint32_t k = 0; k < cf.n; k++) printf(" %u", cf.rows[k]); printf(" |"); for (uint32_t k = 0; k < np; k++) printf(" %u", mv[k]); printf("\n"); }
    }
    return 0;
}
