/* 60 draw commands a tick against a ceiling of 24 */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { for (uint32_t i = 0; i < 60u; i++) mtr_rect(in->x + (i % 8u), in->y + (i / 8u), 1, 1, in->bg); }
TAU_METER(draw)
