/* reads a hardware register directly (the cycle counter) instead of using the host table */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { volatile uint32_t *r = (volatile uint32_t *)0x8000000Cu; mtr_rect(in->x, in->y, 1u + (*r & 3u), 4, in->bg); }
TAU_METER(draw)
