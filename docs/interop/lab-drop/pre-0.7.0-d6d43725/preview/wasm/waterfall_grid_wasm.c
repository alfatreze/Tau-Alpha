/* WATERFALL GRID for the meter lab (Tau Omega's Meter Lab, tools/meters/preview/index.html): the pack's OWN drawing source (fw2/ui/meter_waterfall_grid.c, as the pack builds it with P2_PACK) compiled to WebAssembly, so the
 * preview is the firmware's code, not a port that can drift (the same approach as SOUND MAP and LAYERED WAVE, B-959, B-975). The engine calls the pack makes through the host table become imports of the module: rect, the
 * off-screen stash (rect_stash), the palette (clut_load: a pointer into the module's memory) and the palette blit (cblit) that shows the entering row, and the two scrolls (scroll_down, scroll_left). The lab's framebuffer
 * implements them (tau_fb.js). Build: python3 tools/meters/preview/wasm/build_wasm.py waterfall_grid (needs Homebrew llvm + lld; the result is committed as meters/waterfall_grid_wasm.js so nobody else needs the toolchain).
 * Freestanding: no libc. */
#define P2_PACK 1
#include <stdint.h>
#include <stddef.h>
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/meter_core.h"

#define WASM_IMPORT(n) __attribute__((import_module("env"), import_name(n)))
#define WASM_EXPORT(n) __attribute__((export_name(n), used))
WASM_IMPORT("rect") void js_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("rect_stash") void js_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("clut_load") void js_clut_load(uint32_t start, const uint16_t *colors, uint32_t n);
WASM_IMPORT("cblit") void js_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
WASM_IMPORT("scroll_down") void js_scroll_down(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d);
WASM_IMPORT("scroll_left") void js_scroll_left(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d);
WASM_IMPORT("text") uint32_t js_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
#define gfx_rect(...)        js_rect(__VA_ARGS__)
#define gfx_rect_stash(...)  js_rect_stash(__VA_ARGS__)
#define gfx_clut_load(...)   js_clut_load(__VA_ARGS__)
#define gfx_cblit(...)       js_cblit(__VA_ARGS__)
#define gfx_scroll_down(...) js_scroll_down(__VA_ARGS__)
#define gfx_scroll_left(...) js_scroll_left(__VA_ARGS__)
#define gfx_text(...)        js_text(__VA_ARGS__)
uint16_t th_role[TR_COUNT];                   /* the lab writes the theme's roles here (index = the TR_* enum of fw2/ui/theme.h) */
#include "meter_waterfall_grid.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

static mtr_in_t in_buf;
static uint8_t spec_buf[MTR_BANDS];
static uint16_t params_buf[MPP_WF_N];
/* where the lab writes each field of mtr_in_t (byte offsets), in this order: x y w h bg force paused degrade frame dt_ms peak_l peak_r */
static const uint16_t offs[] = { offsetof(mtr_in_t, x), offsetof(mtr_in_t, y), offsetof(mtr_in_t, w), offsetof(mtr_in_t, h), offsetof(mtr_in_t, bg), offsetof(mtr_in_t, force), offsetof(mtr_in_t, paused),
    offsetof(mtr_in_t, degrade), offsetof(mtr_in_t, frame), offsetof(mtr_in_t, dt_ms), offsetof(mtr_in_t, peak_l), offsetof(mtr_in_t, peak_r) };
WASM_EXPORT("in_ptr") uint8_t *in_ptr(void) { return (uint8_t *)&in_buf; }
WASM_EXPORT("spec_ptr") uint8_t *spec_ptr(void) { return spec_buf; }
WASM_EXPORT("params_ptr") uint16_t *params_ptr(void) { return params_buf; }
WASM_EXPORT("roles_ptr") uint16_t *roles_ptr(void) { return th_role; }
WASM_EXPORT("offs_ptr") const uint16_t *offs_ptr(void) { return offs; }
WASM_EXPORT("n_roles") uint32_t n_roles(void) { return TR_COUNT; }
WASM_EXPORT("n_params") uint32_t n_params(void) { return MPP_WF_N; }
/* the pack as the host calls it: the live parameters, then the picture */
WASM_EXPORT("tick_raw") void tick_raw(void)
{
    in_buf.spec = spec_buf;
    wf_load(params_buf);
    wf_tick(&in_buf);
}
