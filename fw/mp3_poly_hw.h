/* B-307: firmware-side glue for the MP3 window unit (tau_mp3_poly.sv, TAU_POLY, docs/MP3_FILTERBANK_KERNEL_DESIGN.md). Kept out of the
 * vendored third_party/libhelix-mp3/real/subband.c on purpose -- subband.c only calls this thin, host-testable API (no MMIO register
 * addresses in the vendored tree), same separation player.c already uses for wave_hw/spec_hw's own probe registers. */
#ifndef MP3_POLY_HW_H
#define MP3_POLY_HW_H

/* Set by player.c's boot probe (R_POLY_ST bit 0) right after reading it; 0 on any bitstream without the unit, or once tau_poly_hw_slot()
 * sees a real timeout (permanent fallback to software for the rest of the session -- a hardware anomaly is not worth retrying mid-track). */
extern int tau_poly_hw_enable;

/* Self-check: the first TAU_POLY_VERIFY_SLOTS slots of every decoder instance also run the real PolyphaseStereo and compare (subband.c). tau_poly_hw_clear()
 * arms the counter; a mismatch permanently disables the unit (software result used for that slot) and is counted. Info > MP3 WINDOW shows both counters. */
#define TAU_POLY_VERIFY_SLOTS 8
extern int tau_poly_verify_left;
extern unsigned tau_poly_stat_slots, tau_poly_stat_mismatch, tau_poly_stat_timeout;

/* Pulses R_POLY_CTL's clear bit and polls busy (bounded). Call once per decoder instance, before its first hardware slot -- subband.c does
 * this itself via SubbandInfo's own hwPolyReady flag, so nothing else needs to call this. */
void tau_poly_hw_clear(void);

/* Runs one stereo slot in hardware: pushes w0[32]/w1[32] (FDCT32's push-order values, duplicate index 17 already removed by the caller),
 * triggers compute, polls busy (bounded), and on success fills pcm[64] in Helix's own interleave (L0 R0 L1 R1 ... L31 R31).
 * Returns 1 (pcm filled) or 0 (timeout -- pcm untouched, tau_poly_hw_enable is now 0, caller must run PolyphaseStereo itself for this slot). */
int tau_poly_hw_slot(const int *w0, const int *w1, short *pcm);


/* Split form of tau_poly_hw_slot() (Cymo C0 follow-up, B-587): _start() pushes both channels and triggers compute but does NOT wait, so the CPU can run the next slot's
 * FDCT32 while the unit works (the unit needs about 4,400 clocks per slot; blocking on it was most of the measured 5,300 cycles per slot). _finish() waits (bounded)
 * and reads the 32 stereo PCM words into pcm[64]; returns 1, or 0 on a timeout (the unit is then disabled for the session and pcm is untouched). The unit accepts
 * no pushes while busy, so every _start() must be preceded by the previous slot's _finish(). _start() returns 0 only when the unit is disabled. */
int tau_poly_hw_start(const int *w0, const int *w1);
int tau_poly_hw_finish(short *pcm);

#endif
