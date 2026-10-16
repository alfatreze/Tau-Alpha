#pragma GCC optimize ("Os")
#include "halcyon.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../transport/mp3play.h"
#include "../cores/halcyon_sel_core.h"
#define HAL_FN static
#include "../../fw/halcyon_hw.h"
#define HP_FN static
#include "../../fw/halcyon_prst.h"
#include "../../fw/halcyon_curve.h"

uint8_t halcyon_sel, halcyon_saved, halcyon_user_n, halcyon_present, halcyon_cmp;
static hal_ctl_t hal_c;                                     /* the six controls the page works on */
#define HAL_PRST_PSRAM 0xA4540000u                          /* 1,536 bytes: 12 + 8 entries of at most 172 bytes (the old player's address) */
#define HAL_PRST_CAP   1536u
#define HAL_USER       ((const uint8_t *)(uintptr_t)HAL_PRST_PSRAM)
static uint16_t hal_user_off[HP_MAX_PRESETS];
#define NPRESETS (HAL_NPRESET + halcyon_user_n)

void halcyon_init(void) { halcyon_present = (uint8_t)((REG_RD(R_HAL_CTRL) >> 31) & 1u); }

/* The user presets of the PRST section, validated whole (any failure means none), copied to PSRAM (the window the file came in is reused later). */
void halcyon_prst_load(const uint8_t *sec, uint32_t n, int staged, int (*prove)(void))
{
    uint16_t o[HP_MAX_PRESETS]; uint8_t cnt;
    halcyon_user_n = 0u;
    if (n > HAL_PRST_CAP) return;
    if (hp_check(sec, n, o, &cnt) != HP_OK) return;
    if (!staged && !prove()) return;
    volatile uint8_t *d = (volatile uint8_t *)(uintptr_t)HAL_PRST_PSRAM;
    for (uint32_t i = 0; i < n; i++) d[i] = sec[i];
    for (uint32_t i = 0; i < cnt; i++) hal_user_off[i] = o[i];
    halcyon_user_n = cnt;
}

static void apply_sel(void)
{
    if (!halcyon_present) return;
    if (halcyon_sel == 0u) { hal_hw_off(); return; }
    if (halcyon_sel == HS_CUSTOM) { hal_hw_apply_ctl(&hal_c); return; }
    if (halcyon_sel > HAL_NPRESET) {                         /* a user preset */
        hal_ctl_t uc; int32_t coef[HP_MAX_RAW_STAGES * 5u], pre = 0;
        const uint32_t ns = hp_entry(HAL_USER + hal_user_off[halcyon_sel - HAL_NPRESET - 1u], &uc, coef, &pre);
        if (ns) hal_hw_commit_with_infra(coef, ns, pre, 0u, 1u); else hal_hw_apply_ctl(&uc);
        return;
    }
    hal_ctl_t c; const int8_t *p = hal_preset_ctl[halcyon_sel - 1u];
    c.warmth = p[0]; c.bass = p[1]; c.vocal = p[2]; c.punch = p[3]; c.sibilance = p[4]; c.air = p[5];
    hal_hw_apply_ctl(&c);
}
static void apply_dipped(void) { if (!halcyon_present) return; mp3play_dip_begin(); apply_sel(); mp3play_dip_end(); }
static void note_saved(void) { halcyon_saved = (uint8_t)hs_saved(halcyon_sel, halcyon_saved); }

const char *halcyon_name(void)
{
    if (!halcyon_present) return "NO UNIT";
    if (halcyon_cmp) return "A/B: OFF";
    if (halcyon_sel == 0u) return "OFF";
    if (halcyon_sel == HS_CUSTOM) return "CUSTOM";
    if (halcyon_sel > HAL_NPRESET) return hp_name(HAL_USER + hal_user_off[halcyon_sel - HAL_NPRESET - 1u]);
    return hal_preset_name[halcyon_sel - 1u];
}
void halcyon_cycle(void)
{
    if (!halcyon_present) return;
    halcyon_cmp = 0u; halcyon_sel = (uint8_t)hs_cycle(halcyon_sel, NPRESETS);
    apply_dipped(); note_saved();
}
void halcyon_restore(uint32_t saved)
{
    const uint32_t v = hs_restore(saved, NPRESETS);
    if (v == halcyon_sel && saved == halcyon_saved) return;
    halcyon_saved = (uint8_t)(saved < 31u ? saved : 0u);
    if (v == halcyon_sel) return;
    halcyon_sel = (uint8_t)v; halcyon_cmp = 0u;
    apply_dipped();
}

static void load_sel(void)                                   /* the controls of the selected preset (a raw user preset has none: they stay) */
{
    if (halcyon_sel >= 1u && halcyon_sel <= HAL_NPRESET) {
        const int8_t *p = hal_preset_ctl[halcyon_sel - 1u];
        hal_c.warmth = p[0]; hal_c.bass = p[1]; hal_c.vocal = p[2]; hal_c.punch = p[3]; hal_c.sibilance = p[4]; hal_c.air = p[5];
    } else if (halcyon_sel > HAL_NPRESET && halcyon_sel < HS_CUSTOM) {
        int32_t coef[HP_MAX_RAW_STAGES * 5u], pre = 0; hal_ctl_t uc;
        if (hp_entry(HAL_USER + hal_user_off[halcyon_sel - HAL_NPRESET - 1u], &uc, coef, &pre) == 0u) hal_c = uc;
    }
}
void halcyon_page_open(void) { halcyon_cmp = 0u; load_sel(); }
int32_t halcyon_ctl(uint32_t i) { return ((int8_t *)&hal_c)[i]; }
static void apply_live(void) { if (!halcyon_present) return; halcyon_sel = HS_CUSTOM; halcyon_cmp = 0u; hal_hw_apply_ctl_live(&hal_c); note_saved(); }
void halcyon_ctl_set(uint32_t i, int32_t v)
{
    v = hs_ctl_clamp(i + 1u, v);
    if (v == ((int8_t *)&hal_c)[i]) return;
    ((int8_t *)&hal_c)[i] = (int8_t)v; apply_live();
}
void halcyon_step(int dir)
{
    if (!halcyon_present) return;
    halcyon_sel = (uint8_t)hs_step(halcyon_sel, NPRESETS, dir); halcyon_cmp = 0u;
    load_sel(); apply_dipped(); note_saved();
}
void halcyon_compare(void)
{
    if (!halcyon_present) return;
    if (!halcyon_cmp) { halcyon_cmp = 1u; hal_hw_off(); } else apply_live();
}
void halcyon_reset(void) { hal_c.warmth = hal_c.bass = hal_c.vocal = hal_c.punch = hal_c.sibilance = hal_c.air = 0; apply_live(); }
int32_t halcyon_curve_get(int16_t out[HALCYON_NGRID]) { return hal_curve(&hal_c, out); }
