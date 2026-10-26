/* Firmware-in-the-loop test of the font memory registers in tau_soc.v (FONT_CTRL 0x1A0, FONT_IDX 0x1A4, FONT_DATA 0x1A8), run by the real VexRiscv; sim/tb_font_soc.v watches
 * the write port (fb_font_we / waddr / wdata) and the console. Every check prints "FONT ok n" or "FONT FAIL n"; the last line is "FONT DONE". */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE 0x80000000u
#define R_FONT_CTRL 0x800001A0u
#define R_FONT_IDX  0x800001A4u
#define R_FONT_DATA 0x800001A8u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "FONT ok " : "FONT FAIL "); num_(n); putc_('\n'); }
int main(void)
{
    chk(1, REG(R_FONT_CTRL) == 0x800017E0u);          /* present, no load completed, 6112 words */
    chk(2, REG(R_FONT_IDX) == 0u);
    REG(R_FONT_IDX) = 6110u;
    chk(3, REG(R_FONT_IDX) == 6110u);
    REG(R_FONT_DATA) = 0xAAAA0001u;                    /* word 6110 */
    chk(4, REG(R_FONT_IDX) == 6111u);                  /* the index steps on after every data write */
    REG(R_FONT_DATA) = 0xBBBB0002u;                    /* word 6111 = the last: the index wraps to 0 and a load is counted */
    chk(5, REG(R_FONT_IDX) == 0u);
    chk(6, REG(R_FONT_CTRL) == 0x800117E0u);           /* one load completed */
    REG(R_FONT_IDX) = 7000u;                           /* past the end: back to 0, never an address beyond the memory */
    chk(7, REG(R_FONT_IDX) == 0u);
    REG(R_FONT_DATA) = 0x12345678u;                    /* word 0 */
    chk(8, REG(R_FONT_IDX) == 1u);
    REG(R_FONT_IDX) = 100u;
    REG(R_FONT_CTRL) = 1u;                             /* bit 0: restart the write index */
    chk(9, REG(R_FONT_IDX) == 0u);
    chk(10, (REG(R_FONT_CTRL) & 0xFFFFu) == 6112u);
    puts_("FONT DONE\n");
    for (;;) { }
}
