/* MASTER VU as a loadable meter pack (docs/features/meters/METER_PACKS.md). The meter's own source, fw/vu_master.inc, is included unchanged (fw/player.c includes the same
 * file for the built-in meter). Everything it needs from the firmware comes through the host table: the rectangle fill, the text services (colour, glyph, clipped string,
 * decimal, row height), the theme roles, its setting values, the repaint flag; the stream/machine readout of the info overlay arrives in mtr_in_t (in->info, measured by the
 * host), and its time step is in->dt_ms. Its working state (eased levels, peak holds, what is drawn) is ordinary .bss in the on-chip scratch. Build: tools/pack_meter.py vu_master. */
#include <stdint.h>
#define COLD_FN3
#define COLD_DATA
#define WAVE_COLS 64u
#define SPEC_BANDS 16u
enum { TS_1X = 0 };
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter_pack.h"
#include "meters_gen.h"
#include "meter_core.h"

static const mtr_host_api_t *g_api;
#define fb_rect(...)       g_api->fb_rect(__VA_ARGS__)
#define fb_set_color(...)  g_api->set_color(__VA_ARGS__)
#define fb_char(...)       g_api->ch(__VA_ARGS__)
#define fb_text_clipped(...) g_api->text_clipped(__VA_ARGS__)
#define ui_dec(p, v)       g_api->dec((p), (v))
#define FB_CELL(s)         g_api->cell(s)
#undef  th_role
#define th_role            g_api->role
#define wviz_force         (*g_api->force)
#undef  MV_VU_MASTER
#define MV_VU_MASTER(name) (g_api->params[MP_VU_MASTER_##name])

#include "vu_master.inc"

/* The compiler may call these even in freestanding code; the pack carries its own so it names no firmware symbol. */
void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api;
    vum_tick(in);
    return 1u;
}
