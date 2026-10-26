/* SOUND MAP for the meter lab (Tau Omega's Meter Lab, tools/meters/preview/index.html): the pack's OWN drawing source (fw2/ui/meter_sound_map.c) compiled to WebAssembly, so the preview is the firmware's code, not a port
 * that can drift. The draw calls the pack makes through the host table become imports of the module; the framework's per-picture statistics (what fw2/ui/meters.c computes before it calls a pack: centroid, spectral flux
 * and onset, crest) are done here with the same core (fw2/cores/audio_stats_core.h). Build: python3 tools/meters/preview/wasm/build_wasm.py (needs Homebrew llvm + lld; the result is committed as meters/sound_map_wasm.js,
 * so nobody else needs the toolchain). Freestanding: no libc. */
#define P2_PACK 1
#include <stdint.h>
#include <stddef.h>
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/audio_stats_core.h"

#define WASM_IMPORT(n) __attribute__((import_module("env"), import_name(n)))
#define WASM_EXPORT(n) __attribute__((export_name(n), used))
WASM_IMPORT("rect") void js_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("bar") void js_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint32_t fg, uint32_t bg);
WASM_IMPORT("text") uint32_t js_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
#define gfx_rect(...)   js_rect(__VA_ARGS__)
#define gfx_bar(...)    js_bar(__VA_ARGS__)
#define gfx_text(...)   js_text(__VA_ARGS__)
uint16_t th_role[TR_COUNT];                   /* the lab writes the theme's roles here (index = the TR_* enum of fw2/ui/theme.h) */
#include "meter_sound_map.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

static mtr_in_t in_buf;
static uint8_t spec_buf[MTR_BANDS];
static uint16_t params_buf[MPP_SMP_N];
static as_onset_t onset_st;
/* where the lab writes each field of mtr_in_t (byte offsets), in this order: x y w h bg force paused degrade frame dt_ms peak_l peak_r rms_l rms_r corr centroid crest flux onset stats_ok */
static const uint16_t offs[] = { offsetof(mtr_in_t, x), offsetof(mtr_in_t, y), offsetof(mtr_in_t, w), offsetof(mtr_in_t, h), offsetof(mtr_in_t, bg), offsetof(mtr_in_t, force), offsetof(mtr_in_t, paused),
    offsetof(mtr_in_t, degrade), offsetof(mtr_in_t, frame), offsetof(mtr_in_t, dt_ms), offsetof(mtr_in_t, peak_l), offsetof(mtr_in_t, peak_r), offsetof(mtr_in_t, rms_l), offsetof(mtr_in_t, rms_r),
    offsetof(mtr_in_t, corr), offsetof(mtr_in_t, centroid), offsetof(mtr_in_t, crest), offsetof(mtr_in_t, flux), offsetof(mtr_in_t, onset), offsetof(mtr_in_t, stats_ok) };
WASM_EXPORT("in_ptr") uint8_t *in_ptr(void) { return (uint8_t *)&in_buf; }
WASM_EXPORT("spec_ptr") uint8_t *spec_ptr(void) { return spec_buf; }
WASM_EXPORT("params_ptr") uint16_t *params_ptr(void) { return params_buf; }
WASM_EXPORT("roles_ptr") uint16_t *roles_ptr(void) { return th_role; }
WASM_EXPORT("offs_ptr") const uint16_t *offs_ptr(void) { return offs; }
WASM_EXPORT("n_roles") uint32_t n_roles(void) { return TR_COUNT; }
WASM_EXPORT("n_params") uint32_t n_params(void) { return MPP_SMP_N; }
/* the pack as the host calls it: parameters, then the picture */
WASM_EXPORT("tick_raw") void tick_raw(void)
{
    in_buf.spec = spec_buf;
    smp_load(params_buf);
    smp_tick(&in_buf);
}
/* the lab's live path: the framework's per-picture statistics first (as fw2/ui/meters.c does), from the spectrum, the peaks and (when stats_ok) the rms the lab supplies */
WASM_EXPORT("tick") void tick(uint32_t now_ms)
{
    static const uint8_t zero[MTR_BANDS];
    in_buf.centroid = in_buf.paused ? 0u : (uint8_t)as_centroid(spec_buf);
    as_onset_step(&onset_st, in_buf.paused ? zero : spec_buf, now_ms);
    in_buf.flux = onset_st.flux; in_buf.onset = onset_st.onset;
    { const uint32_t pk = in_buf.peak_l > in_buf.peak_r ? in_buf.peak_l : in_buf.peak_r, rm = in_buf.rms_l > in_buf.rms_r ? in_buf.rms_l : in_buf.rms_r; in_buf.crest = (uint8_t)(in_buf.stats_ok ? as_crest_q4(pk, rm) : 0u); }
    tick_raw();
}
WASM_EXPORT("reset") void reset(void) { onset_st = (as_onset_t){ { 0 }, 0, 0, 0, 0 }; }
