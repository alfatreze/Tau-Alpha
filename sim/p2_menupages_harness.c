/* Host harness for fw2/cores/menupages_core.h. Input: key letters (U D L R A B), one per token; "v i n" sets value i to n. After each key prints: depth page sel top and the effect. */
#include <stdio.h>
#include "menupages_core.h"
int main(void)
{
    static uint32_t vals[V_COUNT]; mp_t m; mp_open(&m); char tok[16];
    printf("open depth=%u page=%u sel=%u\n", m.depth, m.page[m.depth], m.sel[m.depth]);
    while (scanf("%15s", tok) == 1) {
        if (tok[0] == 'v') { unsigned i, n; if (scanf("%u %u", &i, &n) == 2) vals[i] = n; continue; }
        uint32_t k = tok[0] == 'U' ? 1u << 0 : tok[0] == 'D' ? 1u << 1 : tok[0] == 'L' ? 1u << 2 : tok[0] == 'R' ? 1u << 3 : tok[0] == 'A' ? 1u << 4 : 1u << 5;
        mp_effect_t e = mp_input(&m, k, vals);
        printf("%s depth=%u page=%u sel=%u top=%u eff=%u,%u,%u\n", tok, m.depth, m.page[m.depth], m.sel[m.depth], m.top[m.depth], e.kind, e.id, e.value);
        if (e.kind == EF_SET) vals[e.id] = e.value;
    }
    return 0;
}
