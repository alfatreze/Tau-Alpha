/* B-639 firmware-in-the-loop test of the Halcyon MMIO glue in mp3_soc.v (registers 0x178-0x180). The real VexRiscv pushes a constant stereo
 * signal (16000) into the PCM FIFO and drives the engine through its registers; sim/tb_halcyon_soc.v watches the audio outputs.
 *   boot            engine disabled, a first bank (gain 0.25) committed with nact 1: audio must be 16000 (the engine is not selected)
 *   key A (bit 4)   second bank (gain 0.5) written to the SHADOW bank, committed, engine enabled: audio must become 8000
 *   key B (bit 5)   bypass on: 16000 again
 *   key X (bit 6)   bypass off, then CLEAR pulse (state cleared, same 8000)
 *   key Y (bit 7)   engine disabled again: 16000
 *   key A again     the Cymo resampler live in front of the engine (the 18-bit path with WIDE_EQ_ENABLE) and the engine on again: about 8000
 *   key X again     a full-scale-ish square wave (+-32000, period 8) instead of the constant: the resampler overshoots it; through the engine's gain of 0.5 the peaks stay under 16-bit
 *                   full scale and, with the wide path, exceed half of the clip level (the testbench measures the peak) */
#include <stdint.h>
#define REG(a)    (*(volatile uint32_t *)(a))
#define CONSOLE   0x80000000u
#define R_AUDIO   0x80000008u
#define R_INPUT   0x8000003Cu
#define R_PCM_ST  0x80000034u
#define R_PCM_RATE 0x80000038u
#define R_CYMO_CTRL 0x80000150u
#define R_HAL_CTRL 0x80000178u
#define R_HAL_IDX  0x8000017Cu
#define R_HAL_DATA 0x80000180u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex_(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789ABCDEF"[(v >> i) & 15]); }

static void load_bank(uint32_t b0)
{
    REG(R_HAL_IDX) = 0;
    REG(R_HAL_DATA) = b0; REG(R_HAL_DATA) = 0; REG(R_HAL_DATA) = 0; REG(R_HAL_DATA) = 0; REG(R_HAL_DATA) = 0;   /* stage 0: b0 b1 b2 a1 a2 */
    REG(R_HAL_IDX) = 85;
    REG(R_HAL_DATA) = 1u << 22;                                                                                  /* preamp 1.0 */
}
#define CTRL(en, byp, commit, clr, nact) (((en) << 0) | ((byp) << 1) | ((commit) << 2) | ((clr) << 3) | ((nact) << 8))

int main(void)
{
    uint32_t keys_old = 0, step = 0, n = 0, square = 0;
    puts_("HAL BOOT\n");
    puts_("HAL READ "); hex_(REG(R_HAL_CTRL)); putc_('\n');                 /* bit 31 = present */
    REG(R_PCM_RATE) = 3435974u;
    load_bank(1u << 20);                                                    /* 0.25 */
    REG(R_HAL_CTRL) = CTRL(0u, 0u, 1u, 0u, 1u);                             /* commit, nact 1, engine not selected */
    for (;;) {
        if (!((REG(R_PCM_ST) >> 17) & 1u)) {
            const uint32_t v = square ? (((n++ & 4u) ? 0x8300u : 32000u) & 0xFFFFu) : 16000u;     /* 0x8300 = -32000 */
            REG(R_AUDIO) = (v << 16) | v;
        }
        uint32_t k = REG(R_INPUT), e = k & ~keys_old; keys_old = k;
        if ((e & 0x10u) && step == 0) { load_bank(1u << 21); REG(R_HAL_CTRL) = CTRL(1u, 0u, 1u, 0u, 1u); step = 1; puts_("HAL ON\n"); }
        else if ((e & 0x20u) && step == 1) { REG(R_HAL_CTRL) = CTRL(1u, 1u, 0u, 0u, 1u); step = 2; puts_("HAL BYPASS\n"); }
        else if ((e & 0x40u) && step == 2) { REG(R_HAL_CTRL) = CTRL(1u, 0u, 0u, 1u, 1u); step = 3; puts_("HAL CLEAR\n"); }
        else if ((e & 0x80u) && step == 3) { REG(R_HAL_CTRL) = CTRL(0u, 0u, 0u, 0u, 1u); step = 4; puts_("HAL OFF\n"); }
        else if ((e & 0x10u) && step == 4) { REG(R_CYMO_CTRL) = 4u; REG(R_HAL_CTRL) = CTRL(1u, 0u, 0u, 0u, 1u); step = 5; puts_("HAL CYMO\n"); }
        else if ((e & 0x40u) && step == 5) { REG(R_PCM_ST) = 0u; square = 1; step = 6; puts_("HAL SQUARE\n"); }
    }
}
