/* B-307: implementation of fw/mp3_poly_hw.h. Textually included into player.c (not a separate translation unit) so it shares REG() and the
 * R_POLY_* register defines already declared above (docs/MMIO_ALLOCATION.md 0x100-0x110). subband.c (a genuinely separate translation unit,
 * in third_party/) only sees the extern declarations in mp3_poly_hw.h and links against the definitions here.
 *
 * Any real hardware anomaly (a clear or a per-slot compute that never clears busy) permanently disables the unit for the rest of the session
 * (tau_poly_hw_enable = 0) rather than retrying mid-track -- an intermittently-misbehaving window unit is worse than a software fallback that
 * is known correct. Timeouts are bounded by a plain iteration count, not a cycle counter: RTL's own budget is about 1,024 clocks for a clear
 * and about 4,700 clocks for a compute (docs/MP3_FILTERBANK_KERNEL_DESIGN.md section 6 step 3); the loop bounds below are two orders of
 * magnitude more generous than that so normal operation never trips them, while a genuinely wedged unit is still caught in well under a
 * frame's time. */

#include "mp3_poly_hw.h"

int tau_poly_hw_enable = 0;
int tau_poly_verify_left;
unsigned tau_poly_stat_slots, tau_poly_stat_mismatch, tau_poly_stat_timeout;   /* hardware slots done / self-check mismatches / unit timeouts (Info page) */

/* FDCT32's capture log (third_party/libhelix-mp3/real/dct32.c, TAU_POLY_LOG): the 33 stores of one call, in call order (index 17 duplicates 1). */
int tau_poly_wlog[33];
int tau_poly_wn;

void tau_poly_hw_clear(void)
{
    if (!tau_poly_hw_enable) return;
    tau_poly_verify_left = TAU_POLY_VERIFY_SLOTS;                  /* a new decoder instance: verify its first slots against software */
    REG(R_POLY_CTL) = 1u;                              /* bit 0: clear (zeroes the shared 1,024-word history) */
    for (int i = 0; i < 200000; i++) {
        if (!(REG(R_POLY_ST) & 2u)) return;            /* bit 1: busy */
    }
    tau_poly_stat_timeout++;
    tau_poly_hw_enable = 0;                             /* clear itself never finished: don't trust the unit any further this session */
}

int tau_poly_hw_slot(const int *w0, const int *w1, short *pcm)
{
    if (!tau_poly_hw_enable) return 0;
    for (int i = 0; i < 32; i++) REG(R_POLY_PUSH) = (uint32_t)w0[i];
    for (int i = 0; i < 32; i++) REG(R_POLY_PUSH) = (uint32_t)w1[i];
    REG(R_POLY_CTL) = 2u;                              /* bit 1: go -- compute the pending slot, both channels */
    int i;
    for (i = 0; i < 400000; i++) {
        if (!(REG(R_POLY_ST) & 2u)) break;
    }
    if (i == 400000) { tau_poly_stat_timeout++; tau_poly_hw_enable = 0; return 0; }
    for (int k = 0; k < 32; k++) {
        REG(R_POLY_IDX) = (uint32_t)k;
        uint32_t w = REG(R_POLY_OUT);                  /* {R sample [31:16], L sample [15:0]}, Helix's own interleave */
        pcm[2 * k]     = (short)(w & 0xFFFFu);
        pcm[2 * k + 1] = (short)((w >> 16) & 0xFFFFu);
    }
    tau_poly_stat_slots++;
    return 1;
}
