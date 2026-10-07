/* B-653 firmware-in-the-loop for sim/tb_audio_pair.v: the real VexRiscv streams a stereo signal whose left and right values are different and change on EVERY sample into the PCM FIFO,
 * and walks the audio path through its configurations on key presses, so the testbench can check, in each one, that the two channels always change in the same clock and that the real
 * sound_i2s writer only ever serialises a pair the SoC presented.
 *   boot          plain FIFO output (gain stage off, Cymo off, engine off)
 *   key A (bit 4) hardware gain stage on, unity
 *   key B (bit 5) Halcyon engine on (one unity stage), gain stage still on
 *   key X (bit 6) Cymo resampler live (the 44.1 kHz path), engine and gain stage on
 *   key Y (bit 7) engine off, resampler live, gain stage on
 *   key A         gain stage off, resampler live, engine off */
#include <stdint.h>
#define REG(a)    (*(volatile uint32_t *)(a))
#define CONSOLE   0x80000000u
#define R_AUDIO   0x80000008u
#define R_INPUT   0x8000003Cu
#define R_PCM_ST  0x80000034u
#define R_PCM_RATE 0x80000038u
#define R_GAIN_CTRL 0x80000168u
#define R_GAIN_TARGET 0x8000016Cu
#define R_CYMO_CTRL 0x80000150u
#define R_HAL_CTRL 0x80000178u
#define R_HAL_IDX  0x8000017Cu
#define R_HAL_DATA 0x80000180u
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void load_unity(void)
{
    REG(R_HAL_IDX) = 0;
    REG(R_HAL_DATA) = 1u << 22; REG(R_HAL_DATA) = 0; REG(R_HAL_DATA) = 0; REG(R_HAL_DATA) = 0; REG(R_HAL_DATA) = 0;   /* stage 0: b0 = 1.0 */
    REG(R_HAL_IDX) = 85;
    REG(R_HAL_DATA) = 1u << 22;                                                                                  /* preamp 1.0 */
}
#define CTRL(en, commit, nact) (((en) << 0) | ((commit) << 2) | ((nact) << 8))
int main(void)
{
    uint32_t keys_old = 0, step = 0, n = 0;
    puts_("AP BOOT\n");
    REG(R_PCM_RATE) = 3435974u;                                              /* 48 kHz */
    load_unity();
    REG(R_HAL_CTRL) = CTRL(0u, 1u, 1u);                                      /* bank committed, engine not selected */
    for (;;) {
        if (!((REG(R_PCM_ST) >> 17) & 1u)) {
            const int32_t l = (int32_t)((n * 1237u) % 20000u) - 10000, r = (int32_t)((n * 911u + 3000u) % 16000u) - 8000;   /* different, and each one changes every sample */
            n++;
            REG(R_AUDIO) = ((uint32_t)(r & 0xFFFF) << 16) | (uint32_t)(l & 0xFFFF);
        }
        uint32_t k = REG(R_INPUT), e = k & ~keys_old; keys_old = k;
        if ((e & 0x10u) && step == 0)      { REG(R_GAIN_TARGET) = 32768u; REG(R_GAIN_CTRL) = 0x33u; step = 1; puts_("AP GAIN\n"); }
        else if ((e & 0x20u) && step == 1) { REG(R_HAL_CTRL) = CTRL(1u, 0u, 1u); step = 2; puts_("AP ENGINE\n"); }
        else if ((e & 0x40u) && step == 2) { REG(R_CYMO_CTRL) = 4u; step = 3; puts_("AP CYMO\n"); }
        else if ((e & 0x80u) && step == 3) { REG(R_HAL_CTRL) = CTRL(0u, 0u, 1u); step = 4; puts_("AP ENGINE OFF\n"); }
        else if ((e & 0x10u) && step == 4) { REG(R_GAIN_CTRL) = 0u; step = 5; puts_("AP GAIN OFF\n"); }
    }
}
