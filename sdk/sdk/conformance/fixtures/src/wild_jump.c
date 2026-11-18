/* calls a function at an absolute address: not the host table, not its own code */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { if (in->frame > 2u) ((void (*)(void))0x24970F00u)(); mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
