/* fw/halcyon_hw.h -- the Halcyon loader (B-640): builds a coefficient bank from the six controls (fw/halcyon_core.h) or from raw biquads and writes it to the
 * Halcyon engine's shadow bank through the MMIO of mp3_soc.v (docs/MMIO_ALLOCATION.md, 0x178-0x180), then commits it. Portable: the includer provides
 * R_HAL_CTRL / R_HAL_IDX / R_HAL_DATA, REG(a) for the one read, and may override HAL_WR(a, v) (the host test logs the writes with it).
 * Stage order in the bank: stage 0.. are the stages that are written (the six tone stages for a control preset; raw biquads for an imported profile); the engine runs
 * `nact` of them and then the preamp (index HAL_PRE_IDX). A bank is always written complete: the engine keeps the live bank until the next sample boundary. */
#ifndef TAU_HALCYON_HW_H
#define TAU_HALCYON_HW_H
#include <stdint.h>
#include "halcyon_core.h"

#ifndef HAL_WR
#define HAL_WR(a, v) (REG(a) = (v))
#endif
#define HAL_NST      17u                    /* stages the engine has (tau_halcyon.sv NST) */
#define HAL_PRE_IDX  (HAL_NST * 5u)         /* coefficient index of the preamp */
#define HAL_CTRL(en, byp, commit, clr, nact) (((uint32_t)(en) << 0) | ((uint32_t)(byp) << 1) | ((uint32_t)(commit) << 2) | ((uint32_t)(clr) << 3) | ((uint32_t)(nact) << 8))

HAL_FN uint32_t hal_hw_present(void) { return (REG(R_HAL_CTRL) >> 31) & 1u; }

/* Writes `nstage` biquads (b0 b1 b2 a1 a2 each, Q2.22) and the preamp (Q2.22) into the shadow bank, commits it, and enables the engine; `bypass` keeps the engine running
 * but passes the input through (FLAT). A clear pulse follows, so the new filter starts from silence (a recall is done inside a gain dip, the caller's job). */
HAL_FN void hal_hw_commit_bank(const int32_t *coef, uint32_t nstage, int32_t pre, uint32_t bypass)
{
    if (nstage > HAL_NST) nstage = HAL_NST;
    HAL_WR(R_HAL_IDX, 0u);
    for (uint32_t i = 0; i < nstage * 5u; i++) HAL_WR(R_HAL_DATA, (uint32_t)coef[i] & 0xFFFFFFu);
    HAL_WR(R_HAL_IDX, HAL_PRE_IDX);
    HAL_WR(R_HAL_DATA, (uint32_t)pre & 0xFFFFFFu);
    HAL_WR(R_HAL_CTRL, HAL_CTRL(1u, bypass, 1u, 0u, nstage));
    HAL_WR(R_HAL_CTRL, HAL_CTRL(1u, bypass, 0u, 1u, nstage));
}

/* A control preset: the six tone stages from the tables and the peak-safe preamp. All six controls at zero is FLAT: the engine stays selected but bypassed. */
HAL_FN void hal_hw_apply_ctl(const hal_ctl_t *c)
{
    uint8_t step[HAL_NSTAGE];
    int32_t bank[HAL_NSTAGE * 5];
    uint32_t flat = 1u;
    hal_steps(c, step);
    for (uint32_t s = 0; s < HAL_NSTAGE; s++) {
        if (step[s] != 18u) flat = 0u;
        for (uint32_t k = 0; k < 5u; k++) bank[s * 5u + k] = hal_coef[s][step[s]][k];
    }
    hal_hw_commit_bank(bank, HAL_NSTAGE, hal_preamp_q22(hal_atten_eighths(step)), flat);
}

/* Takes the engine out of the audio path (eq_biquad drives the output again). */
HAL_FN void hal_hw_off(void) { HAL_WR(R_HAL_CTRL, HAL_CTRL(0u, 0u, 0u, 0u, 0u)); }
#endif
