/* Chladni as a loadable meter pack: meter_chladni.c included unchanged under P2_PACK (see meter_pack_winamp_bars.c). The figure maths (chladni_core.h) and its tables are in the slot; the figure state (about
 * 2.7 KB) is the working state in the on-chip scratch. The plane is written through the host's mailbox calls and drawn with its scaled blit. Build: tools/player2/pack_meter.py chladni. */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../platform/mmio.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#define CHL_DATA
#include "../cores/chladni_core.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)       (g_cmds++, g_api->rect(__VA_ARGS__))
#define gfx_text(...)       (g_cmds++, g_api->text(__VA_ARGS__))
#define gfx_mb_write(a, d)  g_api->mb_write((a), (d))
#define gfx_mb_read(a, d)   g_api->mb_read((a), (d))
#define gfx_fence()         g_api->fence()
#define gfx_blit(...)       (g_cmds++, g_api->blit(__VA_ARGS__))
#define gfx_blit_abs(...)   (g_cmds++, g_api->blit_abs(__VA_ARGS__))
#define gfx_sblit(...)      (g_cmds++, g_api->sblit(__VA_ARGS__))
#undef  th_role
#define th_role             g_api->role
#include "../generated/desc_chladni.h"          /* the meter descriptor this pack carries (section .mtr_desc, tools/player2/meter_desc.py) */
#include "meter_chladni.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u; live_v = api->params;
    ch_load(api->params);
    meter_chladni_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
