/* 6 KB of static data: the scratch area all meters share is 4 KB */
#include <tau_meter.h>
static volatile uint8_t big[6000];
static void draw(const mtr_in_t *in) { big[in->frame % 6000u] = 1; mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
