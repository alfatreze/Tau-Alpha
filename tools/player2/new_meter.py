#!/usr/bin/env python3
"""Scaffold a new Player 2 meter (make new-meter KEY=sparks [TAG=SPK] [LABEL="SPARKS"]; B-814): the manifest, the preview, the drawing source, the pack source, and a first build of the pack, so a meter starts as a working
pack-only meter you can edit instead of seven hand-wired places. It creates

  meters/<key>/meter.json   the next slot and manifest index, p2.pack_only, three parameters (mode LEVELS or HISTORY, gain, cell size), two presets, a draw-command ceiling and a cycle limit to adjust
  meters/<key>/thumb.json   a 56 x 32 preview (bars), palette from the theme roles
  fw2/ui/meter_<key>.c      the drawing: <tag>_load(params) and <tag>_tick(in) under P2_PACK; LEVELS lights a cell grid from the band levels (fw2/ui/meter_grid.h), HISTORY scrolls a loudness history kept in a
                            ring (fw2/cores/histring_core.h)
  fw2/ui/meter_pack_<key>.c the pack wrapper: the redirects to the host table, the descriptor, the entry point
  tools/meters/preview/wasm/<key>_wasm.c   the WebAssembly wrapper of the same source (owner decision D-M15: every Player 2 meter has a module for the lab and Tau Omega), and
  tools/meters/preview/meters/<key>.js     its adapter; the committed module meters/<key>_wasm.js is built by tools/meters/preview/wasm/build_wasm.py (Homebrew llvm + lld)

then runs the generators (tools/player2/gen_meters_p2.py, tools/gen_meters.py) and builds the pack (tools/player2/pack_meter.py). The pack test (sim/test_p2_meter_pack.py) picks the meter up from its manifest with a generic
trace (every preset, loud and quiet stretches, a pause, the degrade levels, the fullscreen box) and checks the pack against the source compiled straight in. Edit the source, the manifest and the thumbnail, then `make test-host`
and `python3 tools/player2/pack_meter.py <key> --out work/packs-p2/<key>.tmpk`. The pack is delivered to the Pocket in the MPKB section of tau-assets.bin (docs/features/meters/METER_PACKS_PLAYER2.md).
  python3 tools/player2/new_meter.py KEY [--tag TAG] [--label LABEL] [--no-generate]"""
import argparse, json, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
def manifests(): return [json.loads(p.read_text()) for p in sorted((ROOT / "meters").glob("*/meter.json"))]
def rle_preview():
    """a 56 x 32 preview: bars of different heights on the dark background (palette index 0 background, 1 bar, 2 highlight)"""
    W, H = 56, 32; px = [0] * (W * H); hs = [10, 18, 25, 14, 22, 28, 12]
    for b, h in enumerate(hs):
        x0 = 4 + b * 7
        for y in range(H - h, H):
            for x in range(x0, x0 + 5): px[y * W + x] = 2 if y == H - h else 1
    out = []; i = 0
    while i < len(px):
        j = i
        while j < len(px) and px[j] == px[i] and j - i < 32: j += 1
        out.append((px[i] << 5) | (j - i - 1)); i = j
    return bytes(out).hex()
SRC = '''/* %(label)s (meters/%(key)s/meter.json): scaffolded by tools/player2/new_meter.py; edit freely. A PACK-ONLY meter: this file is compiled into its pack (fw2/ui/meter_pack_%(key)s.c, with P2_PACK), never into the firmware;
 * the host calls %(lc)s_load() with the live parameters (the manifest's order) and %(lc)s_tick() once per picture. It draws only inside the box it is given (mtr_in_t x, y, w, h), with plain gfx_rect / gfx_bar / gfx_text
 * (a pack reaches them through the host table). The helpers: fw2/ui/meter_grid.h (a grid of cells: geometry, nibble state, delta drawing) and fw2/cores/histring_core.h (a byte history ring).
 * LEVELS: every cell of a column belongs to one spectrum band and lights from the bottom up with the level (a bar made of cells). HISTORY: one column per picture of the loudness, scrolling by a ring. */
#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "meter_grid.h"
#include "../cores/histring_core.h"
#define %(TAG)s_COLS 66u                                        /* the most cells the fullscreen box holds at the smallest cell */
#define %(TAG)s_ROWS 53u
#define %(TAG)s_HIST 128u                                       /* the history ring: samples (a multiple of 4) */
typedef struct { uint8_t mode, gain, cell; } %(lc)s_cfg_t;
static %(lc)s_cfg_t cfg = { 0u, 100u, 8u };
/* %(lc)s_load: the live parameters (the manifest's order) into the working configuration; the pack calls it each tick with the host's table. */
static void %(lc)s_load(const uint16_t *v) { cfg.mode = (uint8_t)v[MPP_%(TAG)s_MODE]; cfg.gain = (uint8_t)v[MPP_%(TAG)s_GAIN]; cfg.cell = (uint8_t)v[MPP_%(TAG)s_CELL]; }
#ifdef P2_PACK
static uint8_t state[CG_BYTES(%(TAG)s_COLS, %(TAG)s_ROWS)];       /* the cell grid: a nibble a cell, the step drawn */
static mgrid_t grid;
static uint32_t ring[HR_WORDS(%(TAG)s_HIST)], head, geom;
static void %(lc)s_tick(const mtr_in_t *in)
{
    if (!cg_fit(&grid.g, in->x, in->y, in->w, in->h, cfg.cell, %(TAG)s_COLS, %(TAG)s_ROWS)) return;
    const uint32_t cols = grid.g.cols, rows = grid.g.rows, sig = cg_sig(in->w, in->h, grid.g.pitch, cfg.mode);
    mgrid_palette(&grid, in->bg, UI_ACCENT);
    uint32_t level[MTR_BANDS], sum = 0;
    for (uint32_t b = 0; b < MTR_BANDS; b++) { const uint32_t v = in->paused ? 0u : (uint32_t)in->spec[b] * cfg.gain / 100u; level[b] = v > 255u ? 255u : v; sum += level[b]; }
    if (in->force || sig != geom) { geom = sig; mgrid_clear(&grid, state, sizeof state, in->x, in->y, in->w, in->h, in->bg); hr_clear(ring, %(TAG)s_HIST); head = 0; }
    if (cfg.mode == 0u) {                                       /* LEVELS */
        for (uint32_t c = 0; c < cols; c++) {
            const uint32_t lv = level[c * (MTR_BANDS - 1u) / (cols > 1u ? cols - 1u : 1u)];
            for (uint32_t r = 0; r < rows; r++) {
                const uint32_t above = (rows - 1u - r) * 256u / rows;          /* how far up the column this cell is, in 256ths */
                mgrid_put(&grid, state, %(TAG)s_COLS, c, r, lv <= above ? 0u : lv - above > 170u ? 3u : lv - above > 85u ? 2u : 1u);
            }
        }
    } else {                                                    /* HISTORY: the newest loudness enters at the right, the rest scroll left */
        head = hr_back(head, %(TAG)s_HIST); hr_store(ring, head, sum / MTR_BANDS);
        uint8_t col[%(TAG)s_COLS];
        hr_unwrap(ring, head, %(TAG)s_HIST, col, cols);
        const uint32_t h = rows * grid.g.pitch;
        for (uint32_t c = 0; c < cols; c++) {
            const uint32_t v = col[cols - 1u - c], lit = v * h / 256u;
            gfx_bar(cg_x(&grid.g, c), grid.g.y0, grid.g.pitch - 1u, h, lit, UI_ACCENT, in->bg);
        }
    }
}
#endif
'''
PACK = '''/* %(label)s as a loadable meter pack: meter_%(key)s.c included under P2_PACK (its real code exists only there), the services redirected to the host table the entry point receives, the descriptor the firmware reads. Scaffolded
 * by tools/player2/new_meter.py: see meter_pack_winamp_bars.c for the pattern. Build: tools/player2/pack_meter.py %(key)s. */
#define P2_PACK 1
#include <stdint.h>
#include "meter_pack.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
static const mtr_host_api_t *g_api;
static uint32_t g_cmds;
#define gfx_rect(...)   (g_cmds++, g_api->rect(__VA_ARGS__))
#define gfx_bar(...)    (g_cmds++, g_api->bar(__VA_ARGS__))
#define gfx_text(...)   (g_cmds++, g_api->text(__VA_ARGS__))
#undef  th_role
#define th_role         g_api->role
#include "../generated/desc_%(key)s.h"          /* the meter descriptor this pack carries (section .mtr_desc, tools/player2/meter_desc.py) */
#include "meter_%(key)s.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

__attribute__((section(".text.mtr_pack_entry"), used))
uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)
{
    g_api = api; g_cmds = 0u;
    %(lc)s_load(api->params);
    %(lc)s_tick(in);
    if (api->stats) api->stats[0] = g_cmds;
    return g_cmds;
}
'''
WASM = '''/* %(label)s for the meter lab (Tau Omega's Meter Lab, tools/meters/preview/index.html): the pack's OWN drawing source (fw2/ui/meter_%(key)s.c, as the pack builds it with P2_PACK) compiled to WebAssembly, so the preview is
 * the firmware's code, not a port that can drift (owner decision D-M15: every Player 2 meter has a module). Scaffolded by tools/player2/new_meter.py: the engine calls the meter makes (gfx_rect, gfx_bar, gfx_text) become
 * imports of the module and land in the lab's framebuffer (tools/meters/preview/tau_fb.js); a meter that uses another engine call adds its import here, the adapter's environment (meters/_wasm_meter.js) and a model in
 * tau_fb.js. Build: python3 tools/meters/preview/wasm/build_wasm.py %(key)s (needs Homebrew llvm + lld; the result is committed as meters/%(key)s_wasm.js). Freestanding: no libc. */
#define P2_PACK 1
#include <stdint.h>
#include <stddef.h>
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"          /* before the redirects below: its declarations must not be rewritten */
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#define WASM_IMPORT(n) __attribute__((import_module("env"), import_name(n)))
#define WASM_EXPORT(n) __attribute__((export_name(n), used))
WASM_IMPORT("rect") void js_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c);
WASM_IMPORT("bar") void js_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint32_t fg, uint32_t bg);
WASM_IMPORT("text") uint32_t js_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
#define gfx_rect(...) js_rect(__VA_ARGS__)
#define gfx_bar(...)  js_bar(__VA_ARGS__)
#define gfx_text(...) js_text(__VA_ARGS__)
uint16_t th_role[TR_COUNT];                   /* the lab writes the theme's roles here (index = the TR_* enum of fw2/ui/theme.h) */
#include "meter_%(key)s.c"

void *memset(void *d, int c, unsigned n) { uint8_t *p = (uint8_t *)d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = (uint8_t *)d; const uint8_t *q = (const uint8_t *)s; while (n--) *p++ = *q++; return d; }

static mtr_in_t in_buf;
static uint8_t spec_buf[MTR_BANDS];
static uint16_t params_buf[MPP_%(TAG)s_N];
/* where the lab writes each field of mtr_in_t (byte offsets), in this order: x y w h bg force paused degrade frame dt_ms peak_l peak_r */
static const uint16_t offs[] = { offsetof(mtr_in_t, x), offsetof(mtr_in_t, y), offsetof(mtr_in_t, w), offsetof(mtr_in_t, h), offsetof(mtr_in_t, bg), offsetof(mtr_in_t, force), offsetof(mtr_in_t, paused),
    offsetof(mtr_in_t, degrade), offsetof(mtr_in_t, frame), offsetof(mtr_in_t, dt_ms), offsetof(mtr_in_t, peak_l), offsetof(mtr_in_t, peak_r) };
WASM_EXPORT("in_ptr") uint8_t *in_ptr(void) { return (uint8_t *)&in_buf; }
WASM_EXPORT("spec_ptr") uint8_t *spec_ptr(void) { return spec_buf; }
WASM_EXPORT("params_ptr") uint16_t *params_ptr(void) { return params_buf; }
WASM_EXPORT("roles_ptr") uint16_t *roles_ptr(void) { return th_role; }
WASM_EXPORT("offs_ptr") const uint16_t *offs_ptr(void) { return offs; }
WASM_EXPORT("n_roles") uint32_t n_roles(void) { return TR_COUNT; }
WASM_EXPORT("n_params") uint32_t n_params(void) { return MPP_%(TAG)s_N; }
/* the pack as the host calls it: the live parameters, then the picture */
WASM_EXPORT("tick_raw") void tick_raw(void) { in_buf.spec = spec_buf; %(lc)s_load(params_buf); %(lc)s_tick(&in_buf); }
'''
ADAPTER = '''/* %(key)s: the lab's twin of %(label)s: the pack's own source (fw2/ui/meter_%(key)s.c) compiled to WebAssembly (wasm/%(key)s_wasm.c; owner decision D-M15), through the shared adapter _wasm_meter.js.
 * Scaffolded by tools/player2/new_meter.py. sim/test_meter_wasm_golden.py compares its command stream with the native C. */
(function (root) {
  const WM = typeof module !== 'undefined' ? require('./_wasm_meter.js') : root.TauWasmMeter;
  const api = WM.make({ key: '%(key)s', global: 'Tau%(camel)sWasm' });
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).%(key)s = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
'''
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0]); ap.add_argument("key"); ap.add_argument("--tag"); ap.add_argument("--label"); ap.add_argument("--no-generate", action="store_true")
    a = ap.parse_args(); key = a.key
    if not re.fullmatch(r"[a-z][a-z0-9_]{1,23}", key): sys.exit("the key is lower-case letters, digits and underscores, 2 to 24 characters")
    ms = manifests(); tag = (a.tag or re.sub(r"[^A-Z0-9]", "", key.upper())[:6]).upper(); label = (a.label or key.replace("_", " ").upper())
    if len(label) > 23 or not re.fullmatch(r"[A-Z][A-Z0-9]{1,5}", tag): sys.exit("the tag is 2 to 6 capitals/digits, the label at most 23 characters")
    if any(m["key"] == key for m in ms): sys.exit("a meter with the key %s exists" % key)
    if any(m.get("p2", {}).get("tag") == tag for m in ms): sys.exit("the tag %s is taken (--tag)" % tag)
    slot = max(m["p2"]["slot"] for m in ms if "p2" in m) + 1; index = max(m["index"] for m in ms) + 1
    if slot >= 7: sys.exit("seven meters is the limit for now (the METER list shows seven rows without scrolling; the saved presets already persist 20, cores/settings_core.h SF_MPRE*): add list scrolling for slot %d first" % slot)
    d = {"key": key, "enum": "VIZ_" + key.upper(), "index": index, "name": label, "selectable": True, "sel_index": 0, "caps": [], "cost_class": 2, "p2_only": True,
         "considered": "Scaffolded by tools/player2/new_meter.py: LEVELS lights a cell grid from the band levels, HISTORY scrolls the loudness. Replace this with what the meter shows and why.",
         "params": [{"key": "mode", "label": "Mode", "type": "enum", "values": ["LEVELS", "HISTORY"], "default": 0},
                    {"key": "gain", "label": "Gain", "type": "u8", "min": 50, "max": 200, "step": 10, "default": 100, "unit": "%", "note": "how bright / tall for a given level"},
                    {"key": "cell", "label": "Cell size", "type": "u8", "min": 5, "max": 12, "step": 1, "default": 8, "unit": "px", "note": "the cell is this many pixels square, with one pixel between cells"}],
         "p2": {"pack_only": True, "cmds": 1100, "max_cycles": 3000000, "slot": slot, "tag": tag, "compose": False, "budget": {"cold": 4096, "hot_ram": 2048}}, "default_preset": 0}
    presets = [{"name": label[:12], "values": {"mode": 0, "gain": 100, "cell": 8}}, {"name": "HISTORY", "values": {"mode": 1, "gain": 120, "cell": 6}}]
    files = {ROOT / "meters" / key / "meter.json": json.dumps(d, indent=2) + "\n",
             ROOT / "meters" / key / "presets.json": json.dumps({"presets": presets}, indent=2) + "\n",
             ROOT / "meters" / key / "thumb.json": json.dumps({"w": 56, "h": 32, "palette": [0x10A3, 0x5DDC, 0xEF5D, 0, 0, 0, 0, 0], "palette_roles": ["chrome", "accent", "text_primary", None, None, None, None, None], "rle": rle_preview(),
                                                               "note": "56x32 preview (scaffolded), 8-colour RGB565 palette (fallback) with theme roles, raster run-length: byte = (palette index << 5) | (run - 1)."}, indent=1) + "\n",
             ROOT / "fw2/ui" / ("meter_%s.c" % key): SRC % {"key": key, "TAG": tag, "lc": tag.lower(), "label": label},
             ROOT / "fw2/ui" / ("meter_pack_%s.c" % key): PACK % {"key": key, "lc": tag.lower(), "label": label},
             ROOT / "tools/meters/preview/wasm" / ("%s_wasm.c" % key): WASM % {"key": key, "TAG": tag, "lc": tag.lower(), "label": label},
             ROOT / "tools/meters/preview/meters" / ("%s.js" % key): ADAPTER % {"key": key, "label": label, "camel": "".join(x.capitalize() for x in key.split("_"))}}
    for p in files:
        if p.exists(): sys.exit("%s exists" % p)
    for p, t in files.items(): p.parent.mkdir(parents=True, exist_ok=True); p.write_text(t)
    print("created slot %d, manifest index %d, tag %s:" % (slot, index, tag)); [print("  " + str(p.relative_to(ROOT))) for p in files]
    if a.no_generate: return
    py = sys.executable
    # ALL modules are rebuilt: the generated fw2/generated/meter_presets.h gained this meter and every module records its hash (a stale module fails sim/test_meter_wasm_coverage.py)
    wasm_cmds = [[py, "tools/meters/preview/wasm/build_wasm.py"]] if Path("/opt/homebrew/opt/llvm/bin/clang").exists() and Path("/opt/homebrew/opt/lld/bin/wasm-ld").exists() else []
    if not wasm_cmds: print("  NOTE: Homebrew llvm + lld are not installed: the WebAssembly module of %s is NOT built (owner decision D-M15: every Player 2 meter has one; sim/test_meter_wasm_coverage.py fails until `python3 tools/meters/preview/wasm/build_wasm.py %s` has run)" % (key, key))
    for cmd in ([py, "tools/player2/gen_meters_p2.py"], [py, "tools/gen_meters.py"], [py, "tools/player2/pack_meter.py", key, "--out", "work/packs-p2/%s.tmpk" % key]) + tuple(wasm_cmds):
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        print("  $ " + " ".join(cmd[1:]) + ("" if r.returncode == 0 else "   FAILED\n" + r.stdout + r.stderr))
        if r.returncode: sys.exit(1)
    print("next: edit meters/%s/meter.json (parameters, presets, limits), fw2/ui/meter_%s.c and the thumbnail; `make test-host`; the pack goes to the Pocket in tau-assets.bin" % (key, key))
if __name__ == "__main__": main()
