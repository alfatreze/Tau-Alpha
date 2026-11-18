/* calls scroll_left (an ABI 1.2 host-table entry) with the manifest saying abi 1.0 */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { mtr_scroll_left(in->x, in->y, in->w, in->h, 1); }
TAU_METER(draw)
