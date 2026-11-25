/* Copy of fw/flac_lpc_hw.inc (real MMIO glue), register block moved to the simulator's 0xF0000100. */
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(a))
#define R_LPC_CFG       0xF0000100u
#define R_LPC_COEF_IDX  0xF0000104u
#define R_LPC_COEF_DATA 0xF0000108u
#define R_LPC_WARM_IDX  0xF000010Cu
#define R_LPC_WARM_DATA 0xF0000110u
#define R_LPC_RESIDUAL  0xF0000114u
#define R_LPC_SAMPLE    0xF0000118u
#define R_LPC_STATUS    0xF000011Cu

int tau_lpc_hw_enable = 1;
unsigned tau_lpc_stat_samples, tau_lpc_stat_timeout;

int tau_lpc_hw_begin(uint32_t order, int32_t shift, const int32_t *coef, const int32_t *warm)
{
    if (!tau_lpc_hw_enable) return 0;
    if (order < 1u || order > 32u) return 0;
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
        if (REG(R_LPC_STATUS) & 4u) {
            int32_t s = (int32_t)REG(R_LPC_SAMPLE);
            tau_lpc_stat_samples++;
            *ok = 1;
            return s;
        }
    }
    tau_lpc_stat_timeout++;
    tau_lpc_hw_enable = 0;
    *ok = 0;
    return 0;
}
