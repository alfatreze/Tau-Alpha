/* the control: a meter that obeys every rule (the template's drawing, shortened) */
#include <tau_meter.h>
static void draw(const mtr_in_t *in)
{
    if (in->force) mtr_rect(in->x, in->y, in->w, in->h, in->bg);
    for (uint32_t b = 0; b < MTR_BANDS; b++) { uint32_t w = in->w / MTR_BANDS; mtr_bar(in->x + b * w, in->y, w - 1u, in->h, (uint32_t)in->spec[b] * in->h / 255u, mtr_role(MTR_ROLE_ACCENT), in->bg); }
}
TAU_METER(draw)
