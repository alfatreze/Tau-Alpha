/* B-631: drives fw/halcyon_core.h. stdin: "w b v p s a" per line. stdout: six steps, 30 coefficients, attenuation in eighths, the preamp. */
#include <stdio.h>
#include "halcyon_core.h"
int main(void)
{
    int w, b, v, p, s, a;
    while (scanf("%d %d %d %d %d %d", &w, &b, &v, &p, &s, &a) == 6) {
        hal_ctl_t c = { (int8_t)w, (int8_t)b, (int8_t)v, (int8_t)p, (int8_t)s, (int8_t)a };
        uint8_t st[HAL_NSTAGE];
        hal_steps(&c, st);
        for (int i = 0; i < HAL_NSTAGE; i++) printf("%d ", st[i]);
        for (int i = 0; i < HAL_NSTAGE; i++) for (int k = 0; k < 5; k++) printf("%d ", (int)hal_coef[i][st[i]][k]);
        int n = hal_atten_eighths(st);
        printf("%d %d\n", n, (int)hal_preamp_q22(n));
    }
    return 0;
}
