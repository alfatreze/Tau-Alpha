/* Winamp Scope as a loadable meter pack: meter_winamp_scope.c included unchanged under P2_PACK (see meter_pack_winamp_bars.c). It reads the capture block's registers directly (hardware addresses from
 * platform/mmio.h) and draws through the host table, including the blended trail. Build: tools/player2/pack_meter.py winamp_scope. */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../platform/mmio.h"
#include "../generated/meter_presets.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)         (g_cmds++, g_api->rect(__VA_ARGS__))
#define gfx_rect_stash(...)   (g_cmds++, g_api->rect_stash(__VA_ARGS__))
#define gfx_blend_ready()     g_api->blend_ready()
#define gfx_blend_on(a)       g_api->blend_on(a)
#define gfx_blend_blit(...)   (g_cmds++, g_api->blend_blit(__VA_ARGS__))
#define gfx_blend_off()       g_api->blend_off()
#undef  th_role
#define th_role               g_api->role
#include "../generated/desc_winamp_scope.h"          /* the meter descriptor this pack carries (section .mtr_desc, tools/player2/meter_desc.py) */
#include "meter_winamp_scope.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u;
    ws_load(api->params);
    meter_winamp_scope_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
