/* B-653: runs the REAL fw/gain_handover.h with logging callbacks; prints the sequence of events and the final owner state. Input: "<on 0|1> <hw 0|1> <target>" */
#include <stdio.h>
#include <stdint.h>
#include "../fw/gain_handover.h"
static void c(uint32_t v) { printf("CTRL %u\n", v); }
static void t(uint32_t v) { printf("TARGET %u\n", v); }
static void w(uint32_t ms) { printf("WAIT %u\n", ms); }
static void f(void) { printf("FLUSH\n"); }
int main(void)
{
    unsigned on, hw, target;
    while (scanf("%u %u %u", &on, &hw, &target) == 3) {
        pcm_vol_t v = { (int32_t)target, (int32_t)target, (uint8_t)hw };
        uint32_t fade_left = 777u;
        const gh_ops_t ops = { c, t, w, f };
        printf("BEGIN\n");
        gh_handover(&ops, &v, &fade_left, 2048u, 0x31u, on);
        printf("STATE hw=%u cur=%d target=%d fade=%u\n", v.hw, v.cur, v.target, fade_left);
        printf("END\n");
    }
    return 0;
}
