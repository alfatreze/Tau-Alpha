/* CHLADNI for the meter lab (Tau Omega's Meter Lab, tools/meters/preview/index.html): the pack's OWN drawing source (fw2/ui/meter_chladni.c, as the pack builds it with P2_PACK) compiled to WebAssembly, so the preview is the
 * firmware's code, not a port that can drift (owner decision D-M15: every Player 2 meter has a module; the same approach as SOUND MAP, LAYERED WAVE and WATERFALL GRID). The engine calls the meter makes become imports of
 * the module and land in the lab's framebuffer (tau_fb.js). The SDRAM mailbox, the engine's copy inside its memory and the scaled blit it draws through are modelled by the lab's framebuffer (mbWrite, mbRead, blitAbs, sblit, fence); its known-answer probe therefore passes (the lower address is the low half). The CPU cycle counter reads 0 here (the load-shedding rule never sheds). Build: python3 tools/meters/preview/wasm/build_wasm.py chladni (needs Homebrew llvm + lld; the result is committed as meters/chladni_wasm.js so nobody else
 * needs the toolchain). Freestanding: no libc. */
#define P2_PACK 1
#define P2_HOST 1                                  /* mmio.h then lets this file supply REG_RD / REG_WR (the model of the hardware below)*/
#include <stdint.h>
#include <stddef.h>
#define WASM_IMPORT(n) __attribute__((import_module("env"), import_name(n)))
#define WASM_EXPORT(n) __attribute__((export_name(n), used))
static uint32_t reg_rd(uint32_t a) { (void)a; return 0u; }                         /* the CPU cycle counter (R_CYCLES) reads 0 */
#define REG_RD(a) reg_rd(a)
#define REG_WR(a, v) ((void)(a), (void)(v))
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../platform/mmio.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#define CHL_DATA
#include "../cores/chladni_core.h"
WASM_IMPORT("rect") void js_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("text") uint32_t js_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
WASM_IMPORT("blit") void js_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
WASM_IMPORT("blit_abs") void js_blit_abs(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);
WASM_IMPORT("sblit") void js_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t scale);
WASM_IMPORT("fence") void js_fence(void);
WASM_IMPORT("mb_write") int js_mb_write(uint32_t addr, uint32_t two_pixels);
WASM_IMPORT("mb_read") uint32_t js_mb_read(uint32_t addr);
static int mb_read_word(uint32_t addr, uint32_t *out) { *out = js_mb_read(addr); return 0; }
#define gfx_rect(...)       js_rect(__VA_ARGS__)
#define gfx_text(...)       js_text(__VA_ARGS__)
#define gfx_blit(...)       js_blit(__VA_ARGS__)
#define gfx_blit_abs(...)   js_blit_abs(__VA_ARGS__)
#define gfx_sblit(...)      js_sblit(__VA_ARGS__)
#define gfx_fence()         js_fence()
#define gfx_mb_write(a, d)  js_mb_write((a), (d))
#define gfx_mb_read(a, d)   mb_read_word((a), (d))
uint16_t th_role[TR_COUNT];                   /* the lab writes the theme's roles here (index = the TR_* enum of fw2/ui/theme.h) */
#include "meter_chladni.c"
static uint16_t params_buf[MPP_CHL_N];

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
WASM_EXPORT("n_params") uint32_t n_params(void) { return MPP_CHL_N; }
WASM_EXPORT("tick_raw") void tick_raw(void) { in_buf.spec = spec_buf; live_v = params_buf; ch_load(params_buf); meter_chladni_tick(&in_buf); }
