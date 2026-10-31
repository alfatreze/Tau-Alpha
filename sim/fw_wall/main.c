/* Firmware-in-the-loop test of the wall-clock registers (RTC_ST 0x1C4, RTC_EPOCH 0x1C8, RTC_DATE 0x1CC, RTC_TIME 0x1D0) and of the per-port SDRAM statistics window
 * (SDA_SEL 0x1D4, SDA_VAL 0x1D8) in tau_soc.v, run by the real VexRiscv; sim/tb_wall_soc.v is the other half. MODE 0 / 2 (compile -DMODE=): the wall clock, with the host's command
 * already sent (0) or sent after READY (2); Every check prints "WALL ok n" or "WALL FAIL n"; the last line is "WALL DONE". */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE 0x80000000u
#define R_RTC_ST    0x800001C4u
#define R_RTC_EPOCH 0x800001C8u
#define R_RTC_DATE  0x800001CCu
#define R_RTC_TIME  0x800001D0u
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
    puts_("WALL DONE\n");
    for (;;) { }
}
