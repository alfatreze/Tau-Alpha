/* writes single bytes into its PSRAM state (the player's notes say whole words only) */
#include <tau_meter.h>
TAU_PSTATE static volatile uint8_t hist[64];
static void draw(const mtr_in_t *in) { hist[in->frame & 63u] = (uint8_t)in->frame; mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
