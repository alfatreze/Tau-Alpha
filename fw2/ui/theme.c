#include "theme.h"
#include "../platform/mmio.h"
#include "../cores/theme_core.h"

#define R_TEXT_MODE 0x80000114u   /* write bit 0 = 1 selects the light-polarity text weight table; read bit 31 = the bitstream has it */
uint16_t th_role[TR_COUNT];
uint8_t th_theme, th_pol, th_pal = 1u, th_file_n, th_file_err, th_changed;
static uint8_t text_mode_hw;

#include "../../fw/theme_data.h"                      /* th_themes[] (generated), TH_THEME_N, TH_LIGHT_ACC_MAX_L */
_Static_assert(TH_LIGHT_ACC_MAX_L == TC_LIGHT_ACC_MAX_L, "the generated themes and the core must agree on the light accent ceiling");

static const uint16_t palette[TC_PALETTE_N] = {
    0x18C3u, 0xF7BEu, 0x2A83u, 0xE73Bu, 0x8C51u, 0xD1E6u, 0xE429u, 0x5D0Du, 0x4BD4u, 0x9BF6u, 0xFEA0u, 0xE429u, 0xD1E6u, 0xE4B6u, 0x4BD4u, 0x5D0Du, 0x6AD2u, 0xBDF7u, 0x8C71u };
const char *const th_palette_name[19] = {
    "BLACK", "WHITE", "GLOW", "TRANS_CLEAR", "TRANS_SMOKE", "TRANS_RED", "TRANS_ORANGE", "TRANS_GREEN", "TRANS_BLUE", "TRANS_PURPLE",
    "CLASSIC_YELLOW", "CLASSIC_ORANGE", "CLASSIC_RED", "CLASSIC_PINK", "CLASSIC_BLUE", "CLASSIC_GREEN", "CLASSIC_INDIGO", "CLASSIC_SILVER", "ALUMINUM" };

uint32_t theme_count(void) { return TH_THEME_N + th_file_n; }
const char *theme_name(uint32_t i) { return i < TH_THEME_N ? th_themes[i].name : theme_file_name(i - TH_THEME_N); }
uint16_t theme_builtin_role(uint32_t pol, uint32_t role) { return th_themes[0].role[pol & 1u][role]; }

static void apply(void)
{
    const uint32_t p = th_pol ? 1u : 0u, fi = th_theme >= TH_THEME_N ? th_theme - TH_THEME_N : 0u;
    for (uint32_t i = 0; i < TR_COUNT; i++) {
        if (i == TR_BG_TOP || i == TR_ACCENT || i == TR_ACCENT2) continue;
        th_role[i] = th_theme < TH_THEME_N ? th_themes[th_theme].role[p][i] : theme_file_role(fi, p, i);
    }
    const uint16_t acc = tc_accent(palette[th_pal], p);
    th_role[TR_ACCENT] = acc; th_role[TR_ACCENT2] = acc; th_role[TR_BG_TOP] = th_role[TR_BG_BOTTOM];
    if (text_mode_hw) REG_WR(R_TEXT_MODE, p);                    /* dark text on a light ramp needs its own edge weights */
    th_changed = 1u;
}
void theme_init(void)
{
    text_mode_hw = (uint8_t)((REG_RD(R_TEXT_MODE) >> 31) & 1u);
    th_theme = 0u; th_pol = 0u; th_pal = 1u; apply();
}
void theme_set(uint32_t theme, uint32_t pol, uint32_t pal)
{
    theme = tc_valid(theme, theme_count(), 0u); pol = tc_valid(pol, 2u, 0u); pal = tc_valid(pal, TC_PALETTE_N, 1u);
    if (theme == th_theme && pol == th_pol && pal == th_pal) return;
    th_theme = (uint8_t)theme; th_pol = (uint8_t)pol; th_pal = (uint8_t)pal; apply();
}
void theme_pick_step(int dir) { theme_set(th_theme, th_pol, tc_pick_step(th_pal, dir)); }
