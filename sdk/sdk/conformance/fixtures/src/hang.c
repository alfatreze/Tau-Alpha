/* works for three pictures, then never returns */
#include <tau_meter.h>
static uint32_t n;
static void draw(const mtr_in_t *in) { mtr_rect(in->x, in->y, 4, 4, in->bg); if (++n > 3u) for (;;) { __asm__ volatile(""); } }
TAU_METER(draw)
