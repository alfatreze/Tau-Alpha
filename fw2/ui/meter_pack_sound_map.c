/* SOUND MAP as a loadable meter pack: meter_sound_map.c included under P2_PACK (its real code exists only there), the services redirected to the host table the entry point receives, the descriptor the firmware reads. Scaffolded
 * by tools/player2/new_meter.py: see meter_pack_winamp_bars.c for the pattern. Build: tools/player2/pack_meter.py sound_map. */
#define MTR_PACK_NEEDS_MINOR 1                    /* reads mtr_in_t's audio statistics (ABI 1.1) */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)   (g_cmds++, g_api->rect(__VA_ARGS__))
#define gfx_bar(...)    (g_cmds++, g_api->bar(__VA_ARGS__))
#define gfx_text(...)   (g_cmds++, g_api->text(__VA_ARGS__))
#undef  th_role
#define th_role         g_api->role
#include "../generated/desc_sound_map.h"          /* the meter descriptor this pack carries (section .mtr_desc, tools/player2/meter_desc.py) */
#include "meter_sound_map.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u;
    smp_load(api->params);
    smp_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
