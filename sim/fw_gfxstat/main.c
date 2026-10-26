/* Firmware-in-the-loop test of the Talos 2 T3 status registers in tau_soc.v: R_FB_GO bit 1 (idle), R_GFX_FENCE 0x1B8, R_GFX_STATUS 0x1BC, R_GFX_COUNTS 0x1C0,
 * run by the real VexRiscv. Phase 1 values are fixed by sim/tb_gfx_status_soc.v; after "READY" the bench changes them (phase 2). Every check prints "GFXST ok n" or
 * "GFXST FAIL n"; the last line is "GFXST DONE". */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE 0x80000000u
#define R_FB_GO      0x80000054u
#define R_GFX_FENCE  0x800001B8u
#define R_GFX_STATUS 0x800001BCu
#define R_GFX_COUNTS 0x800001C0u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "GFXST ok " : "GFXST FAIL "); num_(n); putc_('\n'); }
int main(void)
{
    chk(1, REG(R_FB_GO) == 2u);                                         /* idle, not full */
    chk(2, REG(R_GFX_FENCE) == 0x00C0FFEEu);
    chk(3, REG(R_GFX_STATUS) == (0x80000000u | (1u << 16) | 123u));     /* present, idle, 123 free */
    chk(4, REG(R_GFX_COUNTS) == ((7u << 16) | 45u));                    /* underflow 7, overflow 45 */
    puts_("READY\n");
    for (volatile uint32_t i = 0; i < 4000u; i++) { }                   /* the bench changes the inputs meanwhile */
    chk(5, REG(R_FB_GO) == 1u);                                         /* busy, full */
    chk(6, REG(R_GFX_FENCE) == 0x00C0FFEFu);
    chk(7, REG(R_GFX_STATUS) == (0x80000000u | 0u));                    /* present, not idle, 0 free */
    chk(8, REG(R_GFX_COUNTS) == ((8u << 16) | 46u));
    puts_("GFXST DONE\n");
    for (;;) { }
}
