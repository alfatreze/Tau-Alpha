/* fw/halcyon_curve.h -- the response curve of a control setting for the Halcyon page (B-644). Portable, no MMIO: the curve is the SUM of the six selected table rows
 * (cascade magnitudes add in dB exactly), the infrasonic stage, and the peak-safe preamp's attenuation, on the table's 40-point log grid (20 Hz to 20 kHz) in 1/64 dB.
 * No trigonometry on the CPU. Host-tested against the model's exact response (sim/test_halcyon_curve.py). */
#ifndef TAU_HALCYON_CURVE_H
#define TAU_HALCYON_CURVE_H
#include "halcyon_core.h"

/* out[HAL_NGRID] in 1/64 dB for the given controls; returns the attenuation given back in 1/8 dB (what the preamp does). */
HAL_FN int32_t hal_curve(const hal_ctl_t *c, int16_t out[HAL_NGRID])
{
    uint8_t step[HAL_NSTAGE];
    hal_steps(c, step);
    const int32_t n = hal_atten_eighths(step);
    for (uint32_t k = 0; k < HAL_NGRID; k++) {
        int32_t sum = hal_infra_mag[k] - n * 8;
        for (uint32_t s = 0; s < HAL_NSTAGE; s++) sum += hal_mag[s][step[s]][k];
        out[k] = (int16_t)sum;
    }
    return n;
}
#endif
