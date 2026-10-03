#!/usr/bin/env python3
"""Golden frames for the Layered Wave meter: the JS integer twin (tools/meters/preview/meters/layered_wave.js) and the REAL firmware code
(fw/layered_wave.inc, compiled on the host with stub engine calls) must issue the same engine commands, line for line, for the same input trace --
every preset plus a grid of views, draw modes, layer counts, splits, colour sources and boxes, with a paused stretch in the middle of each run.
A change to either side that alters drawing fails here, not on a card.
"""
import json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define COLD_FN3
#define COLD_DATA
#define WAVE_COLS 64u
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#include "meters_gen.h"
#include "meter_core.h"
static uint16_t ui_accent;
static uint8_t paused, wviz_force;
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { printf("rect %u %u %u %u %u\n", x, y, w, h, c); }
#include "layered_wave.inc"

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "r");
    int nscen; if (fscanf(f, "%d", &nscen) != 1) return 2;
    for (int s = 0; s < nscen; s++) {
        int nframes, chg, frc, geoat; unsigned bx, by, bw, bh, bx2, by2, bw2, bh2, role[12], pv2[64];
        if (fscanf(f, "%u %u %u %u %d %d %d %d %u %u %u %u", &bx, &by, &bw, &bh, &nframes, &chg, &frc, &geoat, &bx2, &by2, &bw2, &bh2) != 12) return 2;
        for (int i = 0; i < 12; i++) if (fscanf(f, "%u", &role[i]) != 1) return 2;
        for (int i = 0; i < MP_LAYERED_WAVE_NTOT; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; if (i < MP_LAYERED_WAVE_N) mtr_v_layered_wave[i] = (uint16_t)v; }
        for (int i = 0; i < MP_LAYERED_WAVE_NTOT; i++) if (fscanf(f, "%u", &pv2[i]) != 1) return 2;
        ui_accent = (uint16_t)role[0];
        th_role[TR_TEXT_PRIMARY] = (uint16_t)role[1]; th_role[TR_TEXT_SECONDARY] = (uint16_t)role[2]; th_role[TR_OK] = (uint16_t)role[3];
        th_role[TR_WARN] = (uint16_t)role[4]; th_role[TR_DANGER] = (uint16_t)role[5]; th_role[TR_PILL] = (uint16_t)role[6]; th_role[TR_ERROR] = (uint16_t)role[7];
        th_role[TR_SURFACE] = (uint16_t)role[8]; th_role[TR_SURFACE_TRACK] = (uint16_t)role[9]; th_role[TR_BASE] = (uint16_t)role[10]; th_role[TR_BG_BOTTOM] = (uint16_t)role[11];
        lw_init = 0; wviz_force = 1;
        for (int n = 0; n < nframes; n++) {
            unsigned pz; uint8_t spec[16]; int8_t wave[64];
            if (fscanf(f, "%u", &pz) != 1) return 2; paused = (uint8_t)pz;
            for (int i = 0; i < 16; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 2; spec[i] = (uint8_t)v; }
            for (int i = 0; i < 64; i++) { int v; if (fscanf(f, "%d", &v) != 1) return 2; wave[i] = (int8_t)v; }
            printf("F %d\n", n);
            if (n == chg) for (int i = 0; i < MP_LAYERED_WAVE_N; i++) if (i < MP_LAYERED_WAVE_N) mtr_v_layered_wave[i] = (uint16_t)pv2[i];
            mtr_in_t in = {0};
            in.paused = paused; in.spec = spec; in.wave = wave; in.force = (n == 0 || n == frc) ? 1 : 0; in.dt_ms = 26u;
            if (geoat >= 0 && n >= geoat) { in.x = (uint16_t)bx2; in.y = (uint16_t)by2; in.w = (uint16_t)bw2; in.h = (uint16_t)bh2; }
            else { in.x = (uint16_t)bx; in.y = (uint16_t)by; in.w = (uint16_t)bw; in.h = (uint16_t)bh; }
            lw_tick(&in);
        }
        printf("S\n");
    }
    return 0;
}
'''


def main():
    node = subprocess.run(["node", str(ROOT / "tools/meters/preview/golden_lw.js")], capture_output=True, text=True, cwd=ROOT)
    if node.returncode:
        print("golden_lw.js failed:\n" + node.stderr); sys.exit(1)
    scen = json.loads(node.stdout)
    EXP_DEF = {"hstyle": 0, "aa": 0, "bmode": 0, "balpha": 60, "guard": 0, "g1": 18, "g2": 18, "g3": 18, "g4": 18, "g5": 18, "g6": 18}
    names = json.loads((ROOT / "tools/meters_schema.json").read_text())["meters"]
    keys = [m for m in names if m["key"] == "layered_wave"][0]["params"]
    keys = [k["key"] for k in keys]
    fails = 0; total = 0
    for diag in (1, 0):
        # the main build has no experimental settings at all: only scenarios that leave them at their defaults must match there
        sc = [x for x in scen if diag or all(x["params"][keys.index(k)] == v and x["params2"][keys.index(k)] == v for k, v in EXP_DEF.items())]
        f, t = compare(sc, diag); fails += f; total += t
    print("layered wave golden frames OK: %d commands identical between the firmware code (Diagnostic and main build) and the JS twin" % total if not fails else "%d scenarios differ" % fails)
    sys.exit(1 if fails else 0)


def compare(scen, diag):
    lines = [str(len(scen))]
    for s in scen:
        b = s["box"]
        b2 = s.get("box2") or b
        lines.append("%d %d %d %d %d %d %d %d %d %d %d %d" % (b["x"], b["y"], b["w"], b["h"], len(s["frames"]), s["chg"], s.get("frc", -1), s.get("geoat", -1), b2["x"], b2["y"], b2["w"], b2["h"]))
        lines.append(" ".join(str(v) for v in s["roles"]))
        lines.append(" ".join(str(v) for v in s["params"]))
        lines.append(" ".join(str(v) for v in s["params2"]))
        for fr in s["frames"]:
            lines.append(str(fr["paused"]) + " " + " ".join(str(v) for v in fr["spec"]) + " " + " ".join(str(v) for v in fr["wave"]))
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "h.c").write_text(HARNESS); (d / "in.txt").write_text("\n".join(lines) + "\n")
        r = subprocess.run(["cc", "-O1", "-Wall", "-DTAU_DIAGNOSTIC=%d" % diag, "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-I", str(ROOT / "fw"), "-o", str(d / "h"), str(d / "h.c")], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr); sys.exit(1)
        out = subprocess.run([str(d / "h"), str(d / "in.txt")], capture_output=True, text=True, check=True).stdout.splitlines()
    got, cur, frame = [], None, None
    for ln in out:
        if ln.startswith("F "):
            if cur is None: cur = []
            frame = []; cur.append(frame)
        elif ln == "S":
            got.append(cur); cur = None
        else:
            frame.append(ln)
    fails = 0
    if len(got) != len(scen):
        print("scenario count differs: js %d, c %d" % (len(scen), len(got))); sys.exit(1)
    for s, cframes in zip(scen, got):
        for n, (jl, cl) in enumerate(zip(s["log"], cframes)):
            if jl != cl:
                fails += 1
                i = next((k for k in range(min(len(jl), len(cl))) if jl[k] != cl[k]), min(len(jl), len(cl)))
                print(f"MISMATCH {s['name']} frame {n}: command {i} js {jl[i:i+2]} vs c {cl[i:i+2]} (js {len(jl)} commands, c {len(cl)})")
                break
    total = sum(len(x) for s in scen for x in s["log"])
    return fails, total


main()
