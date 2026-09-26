/* ==================================================================== HELIOS RECT SUBTRACT
 * Pure geometry: given a fill rectangle R and a hole H, produce the (up to 4) non-overlapping
 * rectangles that cover exactly R minus H. No MMIO, no framebuffer access -- portable and host-
 * testable (tools/host/helios_rect_harness.c, sim/test_helios_rect.py), same pattern as
 * fw/chladni_core.h / fw/library_core.h.
 *
 * Used by fw/helios.inc's helios_fill_excl() to let a meter's fill calls skip a reserved area (an
 * overlay like the fullscreen CPU% label) instead of drawing over it and relying on the overlay to
 * repaint itself every frame -- see docs/HELIOS_SPEC.md section 4 and the fullscreen label glitch
 * this was built for.
 *
 * The decomposition (top strip, bottom strip, left-middle strip, right-middle strip) is the
 * standard rect-minus-rect split: at most 4 pieces, each full R.w or exactly I.h tall, never
 * overlapping, degenerate (zero-area) pieces omitted. If R and H do not overlap, R itself is the
 * one output piece; if H fully covers R, there are zero output pieces.
 */
#ifndef TAU_HELIOS_RECT_H
#define TAU_HELIOS_RECT_H

#include <stdint.h>

typedef struct { int32_t x, y, w, h; } helios_rect_t;

/* out must have room for 4 rects. Returns the count written (0..4). */
static int helios_rect_subtract(int32_t rx, int32_t ry, int32_t rw, int32_t rh,
                                 int32_t hx, int32_t hy, int32_t hw, int32_t hh,
                                 helios_rect_t *out)
{
    if (rw <= 0 || rh <= 0) return 0;

    /* Intersection of R and H, clamped to R's own bounds. */
    int32_t ix0 = hx > rx ? hx : rx;
    int32_t iy0 = hy > ry ? hy : ry;
    int32_t ix1 = (hx + hw) < (rx + rw) ? (hx + hw) : (rx + rw);
    int32_t iy1 = (hy + hh) < (ry + rh) ? (hy + hh) : (ry + rh);

    if (ix1 <= ix0 || iy1 <= iy0) {                 /* no overlap: R passes through untouched */
        out[0].x = rx; out[0].y = ry; out[0].w = rw; out[0].h = rh;
        return 1;
    }

    int n = 0;
    if (iy0 > ry) {                                  /* top strip: full width, above the hole */
        out[n].x = rx; out[n].y = ry; out[n].w = rw; out[n].h = iy0 - ry; n++;
    }
    if (iy1 < ry + rh) {                              /* bottom strip: full width, below the hole */
        out[n].x = rx; out[n].y = iy1; out[n].w = rw; out[n].h = (ry + rh) - iy1; n++;
    }
    if (ix0 > rx) {                                   /* left-middle strip: just the hole's row band */
        out[n].x = rx; out[n].y = iy0; out[n].w = ix0 - rx; out[n].h = iy1 - iy0; n++;
    }
    if (ix1 < rx + rw) {                              /* right-middle strip: just the hole's row band */
        out[n].x = ix1; out[n].y = iy0; out[n].w = (rx + rw) - ix1; out[n].h = iy1 - iy0; n++;
    }
    return n;
}

#endif /* TAU_HELIOS_RECT_H */
