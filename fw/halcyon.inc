/* fw/halcyon.inc -- the Halcyon engine selector (B-640, Diagnostic Build first). Everything here is cold code and cold data: the tables (halcyon_tab.h, about 24 KB) live in PSRAM,
 * and the one hot cost is the boot probe in player.c (`hw_hal`). The portable loader is fw/halcyon_hw.h (host-tested, sim/test_halcyon_hw.py); this file only picks a built-in preset
 * and applies it. Diagnostics > HALCYON cycles OFF and the eight built-in presets for the hardware A/B against the old EQ; never persisted (a test switch, like the other
 * Diagnostics toggles). On a bitstream without the engine the row reads NO UNIT and nothing is written. */
#if TAU_HALCYON_FW
#define HAL_TAB COLD_DATA
#define HAL_FN COLD_FN static
#include "halcyon_hw.h"
#define AS_FN COLD_FN static
#include "assets_core.h"        /* as_find (the container reader), before assets.inc includes it itself */
#define HP_FN COLD_FN static
#include "halcyon_prst.h"

/* User presets (B-641): the PRST section of tau-assets.bin, validated at boot by hal_prst_load() and kept in PSRAM (a small file is read through the tag buffer, which is reused later,
 * so the section is copied out). Only the entry offsets live in on-chip RAM (17 bytes). `hal_user_n` 0 = no file or an invalid one (the whole section is refused, never half-loaded). */
#define HAL_PRST_PSRAM 0xA4540000u              /* 1,536 bytes: 12 + 8 entries of at most 172 bytes */
#define HAL_PRST_CAP   1536u
#define HAL_USER       ((const uint8_t *)(uintptr_t)HAL_PRST_PSRAM)
static uint8_t  hal_user_n;
static uint16_t hal_user_off[HP_MAX_PRESETS];
static uint8_t hal_sel;                 /* 0 = engine off (the old EQ drives the output), 1..HAL_NPRESET = built-in preset index + 1, then the user presets */

/* Called by th_assets_load() with the whole file in memory. `staged`: base is the PSRAM staging area already (a large file); otherwise `prove` must say the PSRAM window is usable. */
COLD_FN static void hal_prst_load(const uint8_t *base, uint32_t total, int staged, int (*prove)(void))
{
    uint32_t off, n;
    uint16_t o[HP_MAX_PRESETS];
    uint8_t cnt;
    hal_user_n = 0u;
    if (as_find(base, total, "PRST", &off, &n) != AS_OK || n > HAL_PRST_CAP) return;
    if (hp_check(base + off, n, o, &cnt) != HP_OK) return;
    if (!staged && !prove()) return;
    volatile uint8_t *d = (volatile uint8_t *)(uintptr_t)HAL_PRST_PSRAM;
    for (uint32_t i = 0; i < n; i++) d[i] = base[off + i];
    for (uint32_t i = 0; i < cnt; i++) hal_user_off[i] = o[i];
    hal_user_n = cnt;
}

COLD_FN static void hal_apply_sel(void)
{
    if (!hw_hal) return;
    if (hal_sel == 0u) { hal_hw_off(); return; }
    if (hal_sel > HAL_NPRESET) {                                 /* a user preset from the PRST section */
        hal_ctl_t uc; int32_t coef[HP_MAX_RAW_STAGES * 5u], pre = 0;
        const uint32_t ns = hp_entry(HAL_USER + hal_user_off[hal_sel - HAL_NPRESET - 1u], &uc, coef, &pre);
        if (ns) hal_hw_commit_bank(coef, ns, pre, 0u); else hal_hw_apply_ctl(&uc);
        return;
    }
    hal_ctl_t c;
    const int8_t *p = hal_preset_ctl[hal_sel - 1u];
    c.warmth = p[0]; c.bass = p[1]; c.vocal = p[2]; c.punch = p[3]; c.sibilance = p[4]; c.air = p[5];
    hal_hw_apply_ctl(&c);
}

/* A on the row: the next preset (after the last, back to OFF), inside a gain dip so the change of filter and preamp is not a step in the listener's ears (B-620). */
COLD_FN static void hal_cycle(void)
{
    if (!hw_hal) return;
    gain_dip_begin();
    hal_sel = (uint8_t)((hal_sel + 1u) % (HAL_NPRESET + 1u + hal_user_n));
    hal_apply_sel();
    gain_dip_end();
}

COLD_FN static const char *hal_row_text(void)
{
    if (!hw_hal) return "NO UNIT";
    if (hal_sel == 0u) return "OFF";
    if (hal_sel > HAL_NPRESET) return hp_name(HAL_USER + hal_user_off[hal_sel - HAL_NPRESET - 1u]);
    return hal_preset_name[hal_sel - 1u];
}
#endif
