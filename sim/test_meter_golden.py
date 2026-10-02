#!/usr/bin/env python3
"""Golden frames (meter module M3): the JS preview modules and the REAL firmware drawing functions must issue the same engine
commands for the same input trace. wviz_bars_tick() and wviz_scope_tick() are cut out of fw/player.c and compiled on the host with
stub engine calls that print each command; the same trace and parameters run through tools/meters/preview (node). Every preset of both
meters, 90 frames each, Dark and Light, including paused frames. A change to either side that alters drawing fails here, not on a card.
"""
import json, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = (ROOT / "fw" / "player.c").read_text()


def cut(sig):
    i = SRC.index(sig)
    j = SRC.index("\n}\n", i) + 3
    return SRC[i:j]


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define COLD_FN3
#define SPEC_BANDS 16u
#define WAVE_COLS 64u
#define WAVE_HW_COLS 256u
#define SCOPE_UNIT 100
#define WVIZ_BANDS_MIN 4u
#define WVIZ_BANDS_MAX 16u
#define REG(a) (*(volatile uint32_t *)0)   /* only reached in the compiled-out hardware path */
#define R_WAVE_CTL 0
#define R_WAVE_ST 0
#define R_WAVE_IDX 0
#define R_WAVE_DATA 0
#define FB_W 400u
#define UI_WHITE g_prim
#define UI_TRACK g_track
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#ifndef COLD_DATA
#define COLD_DATA
#endif
#include "meters_gen.h"
static uint16_t g_prim, g_track, ui_accent;
static uint8_t spec_lvl[16], paused, wave_hw, ui_fullscreen;
static signed char wav_v[64];
#include "meter_core.h"
static uint8_t wviz_force, wviz_disp[16], wviz_peak_drawn[16], wviz_drawn[16], wviz_scope_init;
static int16_t wviz_vel[16], wviz_scope_y[256];
static mtr_peak_t wviz_pk[16];
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { printf("rect %u %u %u %u %u\n", x, y, w, h, c); }
static void fb_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t l, uint16_t u) { printf("bar %u %u %u %u %u %u %u\n", x, y, w, h, lit, l, u); }
static void blit_probe_ensure(void) {}
#define BLIT_READY() 1
static void ui_bg_restore(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; }
static int ui_bg_blend(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t a) { (void)x; (void)y; (void)w; (void)h; (void)a; return 0; }   /* no blend bitstream: the trail falls back to the plain erase the JS twin models */
static uint32_t dbg_scope_blend_ok, dbg_scope_blend_fail;   /* B-413: cut into wviz_scope_tick(), stubbed here same as the other globals it touches */
static void dbg_strip_check(void) {}   /* B-433: same reasoning -- real body reads SDRAM via the mailbox, irrelevant to this host trace comparison */
static void dbg_pixel_log(uint32_t x, uint32_t y) { (void)x; (void)y; }   /* B-444: same reasoning */
''' + "@@BARS@@\n@@SCOPE@@\n" + r'''
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "r");
    int nscen; fscanf(f, "%d", &nscen);
    for (int s = 0; s < nscen; s++) {
        int isbars, np, nframes; unsigned acc, prim, track, bg, bx, by, bw, bh;
        fscanf(f, "%d %d %u %u %u %u %u %u %u %u %d", &isbars, &np, &acc, &prim, &track, &bg, &bx, &by, &bw, &bh, &nframes);
        unsigned p[8]; for (int i = 0; i < np; i++) fscanf(f, "%u", &p[i]);
        ui_accent = (uint16_t)acc; g_prim = (uint16_t)prim; g_track = (uint16_t)track;
        if (isbars) { for (int i = 0; i < np; i++) mtr_v_winamp_bars[i] = (uint16_t)p[i]; }
        else        { for (int i = 0; i < np; i++) mtr_v_winamp_scope[i] = (uint16_t)p[i]; }
        for (int i = 0; i < 16; i++) { wviz_disp[i] = 0; wviz_vel[i] = 0; wviz_pk[i].peak = 0; wviz_pk[i].vel = 0; wviz_pk[i].hold = 0; wviz_drawn[i] = 0; wviz_peak_drawn[i] = 0; }
        for (int i = 0; i < 256; i++) wviz_scope_y[i] = 0;
        wviz_scope_init = 0; wviz_force = 1;
        for (int n = 0; n < nframes; n++) {
            unsigned pz; fscanf(f, "%u", &pz); paused = (uint8_t)pz;
            for (int i = 0; i < 16; i++) { unsigned v; fscanf(f, "%u", &v); spec_lvl[i] = (uint8_t)v; }
            for (int i = 0; i < 64; i++) { int v; fscanf(f, "%d", &v); wav_v[i] = (signed char)v; }
            printf("F %d\n", n);
            mtr_in_t in = {0};
            in.spec = spec_lvl; in.wave = wav_v; in.force = wviz_force; in.dt_ms = 26u;
            in.x = (uint16_t)bx; in.y = (uint16_t)by; in.w = (uint16_t)bw; in.h = (uint16_t)bh; in.bg = (uint16_t)bg;
            if (isbars) wviz_bars_tick(&in); else wviz_scope_tick(&in, 0);
        }
        printf("S\n");
    }
    return 0;
}
'''


def main():
    extra = ["--trace", sys.argv[sys.argv.index("--trace") + 1]] if "--trace" in sys.argv else []
    node = subprocess.run(["node", str(ROOT / "tools/meters/preview/golden.js")] + extra, capture_output=True, text=True, cwd=ROOT)
    if node.returncode:
        print("golden.js failed:\n" + node.stderr); sys.exit(1)
    scen = json.loads(node.stdout)
    lines = [str(len(scen))]
    for s in scen:
        c = s["colors"]; b = s["box"]
        lines.append("%d %d %d %d %d %d %d %d %d %d %d" % (1 if s["key"] == "winamp_bars" else 0, len(s["params"]), c["accent"], c["prim"], c["track"], c["bg"], b["x"], b["y"], b["w"], b["h"], len(s["frames"])))
        lines.append(" ".join(str(v) for v in s["params"]))
        for fr in s["frames"]:
            lines.append(str(fr["paused"]) + " " + " ".join(str(v) for v in fr["spec"]) + " " + " ".join(str(v) for v in fr["wave"]))
    code = HARNESS.replace("@@BARS@@", cut("COLD_FN3 static void wviz_bars_tick")).replace("@@SCOPE@@", cut("COLD_FN3 static void wviz_scope_tick"))
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "h.c").write_text(code); (d / "in.txt").write_text("\n".join(lines) + "\n")
        r = subprocess.run(["cc", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-I", str(ROOT / "fw"), "-o", str(d / "h"), str(d / "h.c")], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr); sys.exit(1)
        out = subprocess.run([str(d / "h"), str(d / "in.txt")], capture_output=True, text=True, check=True).stdout.splitlines()
    # split the C output into scenarios and frames
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
    for s, cframes in zip(scen, got):
        for n, (jl, cl) in enumerate(zip(s["log"], cframes)):
            if jl != cl:
                fails += 1
                print(f"MISMATCH {s['name']} frame {n}: js {jl[:4]} ... vs c {cl[:4]} ...")
                break
    total = sum(len(x) for s in scen for x in s["log"])
    print("meter golden frames OK: %d scenarios, %d commands identical between the firmware code and the JS modules" % (len(scen), total) if not fails else "%d scenarios differ" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
