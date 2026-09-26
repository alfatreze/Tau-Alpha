#!/usr/bin/env python3
"""Host test for fw/meter_core.h (M1.5). Two jobs:
 1. EQUIVALENCE: the core's functions equal the ORIGINAL code that lived in fw/player.c's wviz_bars_tick() (a verbatim copy is kept
    below as the reference) -- exhaustively for ease, over long randomised sequences for the peak cap, and for every band count for
    the band mapping and redraw cache. A ROM diff cannot prove this (the compiler orders code differently), so behaviour is compared.
 2. VECTORS: writes tools/meters/preview/fixtures/core_vectors.json, the golden vectors the JS twin (tau_core.js) is tested against.
    `--check` verifies the checked-in file is what the core produces now.
"""
import json, random, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VEC = ROOT / "tools" / "meters" / "preview" / "fixtures" / "core_vectors.json"

REFERENCE = r'''
/* ---- verbatim from fw/player.c before M1.5 -------------------------------------------------------- */
static uint8_t ref_ease(uint8_t cur, uint8_t target, uint32_t mode, uint32_t rate, int16_t *vel)
{
    if (mode == 0u) return target;
    if (mode == 1u) {
        int32_t step = 1 + (int32_t)rate / 6;
        int32_t d = (int32_t)target - (int32_t)cur;
        if (d > step) d = step; else if (d < -step) d = -step;
        return (uint8_t)((int32_t)cur + d);
    }
    if (mode == 2u) {
        int32_t k = 8 + ((int32_t)rate * 248) / 100;
        int32_t d = (((int32_t)target - (int32_t)cur) * k) / 256;
        return (uint8_t)((int32_t)cur + d);
    }
    {
        int32_t stiff = 6 + ((int32_t)rate * 58) / 100;
        int32_t accel = (((int32_t)target - (int32_t)cur) * stiff) / 256 - ((int32_t)*vel * stiff) / 512;
        int32_t v = (int32_t)*vel + accel;
        if (v > 60) v = 60; else if (v < -60) v = -60;
        *vel = (int16_t)v;
        int32_t p = (int32_t)cur + v;
        if (p < 0) p = 0; else if (p > 255) p = 255;
        return (uint8_t)p;
    }
}
typedef struct { uint8_t on, gravity; uint16_t hold_ms; uint8_t fall; } ref_cfg_t;
static void ref_peak(uint8_t *peak, uint8_t *pvel, uint16_t *phold, uint8_t disp, const ref_cfg_t *c, uint32_t dec_ms)
{
    if (c->on) {
        if (disp >= *peak) {
            *peak = disp; *phold = c->hold_ms; *pvel = 0u;
        } else if (*phold > 0u) {
            *phold = (*phold > dec_ms) ? (uint16_t)(*phold - dec_ms) : 0u;
        } else {
            uint32_t fall;
            if (c->gravity) {
                uint32_t nv = *pvel + 1u + c->fall / 20u;
                *pvel = (uint8_t)((nv > 255u) ? 255u : nv);
                fall = 1u + (*pvel >> 3);
            } else {
                fall = 1u + c->fall / 12u;
            }
            *peak = (*peak > fall) ? (uint8_t)(*peak - fall) : 0u;
            if (*peak < disp) *peak = disp;
        }
    } else {
        *peak = disp;
    }
}
static uint32_t ref_band(const uint8_t *spec, uint32_t nspec, uint32_t bands, uint32_t b)
{
    uint32_t lo = (b * nspec) / bands, hi = ((b + 1u) * nspec) / bands;
    if (hi <= lo) hi = lo + 1u;
    uint32_t sum = 0, n = 0;
    for (uint32_t k = lo; k < hi && k < nspec; k++) { sum += spec[k]; n++; }
    return n ? sum / n : 0u;
}
static int ref_delta(uint8_t *da, uint8_t *db, uint8_t a, uint8_t b, uint8_t force)
{
    if (!force && a == *da && b == *db) return 0;
    *da = a; *db = b; return 1;
}
/* ---------------------------------------------------------------------------------------------------- */
'''

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include "meter_core.h"
''' + REFERENCE + r'''
static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(int argc, char **argv) {
    int emit = argc > 1;
    long bad = 0;
    /* ease: every mode, every cur/target, sampled rates and spring velocities */
    for (uint32_t mode = 0; mode < 4; mode++)
        for (uint32_t rate = 1; rate <= 100; rate += 11)
            for (int vs = -60; vs <= 60; vs += 20)
                for (uint32_t cur = 0; cur < 256; cur++)
                    for (uint32_t tgt = 0; tgt < 256; tgt++) {
                        int16_t va = (int16_t)vs, vb = (int16_t)vs;
                        uint8_t a = mtr_ease((uint8_t)cur, (uint8_t)tgt, mode, rate, &va), b = ref_ease((uint8_t)cur, (uint8_t)tgt, mode, rate, &vb);
                        if (a != b || va != vb) bad++;
                    }
    /* peak: long random sequences, random configs incl. the five presets' shapes */
    for (int run = 0; run < 400; run++) {
        ref_cfg_t rc = { (uint8_t)(rnd() % 8 != 0), (uint8_t)(rnd() & 1), (uint16_t)(rnd() % 801), (uint8_t)(1 + rnd() % 100) };
        mtr_peak_cfg_t mc = { rc.on, rc.gravity, rc.hold_ms, rc.fall };
        mtr_peak_t p = {0, 0, 0}; uint8_t rp = 0, rv = 0; uint16_t rh = 0;
        uint32_t dt = 1 + rnd() % 60;
        for (int i = 0; i < 2000; i++) {
            uint8_t disp = (rnd() % 5 == 0) ? (uint8_t)rnd() : (uint8_t)((i * 3) & 0xFF) / (uint8_t)(1 + (i / 64) % 4);
            mtr_peak_step(&p, disp, &mc, dt); ref_peak(&rp, &rv, &rh, disp, &rc, dt);
            if (p.peak != rp || p.vel != rv || p.hold != rh) bad++;
        }
    }
    /* band mapping: every band count 4..16 over 16 source bands, random data */
    for (int t = 0; t < 2000; t++) {
        uint8_t spec[16]; for (int i = 0; i < 16; i++) spec[i] = (uint8_t)rnd();
        for (uint32_t bands = 1; bands <= 20; bands++) for (uint32_t b = 0; b < bands; b++)
            if (mtr_band_target(spec, 16, bands, b) != ref_band(spec, 16, bands, b)) bad++;
    }
    /* delta */
    for (int t = 0; t < 100000; t++) {
        uint8_t a1 = (uint8_t)rnd(), b1 = (uint8_t)rnd(), a2 = a1, b2 = b1, x = (uint8_t)(rnd() % 4 ? a1 : rnd()), y = (uint8_t)(rnd() % 4 ? b1 : rnd()), f = (uint8_t)(rnd() % 3 == 0);
        if (mtr_delta(&a1, &b1, x, y, f) != ref_delta(&a2, &b2, x, y, f) || a1 != a2 || b1 != b2) bad++;
    }
    printf("mismatches %ld\n", bad);
    if (emit) {                                     /* golden vectors for the JS twin */
        rng = 777u;
        printf("VEC_EASE\n");
        for (int i = 0; i < 300; i++) { uint32_t mode = i % 4, rate = 1 + rnd() % 100; uint8_t cur = (uint8_t)rnd(), tgt = (uint8_t)rnd(); int16_t v = (int16_t)((int)(rnd() % 121) - 60), v0 = v;
            uint8_t o = mtr_ease(cur, tgt, mode, rate, &v); printf("%u %u %u %u %d %u %d\n", mode, rate, cur, tgt, v0, o, v); }
        printf("VEC_PEAK\n");
        for (int run = 0; run < 12; run++) { mtr_peak_cfg_t c = { (uint8_t)(run % 6 != 5), (uint8_t)(run & 1), (uint16_t)(50 * (run % 9)), (uint8_t)(10 + run * 7) };
            mtr_peak_t p = {0,0,0}; printf("CFG %u %u %u %u\n", c.on, c.gravity, c.hold_ms, c.fall);
            for (int i = 0; i < 60; i++) { uint8_t d = (uint8_t)((i % 17 == 0) ? 200 : (rnd() % 120)); mtr_peak_step(&p, d, &c, 26); printf("%u %u %u %u\n", d, p.peak, p.vel, p.hold); } }
        printf("VEC_BAND\n");
        for (int i = 0; i < 40; i++) { uint8_t spec[16]; for (int k = 0; k < 16; k++) spec[k] = (uint8_t)rnd(); uint32_t bands = 4 + rnd() % 13;
            printf("%u", bands); for (int k = 0; k < 16; k++) printf(" %u", spec[k]); printf(" :"); for (uint32_t b = 0; b < bands; b++) printf(" %u", mtr_band_target(spec, 16, bands, b)); printf("\n"); }
    }
    return bad != 0;
}
'''


def build_run(emit):
    with tempfile.TemporaryDirectory() as d:
        c, exe = Path(d) / "h.c", Path(d) / "h"
        c.write_text(HARNESS)
        r = subprocess.run(["cc", "-O2", "-Wall", "-Werror", "-Wno-unused-function", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr); sys.exit(1)
        p = subprocess.run([str(exe)] + (["emit"] if emit else []), capture_output=True, text=True)
        return p.returncode, p.stdout


def parse_vectors(out):
    lines = out.splitlines()
    res = {"ease": [], "peak": [], "band": []}
    mode = None
    for ln in lines[1:]:
        if ln.startswith("VEC_"):
            mode = ln[4:].lower(); continue
        f = ln.split()
        if mode == "ease":
            m, rate, cur, tgt, v0, o, v1 = (int(x) for x in f)
            res["ease"].append({"mode": m, "rate": rate, "cur": cur, "target": tgt, "vel_in": v0, "out": o, "vel_out": v1})
        elif mode == "peak":
            if f[0] == "CFG":
                res["peak"].append({"cfg": {"on": int(f[1]), "gravity": int(f[2]), "hold_ms": int(f[3]), "fall": int(f[4])}, "steps": []})
            else:
                d, pk, vel, hold = (int(x) for x in f)
                res["peak"][-1]["steps"].append({"disp": d, "peak": pk, "vel": vel, "hold": hold})
        elif mode == "band":
            left, right = ln.split(":")
            a = [int(x) for x in left.split()]
            res["band"].append({"bands": a[0], "spec": a[1:], "out": [int(x) for x in right.split()]})
    return res


def main():
    rc, out = build_run(True)
    if rc:
        print("MISMATCH between meter_core.h and the original wviz_bars_tick code:\n" + out.splitlines()[0]); sys.exit(1)
    text = json.dumps(parse_vectors(out), separators=(",", ":")) + "\n"
    if "--check" in sys.argv:
        if not VEC.exists() or VEC.read_text() != text:
            print("core_vectors.json is stale; run sim/test_meter_core.py --write"); sys.exit(1)
    elif "--write" in sys.argv:
        VEC.parent.mkdir(parents=True, exist_ok=True); VEC.write_text(text); print("wrote", VEC)
    print("meter core OK (equal to the original code; vectors " + ("checked" if "--check" in sys.argv else "computed") + ")")


if __name__ == "__main__":
    main()
