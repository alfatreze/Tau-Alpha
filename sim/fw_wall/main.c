/* Firmware-in-the-loop test of the wall-clock registers (RTC_ST 0x1C4, RTC_EPOCH 0x1C8, RTC_DATE 0x1CC, RTC_TIME 0x1D0) and of the per-port SDRAM statistics window
 * (SDA_SEL 0x1D4, SDA_VAL 0x1D8) in tau_soc.v, run by the real VexRiscv; sim/tb_wall_soc.v is the other half. MODE 0 / 2 (compile -DMODE=): the wall clock, with the host's command
 * already sent (0) or sent after READY (2); MODE 1: the statistics window against static stub counters. Every check prints "WALL ok n" or "WALL FAIL n"; the last line is "WALL DONE". */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE 0x80000000u
#define R_RTC_ST    0x800001C4u
#define R_RTC_EPOCH 0x800001C8u
#define R_RTC_DATE  0x800001CCu
#define R_RTC_TIME  0x800001D0u
#define R_SDA_SEL   0x800001D4u
#define R_SDA_VAL   0x800001D8u
#define R_I2S_MM    0x80000140u
#define R_I2S_CNT   0x80000144u
#define R_I2S_SUM   0x80000148u
#define R_I2S_ST    0x8000014Cu
#ifndef MODE
#define MODE 0
#endif
#define EP1 0x6724B3C5u
#define DA1 0x20261031u
#define TI1 0x06123456u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "WALL ok " : "WALL FAIL "); num_(n); putc_('\n'); }
static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }

#if MODE == 3
/* the I2S jitter statistic's 48-bit interval sum: the bench deposits 0x0000_FFFF_FFF0 into it after READY and then makes five measured intervals (1,389 clk_sys cycles each), so the sum crosses 2^32:
 * the low word wraps and the carry must show in R_I2S_ST [31:16] (a 32-bit sum has no such field and would have lost it). */
int main(void)
{
    chk(1, (REG(R_I2S_ST) & 1u) == 1u);
    puts_("READY\n");
    for (uint32_t t = 0; t < 2000000u && REG(R_I2S_CNT) < 5u; t++) { }
    const uint32_t cnt = REG(R_I2S_CNT), mm = REG(R_I2S_MM), lo = REG(R_I2S_SUM), st = REG(R_I2S_ST);
    const uint64_t sum = ((uint64_t)(st >> 16) << 32) | lo, start = 0x0000FFFFFFF0ull, delta = sum - start;
    const uint32_t mn = mm & 0xFFFFu, mx = mm >> 16;
    chk(2, cnt == 5u);
    chk(3, mn > 1000u && mx < 2000u && mn <= mx);                       /* about 1,389 */
    chk(4, delta >= (uint64_t)cnt * mn && delta <= (uint64_t)cnt * mx);  /* the sum grew by the five intervals, carry included */
    chk(5, (st >> 16) == 1u);                                            /* bits 47:32: the carry out of the low word */
    chk(6, lo < 0x10000u);                                               /* the low word wrapped */
    puts_("WALL DONE\n");
    for (;;) { }
}
#elif MODE == 1
static uint32_t snap(uint32_t sel)                   /* the documented sequence: write the selection, wait for pending to drop, read the value */
{
    REG(R_SDA_SEL) = sel;
    for (uint32_t t = 0; t < 10000u && (REG(R_SDA_SEL) & 0x100u); t++) { }
    return REG(R_SDA_VAL);
}
int main(void)
{
    uint32_t st = REG(R_SDA_SEL);
    chk(1, (st >> 31) == 1u && (st & 0x100u) == 0u && (st & 0x1Fu) == 0u);          /* present, nothing pending, selection 0 after reset */
    for (uint32_t p = 0; p < 3u; p++) {
        chk(10u + p * 10u + 0u, snap(p << 3 | 0u) == 0x11110000u + p);               /* requests */
        chk(10u + p * 10u + 1u, snap(p << 3 | 1u) == 0x22220000u + p * 7u);          /* words */
        chk(10u + p * 10u + 2u, snap(p << 3 | 2u) == 0x33330000u + p * 13u);         /* busy cycles */
        chk(10u + p * 10u + 3u, snap(p << 3 | 3u) == 0x4400u + p);                   /* worst wait (16 bits) */
        chk(10u + p * 10u + 4u, snap(p << 3 | 4u) == 0x5500u + p);                   /* urgent over the bound (16 bits) */
        chk(10u + p * 10u + 5u, snap(p << 3 | 5u) == 0x6600u + p);                   /* bad requests (16 bits) */
    }
    chk(50, snap(0u << 3 | 6u) == 0u && snap(1u << 3 | 7u) == 0u && snap(3u << 3 | 0u) == 0u && snap(3u << 3 | 5u) == 0u);   /* a selection outside the range reads 0 */
    chk(51, (REG(R_SDA_SEL) & 0x1Fu) == (3u << 3 | 5u));                                                                     /* the selection reads back */
    chk(52, REG(R_SDA_VAL) == REG(R_SDA_VAL));                                                                                /* two reads agree */
    chk(53, snap(2u << 3 | 2u) == 0x33330000u + 26u);                                                                         /* a write right behind another is handled once the first is done */
    puts_("WALL DONE\n");
    for (;;) { }
}
#else
int main(void)
{
    uint32_t st = REG(R_RTC_ST);
    chk(1, (st >> 31) == 1u);                                              /* the block is present */
    if (!(st & 1u)) {                                                      /* MODE 2: the host's command has not come yet */
        chk(2, REG(R_RTC_EPOCH) == 0u && REG(R_RTC_DATE) == 0u && REG(R_RTC_TIME) == 0u);
        puts_("READY\n");
        for (uint32_t t = 0; t < 400000u && !(REG(R_RTC_ST) & 1u); t++) { }
    } else { chk(2, 1); puts_("READY\n"); }                                 /* MODE 0: it came while the CPU was in reset */
    chk(3, (REG(R_RTC_ST) & 1u) == 1u);
    chk(4, REG(R_RTC_EPOCH) == EP1);
    chk(5, REG(R_RTC_DATE) == DA1);
    chk(6, REG(R_RTC_TIME) == TI1);
    chk(7, (REG(R_RTC_TIME) >> 24 & 15u) == 6u);                           /* the day-of-week nibble is kept */
    puts_("READY2\n");
    spin(60000u);                                                          /* the testbench sends a second, different command meanwhile */
    chk(8, REG(R_RTC_EPOCH) == EP1 && REG(R_RTC_DATE) == DA1 && REG(R_RTC_TIME) == TI1);   /* the first one stays */
    chk(9, REG(R_SDA_SEL) == 0u && REG(R_SDA_VAL) == 0u);                  /* the statistics window is not built here: it reads 0 */
    puts_("WALL DONE\n");
    for (;;) { }
}
#endif
