/* B-370: implementation of fw/flac_lpc_hw.h. Textually included into player.c (not a separate
 * translation unit) so it shares REG() and the R_LPC_* register defines already declared above
 * (docs/MMIO_ALLOCATION.md 0x120-0x13C). fw/flac.c (a genuinely separate translation unit) only sees
 * the extern declarations in flac_lpc_hw.h and links against the definitions here.
 *
 * Any real hardware anomaly (a residual write whose sample never becomes ready) permanently disables
 * the unit for the rest of the session (tau_lpc_hw_enable = 0) rather than retrying mid-track -- an
 * intermittently-misbehaving unit is worse than a software fallback that is known correct, same
 * reasoning as fw/mp3_poly_hw.inc. The timeout bound is a plain iteration count, not a cycle counter:
 * the RTL's own budget is at most 32 MAC cycles plus a few (docs/research/FLAC_LPC_KERNEL_DESIGN.md
 * section 3, confirmed in simulation -- "clocks for order-31 prediction: 36"), so the loop bound below
 * is roughly two orders of magnitude more generous than that. */

#include "flac_lpc_hw.h"

int tau_lpc_hw_enable = 0;
unsigned tau_lpc_stat_samples, tau_lpc_stat_timeout;

int tau_lpc_hw_begin(uint32_t order, int32_t shift, const int32_t *coef, const int32_t *warm)
{
    if (!tau_lpc_hw_enable) return 0;
    if (order < 1u || order > 32u) return 0;   /* defensive: FIXED (order 0-4) is never routed here */
    REG(R_LPC_CFG) = (order & 0x3Fu) | (((uint32_t)shift & 0x1Fu) << 6u);
    REG(R_LPC_COEF_IDX) = 0u;
    for (uint32_t j = 0; j < order; j++) REG(R_LPC_COEF_DATA) = (uint32_t)coef[j];
    REG(R_LPC_WARM_IDX) = 0u;
    for (uint32_t j = 0; j < order; j++) REG(R_LPC_WARM_DATA) = (uint32_t)warm[order - 1u - j];
    return 1;
}

int32_t tau_lpc_hw_sample(int32_t residual, int *ok)
{
    REG(R_LPC_RESIDUAL) = (uint32_t)residual;
    for (int i = 0; i < 4000; i++) {
        if (REG(R_LPC_STATUS) & 4u) {           /* bit 2: done */
            int32_t s = (int32_t)REG(R_LPC_SAMPLE);   /* the read is itself the ack (mp3_soc.v: sample_rd == the bus's own read pulse) */
            tau_lpc_stat_samples++;
            *ok = 1;
            return s;
        }
    }
    tau_lpc_stat_timeout++;
    tau_lpc_hw_enable = 0;                      /* a residual never completed: don't trust the unit any further this session */
    *ok = 0;
    return 0;
}
