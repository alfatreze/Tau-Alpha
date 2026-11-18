/* asks for 80 KB of PSRAM state: it cannot fit the 64 KiB window */
#include <tau_meter.h>
TAU_PSTATE static volatile uint32_t big[20000];
static void draw(const mtr_in_t *in) { big[in->frame % 20000u] = in->frame; mtr_rect(in->x, in->y, 4, 4, in->bg); }
TAU_METER(draw)
