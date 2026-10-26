/* Host harness for fw2/cores/controls_core.h. Input: key letters (U D L B) one per token, "v n" prints the volume icon for volume n. After each key prints: sel top back. */
#include <stdio.h>
#include "controls_core.h"
int main(void)
{
    ctl_t c; ctl_open(&c); char tok[16];
    printf("open sel=%u top=%u n=%u vis=%u\n", c.sel, c.top, CTL_N, CTL_VIS);
    while (scanf("%15s", tok) == 1) {
        if (tok[0] == 'v') { unsigned n; if (scanf("%u", &n) == 1) printf("vol %u icon=%u\n", n, ctl_vol_icon(n)); continue; }
        uint32_t k = tok[0] == 'U' ? 1u << 0 : tok[0] == 'D' ? 1u << 1 : tok[0] == 'L' ? 1u << 2 : 1u << 5;
        int back = ctl_input(&c, k);
        printf("%s sel=%u top=%u back=%d\n", tok, c.sel, c.top, back);
    }
    printf("icons:");
    for (unsigned i = 0; i < CTL_N; i++) for (int s = 0; s < 3; s++) { unsigned id = ctl_rows[i].ic[s]; if (id != CI_NONE && id != CI_VOLUME && id >= ICON_COUNT) printf(" BAD%u", id); }
    printf(" ok\n");
    return 0;
}
