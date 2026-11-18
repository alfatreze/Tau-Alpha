/* the manifest of this fixture declares a default outside its range */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
