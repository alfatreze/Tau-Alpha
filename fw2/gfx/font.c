#pragma GCC optimize ("Os")
#include "font.h"
#include "gfx.h"
#include "../generated/font_data.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../cores/font_core.h"

uint8_t font_changed;
uint32_t font_cur, font_err, font_loads, font_ms;

static void op_fence(void *c) { (void)c; gfx_fence(); }
static uint32_t op_rd(void *c) { (void)c; return REG_RD(R_FONT_CTRL); }
static void op_wr_ctrl(void *c, uint32_t v) { (void)c; REG_WR(R_FONT_CTRL, v); }
static void op_wr_data(void *c, uint32_t v) { (void)c; REG_WR(R_FONT_DATA, v); }

int font_select(uint32_t idx)
{
    if (idx >= FONT_COUNT) idx = 0;
    if (idx == font_cur) return 0;
    if (!caps.font) { font_err = 2; return 2; }
    if (gfx_held()) { font_err = 4; return 4; }                   /* a crossfade holds the idle buffer: not now */
    ft_t f;
    if (ft_parse(font_recs[idx], font_rec_len, &f)) { font_err = 1; return 1; }
    uint8_t adv[FONT_GLYPHS];
    const ft_ops_t ops = { op_fence, op_rd, op_wr_ctrl, op_wr_data, 0 };
    const uint32_t t0 = cycles();
    const int r = ft_load(&f, &ops, adv);
    if (r) { font_err = (r == FTE_NO_UNIT ? 2u : 3u); return font_err; }
    for (uint32_t i = 0; i < FONT_GLYPHS; i++) font_adv_cur[i] = adv[i];       /* after the memory holds the atlas: nothing is drawn in between (one thread) */
    font_cur = idx; font_err = 0; font_loads++; font_ms = (cycles() - t0) / (CLK_HZ / 1000u); font_changed = 1;
    return 0;
}
