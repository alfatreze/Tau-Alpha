/* Winamp Bars as a loadable meter pack (docs/features/meters/METER_PACKS.md). The meter's own source, fw/winamp_bars.inc, is included unchanged (fw/player.c includes the same
 * file for the built-in meter): the firmware services it uses (the clipped rectangle and bar fills, whether the bar opcode exists, the accent and theme roles, its setting
 * values, the repaint flag) are redirected to the host table the entry point receives. Its working state is ordinary .data/.bss, which the link script places in the on-chip
 * meter scratch area. Build: tools/pack_meter.py winamp_bars. */
#include <stdint.h>
#define COLD_FN3
#define COLD_DATA
#define WAVE_COLS 64u
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter_pack.h"
#include "meters_gen.h"
#include "meter_core.h"

#define SPEC_BANDS     16u
#define WVIZ_BANDS_MIN 4u
#define WVIZ_BANDS_MAX 16u

static const mtr_host_api_t *g_api;
#undef  fig_rect
#undef  fig_bar
#define fig_rect(...)      g_api->rect_clip(__VA_ARGS__)
#define fig_bar(...)       g_api->bar_clip(__VA_ARGS__)
#define blit_probe_ensure() ((void)0)               /* the host probes once, before the pack ever runs */
#define BLIT_READY()       g_api->bar_ready()
#define ui_accent          (*g_api->accent)
#undef  th_role
#define th_role            g_api->role
#define wviz_force         (*g_api->force)
#undef  MV_WINAMP_BARS
#define MV_WINAMP_BARS(name) (g_api->params[MP_WINAMP_BARS_##name])

#include "winamp_bars.inc"

/* The compiler may call these even in freestanding code; the pack carries its own so it names no firmware symbol. */
void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api;
    wviz_bars_tick(in);
    return 1u;
}
