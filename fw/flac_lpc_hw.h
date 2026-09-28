/* B-370: firmware-side glue for the FLAC LPC reconstruction unit (tau_flac_lpc.sv, TAU_LPC,
 * docs/research/FLAC_LPC_KERNEL_DESIGN.md). Kept out of fw/flac.c on purpose -- flac.c only calls this
 * thin, host-testable API (no MMIO register addresses in flac.c itself), same separation
 * fw/mp3_poly_hw.h uses for subband.c's own redirect. */
#ifndef FLAC_LPC_HW_H
#define FLAC_LPC_HW_H

#include <stdint.h>

/* Set by player.c's boot probe (R_LPC_STATUS bit 0) right after reading it; 0 on any bitstream without
 * the unit, or once tau_lpc_hw_sample() sees a real timeout (permanent fallback to software for the
 * rest of the session -- a hardware anomaly is not worth retrying mid-track, same convention as
 * tau_poly_hw_enable). */
extern int tau_lpc_hw_enable;
extern unsigned tau_lpc_stat_samples, tau_lpc_stat_timeout;   /* hardware samples reconstructed / unit timeouts (Info page) */

/* Begins one subframe: loads order (1-32), shift (0-31), `order` coefficients (coef[0] pairs with the
 * MOST RECENT sample -- fw/flac.c's own convention, matched by the hardware's own COEF_IDX 0) and
 * `order` warm-up samples (warm[0] is the FIRST one the bitstream read, i.e. the OLDEST; warm[order-1]
 * is the most recent -- the caller passes them in that read order, this function reverses them into the
 * hardware's own most-recent-first WARM_IDX convention). Returns 0 (do not use hardware for this
 * subframe) if the unit isn't present/enabled or order is out of range. */
int tau_lpc_hw_begin(uint32_t order, int32_t shift, const int32_t *coef, const int32_t *warm);

/* One reconstructed sample: residual in, the FULL reconstructed sample out (residual + prediction,
 * exactly what fw/flac.c's own `out[i] += (p >> shift)` computes into out[i] -- ready to store or EMIT
 * directly). On success, *ok is set to 1 and the return value is the sample. On a real hardware timeout,
 * *ok is set to 0 (return value undefined) and the unit is permanently disabled for the rest of the
 * session -- the caller must finish the current subframe, and every subframe after it, with its own
 * software loop; its own local order/shift/coef and out[]/buf[] history are unaffected by this call. */
int32_t tau_lpc_hw_sample(int32_t residual, int *ok);

#endif
