/* Host check of the Player 2 graphics layer (fw2/gfx/gfx.c) against the engine model (sim/p2_engine_model.h). argv: scenario name. Prints "ok name" / "FAIL name" lines. */
#include "p2_engine_model.h"
#include "../fw2/platform/platform.h"
caps_t caps;
#include "../fw2/gfx/gfx.c"
static int fails;
static void chk(const char *name, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", name); if (!ok) fails++; }
static void fill_front(uint16_t c) { gfx_rect(0, 0, 400, 360, c); gfx_fence(); }
static uint16_t blendpx(uint16_t s, uint16_t d, uint32_t a) { return em_blend_px(s, d, a); }

static void sc_bracket(void)
{
    em_reset(); caps.dbuf = 1; fill_front(0x1111);
    const int act = gfx_frame_begin();
    chk("begin picks the idle buffer", act == 1 && gfx_cur_buf() == 1 && em_cpu == 1);
    gfx_fence(); chk("the idle buffer starts as a copy of the displayed one", em_px(1, 200, 200) == 0x1111 && em_px(1, 399, 359) == 0x1111);
    gfx_rect(10, 10, 50, 50, 0x2222); gfx_fence();
    for (uint32_t k = 0; k < 40u; k++) gfx_rect(100 + k, 100, 1, 200, 0x2222);          /* a long queue at the moment the flip is requested */
    chk("while drawing, the displayed buffer is untouched and the idle one has the picture", em_px(0, 20, 20) == 0x1111 && em_px(1, 20, 20) == 0x2222);
    gfx_frame_end(act);
    chk("the flip never lands with commands still queued", em_torn == 0);
    chk("after end the new picture is displayed and drawing goes there again", em_disp == 1 && em_cpu == 1 && em_px(em_disp, 20, 20) == 0x2222 && gfx_cur_buf() == 1);
    gfx_rect(100, 100, 10, 10, 0x3333); gfx_fence();
    chk("incremental drawing after the flip lands in the displayed buffer", em_px(1, 105, 105) == 0x3333);
    const int act2 = gfx_frame_begin(); gfx_rect(0, 0, 400, 360, 0x4444); gfx_frame_end(act2);
    chk("a second full redraw flips back", em_disp == 0 && em_px(0, 200, 200) == 0x4444 && em_cpu == 0);
}
static void sc_blitmode(void)
{
    em_reset(); caps.dbuf = 1; fill_front(0x1111);
    for (uint32_t i = 0; i < 4u; i++) gfx_mb_write(400u * 512u + i * 2u, (uint32_t)(0x20u + 2u * i + 1u) << 16 | (0x20u + 2u * i));   /* a stash plane of indices 0x20..0x27 */
    for (uint32_t i = 0; i < 256u; i++) { gfx_clut_begin(i); gfx_clut_put((uint16_t)(0x8000u + i)); }
    const int act = gfx_frame_begin();
    gfx_rect(0, 0, 400, 360, 0x0001);
    gfx_cblit(0, 400, 30, 30, 8, 1);
    gfx_blit(30, 30, 60, 30, 8, 1);
    gfx_fence();
    chk("a palette blit lands in the idle buffer, not the displayed one", em_px(1, 30, 30) == 0x8020 && em_px(1, 37, 30) == 0x8027 && em_px(0, 30, 30) == 0x1111);
    chk("a copy inside the box reads and writes the idle buffer", em_px(1, 60, 30) == 0x8020 && em_px(1, 67, 30) == 0x8027);
    gfx_sblit(0, 400, 100, 100, 4, 1, 3);
    gfx_fence();
    chk("a scaled blit reads the stash and writes the idle buffer", em_px(1, 100, 100) == 0x0020 && em_px(1, 103, 100) == 0x0021 && em_px(0, 100, 100) == 0x1111);
    gfx_frame_end(act);
    chk("all of it is displayed after the flip", em_px(em_disp, 30, 30) == 0x8020);
    gfx_cblit(0, 400, 200, 200, 8, 1); gfx_fence();
    chk("after the flip a palette blit lands in the displayed buffer", em_px(em_disp, 200, 200) == 0x8020);
}
static void sc_stash(void)
{
    em_reset(); caps.dbuf = 1;
    const int act = gfx_frame_begin();
    const uint32_t t0 = em_cycles;
    gfx_rect_stash(0, 600, 112, 4, 0x5555); gfx_fence();
    chk("stash rows are shared: a fence with the idle buffer as the target completes quickly", em_cycles - t0 < 400000u && em_px(0, 5, 601) == 0x5555);
    gfx_frame_end(act);
}
static void sc_compose(void)
{
    em_reset(); caps.dbuf = 1; fill_front(0x1111);
    chk("compose is possible with nothing pending", gfx_compose_begin(20, 100, 360, 100) == 1 && gfx_cur_buf() == 1);
    gfx_fence(); chk("the compose box starts as a copy of the shown one, outside it the idle buffer is untouched", em_px(1, 30, 110) == 0x1111 && em_px(1, 350, 190) == 0x1111 && em_px(1, 5, 5) == 0);
    gfx_rect(20, 100, 360, 100, 0x6666); gfx_rect(40, 120, 80, 60, 0x7777); gfx_fence();
    chk("the displayed buffer shows nothing of the compose yet", em_px(0, 50, 130) == 0x1111 && em_px(0, 30, 110) == 0x1111);
    gfx_compose_present(20, 100, 360, 100);
    chk("present copies the box onto the displayed buffer", em_px(0, 50, 130) == 0x7777 && em_px(0, 30, 110) == 0x6666 && em_px(0, 379, 199) == 0x6666);
    chk("present leaves everything outside the box alone and draws on the displayed buffer again", em_px(0, 10, 10) == 0x1111 && em_px(0, 20, 99) == 0x1111 && em_px(0, 380, 150) == 0x1111 && gfx_cur_buf() == 0 && em_cpu == 0);
    chk("a compose is refused while a flip is pending", (em_flip_pending = 1, gfx_compose_begin(20, 100, 360, 100) == 0));
}
static void sc_blend(void)
{
    em_reset(); caps.dbuf = 1;
    chk("the blend probe finds the blend", gfx_blend_ready() == 1);
    gfx_rect_stash(0, 600, 112, 100, 0x0841); gfx_rect(20, 100, 360, 100, 0xF81F); gfx_fence();
    gfx_blend_on(153);
    gfx_blend_blit(0, 600, 20, 100, 360, 100);
    gfx_blend_off();
    const uint16_t want = blendpx(0x0841, 0xF81F, 153);
    chk("a blend moves every pixel of the box toward the strip by the weight", em_px(0, 20, 100) == want && em_px(0, 379, 199) == want && em_px(0, 130, 150) == want && em_px(0, 19, 100) == 0 && em_px(0, 380, 100) == 0);
    gfx_blit(0, 600, 300, 300, 4, 1); gfx_fence();
    chk("a plain blit after blend off is a plain copy", em_px(0, 300, 300) == 0x0841);
    for (int k = 0; k < 12; k++) { gfx_blend_on(153); gfx_blend_blit(0, 600, 20, 100, 360, 100); gfx_blend_off(); }
    const uint16_t sr = 0x0841 >> 11;
    chk("repeated blends converge on the strip colour", (em_px(0, 100, 150) >> 11) <= sr + 1u);
}
static void sc_noblend(void)
{
    em_reset(); caps.dbuf = 1; em_has_blend = 0;
    chk("the blend probe says no on a bitstream without it", gfx_blend_ready() == 0);
}
static void sc_nodbuf(void)
{
    em_reset(); caps.dbuf = 0; em_has_dbuf = 0;
    chk("no double buffer: begin and compose say so", gfx_frame_begin() == 0 && gfx_compose_begin(0, 0, 10, 10) == 0);
    gfx_rect(5, 5, 10, 10, 0x1234); gfx_fence();
    gfx_cblit(0, 400, 30, 30, 8, 1); gfx_frame_end(0); gfx_fence();
    chk("drawing goes to buffer 0 and the registers were not touched", em_px(0, 8, 8) == 0x1234 && em_cpu == 0 && em_disp == 0 && em_nflips == 0);
}
int main(int argc, char **argv)
{
    const char *s = argc > 1 ? argv[1] : "bracket";
    if (!strcmp(s, "bracket")) sc_bracket(); else if (!strcmp(s, "blitmode")) sc_blitmode(); else if (!strcmp(s, "stash")) sc_stash();
    else if (!strcmp(s, "compose")) sc_compose(); else if (!strcmp(s, "blend")) sc_blend(); else if (!strcmp(s, "noblend")) sc_noblend(); else if (!strcmp(s, "nodbuf")) sc_nodbuf();
    return fails ? 1 : 0;
}
