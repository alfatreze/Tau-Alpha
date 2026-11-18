/* draws 4 pixels left of its box */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { mtr_rect(in->x - 4u, in->y, 8, 8, in->bg); }
TAU_METER(draw)
