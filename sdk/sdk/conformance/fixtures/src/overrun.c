/* every picture takes far more than the 200,000 cycles it declares, but returns (a strike each, three strikes disable it) */
#include <tau_meter.h>
static void draw(const mtr_in_t *in)
{
    volatile uint32_t sink = 0;
    for (uint32_t i = 0; i < 100000u; i++) sink += i;      /* about 400,000 estimated cycles */
    mtr_rect(in->x, in->y, 4 + (sink & 1u), 4, in->bg);
}
TAU_METER(draw)
