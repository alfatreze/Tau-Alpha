#!/usr/bin/env python3
"""Golden frames for Winamp Bars, Winamp Scope, Master VU and Chladni (owner decision D-M15: every Player 2 meter is a WebAssembly module of its firmware source): the lab's module (tools/meters/preview/meters/<key>_wasm.js
through its adapter meters/<key>.js, which the lab and Tau Omega run) and the REAL firmware source (fw2/ui/meter_<key>.c, compiled natively with printing stubs for the engine calls and a model of the hardware it reads) must
issue the same engine commands, line for line, for the same input trace: every factory preset, each parameter at its lowest and highest value, the normal and the fullscreen box, a pause, the degrade levels, a forced repaint,
a moved box, and the Scope with and without the pipelined blend. A change to a meter or its module that alters drawing fails here, not on a card. Also: every committed module is current, and a control (another
parameter) must differ.  Needs node. Host-simulated only (Chladni's mailbox, copy and scaled blit are models, as the lab's framebuffer has them)."""
import contextlib, io, json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
META = {   # key -> (source, extra includes, how a tick is made, parameter count)
    "winamp_bars": ("meter_winamp_bars.c", '#include "../cores/meter_core.h"\n', "wb_load(params); meter_winamp_bars_tick(&in);", "MPP_BARS_N"),
    "vu_master": ("meter_vu_master.c", '#include "../cores/meter_core.h"\n#include "../cores/vu_master_core.h"\n', "vu_load(params); meter_vu_master_tick(&in);", "MPP_VU_N"),
    "winamp_scope": ("meter_winamp_scope.c", "", "ws_load(params); meter_winamp_scope_tick(&in);", "MPP_SCOPE_N"),
    "chladni": ("meter_chladni.c", '#include "../cores/ramp_core.h"\n#define CHL_DATA\n#include "../cores/chladni_core.h"\n', "live_v = params; ch_load(params); meter_chladni_tick(&in);", "MPP_CHL_N"),
}
def _generic():
    """the meters made by tools/player2/new_meter.py (their source is scaffolded: <tag>_load / <tag>_tick), every Player 2 meter with a module and no golden test of its own"""
    out = {}
    for p in sorted((ROOT / "meters").glob("*/meter.json")):
        m = json.loads(p.read_text())
        if "p2" not in m or m["key"] in META or m["key"] in ("layered_wave", "sound_map", "waterfall_grid"): continue
        if not (ROOT / f"tools/meters/preview/meters/{m['key']}_wasm.js").is_file(): continue
        tag = m["p2"]["tag"]; lc = tag.lower()
        out[m["key"]] = (f"meter_{m['key']}.c", '#include "../cores/ramp_core.h"\n', f"{lc}_load(params); {lc}_tick(&in);", f"MPP_{tag}_N")
    return out
META.update(_generic())
HARNESS = r'''
#define P2_PACK 1
#define P2_HOST 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t cap_buf[256], cap_idx; static int blend_flag;
static uint32_t reg_rd(uint32_t a) { return a == 0x800000F4u ? cap_buf[cap_idx & 255u] : 0u; }
static void reg_wr(uint32_t a, uint32_t v) { if (a == 0x800000F0u) cap_idx = v; }
#define REG_RD(a) reg_rd(a)
#define REG_WR(a, v) reg_wr((a), (v))
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../generated/meter_presets.h"
static uint16_t mem[512 * 1024];                                  /* the engine's memory beyond the visible frame (pitch 512), as tau_fb.js models it */
static void p6(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { printf("%s %u %u %u %u %u %u\n", n, a, b, c, d, e, f); }
static void p5(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { printf("%s %u %u %u %u %u\n", n, a, b, c, d, e); }
static void pr_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint32_t fg, uint32_t bg) { printf("bar %u %u %u %u %u %u %u\n", x, y, w, h, lit, fg, bg); }
static uint32_t pr_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) { printf("text %u %u %s %u %u\n", x, y, s, fg, bg); return 0; }
static void pr_sblit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t s) { printf("sblit %u %u %u %u %u %u %u\n", a, b, c, d, e, f, s); }
static void pr_babs(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { p6("blit_abs", sx, sy, dx, dy, w, h); for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) mem[(dy + y) * 512 + dx + x] = mem[(sy + y) * 512 + sx + x]; }
static int pr_mbw(uint32_t a, uint32_t d) { printf("mb_write %u %u\n", a, d); mem[a] = (uint16_t)d; mem[a + 1] = (uint16_t)(d >> 16); return 0; }
static int pr_mbr(uint32_t a, uint32_t *d) { printf("mb_read %u\n", a); *d = (uint32_t)mem[a] | ((uint32_t)mem[a + 1] << 16); return 0; }
static void pr_bon(uint32_t a) { printf("blend_on %u\n", a); }
#define gfx_rect(x, y, w, h, c)       p5("rect", x, y, w, h, c)
#define gfx_rect_stash(x, y, w, h, c) p5("rect_stash", x, y, w, h, c)
#define gfx_bar(x, y, w, h, l, f, b)  pr_bar(x, y, w, h, l, f, b)
#define gfx_text(x, y, s, f, b)       pr_text(x, y, s, f, b)
#define gfx_blit(a, b, c, d, e, f)    p6("blit", a, b, c, d, e, f)
#define gfx_blit_abs(a, b, c, d, e, f) pr_babs(a, b, c, d, e, f)
#define gfx_sblit(a, b, c, d, e, f, s) pr_sblit(a, b, c, d, e, f, s)
#define gfx_fence()                   printf("fence\n")
#define gfx_mb_write(a, d)            pr_mbw(a, d)
#define gfx_mb_read(a, d)             pr_mbr(a, d)
#define gfx_blend_ready()             blend_flag
#define gfx_blend_on(a)               pr_bon(a)
#define gfx_blend_blit(a, b, c, d, e, f) p6("blend_blit", a, b, c, d, e, f)
#define gfx_blend_off()               printf("blend_off\n")
uint16_t th_role[TR_COUNT];
@INCLUDES@
#include "@SRC@"
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "r"); int nframes; if (fscanf(f, "%d %d", &blend_flag, &nframes) != 2) return 2;
    for (int i = 0; i < TR_COUNT; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; th_role[i] = (uint16_t)v; }
    uint16_t params[@NPARAMS@];
    for (int i = 0; i < @NPARAMS@; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; params[i] = (uint16_t)v; }
    for (int n = 0; n < nframes; n++) {
        unsigned pz, dg, fc, x, y, w, h, pl, pr; uint8_t spec[16];
        if (fscanf(f, "%u %u %u %u %u %u %u %u %u", &pz, &dg, &fc, &x, &y, &w, &h, &pl, &pr) != 9) return 2;
        for (int i = 0; i < 16; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; spec[i] = (uint8_t)v; }
        for (int i = 0; i < 256; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; cap_buf[i] = v; }
        printf("F %d\n", n);
        mtr_in_t in = {0}; in.spec = spec; in.frame = (uint32_t)n; in.dt_ms = 26; in.x = (uint16_t)x; in.y = (uint16_t)y; in.w = (uint16_t)w; in.h = (uint16_t)h;
        in.bg = 0x18C3; in.force = (uint8_t)fc; in.paused = (uint8_t)pz; in.degrade = (uint8_t)dg; in.peak_l = pl; in.peak_r = pr;
        @TICK@
    }
    return 0;
}
'''
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
def native(key, scen, tmp):
    src, inc, tick, npar = META[key]
    text = HARNESS.replace("@INCLUDES@", inc).replace("@SRC@", src).replace("@TICK@", tick).replace("@NPARAMS@", npar)
    d = Path(tmp) / key; d.mkdir(exist_ok=True); (d / "h.c").write_text(text)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2"), "-I", str(ROOT / "fw"), "-o", str(d / "h"), str(d / "h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr[-1800:]); sys.exit(1)
    got = []
    for s in scen:
        lines = ["%d %d" % (s["blend"], len(s["frames"])), " ".join(str(v) for v in s["roles"]), " ".join(str(v) for v in s["params"])]
        for fr in s["frames"]:
            b = fr["box"]
            lines.append("%d %d %d %d %d %d %d %d %d %s %s" % (fr["paused"], fr["degrade"], fr["force"], b["x"], b["y"], b["w"], b["h"], fr["pl"], fr["pr"], " ".join(str(v) for v in fr["spec"]), " ".join(str(v) for v in fr["cap"])))
        (d / "in.txt").write_text("\n".join(lines) + "\n")
        out = subprocess.run([str(d / "h"), str(d / "in.txt")], capture_output=True, text=True, check=True).stdout.splitlines()
        frames = []
        for ln in out:
            if ln.startswith("F "): frames.append([])
            else: frames[-1].append(ln)
        got.append(frames)
    return got
def compare(scen, got):
    bad = total = 0
    for s, cframes in zip(scen, got):
        for n, (wl, cl) in enumerate(zip(s["log"], cframes)):
            total += len(wl)
            if wl != cl:
                i = next((k for k in range(min(len(wl), len(cl))) if wl[k] != cl[k]), min(len(wl), len(cl)))
                print(f"MISMATCH {s['name']} frame {n}: command {i} wasm {wl[i:i+2]} vs c {cl[i:i+2]} (wasm {len(wl)} commands, c {len(cl)})"); bad += 1; break
    return bad, total
if subprocess.run(["node", "--version"], capture_output=True).returncode:
    print("node not found: skipped"); sys.exit(0)
for key in META:
    r = subprocess.run([sys.executable, str(ROOT / "tools/meters/preview/wasm/build_wasm.py"), "--check", key], capture_output=True, text=True)
    check("%s: the committed module is current" % key, r.returncode == 0, r.stdout[-200:])
node = subprocess.run(["node", str(ROOT / "tools/meters/preview/golden_meters.js")], capture_output=True, text=True, cwd=ROOT)
if node.returncode: print(node.stderr[-1500:]); sys.exit(1)
allscen = json.loads(node.stdout)
with tempfile.TemporaryDirectory() as tmp:
    for key in META:
        scen = [s for s in allscen if s["key"] == key]
        bad, total = compare(scen, native(key, scen, tmp))
        kinds = sorted({l.split()[0] for s in scen for f in s["log"] for l in f})
        check("%s: every scenario draws identically through the lab's module and the real source: %d commands in %d scenarios (%s)" % (key, total, len(scen), ", ".join(kinds)), bad == 0 and total > 1000, (bad, total))
    # a control: the real source with another parameter must differ from the module's log
    s0 = next(s for s in allscen if s["key"] == "winamp_bars" and s["name"].startswith("winamp_bars/preset")); s1 = json.loads(json.dumps(s0)); s1["params"][0] = (s1["params"][0] + 3) % 16
    with contextlib.redirect_stdout(io.StringIO()): bad2, _ = compare([s0], native("winamp_bars", [s1], tmp))
    check("control: a scenario with another parameter is a mismatch", bad2 == 1)
    kinds_all = {l.split()[0] for s in allscen for f in s["log"] for l in f}
    check("the traces use the engine calls of the four meters (%s)" % ", ".join(sorted(kinds_all)), {"rect", "bar", "text", "rect_stash", "blend_on", "blend_blit", "blend_off", "blit", "blit_abs", "sblit", "fence", "mb_write", "mb_read"} <= kinds_all, kinds_all)
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
