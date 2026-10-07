/* B-644: drives fw/halcyon_curve.h. stdin: "w b v p s a" per line. stdout per line: the attenuation in eighths and the 40 curve values in 1/64 dB. */
#include <stdio.h>
#include "halcyon_curve.h"
int main(void)
{
    int w, b, v, p, s, a;
    while (scanf("%d %d %d %d %d %d", &w, &b, &v, &p, &s, &a) == 6) {
        hal_ctl_t c = { (int8_t)w, (int8_t)b, (int8_t)v, (int8_t)p, (int8_t)s, (int8_t)a };
        int16_t o[HAL_NGRID];
        int32_t n = hal_curve(&c, o);
        printf("%d", (int)n);
        for (int k = 0; k < HAL_NGRID; k++) printf(" %d", (int)o[k]);
        printf("\n");
    }
    return 0;
}
