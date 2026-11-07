/* LAYERED WAVE for the meter lab and Tau Omega (Meter Lab, tools/meters/preview/index.html): the firmware's OWN drawing source (fw2/ui/meter_layered_wave.c, which includes fw2/ui/layered_wave.inc) compiled to WebAssembly, so the
 * preview is the Pocket's code, not a hand-written twin that can drift (B-959; the same approach as SOUND MAP, sound_map_wasm.c). The engine calls the meter makes (gfx_rect, gfx_scroll_left) become imports of the module and land
 * in the lab's framebuffer; the hardware registers it reads (the PSRAM window proof) are answered here as the simulator's device model answers them. Build: python3 tools/meters/preview/wasm/build_wasm.py (needs Homebrew
 * llvm + lld; the result is committed as meters/layered_wave_wasm.js so nobody else needs the toolchain). Freestanding: no libc.
 * The module is built the host way (P2_HOST): the cost guard counts commands, exactly like the golden test's C build. A second build, -DWASM_TIMED (layered_wave_timed_wasm.js), counts modelled CPU time like the Pocket: the lab's `clock` import is its cycle counter. */
#define P2_HOST 1
#include <stdint.h>
#include <stddef.h>
#define WASM_IMPORT(n) __attribute__((import_module("env"), import_name(n)))
#define WASM_EXPORT(n) __attribute__((export_name(n), used))
#ifdef WASM_TIMED
#define LW_TIME_GUARD 1                                  /* the cost guard counts CPU time, as on the Pocket (B-948); the lab models the clock (layered_wave_timed_wasm.js, B-977) */
WASM_IMPORT("clock") uint32_t js_clock(void);
static uint32_t reg_rd(uint32_t a) { return a == 0x80000088u ? 0x50535231u : a == 0x800000A0u ? 0x10000u : a == 0x8000000Cu ? js_clock() : 0u; }       /* R_CYCLES: the lab's modelled cycle counter */
#else
static uint32_t reg_rd(uint32_t a) { return a == 0x80000088u ? 0x50535231u : a == 0x800000A0u ? 0x10000u : 0u; }
#endif       /* the PSRAM controller and its CPU window are "in the bitstream" (tools/p2_dev_model.py) */
#define REG_RD(a) reg_rd(a)
#define REG_WR(a, v) ((void)(a), (void)(v))
static uint32_t psram_state[16];                                                 /* the history ring's proof words (mtr_psram_ready): plain memory here */
#define __psram_state_start psram_state
#define __psram_state_end (psram_state + 16)
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/lw_presets.h"
WASM_IMPORT("rect") void js_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("scroll_left") void js_scroll_left(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d);
WASM_IMPORT("blit") void js_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);       /* a copy inside the box being drawn into (the SCROLL view moving right is made of these) */
WASM_IMPORT("text") uint32_t js_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
#define gfx_rect(...)        js_rect(__VA_ARGS__)
#define gfx_scroll_left(...) js_scroll_left(__VA_ARGS__)
#define gfx_blit(...)        js_blit(__VA_ARGS__)
#define gfx_text(...)        js_text(__VA_ARGS__)
uint16_t th_role[TR_COUNT];                   /* the lab writes the theme's roles here (index = the TR_* enum of fw2/ui/theme.h) */
#include "meter_layered_wave.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

#undef paused                                                                    /* meter_layered_wave.c defines it as a shorthand for in->paused */
static mtr_in_t in_buf;
static uint8_t spec_buf[MTR_BANDS];
static int8_t wave_buf[WAVE_COLS];                                             /* WAVE_COLS = 256 in Player 2 (the golden test's host build has 64): the lab writes up to that many samples, the rest stay 0 */
/* where the lab writes each field of mtr_in_t (byte offsets), in this order: x y w h bg force paused degrade frame dt_ms peak_l peak_r */
static const uint16_t offs[] = { offsetof(mtr_in_t, x), offsetof(mtr_in_t, y), offsetof(mtr_in_t, w), offsetof(mtr_in_t, h), offsetof(mtr_in_t, bg), offsetof(mtr_in_t, force), offsetof(mtr_in_t, paused),
    offsetof(mtr_in_t, degrade), offsetof(mtr_in_t, frame), offsetof(mtr_in_t, dt_ms), offsetof(mtr_in_t, peak_l), offsetof(mtr_in_t, peak_r) };
WASM_EXPORT("in_ptr") uint8_t *in_ptr(void) { return (uint8_t *)&in_buf; }
WASM_EXPORT("spec_ptr") uint8_t *spec_ptr(void) { return spec_buf; }
WASM_EXPORT("wave_ptr") int8_t *wave_ptr(void) { return wave_buf; }
WASM_EXPORT("n_wave") uint32_t n_wave(void) { return WAVE_COLS; }
WASM_EXPORT("params_ptr") uint16_t *params_ptr(void) { return meter_layered_wave_mv; }       /* the live parameters, the manifest's order (MP_LAYERED_WAVE_*) */
WASM_EXPORT("roles_ptr") uint16_t *roles_ptr(void) { return th_role; }
WASM_EXPORT("offs_ptr") const uint16_t *offs_ptr(void) { return offs; }
WASM_EXPORT("n_roles") uint32_t n_roles(void) { return TR_COUNT; }
WASM_EXPORT("ebounds_ptr") const uint8_t *ebounds_ptr(void) { return lw_ebounds; }                     /* the ENERGY split's learned boundaries (layers + 1 bytes, band indices), for the lab's split map */
WASM_EXPORT("ebounds_ok") uint32_t ebounds_ok(void) { return lw_ebounds_ok; }
WASM_EXPORT("stride") uint32_t stride_now(void) { return lw_stride; }                                   /* the cost guard's cell width in pixels (the lab shows it) */
WASM_EXPORT("n_params") uint32_t n_params(void) { return MP_LAYERED_WAVE_N; }
/* the module's own idea of the accent (ui_accent is a role here) is th_role[TR_ACCENT]: the lab sets it with the other roles */
/* the picture as the Pocket's player makes it: the meter's entry (the waveform is the loudest peak as a flat envelope, as fw2/ui/meters.c hands it over) */
WASM_EXPORT("tick") void tick(void)
{
    in_buf.spec = spec_buf;
    meter_layered_wave_tick(&in_buf);
}
/* the picture with the waveform exactly as given (the parity test against the golden frames, which feed the real waveform) */
WASM_EXPORT("tick_raw") void tick_raw(void)
{
    in_buf.spec = spec_buf; in_buf.wave = wave_buf;
    wviz_force = in_buf.force;
    lw_tick(&in_buf);
}
