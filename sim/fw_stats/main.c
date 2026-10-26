/* Firmware-in-the-loop test of the audio statistics registers in mp3_soc.v (STATS_CTL 0x1AC, STATS_IDX 0x1B0, STATS_DATA 0x1B4), run by the real VexRiscv; sim/tb_stats_soc.v forces eight
 * known samples into the block's tap after it sees READY. Every check prints "STATS ok n" or "STATS FAIL n"; the last line is "STATS DONE". */
#include <stdint.h>
#include "samples.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE 0x80000000u
#define R_STATS_CTL  0x800001ACu
#define R_STATS_IDX  0x800001B0u
#define R_STATS_DATA 0x800001B4u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "STATS ok " : "STATS FAIL "); num_(n); putc_('\n'); }
static uint32_t rd(uint32_t i) { REG(R_STATS_IDX) = i; return REG(R_STATS_DATA); }
int main(void)
{
    chk(1, (rd(0) >> 16) == 0x5354u && (rd(0) & 3u) == 1u);          /* 'ST' signature, present, no overflow */
    REG(R_STATS_CTL) = 1u; chk(2, rd(1) == 0u && rd(2) == 0u);        /* an empty first window */
    chk(3, ((rd(0) >> 8) & 0xFFu) == 1u);                              /* one latch so far */
    puts_("READY\n");
    for (volatile uint32_t i = 0; i < 30000u; i++) { }                 /* the testbench injects its eight samples meanwhile */
    REG(R_STATS_CTL) = 1u;
    chk(4, rd(1) == EXP_N);
    const uint64_t ll = ((uint64_t)(rd(3) & 0xFFFFu) << 32) | rd(2), rr = ((uint64_t)(rd(5) & 0xFFFFu) << 32) | rd(4);
    const uint32_t lo = rd(6), hi = rd(7);
    const int64_t lr = (int64_t)(((uint64_t)hi << 32) | lo);          /* hi arrives sign-extended to 32 bits: a signed 48-bit value */
    chk(5, ll == (uint64_t)EXP_LL);
    chk(6, rr == (uint64_t)EXP_RR);
    chk(7, lr == (int64_t)EXP_LR);
    chk(8, hi == 0xFFFFFFFFu || hi == 0u);                              /* sign extension is whole */
    REG(R_STATS_CTL) = 1u; chk(9, rd(1) == 0u);                        /* the next window is empty again */
    REG(R_STATS_CTL) = 2u; chk(10, ((rd(0) >> 8) & 0xFFu) == 0u);      /* clear resets the sequence */
    puts_("STATS DONE\n");
    for (;;) { }
}
