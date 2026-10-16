#include "helios.h"
#include "../platform/mmio.h"

#define R_SCAN 0x800000E8u                  /* B-267 beam position: bit 9 = present, bits 8:0 = the video line counter */
hl_t helios;
uint32_t helios_beam;

void helios_init(void) { helios_beam = (REG_RD(R_SCAN) >> 9) & 1u; }
static int beam_safe(void *ctx, uint32_t y0, uint32_t y1) { (void)ctx; return hl_rows_safe((int)helios_beam, REG_RD(R_SCAN) & 0x1FFu, y0, y1); }
uint8_t helios_region(hl_redraw_fn fn, uint32_t y0, uint32_t y1) { return hl_add(&helios, fn, (uint16_t)y0, (uint16_t)y1); }
void helios_dirty(uint8_t id) { hl_dirty(&helios, id); }
void helios_dirty_mask(uint32_t mask) { hl_dirty_mask(&helios, mask); }
void helios_flush(void) { hl_flush(&helios, beam_safe, 0); }
