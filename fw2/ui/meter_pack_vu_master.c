/* Master VU as a loadable meter pack: meter_vu_master.c included unchanged under P2_PACK (see meter_pack_winamp_bars.c). Its tables (the dB-to-segment table, the tick marks) are read-only data in the slot. Build: tools/player2/pack_meter.py vu_master. */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/meter_core.h"
#include "../cores/vu_master_core.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)   (g_cmds++, g_api->rect(__VA_ARGS__))
#define gfx_text(...)   (g_cmds++, g_api->text(__VA_ARGS__))
#undef  th_role
#define th_role         g_api->role
#include "meter_vu_master.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u;
    vu_load(api->params);
    meter_vu_master_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
