#pragma GCC optimize ("Os")
#include "xfade.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#define XF_N 5u
#define XF_STEP (CLK_HZ / 50u)                               /* 20 ms between steps */
static uint32_t step, t_last;
void xfade_request(void)
{
    if (gfx_cur_buf() == gfx_front_buf()) return;            /* not inside a frame: the page is drawn straight onto the screen */
    if (!gfx_blend_ready()) return;
    gfx_hold_next();
}
int xfade_active(void) { return gfx_held(); }
void xfade_finish(void) { if (!gfx_held()) return; gfx_xfade_step(256u); gfx_xfade_end(); step = 0; }
void xfade_tick(void)
{
    if (!gfx_held()) { step = 0; return; }
    const uint32_t now = cycles();
    if (step && (uint32_t)(now - t_last) < XF_STEP) return;
    t_last = now;
    const uint32_t a = (step + 1u >= XF_N) ? 256u : 256u / (XF_N - step);     /* 51, 64, 85, 128, then the copy */
    gfx_xfade_step(a);
    if (++step >= XF_N) { gfx_xfade_end(); step = 0; }
}
