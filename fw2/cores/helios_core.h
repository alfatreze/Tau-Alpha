/* fw2/cores/helios_core.h -- Helios, the Player 2 UI layer's logic (docs/features/HELIOS_SPEC.md sections 2, 4, 11). Pure: no registers, no drawing. Host-tested (sim/test_p2_helios.py).
 *
 * 1. BEAM-AWARE DRAWING. The scanout reads each row from SDRAM ONE LINE BEFORE it is shown and moves down one row every ~41.7 us. Drawing a region while the beam is inside it shows half old, half new
 *    content for a frame (a tear). A row span is safe to draw when (a) the beam is in vertical blanking, (b) the beam has already passed it (it shows the finished picture on the NEXT frame), or (c) it is
 *    entirely AHEAD of the beam by more than the prefetch line plus a small lead (the draw engine writes about 8x faster than the beam moves). Without a beam counter in the bitstream everything is safe.
 * 2. DIRTY REGIONS. A region is a redraw callback with the rows it paints. Anything that changes what a region shows marks it dirty; one flush per loop pass draws the dirty regions whose rows are
 *    safe and leaves the others dirty for the next pass. A region with no stated extent (y1 == 0xFFFF, a full repaint) is drawn at once: it takes tens of ms against a 1.7 ms blanking interval, so waiting
 *    would only delay it (tearing there needs double buffering, section 5).
 * 3. WRAPPING INDEX: the one rule every list uses (Up on the first row goes to the last).
 * VOFF/V_ACT mirror src/fpga/core/mp3_fb.sv (VOFF 4, V_ACT 360) and must be kept in step with it. */
#ifndef HELIOS_CORE_H
#define HELIOS_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define HL_VOFF 4u
#define HL_VACT 360u
#define HL_LEAD 3u                      /* lines of margin ahead of the prefetch line */
#define HL_MAX_REGIONS 16u
#define HL_WHOLE 0xFFFFu                /* y1 for a region with no stated extent */

/* vc: the video line counter (0 when outside the picture area counts as blanking); beam_ok: the bitstream has a beam counter. y0 inclusive, y1 exclusive, in framebuffer rows. */
static inline int hl_rows_safe(int beam_ok, uint32_t vc, uint32_t y0, uint32_t y1)
{
    if (!beam_ok) return 1;
    if (vc < HL_VOFF || vc >= HL_VOFF + HL_VACT) return 1;                      /* (a) vertical blanking */
    uint32_t by = vc - HL_VOFF;                                                 /* the row being scanned now */
#if BUG == 1
    if (y1 <= by + 2u) return 1;                                                /* passed: one row too eager (the prefetched row is still being read) */
#else
    if (y1 <= by + 1u) return 1;                                                /* (b) already passed or prefetched */
#endif
#if BUG == 2
    if (y0 >= by + 2u) return 1;                                                /* ahead: no lead */
#else
    if (y0 >= by + 2u + HL_LEAD) return 1;                                      /* (c) safely ahead */
#endif
    return 0;
}

/* The one wrap rule. idx is the current row, count the number of rows, up = moving towards the first. */
static inline uint32_t hl_wrap(uint32_t idx, uint32_t count, int up)
{
    if (!count) return 0;
#if BUG == 3
    return up ? (idx ? idx - 1u : 0u) : (idx + 1u < count ? idx + 1u : count - 1u);     /* clamps instead of wrapping */
#else
    return up ? (idx ? idx - 1u : count - 1u) : (idx + 1u < count ? idx + 1u : 0u);
#endif
}

typedef void (*hl_redraw_fn)(void);
typedef struct { hl_redraw_fn redraw; uint16_t y0, y1; uint8_t dirty; } hl_region_t;
typedef struct { hl_region_t r[HL_MAX_REGIONS]; uint8_t n; uint32_t drawn, deferred; } hl_t;

/* Setup time only. Returns the region id, or 0xFF when full (the caller's region then simply never draws: never a crash). */
static inline uint8_t hl_add(hl_t *h, hl_redraw_fn fn, uint16_t y0, uint16_t y1)
{
    if (h->n >= HL_MAX_REGIONS) return 0xFFu;
    uint8_t id = h->n++;
    h->r[id].redraw = fn; h->r[id].y0 = y0; h->r[id].y1 = y1; h->r[id].dirty = 0;
    return id;
}
static inline void hl_dirty(hl_t *h, uint8_t id) { if (id < h->n) h->r[id].dirty = 1u; }
/* Forgets every pending redraw (a view change: what the old view still wanted drawn must not land on the new view). */
static inline void hl_clear(hl_t *h) { for (uint32_t i = 0; i < h->n; i++) h->r[i].dirty = 0u; }
static inline void hl_dirty_mask(hl_t *h, uint32_t mask) { for (uint32_t i = 0; i < h->n; i++) if (mask & (1u << i)) h->r[i].dirty = 1u; }
/* True when a dirty region repaints the whole screen (it cannot be kept off the beam): such a pass is drawn into the idle buffer and flipped. */
static inline int hl_any_whole_dirty(const hl_t *h)
{
    for (uint32_t i = 0; i < h->n; i++) if (h->r[i].dirty && h->r[i].y1 == HL_WHOLE) return 1;
    return 0;
}
static inline int hl_all_safe(void *ctx, uint32_t y0, uint32_t y1) { (void)ctx; (void)y0; (void)y1; return 1; }       /* drawing into a buffer nobody sees: every span is safe */
/* One pass: every dirty region whose rows are safe is drawn (and cleared before its callback runs, so the callback may mark itself dirty again); the rest stay dirty. `safe` answers for a row span. */
static inline uint32_t hl_flush(hl_t *h, int (*safe)(void *ctx, uint32_t y0, uint32_t y1), void *ctx)
{
    uint32_t drawn = 0;
    for (uint32_t i = 0; i < h->n; i++) {
        if (!h->r[i].dirty) continue;
        if (h->r[i].y1 != HL_WHOLE
#if BUG == 4
            && 0
#endif
            ) {
            if (!safe(ctx, h->r[i].y0, h->r[i].y1)) { h->deferred++; continue; }
            h->drawn++;
        }
        h->r[i].dirty = 0u;
        h->r[i].redraw();
        drawn++;
    }
    return drawn;
}
#endif
