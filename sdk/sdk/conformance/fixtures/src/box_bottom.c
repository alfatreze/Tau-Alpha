/* draws below its box */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { mtr_rect(in->x, in->y + in->h - 4u, 8, 8, in->bg); }
TAU_METER(draw)
