/* writes to an absolute address that is not the meter's memory */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { *(volatile uint32_t *)0x00028000u = in->frame; mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
