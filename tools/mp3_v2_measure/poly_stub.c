/* MP3 v2 measurement: the window unit as a RAM stub for the profiler (rvprof). Own code (Tau Alpha, MIT).
 *
 * Player 2 links fw2/decode/poly_hw.c, whose functions are loops over MMIO registers (64 pushes, a status poll, 32 index writes and 32 result reads per slot). The profiler
 * has no unit, so the CPU-side work is counted by giving the SAME loops RAM-backed registers: same instruction shape, same number of words (64 stores per slot into the push
 * register, one control store, one poll read, 32 x {index store, result read, two halfword stores}), no waiting. What it does NOT model: the latency of a real MMIO access
 * and the unit's compute time (the unit works while the CPU runs the next slot's FDCT32; the review measured sub_hw at about 3 % of a frame on the Pocket against about
 * 34 k cycles in this model's first version), so `sub_hw` here is a floor, not a measurement.
 * The status register reads 0 (not busy), so each poll exits on its first read; the result register reads 0, so the PCM it returns is silence (the profile does not look at it).
 * Same API as fw/mp3_poly_hw.h, TAU_POLY_REQUIRED build (no self-check window, no software window).
 *
 * TWO SHAPES. The default is faithful to fw2/decode/poly_hw.c. -DPOLY_STUB_REVIEW=1 is the shape of the review's scratch stub, whose source was not kept: against poly_hw.c's loops
 * it executed 212 MORE instructions (244 + 32, the 32 being the index store that had no counterpart) and 32 FEWER stores per slot (found as a constant 7,636 instructions, 7,566 model cycles and 1,152 stores per frame on every stereo row of the review's
 * table, 36 slots per frame), consistent with a result array indexed in RAM (no index store per result word) plus about 276 extra ALU instructions per slot of copying. It exists only
 * so that tools/mp3_v2_measure/measure.py --review can reproduce that table; the faithful shape is the one to use for any new measurement. The padding below is NOT decoder
 * work and has no meaning on the Pocket. */
#include <stdint.h>
#include "mp3_poly_hw.h"

#define TAU_POLY_STUB_WORDS 8
static volatile uint32_t poly_regs[TAU_POLY_STUB_WORDS];     /* 0 CTL, 1 PUSH, 2 IDX, 3 OUT, 4 ST (all RAM: the D$ model sees ordinary loads and stores) */
#define R_POLY_CTL  poly_regs[0]
#define R_POLY_PUSH poly_regs[1]
#define R_POLY_IDX  poly_regs[2]
#define R_POLY_OUT  poly_regs[3]
#define R_POLY_ST   poly_regs[4]

int tau_poly_hw_enable = 1;
int tau_poly_verify_left;
unsigned tau_poly_stat_slots, tau_poly_stat_mismatch, tau_poly_stat_timeout, tau_poly_stat_silent;
int tau_poly_wlog[33];
int tau_poly_wn;

void tau_poly_hw_clear(void)
{
    if (!tau_poly_hw_enable) return;
    tau_poly_verify_left = TAU_POLY_VERIFY_SLOTS;
    R_POLY_CTL = 1u;
    for (int i = 0; i < 200000; i++) if (!(R_POLY_ST & 2u)) return;
    tau_poly_stat_timeout++; tau_poly_hw_enable = 0;
}
#if POLY_STUB_REVIEW
/* 138 ALU instructions as a small loop (12 iterations of 8 adds, a counter and a branch = 10 instructions each, 120, plus 18 straight ones), so the padding costs instruction
 * count and about 1.1 cycles per instruction (a taken branch is +2) without filling the I$ with straight-line code. */
#define POLY_STUB_PAD() __asm__ volatile ("li t1, 12\n1:\n\t.rept 8\n\tadd t0, t0, t0\n\t.endr\n\taddi t1, t1, -1\n\tbnez t1, 1b\n\t.rept 18\n\tadd t0, t0, t0\n\t.endr" ::: "t0", "t1")
#endif
int tau_poly_hw_start(const int *w0, const int *w1)
{
    if (!tau_poly_hw_enable) return 0;
#if POLY_STUB_REVIEW
    POLY_STUB_PAD();
#endif
    for (int i = 0; i < 32; i++) R_POLY_PUSH = (uint32_t)w0[i];
    for (int i = 0; i < 32; i++) R_POLY_PUSH = (uint32_t)w1[i];
    R_POLY_CTL = 2u;
    return 1;
}
int tau_poly_hw_finish(short *pcm)
{
    int i;
    for (i = 0; i < 400000; i++) if (!(R_POLY_ST & 2u)) break;
    if (i == 400000) { tau_poly_stat_timeout++; tau_poly_hw_enable = 0; return 0; }
#if POLY_STUB_REVIEW
    POLY_STUB_PAD();
#endif
    for (int k = 0; k < 32; k++) {
#if !POLY_STUB_REVIEW
        R_POLY_IDX = (uint32_t)k;
#endif
        uint32_t w = R_POLY_OUT;
        pcm[2 * k] = (short)(w & 0xFFFFu); pcm[2 * k + 1] = (short)((w >> 16) & 0xFFFFu);
    }
    tau_poly_stat_slots++;
    return 1;
}
int tau_poly_hw_slot(const int *w0, const int *w1, short *pcm) { return tau_poly_hw_start(w0, w1) && tau_poly_hw_finish(pcm); }
