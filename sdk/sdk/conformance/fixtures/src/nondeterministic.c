/* uses an uninitialised local: the picture depends on what was on the stack */
#include <tau_meter.h>
static void draw(const mtr_in_t *in)
{
    volatile uint32_t junk[8];
    mtr_rect(in->x, in->y, 2u + (junk[3] & 7u), 4, in->bg);
}
TAU_METER(draw)
