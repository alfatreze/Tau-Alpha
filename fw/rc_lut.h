/* B11 corner-cut LUT load: portable, host-testable logic, no MMIO (matches fw/chladni_core.h's and
 * fw/helios_rect.h's own "pure logic, no REG()" convention -- sim/test_rc_lut.py exercises this file
 * directly through tools/host/rc_lut_harness.c).
 *
 * Root cause this fixes: mp3_soc.v's rc_cut_lut_r (16 entries x 5 bits, R_RC_IDX/R_RC_DATA) resets to
 * all-zero and nothing in firmware ever wrote it -- every hardware-drawn OP_RRECT rendered square
 * corners. fw/player.c's fb_round_rect_on() must call rc_lut_ensure(r) (defined there, wrapping
 * rc_lut_prepare() below with the real REG(R_RC_IDX)/REG(R_RC_DATA) writes) before fb_rrect() whenever
 * RRECT_READY() and r != 0.
 */
#ifndef RC_LUT_H
#define RC_LUT_H
#include <stdint.h>

#define RC_LUT_RADIUS_NONE 0xFFu
#define RC_LUT_N            16u

/* Same integer quarter-circle search as fb_round_rect()/fb_round_rect_on()'s software fallback
 * (fw/player.c), evaluated per dy instead of per row: for row i (0..r-1) the software path looks up
 * dy = r - i (so dy runs 1..r over a draw); mp3_soc.v's own comment says the hardware LUT is indexed
 * the same way ("dy = r - row is looked up in rc_cut_lut"). dy=0 and dy>r are never actually queried
 * by the RTL for a given radius, so this returns 0 (no cut) for them rather than reuse a stale
 * larger-radius value there. */
static uint32_t rc_lut_cut(uint32_t r, uint32_t dy)
{
    if (!r || dy < 1u || dy > r) return 0u;
    uint32_t inner = 0u;
    while ((inner + 1u) * (inner + 1u) + dy * dy <= r * r) inner++;
    return (r > inner) ? (r - inner) : 0u;
}

/* Fills out[0..15] with rc_lut_cut(r, dy) and returns 1 if a real load is needed (r differs from
 * *cache_radius), updating *cache_radius to r. Returns 0 (out[] untouched) when r == *cache_radius
 * already, or when r is 0 -- RTL's rrect_pending never arms for radius 0 (fb_rrect()'s own comment),
 * so the LUT is never read for it either, and loading it would be 16 wasted MMIO writes. */
static int rc_lut_prepare(uint32_t r, uint8_t *cache_radius, uint8_t out[RC_LUT_N])
{
    if (!r || r == *cache_radius) return 0;
    for (uint32_t dy = 0u; dy < RC_LUT_N; dy++) out[dy] = (uint8_t)rc_lut_cut(r, dy);
    *cache_radius = (uint8_t)r;
    return 1;
}

#endif
