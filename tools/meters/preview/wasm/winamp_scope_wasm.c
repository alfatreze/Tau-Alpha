/* WINAMP SCOPE for the meter lab (Tau Omega's Meter Lab, tools/meters/preview/index.html): the pack's OWN drawing source (fw2/ui/meter_winamp_scope.c, as the pack builds it with P2_PACK) compiled to WebAssembly, so the preview is the
 * firmware's code, not a port that can drift (owner decision D-M15: every Player 2 meter has a module; the same approach as SOUND MAP, LAYERED WAVE and WATERFALL GRID). The engine calls the meter makes become imports of
 * the module and land in the lab's framebuffer (tau_fb.js). The hardware capture block (tau_wave_meter.sv) the meter reads through registers is modelled here: the lab writes the 256 min/max words of a capture into cap_ptr(), and the meter's R_WAVE_IDX / R_WAVE_DATA / R_WAVE_ST accesses are answered from it (a capture is always complete). Build: python3 tools/meters/preview/wasm/build_wasm.py winamp_scope (needs Homebrew llvm + lld; the result is committed as meters/winamp_scope_wasm.js so nobody else
 * needs the toolchain). Freestanding: no libc. */
#define P2_PACK 1
#define P2_HOST 1                                  /* mmio.h then lets this file supply REG_RD / REG_WR (the model of the hardware below)*/
#include <stdint.h>
#include <stddef.h>
#define WASM_IMPORT(n) __attribute__((import_module("env"), import_name(n)))
#define WASM_EXPORT(n) __attribute__((export_name(n), used))
static uint32_t cap_buf[256], cap_idx;                                            /* the capture: {min[31:16], max[15:0]} (signed 16-bit each) of each hardware column */
static uint32_t reg_rd(uint32_t a) { return a == 0x800000F4u ? cap_buf[cap_idx & 255u] : 0u; }          /* R_WAVE_DATA; R_WAVE_ST (still capturing) and the rest read 0 */
static void reg_wr(uint32_t a, uint32_t v) { if (a == 0x800000F0u) cap_idx = v; }                         /* R_WAVE_IDX: the column presented at R_WAVE_DATA; R_WAVE_CTL (arm) needs nothing */
#define REG_RD(a) reg_rd(a)
#define REG_WR(a, v) reg_wr((a), (v))
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../platform/mmio.h"
#include "../generated/meter_presets.h"
WASM_IMPORT("rect") void js_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("rect_stash") void js_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("blend_ready") int js_blend_ready(void);
WASM_IMPORT("blend_on") void js_blend_on(uint32_t a);
WASM_IMPORT("blend_blit") void js_blend_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
WASM_IMPORT("blend_off") void js_blend_off(void);
#define gfx_rect(...)       js_rect(__VA_ARGS__)
#define gfx_rect_stash(...) js_rect_stash(__VA_ARGS__)
#define gfx_blend_ready()   js_blend_ready()
#define gfx_blend_on(a)     js_blend_on(a)
#define gfx_blend_blit(...) js_blend_blit(__VA_ARGS__)
#define gfx_blend_off()     js_blend_off()
uint16_t th_role[TR_COUNT];                   /* the lab writes the theme's roles here (index = the TR_* enum of fw2/ui/theme.h) */
#include "meter_winamp_scope.c"
static uint16_t params_buf[MPP_SCOPE_N];

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

static mtr_in_t in_buf;
static uint8_t spec_buf[MTR_BANDS];
/* where the lab writes each field of mtr_in_t (byte offsets), in this order: x y w h bg force paused degrade frame dt_ms peak_l peak_r */
static const uint16_t offs[] = { offsetof(mtr_in_t, x), offsetof(mtr_in_t, y), offsetof(mtr_in_t, w), offsetof(mtr_in_t, h), offsetof(mtr_in_t, bg), offsetof(mtr_in_t, force), offsetof(mtr_in_t, paused),
    offsetof(mtr_in_t, degrade), offsetof(mtr_in_t, frame), offsetof(mtr_in_t, dt_ms), offsetof(mtr_in_t, peak_l), offsetof(mtr_in_t, peak_r) };
WASM_EXPORT("in_ptr") uint8_t *in_ptr(void) { return (uint8_t *)&in_buf; }
WASM_EXPORT("spec_ptr") uint8_t *spec_ptr(void) { return spec_buf; }
WASM_EXPORT("params_ptr") uint16_t *params_ptr(void) { return params_buf; }
WASM_EXPORT("roles_ptr") uint16_t *roles_ptr(void) { return th_role; }
WASM_EXPORT("offs_ptr") const uint16_t *offs_ptr(void) { return offs; }
WASM_EXPORT("n_roles") uint32_t n_roles(void) { return TR_COUNT; }
WASM_EXPORT("cap_ptr") uint32_t *cap_ptr(void) { return cap_buf; }
WASM_EXPORT("n_params") uint32_t n_params(void) { return MPP_SCOPE_N; }
WASM_EXPORT("tick_raw") void tick_raw(void) { in_buf.spec = spec_buf; ws_load(params_buf); meter_winamp_scope_tick(&in_buf); }
