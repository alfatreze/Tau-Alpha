/* The BLIT OPS check (T0 of the Talos plan, D-T05): does the draw engine do what the firmware, the host model (sim/p2_engine_model.h) and the reference renderer say, on THIS bitstream? Draws with the engine,
 * reads the pixels back through the SDRAM mailbox and compares them with values worked out here from first principles. Needs no audio. The same file is compiled against the host model by
 * sim/test_p2_blit_check.py (the expectations are proven there, mutants of the model must fail them), so a FAIL on a Pocket means the engine differs from the model, not that the check is wrong.
 *
 * Rows: the stash band 540..555 (columns 0..400, the font test's, tools/sdram_map.json; the tests run one after another) and a small box in the title strip of the visible buffer (x 330..335, y 6..29).
 * Bits of the result (set = failed): 0 RECT, 1 BAR, 2 COPY, 3 PALETTE BLIT, 4 SCALED BLIT, 5 ALPHA BLEND, 6 CLUT RELOAD ORDER, 7 WIDE COPY (126 words, the row buffer's limit class). */
#include "../gfx/gfx.h"
#include "../platform/platform.h"
#include "blit_check.h"

#define BC_DBUF1 1048576u                       /* the second display buffer (mp3_fb.sv DBUF_BASE1, gfx.c DBUF_BASE1_W: sim/test_sdram_map_fw2.py keeps the three equal) */
static uint32_t bad_px;
static uint16_t rd(uint32_t base, uint32_t x, uint32_t y)
{
    uint32_t r = 0;
    if (gfx_mb_read(base + y * GFX_STRIDE + (x & ~1u), &r)) return 0xDEADu;
    return (uint16_t)((x & 1u) ? r >> 16 : r & 0xFFFFu);
}
static uint16_t spx(uint32_t x, uint32_t y) { return rd(0u, x, y); }                                           /* stash rows: shared by both buffers */
static uint16_t vpx(uint32_t x, uint32_t y) { return rd(gfx_cur_buf() ? BC_DBUF1 : 0u, x, y); }                /* a visible row of the buffer being drawn into */
static uint32_t is(uint16_t got, uint16_t want) { if (got != want) { bad_px++; return 1u; } return 0u; }
static void put_pair(uint32_t x, uint32_t y, uint16_t a, uint16_t b) { gfx_mb_write(y * GFX_STRIDE + x, ((uint32_t)b << 16) | a); }    /* x even */

static uint32_t t_rect(void)
{
    uint32_t b = 0;
    gfx_rect_stash(196u, 540u, 16u, 6u, 0x0001u); gfx_rect_stash(200u, 541u, 5u, 3u, 0x1234u); gfx_fence();
    for (uint32_t y = 540u; y < 546u; y++) for (uint32_t x = 196u; x < 212u; x++) {
        const int in = x >= 200u && x < 205u && y >= 541u && y < 544u;
        b |= is(spx(x, y), in ? 0x1234u : 0x0001u);
    }
    return b;
}
static uint32_t t_bar(void)
{
    uint32_t b = 0;
    static const uint8_t lits[4] = { 5u, 0u, 16u, 200u };                                  /* partly lit, none, all, more than the height and past 127 (the two high bits of the count) */
    for (uint32_t k = 0; k < 4u; k++) {
        gfx_rect(330u, 6u, 6u, 18u, 0x0001u);
        gfx_bar(331u, 7u, 4u, 16u, lits[k], 0xF800u, 0x001Fu); gfx_fence();
        const uint32_t lit = lits[k] > 16u ? 16u : lits[k];
        for (uint32_t y = 6u; y < 24u; y++) for (uint32_t x = 330u; x < 336u; x++) {
            uint16_t want = 0x0001u;
            if (x >= 331u && x < 335u && y >= 7u && y < 23u) want = (y - 7u) < 16u - lit ? 0x001Fu : 0xF800u;
            b |= is(vpx(x, y), want);
        }
    }
    return b;
}
static uint32_t t_copy(void)
{
    uint32_t b = 0;
    for (uint32_t i = 0; i < 8u; i += 2u) { put_pair(i, 546u, (uint16_t)(0x1000u + i), (uint16_t)(0x1001u + i)); put_pair(i, 547u, (uint16_t)(0x2000u + i), (uint16_t)(0x2001u + i)); }
    gfx_rect_stash(100u, 548u, 10u, 2u, 0x0001u);
    gfx_blit_abs(0u, 546u, 101u, 548u, 8u, 2u); gfx_fence();
    for (uint32_t y = 0; y < 2u; y++) for (uint32_t x = 0; x < 10u; x++) {
        const int in = x >= 1u && x < 9u;
        b |= is(spx(100u + x, 548u + y), in ? (uint16_t)((y ? 0x2000u : 0x1000u) + (x - 1u)) : 0x0001u);
    }
    return b;
}
static void clut_fill(uint16_t base) { gfx_clut_begin(0u); for (uint32_t i = 0; i < 256u; i++) gfx_clut_put((uint16_t)(base + i)); }
static uint32_t t_pal(void)
{
    uint32_t b = 0;
    static const uint8_t idx[8] = { 0u, 1u, 37u, 74u, 128u, 200u, 254u, 255u };
    for (uint32_t i = 0; i < 8u; i += 2u) put_pair(i, 550u, idx[i], idx[i + 1u]);                  /* the index sits in the low byte of each word */
    clut_fill(0x4000u);
    gfx_cblit_abs(0u, 550u, 100u, 551u, 8u, 1u); gfx_fence();
    for (uint32_t x = 0; x < 8u; x++) b |= is(spx(100u + x, 551u), (uint16_t)(0x4000u + idx[x]));
    return b;
}
static uint32_t t_scale(void)
{
    uint32_t b = 0;
    put_pair(0u, 552u, 0x1111u, 0x2222u); put_pair(0u, 553u, 0x3333u, 0x4444u);
    gfx_rect(328u, 24u, 10u, 8u, 0x0001u);
    gfx_sblit(0u, 552u, 330u, 25u, 2u, 2u, 3u); gfx_fence();                                        /* 2 x 2 source pixels, each shown 3 x 3 */
    static const uint16_t src[2][2] = { { 0x1111u, 0x2222u }, { 0x3333u, 0x4444u } };
    for (uint32_t y = 0; y < 6u; y++) for (uint32_t x = 0; x < 6u; x++) b |= is(vpx(330u + x, 25u + y), src[y / 3u][x / 3u]);
    b |= is(vpx(336u, 25u), 0x0001u) | is(vpx(329u, 25u), 0x0001u) | is(vpx(330u, 31u), 0x0001u);          /* nothing beyond the 6 x 6 box */
    return b;
}
static uint32_t t_blend(void)
{
    uint32_t b = 0;
    if (!gfx_blend_ready()) return 1u;                                                              /* this bitstream has no blend: that is a failure here */
    put_pair(0u, 554u, 0xFFFFu, 0xFFFFu); put_pair(2u, 554u, 0xFFFFu, 0xFFFFu);
    gfx_rect(340u, 24u, 6u, 2u, 0x0000u);
    gfx_blend_on(128u); gfx_blend_blit(0u, 554u, 340u, 24u, 4u, 1u); gfx_blend_off(); gfx_fence();
    for (uint32_t x = 0; x < 4u; x++) b |= is(vpx(340u + x, 24u), 0x7BEFu);                         /* white over black at a = 128: (31*128)>>8 = 15, (63*128)>>8 = 31, 15 */
    b |= is(vpx(344u, 24u), 0x0000u) | is(vpx(340u, 25u), 0x0000u);
    return b;
}
static uint32_t t_clutorder(void)
{
    uint32_t b = 0;
    put_pair(0u, 550u, 5u, 6u);
    clut_fill(0x4000u);
    gfx_cblit_abs(0u, 550u, 120u, 551u, 2u, 1u);                                                    /* queued with the first palette... */
    clut_fill(0x5000u);                                                                             /* ...which must not be replaced under it */
    gfx_cblit_abs(0u, 550u, 124u, 551u, 2u, 1u); gfx_fence();
    b |= is(spx(120u, 551u), 0x4005u) | is(spx(121u, 551u), 0x4006u) | is(spx(124u, 551u), 0x5005u) | is(spx(125u, 551u), 0x5006u);
    return b;
}
static uint32_t t_wide(void)
{
    uint32_t b = 0;
    gfx_rect_stash(0u, 546u, 126u, 1u, 0x2345u);
    gfx_rect_stash(128u, 547u, 140u, 1u, 0x0001u);
    gfx_blit_abs(0u, 546u, 130u, 547u, 126u, 1u); gfx_fence();
    b |= is(spx(129u, 547u), 0x0001u) | is(spx(130u, 547u), 0x2345u) | is(spx(255u, 547u), 0x2345u) | is(spx(256u, 547u), 0x0001u);
    return b;
}
uint32_t blit_check_bad_px(void) { return bad_px; }
uint32_t blit_check_run(void)
{
    uint32_t m = 0;
    bad_px = 0;
    gfx_fence();
    if (t_rect()) m |= 1u << 0;
    if (t_bar()) m |= 1u << 1;
    if (t_copy()) m |= 1u << 2;
    if (t_pal()) m |= 1u << 3;
    if (t_scale()) m |= 1u << 4;
    if (t_blend()) m |= 1u << 5;
    if (t_clutorder()) m |= 1u << 6;
    if (t_wide()) m |= 1u << 7;
    gfx_rect(328u, 4u, 20u, 30u, BLIT_CHECK_BG);
    return m;
}
