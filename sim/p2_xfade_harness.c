/* Host check of the page crossfade (fw2/ui/xfade.c + the graphics layer + the engine model). Prints "ok name" / "FAIL name". argv: scenario. */
#include "p2_engine_model.h"
#include "../fw2/platform/platform.h"
caps_t caps;
#include "../fw2/gfx/gfx.c"
#include "../fw2/ui/xfade.c"
static int fails;
static void chk(const char *n, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", n); if (!ok) fails++; }
static uint32_t red(uint16_t v) { return v >> 11; }
static void sc_fade(void)
{
    em_reset(); caps.dbuf = 1;
    gfx_rect(0, 0, 400, 360, 0xF800); gfx_fence();                      /* the old page: all red */
    int a = gfx_frame_begin();
    xfade_request();
    gfx_rect(0, 0, 400, 360, 0x07E0); gfx_rect(50, 50, 100, 100, 0x001F);       /* the new page: green with a blue square */
    gfx_frame_end(a);
    chk("the new page is held: nothing flipped, the shown page is still the old one, drawing goes to the shown buffer", em_nflips == 0 && em_px(em_disp, 10, 10) == 0xF800 && gfx_held() && em_cpu == em_disp);
    chk("a second full redraw is refused while the fade holds the idle buffer", gfx_frame_begin() == 0 && gfx_compose_begin(0, 0, 10, 10) == 0);
    uint32_t prev_red = red(0xF800), steps = 0, mono = 1;
    while (xfade_active() && steps < 50u) {
        const uint32_t before = em_cycles; while (em_cycles - before < 1400000u) (void)host_rd(0x8000000Cu);
        xfade_tick(); gfx_fence(); steps++;
        const uint32_t r = red(em_px(em_disp, 10, 10)); if (r > prev_red) mono = 0; prev_red = r;
    }
    chk("the fade takes five steps and the red falls monotonically", steps == 5u && mono);
    chk("after the last step the shown page is exactly the new page, in every pixel checked", em_px(em_disp, 10, 10) == 0x07E0 && em_px(em_disp, 100, 100) == 0x001F && em_px(em_disp, 399, 359) == 0x07E0 && em_px(em_disp, 0, 0) == 0x07E0);
    chk("the hold is released and no flip ever happened", !gfx_held() && em_nflips == 0);
    chk("full redraws work again afterwards", gfx_frame_begin() == 1);
}
static void sc_mid(void)
{
    em_reset(); caps.dbuf = 1;
    gfx_rect(0, 0, 400, 360, 0xF800); gfx_fence();
    int a = gfx_frame_begin(); xfade_request(); gfx_rect(0, 0, 400, 360, 0x07E0); gfx_frame_end(a);
    em_cycles += 1400000u; xfade_tick(); gfx_fence();
    const uint16_t p = em_px(em_disp, 5, 5);
    chk("after one step the page is part way (not the old page, not the new one)", p != 0xF800 && p != 0x07E0);
    xfade_finish(); gfx_fence();
    chk("finish completes the fade at once, exactly", em_px(em_disp, 5, 5) == 0x07E0 && !gfx_held());
}
static void sc_none(void)
{
    em_reset(); caps.dbuf = 1; em_has_blend = 0;
    gfx_rect(0, 0, 400, 360, 0xF800); gfx_fence();
    int a = gfx_frame_begin(); xfade_request(); gfx_rect(0, 0, 400, 360, 0x07E0); gfx_frame_end(a);
    chk("without the blend there is no fade: the frame flips as usual", !gfx_held() && em_nflips == 1 && em_px(em_disp, 5, 5) == 0x07E0);
    xfade_request();
    chk("a request outside a frame does nothing", !gfx_held());
}
int main(int argc, char **argv)
{
    const char *s = argc > 1 ? argv[1] : "fade";
    if (!strcmp(s, "fade")) sc_fade(); else if (!strcmp(s, "mid")) sc_mid(); else if (!strcmp(s, "none")) sc_none();
    return fails ? 1 : 0;
}
