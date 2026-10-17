/* Host check of the Player 2 icon drawing (fw2/gfx/icon.c with fw2/cores/icon_core.h) against the engine model (sim/p2_engine_model.h). argv: scenario. Prints "ok name" / "FAIL name". */
#include "p2_engine_model.h"
#include "../fw2/platform/platform.h"
caps_t caps;
#include "../fw2/gfx/gfx.c"
#include "../fw2/generated/icon_data.c"
#include "../fw2/gfx/icon.c"
static int fails;
/* The model executes one command per read of the cycle counter and a fence times out after 50 ms of model time (about 1,100 commands): the first draw of an icon queues about 2,700 plane rectangles (a few ms on the
 * real engine), so the host test lets the model run the queue dry itself before it fences. */
static void settle(void) { em_drain(); gfx_fence(); }
static void chk(const char *name, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", name); if (!ok) fails++; }
/* The expected colour, written independently of ti_colour: the background blended toward the foreground by c / 15 per channel, rounded to nearest. */
static uint16_t exp_colour(uint16_t bg, uint16_t fg, uint32_t c)
{
    const int sh[3] = { 11, 5, 0 }, mx[3] = { 31, 63, 31 }; uint32_t out = 0;
    for (int k = 0; k < 3; k++) { const int b = (bg >> sh[k]) & mx[k], f = (fg >> sh[k]) & mx[k]; const int v = b + ((f - b) * (int)c * 2 + (f >= b ? 15 : -15)) / 30; out |= (uint32_t)v << sh[k]; }
    return (uint16_t)out;
}
static uint32_t mismatches(uint32_t buf, uint32_t id, uint32_t x0, uint32_t y0, uint16_t fg, uint16_t bg)
{
    ti_icon_t ic; if (ti_find(icon_rec, icon_rec_len, id, &ic)) return 99999u;
    uint32_t bad = 0;
    for (uint32_t y = 0; y < ic.h; y++) for (uint32_t x = 0; x < ic.w; x++) if (em_px(buf, x0 + x, y0 + y) != exp_colour(bg, fg, ti_cov(&ic, x, y))) bad++;
    return bad;
}
int main(int argc, char **argv)
{
    const int dbuf = argc > 1 && argv[1][0] == 'd';                 /* a fresh process per scenario (the graphics layer keeps shadows of the engine's registers) */
    em_reset(); caps.dbuf = (uint8_t)dbuf;
    gfx_rect(0, 0, 400, 360, 0x1111); settle();
    if (!dbuf) {
        chk("the record is usable (magic, CRC, sizes)", icon_draw(ICON_PLAY, 20, 20, 0xFFFF, 0x0000) == 0);
        settle();
        chk("play, white on black: every pixel is the table colour of its coverage", mismatches(0, ICON_PLAY, 20, 20, 0xFFFF, 0x0000) == 0);
        icon_draw(ICON_SHUFFLE, 60, 20, 0xF800, 0x001F); settle();
        chk("shuffle in red on blue: a different table is loaded at every draw", mismatches(0, ICON_SHUFFLE, 60, 20, 0xF800, 0x001F) == 0);
        icon_draw(ICON_PLAY, 120, 20, 0x07E0, 0x0000); settle();
        chk("the first icon drawn again with other colours after another one: still exact (nothing stale)", mismatches(0, ICON_PLAY, 120, 20, 0x07E0, 0x0000) == 0 && mismatches(0, ICON_SHUFFLE, 60, 20, 0xF800, 0x001F) == 0);
        chk("every icon of the record draws without a failure", ({ int e = 0; for (uint32_t id = 0; id < ICON_COUNT; id++) e += icon_draw(id, 10, 100, 0xFFFF, 0x0000); e == 0; }));
        chk("an unknown id is refused and draws nothing", icon_draw(99, 10, 200, 0xFFFF, 0x0000) == 1);
        chk("widths: play 12, pause 17, vol3 26, unknown 0", icon_width(ICON_PLAY) == 12 && icon_width(ICON_PAUSE) == 17 && icon_width(ICON_VOL3) == 26 && icon_width(99) == 0);
    } else {
        int act = gfx_frame_begin(); icon_draw(ICON_PAUSE, 30, 40, 0xFFFF, 0x0000); settle();
        chk("with double buffering the icon lands in the buffer being drawn into, not the displayed one", act == 1 && mismatches(1, ICON_PAUSE, 30, 40, 0xFFFF, 0x0000) == 0 && em_px(0, 35, 50) == 0x1111);
        gfx_frame_end(act);
    }
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails != 0;
}
