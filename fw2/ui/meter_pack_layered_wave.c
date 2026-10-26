/* Layered Wave as a loadable meter pack: meter_layered_wave.c included unchanged under P2_PACK (see meter_pack_winamp_bars.c). Its 6 x 404-byte history ring is the pack's own PSRAM state (.pstate, in the slot after
 * the image, zeroed on activation and proven before first use); the rest of its working state is in the on-chip scratch. Build: tools/player2/pack_meter.py layered_wave. */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../platform/mmio.h"
#include "../generated/lw_presets.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)   (g_cmds++, g_api->rect(__VA_ARGS__))
#define gfx_text(...)   (g_cmds++, g_api->text(__VA_ARGS__))
#undef  th_role
#define th_role         g_api->role
#include "../generated/desc_layered_wave.h"          /* the meter descriptor this pack carries (section .mtr_desc, tools/player2/meter_desc.py) */
#include "meter_layered_wave.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u; lw_live = api->params;
    meter_layered_wave_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
