/* Winamp Bars as a loadable meter pack. The meter's own source, meter_winamp_bars.c, is included unchanged (the firmware compiles the same file for the built-in meter): P2_PACK leaves out its
 * firmware-only part (presets, the live parameter array) and the services it uses are redirected to the host table the entry point receives. Its working state is ordinary .data/.bss, which the
 * link script places in the on-chip meter scratch area. Build: tools/player2/pack_meter.py winamp_bars. */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirect below: its declaration must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/meter_core.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)   (g_cmds++, g_api->rect(__VA_ARGS__))
#undef  th_role
#define th_role         g_api->role
#include "meter_winamp_bars.c"

/* the compiler may call these even in freestanding code; the pack carries its own so it names no firmware symbol */
void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u;
    wb_load(api->params);
    meter_winamp_bars_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
