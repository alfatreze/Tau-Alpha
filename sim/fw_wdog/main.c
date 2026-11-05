/* Firmware-in-the-loop test of the pack watchdog (docs/features/PACK_WATCHDOG_PLAN.md, W2), run by the real VexRiscv on tau_soc with WDOG_ENABLE=1 (sim/tb_wdog_soc.v).
 * It exercises the exact mechanism the player will use: arm the watchdog, enable machine interrupts only inside the window, spin forever, take the timer interrupt in
 * trap_entry, acknowledge it, and resume at the call site as if wd_save() had returned 1 (a minimal setjmp). Every check prints "WDOG ok n" or "WDOG FAIL n"; the last line is "WDOG DONE".
 * Build-time mutants (-DMUT_...) must each make it fail: MUT_NOACK (the handler does not acknowledge), MUT_MIE_ON (interrupts stay enabled after the recovery),
 * MUT_NO_RESTORE (the callee-saved registers are not restored). */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define CONSOLE   0x80000000u
#define R_CYCLES  0x8000000Cu
#define R_WDOG_ARM 0x800001DCu
#define R_WDOG_ST  0x800001E0u
#define R_WDOG_ACK 0x800001E4u
#define R_WDOG_CNT 0x800001E8u
#define MSTATUS_MIE (1u << 3)
#define MIE_MTIE    (1u << 7)
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void num_(uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) putc_(t[--n]); }
static void chk(uint32_t n, int ok) { puts_(ok ? "WDOG ok " : "WDOG FAIL "); num_(n); putc_('\n'); }

volatile uint32_t trap_count, trap_mcause, trap_mepc;
uint32_t *wd_jb_ptr;

/* wd_save(jb): saves ra, sp, s0..s11 into jb[0..13], returns 0. wd_resume (entered by mret from the handler): restores them from *wd_jb_ptr and returns 1 to wd_save's caller. */
__asm__(
    ".text\n.globl wd_save\nwd_save:\n"
    "  sw ra,0(a0)\n  sw sp,4(a0)\n  sw s0,8(a0)\n  sw s1,12(a0)\n  sw s2,16(a0)\n  sw s3,20(a0)\n  sw s4,24(a0)\n  sw s5,28(a0)\n"
    "  sw s6,32(a0)\n  sw s7,36(a0)\n  sw s8,40(a0)\n  sw s9,44(a0)\n  sw s10,48(a0)\n  sw s11,52(a0)\n  li a0,0\n  ret\n"
    ".globl wd_resume\nwd_resume:\n"
    "  la t0, wd_jb_ptr\n  lw t0,0(t0)\n  lw ra,0(t0)\n  lw sp,4(t0)\n"
#ifndef MUT_NO_RESTORE
    "  lw s0,8(t0)\n  lw s1,12(t0)\n  lw s2,16(t0)\n  lw s3,20(t0)\n  lw s4,24(t0)\n  lw s5,28(t0)\n  lw s6,32(t0)\n  lw s7,36(t0)\n"
    "  lw s8,40(t0)\n  lw s9,44(t0)\n  lw s10,48(t0)\n  lw s11,52(t0)\n"
#endif
    "  li a0,1\n  ret\n"
    ".globl trap_entry\n.balign 4\ntrap_entry:\n"
    "  csrr t0, mcause\n  li t1, 0x80000007\n  bne t0, t1, trap_fatal\n"
    "  la t3, trap_mcause\n  sw t0,0(t3)\n  csrr t2, mepc\n  la t3, trap_mepc\n  sw t2,0(t3)\n"
    "  la t3, trap_count\n  lw t4,0(t3)\n  addi t4,t4,1\n  sw t4,0(t3)\n"
#ifndef MUT_NOACK
    "  li t3, 0x800001E4\n  sw zero,0(t3)\n"
#endif
#ifndef MUT_MIE_ON
    "  li t3, 0x80\n  csrc mstatus, t3\n"        /* MPIE := 0: after mret MIE is 0, the window is closed */
#endif
    "  la t3, wd_resume\n  csrw mepc, t3\n  mret\n"
    "trap_fatal:\n  j trap_fatal\n"
);
extern int wd_save(uint32_t *jb);
static uint32_t jb[14];

static inline void irq_on(void)  { __asm__ volatile("csrs mie, %0\n csrs mstatus, %1" :: "r"(MIE_MTIE), "r"(MSTATUS_MIE)); }
static inline void irq_off(void) { __asm__ volatile("csrc mstatus, %0" :: "r"(MSTATUS_MIE)); }
static inline uint32_t mstatus_rd(void) { uint32_t v; __asm__ volatile("csrr %0, mstatus" : "=r"(v)); return v; }

int main(void)
{
    chk(1, (REG(R_WDOG_ST) & 0x80000003u) == 0x80000000u && REG(R_WDOG_CNT) == 0u);       /* present, idle, no counts */

    /* 2: a tick that finishes inside its window: no trap, no expiry, then disarmed */
    wd_jb_ptr = jb;
    if (wd_save(jb) == 0) {
        REG(R_WDOG_ARM) = 200000u; irq_on();
        for (volatile uint32_t i = 0; i < 300u; i++) { }
        irq_off(); REG(R_WDOG_ARM) = 0u;
    }
    chk(2, trap_count == 0 && (REG(R_WDOG_ST) & 3u) == 0u && (REG(R_WDOG_CNT) >> 16) == 1u && (REG(R_WDOG_CNT) & 0xFFFFu) == 0u);

    /* 3: the endless tick is cut off, and the caller's registers and stack are back. s-registers are held by volatile locals the compiler keeps live across the call. */
    {
        register uint32_t a __asm__("s2") = 0x1234ABCDu, b __asm__("s5") = 0xCAFEF00Du, c __asm__("s11") = 0x0BADBEEFu;
        const uint32_t t0 = REG(R_CYCLES);
        uint32_t took = 0, mst = 0;
        if (wd_save(jb) == 0) {
            REG(R_WDOG_ARM) = 3000u; irq_on();
            __asm__ volatile("li s2,0\n li s5,0\n li s11,0\n 1: j 1b" ::: "s2", "s5", "s11");   /* the pack that never returns, and trashes the callee-saved registers on the way */
        } else {
            mst = mstatus_rd();                                                              /* read BEFORE the firmware touches mstatus: the handler alone must have closed the window */
            took = REG(R_CYCLES) - t0;
        }
        irq_off();
        chk(3, trap_count == 1 && trap_mcause == 0x80000007u);
        chk(4, (REG(R_WDOG_ST) & 3u) == 0u);                                                /* acknowledged, not armed */
        chk(5, (mst & MSTATUS_MIE) == 0u);                                         /* the window is closed again */
        chk(6, took >= 3000u && took < 3400u);                                              /* cut off after the limit, plus the trap latency */
        chk(7, a == 0x1234ABCDu && b == 0xCAFEF00Du && c == 0x0BADBEEFu);                  /* callee-saved registers restored */
        chk(8, (REG(R_WDOG_CNT) & 0xFFFFu) == 1u && (REG(R_WDOG_CNT) >> 16) == 2u);         /* one expiry, two arms so far */
        chk(9, trap_mepc >= 0x0u && trap_mepc < 0x10000u);                                  /* the interrupted pc was inside the image */
    }

    /* 10: a second cut-off works the same way (nothing was left half-armed) */
    if (wd_save(jb) == 0) {
        REG(R_WDOG_ARM) = 1500u; irq_on();
        for (volatile uint32_t i = 0;; i++) { }
    }
    irq_off();
    chk(10, trap_count == 2 && (REG(R_WDOG_ST) & 3u) == 0u && (REG(R_WDOG_CNT) & 0xFFFFu) == 2u);

    /* 11: no interrupt outside the window: with machine interrupts masked the expiry only sets the flag; the code runs on, the flag is read and acknowledged */
    REG(R_WDOG_ARM) = 400u;
    for (volatile uint32_t i = 0; i < 400u; i++) { }
    chk(11, trap_count == 2 && (REG(R_WDOG_ST) & 2u) == 2u);
    REG(R_WDOG_ACK) = 1u;
    chk(12, (REG(R_WDOG_ST) & 3u) == 0u);

    puts_("WDOG DONE\n");
    for (;;) { }
}
