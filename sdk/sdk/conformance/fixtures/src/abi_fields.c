/* reads an ABI 1.1 field (rms_l) but the manifest says abi 1.0 */
#include <tau_meter.h>
static void draw(const mtr_in_t *in) { mtr_rect(in->x, in->y, 2u + (in->rms_l >> 12), 4, in->bg); }
TAU_METER(draw)
