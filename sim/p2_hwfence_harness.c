/* Host check of the hardware fence path in fw2/gfx/gfx.c (Talos T3) against the engine model. argv: hw | old | stuck. Prints "ok name" / "FAIL name" lines. */
#include "p2_engine_model.h"
#include "../fw2/platform/platform.h"
caps_t caps;
#include "../fw2/gfx/gfx.c"
static int fails;
static void chk(const char *name, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", name); if (!ok) fails++; }
int main(int argc, char **argv)
{
    const char *sc = argc > 1 ? argv[1] : "hw";
    em_reset(); caps.dbuf = 0; em_has_dbuf = 0;
    if (!strcmp(sc, "old")) em_has_fence = 0;
    if (!strcmp(sc, "stuck")) em_fence_stuck = 1;
    gfx_rect(10, 10, 20, 5, 0x1234); gfx_rect(40, 40, 8, 8, 0x4321);
    for (uint32_t i = 0; i < 12u; i++) gfx_rect(200 + i * 4u, 200, 4, 4, 0x2000u + i);           /* more queued commands than the model drains while the firmware spins */
    const uint32_t cmds_before = gfx_cmds;
    gfx_fence();
    chk("the draws before the fence are complete when it returns", em_px(0, 10, 10) == 0x1234 && em_px(0, 29, 14) == 0x1234 && em_px(0, 47, 47) == 0x4321 && em_px(0, 203, 203) == 0x2000u && em_px(0, 247, 203) == 0x200Bu);
    if (!strcmp(sc, "hw")) {
        chk("hardware path: one FENCE command retired, the mailbox marker was not used", em_fence_n == 1 && em_executed == 14);
        chk("a fence is not a draw command (gfx_cmds unchanged)", gfx_cmds == cmds_before);
        gfx_rect(100, 100, 4, 4, 0x7777); gfx_fence(); gfx_fence();
        chk("three fences retired, the draw before the second is complete", em_fence_n == 3 && em_px(0, 103, 103) == 0x7777);
    } else if (!strcmp(sc, "old")) {
        chk("earlier bitstream: no FENCE command, the mailbox marker rect did the job", em_fence_n == 0 && em_executed == 15);
    } else {
        chk("stuck counter: the fence returned after its timeout (no hang)", 1);
        chk("stuck counter: the work is complete after the fallback, no later hardware fence is used", em_fence_n == 0 && hw_fence == 0);
        gfx_rect(100, 100, 4, 4, 0x7777); gfx_fence();
        chk("stuck counter: the next fence works through the mailbox", em_px(0, 103, 103) == 0x7777 && em_fence_n == 0);
    }
    return fails ? 1 : 0;
}
