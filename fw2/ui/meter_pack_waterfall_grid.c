/* Waterfall as a loadable meter pack: meter_waterfall_grid.c included under P2_PACK, where its real code exists (the firmware has only a stub): see meter_pack_winamp_bars.c. It draws with the framework's
 * methods through the host table: rectangles for the cells, gfx_scroll_down for the spectrogram's scroll and a palette blit (gfx_cblit from a one-row plane in the stash) for the entering row. Build: tools/player2/pack_meter.py waterfall_grid. */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/meter_core.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)         (g_cmds++, g_api->rect(__VA_ARGS__))
#define gfx_scroll_down(...)  (g_cmds++, g_api->scroll_down(__VA_ARGS__))
#define gfx_rect_stash(...)   (g_cmds++, g_api->rect_stash(__VA_ARGS__))
#define gfx_clut_load(...)    (g_api->clut_load(__VA_ARGS__))
#define gfx_cblit(...)        (g_cmds++, g_api->cblit(__VA_ARGS__))
#undef  th_role
#define th_role               g_api->role
#include "../generated/desc_waterfall_grid.h"          /* the meter descriptor this pack carries (section .mtr_desc, tools/player2/meter_desc.py) */
#include "meter_waterfall_grid.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u;
    wf_load(api->params);
    wf_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
