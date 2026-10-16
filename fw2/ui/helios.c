#include "helios.h"
#include "../platform/mmio.h"
#include "../gfx/gfx.h"

#define R_SCAN 0x800000E8u                  /* B-267 beam position: bit 9 = present, bits 8:0 = the video line counter */
hl_t helios;
uint32_t helios_beam;

void helios_init(void) { helios_beam = (REG_RD(R_SCAN) >> 9) & 1u; }
static int beam_safe(void *ctx, uint32_t y0, uint32_t y1) { (void)ctx; return hl_rows_safe((int)helios_beam, REG_RD(R_SCAN) & 0x1FFu, y0, y1); }
uint8_t helios_region(hl_redraw_fn fn, uint32_t y0, uint32_t y1) { return hl_add(&helios, fn, (uint16_t)y0, (uint16_t)y1); }
void helios_dirty(uint8_t id) { hl_dirty(&helios, id); }
void helios_dirty_mask(uint32_t mask) { hl_dirty_mask(&helios, mask); }
/* A pass with a whole-screen region in it is drawn into the idle buffer (every dirty region, whatever the beam is doing) and flipped in at the next vblank, so a full repaint is never seen half done; any other
 * pass draws straight onto the displayed buffer, only the spans the beam is not in. Without a second buffer (or while a flip is pending) it is the beam-aware path. */
void helios_flush(void)
{
    if (hl_any_whole_dirty(&helios)) {
        const int a = gfx_frame_begin();
        if (a) { hl_flush(&helios, hl_all_safe, 0); gfx_frame_end(a); return; }
    }
    hl_flush(&helios, beam_safe, 0);
}
void helios_flush_now(void) { hl_flush(&helios, hl_all_safe, 0); }               /* for the view layer, inside its own frame */
