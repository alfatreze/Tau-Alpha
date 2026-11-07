#!/usr/bin/env python3
"""Golden frames for the Waterfall Grid meter (B-983): the lab's WebAssembly module (tools/meters/preview/meters/waterfall_grid_wasm.js, the pack's own source compiled by build_wasm.py; its adapter meters/waterfall_grid.js is what
the lab and Tau Omega run) and the REAL firmware source (fw2/ui/meter_waterfall_grid.c, compiled natively with printing stubs for the engine calls) must issue the same engine commands, line for line, for the same input trace:
every factory preset plus a grid of modes, flows, palettes and cell sizes, a pause, the degrade levels, a forced repaint, a moved box and the fullscreen box. A change to the meter or the module that alters drawing fails
here, not on a card. Also: the committed module is current, and a control (another parameter) must differ.  Needs node. Host-simulated only."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#define P2_PACK 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "../cores/meter_core.h"
static void pr_rect(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { printf("%s %u %u %u %u %u\n", n, a, b, c, d, e); }
static void pr_six(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { printf("%s %u %u %u %u %u %u\n", n, a, b, c, d, e, f); }
#define gfx_rect(x, y, w, h, c)       pr_rect("rect", x, y, w, h, c)
#define gfx_rect_stash(x, y, w, h, c) pr_rect("rect_stash", x, y, w, h, c)
#define gfx_scroll_down(x, y, w, h, d) pr_rect("scroll_down", x, y, w, h, d)
#define gfx_scroll_left(x, y, w, h, d) pr_rect("scroll_left", x, y, w, h, d)
#define gfx_cblit(a, b, c, d, e, f)   pr_six("cblit", a, b, c, d, e, f)
static void pr_clut(uint32_t start, const uint16_t *c, uint32_t n) { printf("clut_load %u %u", start, n); for (uint32_t i = 0; i < n; i++) printf(" %u", c[i]); printf("\n"); }
#define gfx_clut_load(s, c, n)        pr_clut(s, c, n)
#define gfx_text(...)                 0
uint16_t th_role[TR_COUNT];
#include "meter_waterfall_grid.c"
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "r"); int nscen; if (fscanf(f, "%d", &nscen) != 1) return 2;
    for (int s = 0; s < nscen; s++) {
        int nframes; if (fscanf(f, "%d", &nframes) != 1) return 2;
        for (int i = 0; i < TR_COUNT; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; th_role[i] = (uint16_t)v; }
        uint16_t params[MPP_WF_N];
        for (int i = 0; i < MPP_WF_N; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; params[i] = (uint16_t)v; }
        wf_load(params);
        for (int n = 0; n < nframes; n++) {
            unsigned pz, dg, fc, x, y, w, h; uint8_t spec[16];
            if (fscanf(f, "%u %u %u %u %u %u %u", &pz, &dg, &fc, &x, &y, &w, &h) != 7) return 2;
            for (int i = 0; i < 16; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; spec[i] = (uint8_t)v; }
            printf("F %d\n", n);
            mtr_in_t in = {0}; in.spec = spec; in.frame = (uint32_t)n; in.dt_ms = 26; in.x = (uint16_t)x; in.y = (uint16_t)y; in.w = (uint16_t)w; in.h = (uint16_t)h;
            in.bg = 0x18C3; in.force = (uint8_t)fc; in.paused = (uint8_t)pz; in.degrade = (uint8_t)dg;
            wf_tick(&in);
        }
        printf("S\n");
    }
    return 0;
}
'''
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
def native(scen):
    """one native process per scenario (the meter keeps its state in statics, and the module's instance starts fresh for each scenario)"""
    with tempfile.TemporaryDirectory() as d:
        d = Path(d); (d / "h.c").write_text(HARNESS)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2"), "-I", str(ROOT / "fw"), "-o", str(d / "h"), str(d / "h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr[-1500:]); sys.exit(1)
        got = []
        for k, s in enumerate(scen):
            lines = ["1", str(len(s["frames"])), " ".join(str(v) for v in s["roles"]), " ".join(str(v) for v in s["params"])]
            for fr in s["frames"]:
                b = fr["box"]
                lines.append("%d %d %d %d %d %d %d %s" % (fr["paused"], fr["degrade"], fr["force"], b["x"], b["y"], b["w"], b["h"], " ".join(str(v) for v in fr["spec"])))
            (d / "in.txt").write_text("\n".join(lines) + "\n")
            out = subprocess.run([str(d / "h"), str(d / "in.txt")], capture_output=True, text=True, check=True).stdout.splitlines()
            frames = []
            for ln in out:
                if ln.startswith("F "): frames.append([])
                elif ln != "S": frames[-1].append(ln)
            got.append(frames)
    return got
def compare(scen, got):
    bad = 0; total = 0
    for s, cframes in zip(scen, got):
        for n, (wl, cl) in enumerate(zip(s["log"], cframes)):
            total += len(wl)
            if wl != cl:
                i = next((k for k in range(min(len(wl), len(cl))) if wl[k] != cl[k]), min(len(wl), len(cl)))
                print(f"MISMATCH {s['name']} frame {n}: command {i} wasm {wl[i:i+2]} vs c {cl[i:i+2]} (wasm {len(wl)} commands, c {len(cl)})"); bad += 1; break
    return bad, total
if subprocess.run(["node", "--version"], capture_output=True).returncode:
    print("node not found: skipped"); sys.exit(0)
r = subprocess.run([sys.executable, str(ROOT / "tools/meters/preview/wasm/build_wasm.py"), "--check", "waterfall_grid"], capture_output=True, text=True)
check("the committed module is current (%s)" % r.stdout.strip().splitlines()[-1][:100], r.returncode == 0, r.stdout + r.stderr)
node = subprocess.run(["node", str(ROOT / "tools/meters/preview/golden_wf.js")], capture_output=True, text=True, cwd=ROOT)
if node.returncode: print(node.stderr); sys.exit(1)
scen = json.loads(node.stdout)
got = native(scen)
check("one native run per scenario (%d)" % len(scen), len(got) == len(scen), (len(got), len(scen)))
bad, total = compare(scen, got)
check("every scenario draws identically through the lab's module and the real source: %d commands in %d scenarios" % (total, len(scen)), bad == 0 and total > 20000, (bad, total))
kinds = {l.split()[0] for s in scen for f in s["log"] for l in f}
check("the trace uses every engine call of the meter (%s)" % ", ".join(sorted(kinds)), {"rect", "rect_stash", "clut_load", "cblit", "scroll_down", "scroll_left"} <= kinds, kinds)
mut = json.loads(node.stdout); k = next(i for i, x in enumerate(mut) if x["name"].startswith("grid/m0f0")); mut[k]["params"][1] += 7      # another speed in a spectrogram: the logs must differ from the native run's
import contextlib, io
with contextlib.redirect_stdout(io.StringIO()): bad2, _ = compare(mut[k:k + 1], native(mut[k:k + 1]))
check("control: a scenario with another speed is a mismatch", bad2 == 1)
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
