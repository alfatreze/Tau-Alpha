/* B-640: drives fw/halcyon_hw.h with a logging HAL_WR. stdin: "w b v p s a live" per line. stdout per line: the write log as "idx:value" pairs, ending with the control writes. */
#include <stdio.h>
#include <stdint.h>
#define R_HAL_CTRL 0x178u
#define R_HAL_IDX  0x17Cu
#define R_HAL_DATA 0x180u
#define REG(a) (mock_ctrl)
static uint32_t mock_ctrl = 0x80000000u;
#define HAL_WR(a, v) printf("%x:%x ", (unsigned)(a), (unsigned)(v))
#include "halcyon_hw.h"
int main(void)
{
    int w, b, v, p, s, a, live;
    while (scanf("%d %d %d %d %d %d %d", &w, &b, &v, &p, &s, &a, &live) == 7) {
        hal_ctl_t c = { (int8_t)w, (int8_t)b, (int8_t)v, (int8_t)p, (int8_t)s, (int8_t)a };
        if (live) hal_hw_apply_ctl_live(&c); else hal_hw_apply_ctl(&c);
        printf("\n");
    }
    printf("present=%u\n", (unsigned)hal_hw_present());
    hal_hw_off(); printf("\n");
    return 0;
}
