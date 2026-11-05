/* Firmware-in-the-loop test of the pack watchdog (docs/features/PACK_WATCHDOG_PLAN.md, W2), run by the real VexRiscv on tau_soc with WDOG_ENABLE=1 (sim/tb_wdog_soc.v). It is linked with the
 * PRODUCTION start-up and trap code (fw2/platform/start.S: mtvec, trap_entry, wd_save, wd_resume, wd_irq_on/off), so what is proven here is the code the player runs. It exercises the mechanism the way
 * the player will: arm, open the interrupt window, spin forever, take the timer interrupt, acknowledge it, and resume at the call site as if wd_save() had returned 1.
 * Every check prints "WDOG ok n" or "WDOG FAIL n"; the last line is "WDOG DONE". Mutants (Makefile test-rtl-wdog-soc) edit a copy of start.S: no acknowledge, interrupts left on, registers not restored,
 * no window flag; each must make this fail. */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE   0x80000000u
#define R_CYCLES  0x8000000Cu
#define R_WDOG_ARM 0x800001DCu
#define R_WDOG_ST  0x800001E0u
#define R_WDOG_ACK 0x800001E4u
#define R_WDOG_CNT 0x800001E8u
#define MSTATUS_MIE (1u << 3)
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "WDOG ok " : "WDOG FAIL "); num_(n); putc_('\n'); }

uint32_t *wd_jb_ptr;                       /* read by wd_resume (start.S) */
volatile uint32_t wd_cut_flag;             /* read by trap_entry (start.S): nonzero only while a guarded window is open */
extern int wd_save(uint32_t *jb);
extern void wd_irq_on(void), wd_irq_off(void);
#define MARK (*(volatile uint32_t *)0xFC00u)   /* survives a restart: below the painted stack, outside .bss */

static inline uint32_t mstatus_rd(void) { uint32_t v; __asm__ volatile("csrr %0, mstatus" : "=r"(v)); return v; }
static uint32_t jb[14];
#define OPEN(limit) do { wd_cut_flag = 1u; REG(R_WDOG_ARM) = (limit); wd_irq_on(); } while (0)
#define CLOSE() do { wd_irq_off(); REG(R_WDOG_ARM) = 0u; wd_cut_flag = 0u; } while (0)

int main(void)
{
    if (MARK == 0xB007B008u) {                              /* restarted by the "any other trap" path (test 14) */
        MARK = 0u; chk(14, 1); puts_("WDOG DONE\n"); for (;;) { }
    }
    if (MARK == 0xB007B007u) {                              /* restarted by a timer interrupt outside a guarded window (test 13) */
        chk(13, (mstatus_rd() & MSTATUS_MIE) == 0u);          /* a fresh start: interrupts off */
        REG(R_WDOG_ACK) = 1u; REG(R_WDOG_ARM) = 0u;
        MARK = 0xB007B008u;
        __asm__ volatile("ebreak");                           /* an unrelated trap must restart the firmware too */
        chk(14, 0);                                           /* not reached */
        for (;;) { }
    }
    chk(1, (REG(R_WDOG_ST) & 0x80000003u) == 0x80000000u && REG(R_WDOG_CNT) == 0u);       /* present, idle, no counts */

    /* 2: a tick that finishes inside its window: no expiry, then closed and disarmed */
    wd_jb_ptr = jb;
    if (wd_save(jb) == 0) {
        OPEN(200000u);
        for (volatile uint32_t i = 0; i < 300u; i++) { }
        CLOSE();
    }
    chk(2, (REG(R_WDOG_ST) & 3u) == 0u && (REG(R_WDOG_CNT) >> 16) == 1u && (REG(R_WDOG_CNT) & 0xFFFFu) == 0u);

    /* 3..9: the endless tick is cut off, and the caller's registers and stack are back. The s-registers are held by register variables the endless loop trashes. */
    {
        register uint32_t a __asm__("s2") = 0x1234ABCDu, b __asm__("s5") = 0xCAFEF00Du, c __asm__("s11") = 0x0BADBEEFu;
        const uint32_t t0 = REG(R_CYCLES);
        uint32_t took = 0, mst = 0, sp0, sp1;
        __asm__ volatile("mv %0, sp" : "=r"(sp0));
        if (wd_save(jb) == 0) {
            OPEN(3000u);
            __asm__ volatile("li s2,0\n li s5,0\n li s11,0\n 1: j 1b" ::: "s2", "s5", "s11");   /* the pack that never returns, and trashes the callee-saved registers on the way */
        } else {
            mst = mstatus_rd();                                                              /* read BEFORE the firmware touches mstatus: the handler alone must have closed the window */
            took = REG(R_CYCLES) - t0;
        }
        wd_irq_off(); wd_cut_flag = 0u;
        __asm__ volatile("mv %0, sp" : "=r"(sp1));
        chk(3, (REG(R_WDOG_CNT) & 0xFFFFu) == 1u);                                           /* one expiry */
        chk(4, (REG(R_WDOG_ST) & 3u) == 0u);                                                 /* acknowledged, not armed */
        chk(5, (mst & MSTATUS_MIE) == 0u);                                                   /* the window is closed again */
        chk(6, took >= 3000u && took < 3400u);                                               /* cut off after the limit, plus the trap latency */
        chk(7, a == 0x1234ABCDu && b == 0xCAFEF00Du && c == 0x0BADBEEFu);                    /* callee-saved registers restored */
        chk(8, (REG(R_WDOG_CNT) >> 16) == 2u);                                               /* two arms so far */
        chk(9, sp1 == sp0);                                                                  /* the stack pointer is back */
    }

    /* 10: a second cut-off works the same way (nothing was left half-armed) */
    if (wd_save(jb) == 0) { OPEN(1500u); for (volatile uint32_t i = 0;; i++) { } }
    wd_irq_off(); wd_cut_flag = 0u;
    chk(10, (REG(R_WDOG_ST) & 3u) == 0u && (REG(R_WDOG_CNT) & 0xFFFFu) == 2u);

    /* 11, 12: no interrupt outside the window: with machine interrupts masked the expiry only sets the flag; the code runs on, the flag is read and acknowledged */
    REG(R_WDOG_ARM) = 400u;
    for (volatile uint32_t i = 0; i < 400u; i++) { }
    chk(11, (REG(R_WDOG_ST) & 2u) == 2u && (REG(R_WDOG_CNT) & 0xFFFFu) == 3u);
    REG(R_WDOG_ACK) = 1u;
    chk(12, (REG(R_WDOG_ST) & 3u) == 0u);

    /* 13: a timer interrupt with the window flag clear is not a recovery: the firmware restarts (the code at the top of main reports it); 14 follows from there */
    MARK = 0xB007B007u; wd_cut_flag = 0u;
    REG(R_WDOG_ARM) = 500u; wd_irq_on();
    for (;;) { }
}
