/* A minimal Tau Alpha meter: one bar per spectrum band, in the theme's accent colour. Uses only tau_meter.h (the host table). */
#include <tau_meter.h>

enum { P_GAIN, P_GAP };                       /* the order of "params" in meter.json */

static void draw(const mtr_in_t *in)
{
    const uint32_t bands = MTR_BANDS, gap = mtr_param(P_GAP);
    const uint32_t gain = mtr_param(P_GAIN);                     /* percent */
    if (in->w < bands * (1u + gap) || in->h < 4u) return;        /* a box too small to draw in: draw nothing, never outside it */
    const uint32_t bw = (in->w - gap * (bands - 1u)) / bands;    /* whole pixels per bar */
    const uint32_t used = bw * bands + gap * (bands - 1u), x0 = in->x + (in->w - used) / 2u;
    if (in->force) mtr_rect(in->x, in->y, in->w, in->h, in->bg);  /* the context changed: paint the whole box once */
    for (uint32_t b = 0; b < bands; b++) {
        uint32_t lit = (uint32_t)(in->paused ? 0u : in->spec[b]) * gain * in->h / (255u * 100u);
        if (lit > in->h) lit = in->h;
        mtr_bar(x0 + b * (bw + gap), in->y, bw, in->h, lit, mtr_role(MTR_ROLE_ACCENT), in->bg);
    }
}
TAU_METER(draw)
