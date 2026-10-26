/* Player 2 side of the FLAC LPC unit (tau_flac_lpc.sv, MMIO 0x120-0x13C): the API fw/flac.c calls with TAU_LPC_FW, same behaviour as the old player's fw/flac_lpc_hw.inc
 * (hardware-confirmed, B-386): any real anomaly disables the unit for the session and the software loop takes over. */
#include "../platform/mmio.h"
#include "../../fw/flac_lpc_hw.h"
#define R_LPC_CFG       0x80000120u
#define R_LPC_COEF_IDX  0x80000124u
#define R_LPC_COEF_DATA 0x80000128u
#define R_LPC_WARM_IDX  0x8000012Cu
#define R_LPC_WARM_DATA 0x80000130u
#define R_LPC_RESIDUAL  0x80000134u
#define R_LPC_SAMPLE    0x80000138u

int tau_lpc_hw_enable = 0;
unsigned tau_lpc_stat_samples, tau_lpc_stat_timeout;

int tau_lpc_hw_begin(uint32_t order, int32_t shift, const int32_t *coef, const int32_t *warm)
{
    if (!tau_lpc_hw_enable) return 0;
    if (order < 1u || order > 32u) return 0;
    REG_WR(R_LPC_CFG, (order & 0x3Fu) | (((uint32_t)shift & 0x1Fu) << 6u));
    REG_WR(R_LPC_COEF_IDX, 0u);
    for (uint32_t j = 0; j < order; j++) REG_WR(R_LPC_COEF_DATA, (uint32_t)coef[j]);
    REG_WR(R_LPC_WARM_IDX, 0u);
    for (uint32_t j = 0; j < order; j++) REG_WR(R_LPC_WARM_DATA, (uint32_t)warm[order - 1u - j]);
    return 1;
}
int32_t tau_lpc_hw_sample(int32_t residual, int *ok)
{
    REG_WR(R_LPC_RESIDUAL, (uint32_t)residual);
    for (int i = 0; i < 4000; i++) {
        if (REG_RD(R_LPC_STATUS) & 4u) { int32_t s = (int32_t)REG_RD(R_LPC_SAMPLE); tau_lpc_stat_samples++; *ok = 1; return s; }
    }
    tau_lpc_stat_timeout++; tau_lpc_hw_enable = 0; *ok = 0;
    return 0;
}
