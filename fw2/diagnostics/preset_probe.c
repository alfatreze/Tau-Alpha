#pragma GCC optimize ("Os")
#include "preset_probe.h"
#include "../stream/slot.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../transport/mp3play.h"
extern char _tag_start;
#define TAG_OFF ((uint32_t)(uintptr_t)&_tag_start)
#ifndef TAGB                                         /* the host test points it at a buffer */
#define TAGB    ((const volatile uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_tag_start))
#endif
#define PP_SLOT 7u                                   /* the cover's slot: the measurement needs the same kind of open and read, not a slot of its own (the real feature will add one in data.json) */
#define PP_LEN 200u

uint32_t pp_runs, pp_ok, pp_bad, pp_err, pp_open_cyc, pp_open_max, pp_read_cyc, pp_read_max, pp_fill_min = 0xFFFFFFFFu, pp_dry;
static char path[96]; static uint8_t path_built;

/* "/Assets/tau_dev/alfatreze.TAU DEV <n>/preset-probe.bin": <n> is the number in the stamped version text ("0.6.1-dev.212+..."): the packager names the core folder the same way. */
P2_COLD_TEXT static int build_path(void)
{
    const volatile char *v = p2_ver_field + 7; uint32_t i = 0;
    while (v[i] && !(v[i] == 'd' && v[i + 1] == 'e' && v[i + 2] == 'v' && v[i + 3] == '.')) i++;
    if (!v[i]) return 0;
    i += 4; char num[8]; uint32_t n = 0; while (v[i] >= '0' && v[i] <= '9' && n < 7u) num[n++] = v[i++];
    if (!n) return 0;
    static const char a[] = "/Assets/tau_dev/alfatreze.TAU DEV ", b[] = "/preset-probe.bin"; uint32_t k = 0;
    for (uint32_t j = 0; a[j]; j++) path[k++] = a[j];
    for (uint32_t j = 0; j < n; j++) path[k++] = num[j];
    for (uint32_t j = 0; b[j]; j++) path[k++] = b[j];
    path[k] = 0; return 1;
}

P2_COLD_TEXT void preset_probe_run(void)          /* cold code: it runs from the PSRAM image, so it costs no hot RAM (the image is within a few hundred bytes of the free-RAM floor) */
{
    if (!path_built) { path_built = 1; if (!build_path()) pp_err = 1; }
    if (pp_err == 1u) return;
    mp3play_io_quiesce();
    const aout_snap_t s0 = aout_snap(); const uint32_t fill = aout_fill(&s0), dry0 = aout_stats.dry + aout_stats.under_ring;
    pp_runs++; if (fill < pp_fill_min) pp_fill_min = fill;
    const uint32_t t0 = cycles();
    if (!slot_open_path(PP_SLOT, path)) { pp_err = 2u; pp_open_cyc = cycles() - t0; if (pp_open_cyc > pp_open_max) pp_open_max = pp_open_cyc; return; }
    const uint32_t t1 = cycles(); pp_open_cyc = t1 - t0; if (pp_open_cyc > pp_open_max) pp_open_max = pp_open_cyc;
    if (!slot_read_blocking(PP_SLOT, 0u, TAG_OFF, PP_LEN, CLK_HZ * 2u)) { pp_err = 3u; pp_read_cyc = cycles() - t1; if (pp_read_cyc > pp_read_max) pp_read_max = pp_read_cyc; return; }
    pp_read_cyc = cycles() - t1; if (pp_read_cyc > pp_read_max) pp_read_max = pp_read_cyc;
    int good = TAGB[0] == 'T' && TAGB[1] == 'P' && TAGB[2] == 'R' && TAGB[3] == 'B';        /* "TPRB", then byte i = (i * 37 + 11) & 255 */
    for (uint32_t i = 4; i < PP_LEN && good; i++) if (TAGB[i] != (uint8_t)(i * 37u + 11u)) good = 0;
    if (good) pp_ok++; else pp_bad++;
    pp_dry += aout_stats.dry + aout_stats.under_ring - dry0;
}
