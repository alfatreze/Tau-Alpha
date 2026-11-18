/* writes into the structure the player passed (a read-only input) */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { ((mtr_in_t *)in)->x = 0; mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
