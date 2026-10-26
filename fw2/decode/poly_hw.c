/* Player 2 side of the MP3 window unit (tau_mp3_poly.sv, MMIO 0x100-0x110): the small API vendored Helix's subband.c calls (third_party/libhelix-mp3/real/subband.c with TAU_POLY_FW,
 * declared in fw/mp3_poly_hw.h). Same behaviour as the old player's fw/mp3_poly_hw.inc, which ran 404,712 slots on a Pocket with 0 mismatches and 0 timeouts (B-309):
 * any real anomaly (a clear or a compute that never finishes) disables the unit for the session and the software window takes over. */
#include "../platform/mmio.h"
#include "../../fw/mp3_poly_hw.h"

#define R_POLY_CTL  0x80000100u
#define R_POLY_PUSH 0x80000104u
#define R_POLY_IDX  0x80000108u
#define R_POLY_OUT  0x8000010Cu

int tau_poly_hw_enable = 0;
int tau_poly_verify_left;
unsigned tau_poly_stat_slots, tau_poly_stat_mismatch, tau_poly_stat_timeout, tau_poly_stat_silent;
int tau_poly_wlog[33];
int tau_poly_wn;

void tau_poly_hw_clear(void)
{
    if (!tau_poly_hw_enable) return;
    tau_poly_verify_left = TAU_POLY_VERIFY_SLOTS;
    REG_WR(R_POLY_CTL, 1u);
    for (int i = 0; i < 200000; i++) if (!(REG_RD(R_POLY_ST) & 2u)) return;
    tau_poly_stat_timeout++; tau_poly_hw_enable = 0;
}
int tau_poly_hw_start(const int *w0, const int *w1)
{
    if (!tau_poly_hw_enable) return 0;
    for (int i = 0; i < 32; i++) REG_WR(R_POLY_PUSH, (uint32_t)w0[i]);
    for (int i = 0; i < 32; i++) REG_WR(R_POLY_PUSH, (uint32_t)w1[i]);
    REG_WR(R_POLY_CTL, 2u);
    return 1;
}
int tau_poly_hw_finish(short *pcm)
{
    int i;
    for (i = 0; i < 400000; i++) if (!(REG_RD(R_POLY_ST) & 2u)) break;
    if (i == 400000) { tau_poly_stat_timeout++; tau_poly_hw_enable = 0; return 0; }
    for (int k = 0; k < 32; k++) {
        REG_WR(R_POLY_IDX, (uint32_t)k);
        uint32_t w = REG_RD(R_POLY_OUT);
        pcm[2 * k] = (short)(w & 0xFFFFu); pcm[2 * k + 1] = (short)((w >> 16) & 0xFFFFu);
    }
    tau_poly_stat_slots++;
    return 1;
}
int tau_poly_hw_slot(const int *w0, const int *w1, short *pcm) { return tau_poly_hw_start(w0, w1) && tau_poly_hw_finish(pcm); }
