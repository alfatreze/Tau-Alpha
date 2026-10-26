/* Host run of fw2/diagnostics/blit_check.c against the engine model (sim/p2_engine_model.h): prints the failed-test mask and the bad-pixel count. See sim/test_p2_blit_check.py. */
#include "p2_engine_model.h"
#include "../fw2/platform/platform.h"
caps_t caps;
#include "../fw2/gfx/gfx.c"
#define BLIT_CHECK_BG 0x0842u
#include "../fw2/diagnostics/blit_check.c"
int main(void)
{
    em_reset(); caps.dbuf = 0; em_has_dbuf = 0;
    const uint32_t m = blit_check_run();
    printf("mask %u bad %u\n", m, blit_check_bad_px());
    return 0;
}
