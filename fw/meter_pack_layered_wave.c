/* Layered Wave as a loadable meter pack (docs/features/meters/METER_PACKS.md). The meter's own source, fw/layered_wave.inc, is included unchanged:
 * the few firmware services it uses (the rectangle fill, the cycle counter, the accent and theme roles, its setting values, the repaint flag) are
 * redirected to the host table the entry point receives, and everything else it needs is in this file or the blob. Build: tools/pack_meter.py. */
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

/* State that is touched only a word at a time (the history ring) lives in the slot's PSRAM, in .pstate; everything else the meter keeps (its per-frame
 * working state) is ordinary .data/.bss, which the link script places in the on-chip meter scratch area. */
#undef  MTR_PSRAM
#define MTR_PSRAM __attribute__((section(".pstate")))

static const mtr_host_api_t *g_api;
#define fb_rect(...)       g_api->fb_rect(__VA_ARGS__)
#define cycles()           g_api->cycles()
#undef  mtr_psram_ready
#define mtr_psram_ready()  g_api->psram_ready()
#define ui_accent          (*g_api->accent)
#undef  th_role
#define th_role            g_api->role
#define wviz_force         (*g_api->force)
#undef  MV_LAYERED_WAVE
#define MV_LAYERED_WAVE(name) (g_api->params[MP_LAYERED_WAVE_##name])

#include "layered_wave.inc"

/* The compiler may call these even in freestanding code; the pack carries its own so it names no firmware symbol. */
void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api;
    return (uint32_t)lw_tick(in);
}
