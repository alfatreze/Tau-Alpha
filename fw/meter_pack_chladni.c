/* Chladni as a loadable meter pack (docs/features/meters/METER_PACKS.md). The meter's own source, fw/chladni.inc (and the portable maths in fw/chladni_core.h), is included
 * unchanged (fw/player.c includes the same files for the built-in meter). The firmware services it uses are redirected to the host table the entry point receives: the SDRAM
 * mailbox (the figure is written into an off-screen plane through it), the draw engine's blit and scaled blit, fence and wait, the sticky bases, the buffer base of the call,
 * the audio-yield test, the CPU percent, the toast, the accent, its setting values. Its working state (the 3 KB figure state, the row buffers, the layout and probe flags)
 * is ordinary .bss/.data in the on-chip scratch, the largest of any pack. Build: tools/pack_meter.py chladni. */
#include <stdint.h>
#define COLD_FN3
#define COLD_DATA
#define MTR_PACK 1
#define WAVE_COLS 64u
#define SPEC_BANDS 16u
#define FB_STRIDE 512u
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter_pack.h"
#include "meters_gen.h"
#include "meter_core.h"

static const mtr_host_api_t *g_api;
#define CLK_HZ             (g_api->clk_hz)
#define cycles()           g_api->cycles()
#define ui_accent          (*g_api->accent)
#define hs_base            (*g_api->hs_base)
#define FB_HELD()          g_api->held()
#define meter_afford()     g_api->afford()
#define ui_cpu_pct()       g_api->cpu_pct()
#define ui_toast_msg(m)    g_api->toast(m)
#define fb_blit(...)       g_api->blit(__VA_ARGS__)
#define fb_sblit(...)      g_api->sblit(__VA_ARGS__)
#define fb_wait()          g_api->wait()
#define fb_fence()         g_api->fence()
#define fb_set_bases(...)  g_api->set_bases(__VA_ARGS__)
#define chl_mb_write(a, d) g_api->mb_write((a), (d))
#define chl_mb_read(a, o)  g_api->mb_read((a), (o))
#undef  MV_CHLADNI
#define MV_CHLADNI(name)   (g_api->params[MP_CHLADNI_##name])

#include "chladni.inc"

/* The compiler may call these even in freestanding code; the pack carries its own so it names no firmware symbol. */
void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api;
    return (uint32_t)chladni_tick_box(in);
}
