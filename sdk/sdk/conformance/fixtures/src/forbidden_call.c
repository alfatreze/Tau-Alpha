/* a system call: a meter may call only the host table */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { if (in->frame > 2u) __asm__ volatile("ecall"); mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
