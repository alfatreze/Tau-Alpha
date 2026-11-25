/* The output tick increment as a pure function (host-testable): R_PCM_RATE = tick_hz * 2^32 / CLK_HZ, where tick_hz is the SOURCE rate times the varispeed ratio.
 * The I2S side is fixed at 48 kHz and takes the newest sample at each LRCK, so a tick at the source rate is hold-resampled to 48 kHz at the correct speed. */
#ifndef P2_AOUT_RATE_H
#define P2_AOUT_RATE_H
#include <stdint.h>
#define AOUT_RATE_MIN 8000u
#define AOUT_RATE_MAX 48000u
static inline uint32_t aout_rate_inc(uint32_t clk_hz, uint32_t rate_hz, uint32_t spd_num, uint32_t spd_den)
{
    uint32_t hz = rate_hz < AOUT_RATE_MIN ? AOUT_RATE_MIN : rate_hz > AOUT_RATE_MAX ? AOUT_RATE_MAX : rate_hz;
    uint64_t inc = ((uint64_t)hz << 32) / clk_hz;
    if (spd_num != spd_den) inc = inc * spd_num / spd_den;                         /* varispeed: the output tick runs faster or slower (the pitch follows) */
    return (uint32_t)inc;
}
#endif
