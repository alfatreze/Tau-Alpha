/* MASTER VU meter core (docs/METER_VU_MASTERING_SPEC.md): portable, host-testable logic with no MMIO, no engine
 * calls and no floating point (rv32im has none) -- same discipline as fw/meter_core.h and fw/chladni_core.h.
 * The firmware module (fw/vu_master.inc) supplies drawing; sim/test_vu_master.py tests this file natively.
 *
 * Two independent pieces:
 *   1. vum_peak_to_segments() -- a peak magnitude (the MTR_HEADROOM-scaled 16-bit peak_l/peak_r accumulators,
 *      fw/player.c:4500-4502) to a lit-segment count 0..24, via an offline-generated lookup table
 *      (tools/gen_vu_segment_table.py -> fw/vu_segment_table.h) instead of a runtime log10 (spec section 2.3).
 *   2. vum_coalesce() -- the "always at most 3 rects" zone-coalescing rule (spec section 4.2): the lit segments
 *      are always a contiguous run from segment 1, and each colour zone (green/yellow/red) is a fixed contiguous
 *      range, so the lit portion always draws as at most one rect per zone that has a lit segment in it.
 */
#ifndef TAU_VU_MASTER_CORE_H
#define TAU_VU_MASTER_CORE_H
#include <stdint.h>

#define VUM_SEGMENTS         24u   /* -48..0 dBFS, 2 dB/segment (spec section 2.3) */
#define VUM_ZONE_GREEN_MAX   16u   /* segments 1..16: green (-48..-16 dBFS)  */
#define VUM_ZONE_YELLOW_MAX  21u   /* segments 17..21: yellow (-16..-6 dBFS) */
                                   /* segments 22..24: red (-6..0 dBFS)      */
#define VUM_TABLE_N          256u  /* fw/vu_segment_table.h: 256 entries over peak's 16-bit range, indexed by >>7 */

/* peak is the MTR_HEADROOM-scaled 16-bit accumulator (0..32767ish); table[] is vu_db_to_segment[VUM_TABLE_N]
 * from fw/vu_segment_table.h, generated offline by tools/gen_vu_segment_table.py from
 * db = 20*log10(peak/32768), clamped -48..0, lit = round((db+48)/2). table_n is passed rather than assumed so
 * the host test can exercise a table of any size it builds. */
static inline uint32_t vum_peak_to_segments(uint32_t peak, const uint8_t *table, uint32_t table_n)
{
    uint32_t idx = peak >> 7;
    if (idx >= table_n) idx = table_n - 1u;
    return table[idx];
}

/* Zone id of 1-based segment position seg (1..VUM_SEGMENTS): 0 green, 1 yellow, 2 red. */
static inline uint32_t vum_zone_of(uint32_t seg)
{
    if (seg <= VUM_ZONE_GREEN_MAX) return 0u;
    if (seg <= VUM_ZONE_YELLOW_MAX) return 1u;
    return 2u;
}

/* Coalesces `lit` (0..VUM_SEGMENTS, clamped) into at most 3 [start, start+count) spans, one per zone that has at
 * least one lit segment, left to right (green then yellow then red). Writes into zone_out/start_out/count_out
 * (each capacity 3, zone_out values as vum_zone_of()'s) and returns the span count 0..3. This is the one place
 * the "at most 3 rects" rule is decided; both the firmware draw and the host test call it. */
static inline uint32_t vum_coalesce(uint32_t lit, uint32_t zone_out[3], uint32_t start_out[3], uint32_t count_out[3])
{
    if (lit > VUM_SEGMENTS) lit = VUM_SEGMENTS;
    const uint32_t bounds[4] = { 0u, VUM_ZONE_GREEN_MAX, VUM_ZONE_YELLOW_MAX, VUM_SEGMENTS };
    uint32_t n = 0;
    for (uint32_t z = 0; z < 3u; z++) {
        uint32_t lo = bounds[z], hi = bounds[z + 1u];
        uint32_t seg_lo = lo, seg_hi = (lit < hi) ? lit : hi;
        if (seg_hi > seg_lo) {
            zone_out[n] = z; start_out[n] = seg_lo; count_out[n] = seg_hi - seg_lo;
            n++;
        }
    }
    return n;
}
#endif
