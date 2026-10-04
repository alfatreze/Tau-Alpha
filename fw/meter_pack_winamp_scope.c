/* Winamp Scope as a loadable meter pack (docs/features/meters/METER_PACKS.md). The meter's own source, fw/winamp_scope.inc, is included unchanged (fw/player.c includes the same
 * file for the built-in meter); the firmware services it uses (rectangle fill, the gradient background restore and blend, the fullscreen flag, the trail diagnostics, the accent,
 * the theme roles, its setting values, the repaint flag) are redirected to the host table the entry point receives. The hardware wave-block path (dead in the firmware) is left
 * out (MTR_PACK). The smoothed trace is 64 columns of working state in the on-chip scratch. Build: tools/pack_meter.py winamp_scope. */
#include <stdint.h>
#define COLD_FN3
#define COLD_DATA
#define MTR_PACK 1
#define WAVE_COLS 64u
#define WVIZ_SCOPE_Y_N WAVE_COLS
#define SCOPE_UNIT 100
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter_pack.h"
#include "meters_gen.h"
#include "meter_core.h"

static const mtr_host_api_t *g_api;
#undef  fig_rect
#define fig_rect(...)      g_api->rect_clip(__VA_ARGS__)
#define ui_accent          (*g_api->accent)
#undef  th_role
#define th_role            g_api->role
#define wviz_force         (*g_api->force)
#define ui_fullscreen      (*g_api->fullscreen)
#define ui_bg_restore(...) g_api->bg_restore(__VA_ARGS__)
#define ui_bg_blend(...)   g_api->bg_blend(__VA_ARGS__)
#define SCOPE_NOTE(ok, px, py) g_api->scope_note((ok), (px), (py))
#undef  MV_WINAMP_SCOPE
#define MV_WINAMP_SCOPE(name) (g_api->params[MP_WINAMP_SCOPE_##name])

#include "winamp_scope.inc"

/* The compiler may call these even in freestanding code; the pack carries its own so it names no firmware symbol. */
void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api;
    wviz_scope_tick(in, *g_api->grad ? 1 : 0);
    return 1u;
}
