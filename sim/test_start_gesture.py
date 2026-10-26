#!/usr/bin/env python3
"""Host test for fw/start_gesture.h (B-522): Start opens Settings on release unless another button claimed the press; Start+Y is the Meter > Configure chord.
Drives the real sg_step() with scripted pad states the way poll_input() does (edge/fall derived from consecutive polls)."""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "%s"
#define START  (1u << 15)
#define Y      (1u << 7)
#define X      (1u << 6)
#define SELECT (1u << 14)
static int bad;
/* run a script of held-key states; returns per-poll: bit0 START edge seen by the rest of the code, bit1 chord, bit2 X edge seen */
static void run(const char *name, const uint32_t *seq, int n, int closed, int menu_ok, const char *want_start_edges, int want_chords)
{
    sg_t g = {0}; uint32_t prev = 0; int starts = 0, chords = 0; char got[64]; int gi = 0;
    for (int i = 0; i < n; i++) {
        uint32_t keys = seq[i], edge = keys & ~prev, fall = prev & ~keys; prev = keys;
        const uint32_t out = sg_step(&g, &edge, fall, keys, START, Y, SELECT, closed, menu_ok);
        if (edge & START) { starts++; got[gi++] = (char)('0' + i); }
        if (out & SG_CHORD) chords++;
    }
    got[gi] = 0;
    if (strcmp(got, want_start_edges) || chords != want_chords) { printf("FAIL %%s: Start edges at [%%s] want [%%s], chords %%d want %%d\n", name, got, want_start_edges, chords, want_chords); bad++; }
    else printf("ok   %%s\n", name);
}
int main(void) {
    { uint32_t s[] = {0, START, START, START, 0, 0};                run("tap: opens on release, not on press", s, 6, 1, 1, "4", 0); }
    { uint32_t s[] = {0, START, START|Y, START|Y, START, 0};        run("Start then Y: chord, no menu on release", s, 6, 1, 1, "", 1); }
    { uint32_t s[] = {0, Y, START|Y, START|Y, Y, 0};                run("Y then Start: chord, no menu on release", s, 6, 1, 1, "", 1); }
    { uint32_t s[] = {0, START, START|X, START|X, START, 0};        run("Start then X: X claims it, no menu", s, 6, 1, 1, "", 0); }
    { uint32_t s[] = {0, START, START|SELECT, START, 0};            run("Start then Select: claimed, no menu", s, 5, 1, 1, "", 0); }
    { uint32_t s[] = {0, SELECT, SELECT|START, SELECT, 0};          run("Select held first: Start passes through at once", s, 5, 1, 1, "2", 0); }
    { uint32_t s[] = {0, START, START, 0};                          run("Settings open: Start closes on press, release adds nothing", s, 4, 0, 1, "1", 0); }
    { uint32_t s[] = {0, START, START, 0};                          run("no cold image: Start keeps its old press meaning", s, 4, 1, 0, "1", 0); }
    { uint32_t s[] = {0, START, 0, START, 0};                       run("two taps open twice", s, 5, 1, 1, "24", 0); }
    { uint32_t s[] = {0, START, START|Y, 0, START, 0};              run("chord does not poison the next tap", s, 6, 1, 1, "5", 1); }
    { uint32_t s[] = {0, Y, 0};                                     run("Y alone is not a chord", s, 3, 1, 1, "", 0); }
    { uint32_t s[] = {0, START|Y, START|Y, 0};                      run("both pressed in the same poll: chord", s, 4, 1, 1, "", 1); }
    printf(bad ? "FAILED %%d\n" : "PASSED\n", bad);
    return bad != 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    src = Path(td) / "t.c"
    src.write_text("#include <string.h>\n" + HARNESS % str(ROOT / "fw/start_gesture.h"))
    exe = Path(td) / "t"
    r = subprocess.run(["cc", "-std=c11", "-Wall", "-Wno-unused-function", "-o", str(exe), str(src)], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    p = subprocess.run([str(exe)], capture_output=True, text=True)
    print(p.stdout.strip()); sys.exit(p.returncode)
