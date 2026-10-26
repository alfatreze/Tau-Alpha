/* stdin lines: mode flags tg ag tp ap boost volume -> prints factor cap target */
#include <stdio.h>
#include "gain_core.h"
int main(void)
{
    unsigned mode, flags, tp, ap, boost, vol; int tg, ag;
    while (scanf("%u %u %d %d %u %u %u %u", &mode, &flags, &tg, &ag, &tp, &ap, &boost, &vol) == 8) {
        gain_trk_t g = gain_track(mode, flags, tg, ag, tp, ap, boost);
        printf("%u %u %u\n", g.factor, g.cap, gain_target(vol, &g));
    }
    return 0;
}
