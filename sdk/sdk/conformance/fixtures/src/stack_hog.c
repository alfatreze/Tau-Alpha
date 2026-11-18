/* a 4 KB local array: the player's stack is 5 KB */
#include <tau_meter.h>
static void draw(const mtr_in_t *in)
{
    volatile uint8_t buf[4096];
    for (uint32_t i = 0; i < 4096u; i += 64u) buf[i] = (uint8_t)in->frame;
    mtr_rect(in->x, in->y, 2u + (buf[0] & 1u), 4, in->bg);
}
TAU_METER(draw)
