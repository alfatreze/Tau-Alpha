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


COLOUR_C = r'''
#include <stdio.h>
#include "meter_core.h"
int main(void) {
    unsigned s = 12345u;
    for (int k = 0; k < 20000; k++) {
        s = s * 1664525u + 1013904223u; uint16_t a = (uint16_t)(s >> 8);
        s = s * 1664525u + 1013904223u; uint16_t b = (uint16_t)(s >> 8);
        s = s * 1664525u + 1013904223u; uint32_t n = 1u + (s >> 8) % 200u;
        s = s * 1664525u + 1013904223u; uint32_t t = (s >> 8) % (n + 1u);
        s = s * 1664525u + 1013904223u; int32_t t256 = (int32_t)((s >> 8) % 257u);
        s = s * 1664525u + 1013904223u; uint32_t r = (s >> 8) % (n + 1u);
        printf("%u %u %u %u %d %u %u %u\n", a, b, n, t, t256, r, mtr_ramp(a, b, t, n), mtr_mix256(a, b, t256));
        printf("L %u %u %u\n", mtr_ladder(a, b, (uint16_t)(a ^ b), r % n, n), 0u, 0u);
    }
    return 0;
}
'''


def check_colour():
    """mtr_ramp / mtr_mix256 / mtr_ladder against independent Python references of the arithmetic ui_mix and lw_mix always used."""
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / "c.c"; c.write_text(COLOUR_C); exe = Path(d) / "c"
        r = subprocess.run(["cc", "-O1", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    def ramp(a, b, t, n):
        rr = (((a >> 11) & 31) * (n - t) + ((b >> 11) & 31) * t) // n
        gg = (((a >> 5) & 63) * (n - t) + ((b >> 5) & 63) * t) // n
        bb = ((a & 31) * (n - t) + (b & 31) * t) // n
        return (rr << 11) | (gg << 5) | bb
    def mix256(a, b, t):
        ar, ag, ab, br, bg, bb = (a >> 11) & 31, (a >> 5) & 63, a & 31, (b >> 11) & 31, (b >> 5) & 63, b & 31
        return ((ar + (((br - ar) * t) >> 8)) << 11) | ((ag + (((bg - ag) * t) >> 8)) << 5) | (ab + (((bb - ab) * t) >> 8))
    for i in range(0, len(out), 2):
        a, b, n, t, t256, r, gr, gm = (int(x) for x in out[i].split())
        if gr != ramp(a, b, t, n) or gm != mix256(a, b, t256):
            print("colour MISMATCH", out[i]); sys.exit(1)
        mid = a ^ b; half = n // 2; rr = r % n
        want = ramp(a, b, rr, half) if rr < half else ramp(b, mid, rr - half, n - half)
        if int(out[i + 1].split()[1]) != want:
            print("ladder MISMATCH", out[i], out[i + 1]); sys.exit(1)
    print("colour helpers OK (mtr_ramp, mtr_mix256, mtr_ladder equal the original arithmetic over 20000 random cases)")


GEOM_C = r'''
#include <stdio.h>
#include "meter_core.h"
int main(void) {
    unsigned s = 777u;
    #define R() (s = s * 1664525u + 1013904223u, s >> 8)
    for (int k = 0; k < 20000; k++) {
        uint32_t x0 = R() % 100u, w = 1u + R() % 500u, n = 1u + R() % 80u, i = R() % n, gap = R() % 8u, x, lit, cw;
        mtr_col_span(x0, w, n, i, gap, &x, &lit); mtr_col_cw(x0, w, n, i, &x, &cw);
        uint32_t h = 1u + R() % 400u, full = 1u + R() % 40000u, v = R() % 70000u;
        int32_t ey = 1 + (int32_t)(R() % 200u), unit = 1 + (int32_t)(R() % 32000u), sv = (int32_t)(R() % 256u) - 128;
        int32_t px = (int32_t)(R() % 600u) - 50, py = (int32_t)(R() % 500u) - 50, sz = 1 + (int32_t)(R() % 3u);
        printf("%u %u %u %u %u %u %u %u %u %u %u %d %d %d %d %d %d %d %u %d %d\n", x0, w, n, i, gap, x, lit, cw, h, full, v, ey, unit, sv,
               px, py, sz, mtr_scale_s(sv, ey, unit), mtr_scale_u(v, h, full), mtr_in_box(px, py, sz, (int32_t)x0, 20, (int32_t)w, (int32_t)h), 0);
    }
    return 0;
}
'''


def check_geometry():
    """The geometry helpers against independent Python references of the arithmetic the meters wrote inline."""
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / "g.c"; c.write_text(GEOM_C); exe = Path(d) / "g"
        r = subprocess.run(["cc", "-O1", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    def cdiv(a, b):   # C division truncates toward zero
        q = abs(a) // abs(b); return q if (a >= 0) == (b > 0) else -q
    for ln in out:
        (x0, w, n, i, gap, x, lit, cw, h, full, v, ey, unit, sv, px, py, sz, ss, su, inb, _) = (int(a) for a in ln.split())
        a_, b_ = x0 + (i * w) // n, x0 + ((i + 1) * w) // n
        ok = x == a_ and lit == ((b_ - a_ - gap) if b_ - a_ > gap else 1) and cw == ((b_ - a_) if b_ > a_ else 1)
        want_s = max(-ey, min(ey, cdiv(sv * ey, unit)))
        want_u = min(h, (v * h) // full)
        want_b = int(px >= x0 and px + sz <= x0 + w and py >= 20 and py + sz <= 20 + h)
        if not ok or ss != want_s or su != want_u or inb != want_b:
            print("geometry MISMATCH", ln); sys.exit(1)
    print("geometry helpers OK (mtr_col_span, mtr_col_cw, mtr_scale_u, mtr_scale_s, mtr_in_box equal the original arithmetic over 20000 random cases)")


CACHE_C = r'''
#include <stdio.h>
#include "meter_core.h"
int main(void) {
    uint8_t a = 7, b = 9, c = 3;
    printf("%d", mtr_delta1(&c, 3, 0)); printf("%d", mtr_delta1(&c, 4, 0)); printf("%d", (int)c); printf("%d", mtr_delta1(&c, 4, 1)); printf("%d", mtr_delta1(&c, 4, 0));
    printf(" %d", mtr_delta(&a, &b, 7, 9, 0)); printf("%d", mtr_delta(&a, &b, 7, 10, 0)); printf("%d%d", (int)a, (int)b);
    uint8_t arr[5] = {1, 2, 3, 4, 5}; mtr_invalidate(arr, 4);
    printf(" %u %u %u %u %u", arr[0], arr[1], arr[2], arr[3], arr[4]);
    /* a stale cell is redrawn by both forms without force */
    uint8_t s1 = MTR_STALE, s2 = MTR_STALE;
    printf(" %d%d", mtr_delta1(&s1, 0, 0), mtr_delta(&s1, &s2, 0, 0, 0));
    printf("\n");
    return 0;
}
'''


def check_cache():
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / "c.c"; c.write_text(CACHE_C); exe = Path(d) / "c"
        r = subprocess.run(["cc", "-O1", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.strip()
    want = "01410 01710 255 255 255 255 5 11"
    if out != want:
        print("cache MISMATCH: got %r want %r" % (out, want)); sys.exit(1)
    print("cache helpers OK (mtr_delta1, mtr_invalidate, stale cells redraw without force)")


SIG_C = r'''
#include <stdio.h>
#include "meter_core.h"
int main(void) {
    unsigned s = 4242u;
    #define R() (s = s * 1664525u + 1013904223u, s >> 8)
    for (int k = 0; k < 3000; k++) {
        uint8_t lvl[16], prev[16]; int32_t w[16], e[16];
        const int quiet = (k % 7) == 0, zero = (k % 29) == 0;
        for (int b = 0; b < 16; b++) { lvl[b] = zero ? 0 : (uint8_t)(quiet ? R() % 3u : R() % 256u); prev[b] = (uint8_t)(R() % 256u); w[b] = (int32_t)(R() % 4096u); e[b] = (int32_t)(R() % 65000u); }
        uint32_t ema = R() % 5000u, sens = 8u + R() % 40u, elapsed = R() % 600u, refr = R() % 300u, peak = (k % 5) ? R() % 3u : 0u;
        int32_t up = (int32_t)(R() % 300u), dn = (int32_t)(R() % 300u), div = 1 + (int32_t)(R() % 64u);
        printf("%u %u", mtr_energy(lvl, 16), (unsigned)mtr_silent(lvl, 16, peak));
        mtr_slew_pow(w, lvl, 16, up, dn); mtr_ema_pow(e, lvl, 16, div);
        uint32_t ema0 = ema; int fire = mtr_onset_flux(prev, &ema, lvl, 16, sens, elapsed, refr);
        printf(" %d %u", fire, ema);
        for (int b = 0; b < 16; b++) printf(" %d %d", w[b], e[b]);
        printf(" |");
        for (int b = 0; b < 16; b++) printf(" %u %u", lvl[b], prev[b]);
        printf(" | %u %u %u %u %d %d %d\n", ema0, sens, elapsed, refr, up, dn, div);
        /* the inputs w0/e0 are regenerated by the python side from the same LCG, see below */
    }
    return 0;
}
'''


def check_signals():
    """The signal functions against Python transcriptions of the arithmetic Chladni (chl_energy/chl_update/chl_detect) and
    Layered Wave (lw_learn) carried before they were moved into the core. The C side prints its results; the LCG replay supplies the inputs."""
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / "c.c"; c.write_text(SIG_C); exe = Path(d) / "c"
        r = subprocess.run(["cc", "-O1", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    s = [4242]
    def R():
        s[0] = (s[0] * 1664525 + 1013904223) & 0xFFFFFFFF; return s[0] >> 8
    def cdiv(a, b):
        q = abs(a) // abs(b); return q if (a >= 0) == (b > 0) else -q
    for k, ln in enumerate(out):
        quiet, zero = k % 7 == 0, k % 29 == 0
        lvl = []; prev = []; w = []; e = []
        for b in range(16):
            lvl.append(0 if zero else (R() % 3 if quiet else R() % 256)); prev.append(R() % 256); w.append(R() % 4096); e.append(R() % 65000)
        ema = R() % 5000; sens = 8 + R() % 40; elapsed = R() % 600; refr = R() % 300; peak = (R() % 3) if (k % 5) else 0
        up = R() % 300; dn = R() % 300; div = 1 + R() % 64
        head, rest = ln.split(" |", 1)
        f = [int(x) for x in head.split()]
        en, sil, fire, ema_out = f[0], f[1], f[2], f[3]
        got_w = f[4::2][:16]; got_e = f[5::2][:16]
        want_en = sum(lvl) >> 4
        want_sil = int(peak == 0 and not any(lvl))
        nw = []
        for b in range(16):
            t = (lvl[b] * lvl[b]) >> 4; dd = t - w[b]
            if dd > up: dd = up
            if dd < -dn: dd = -dn
            nw.append(w[b] + dd)
        ne = [e[b] + cdiv(lvl[b] * lvl[b] - e[b], div) for b in range(16)]
        rise = sum(lvl[b] - prev[b] for b in range(16) if lvl[b] > prev[b])
        thr = ((sens * ema) >> 12) + 6
        want_fire = int(rise > thr and elapsed > refr)
        d = ((rise << 8) - ema) & 0xFFFFFFFF
        d = d - (1 << 32) if d & 0x80000000 else d
        want_ema = (ema + (d >> 5)) & 0xFFFFFFFF
        if (en, sil, fire, ema_out) != (want_en, want_sil, want_fire, want_ema) or got_w != nw or got_e != ne:
            print("signals MISMATCH at case", k, (en, sil, fire, ema_out), (want_en, want_sil, want_fire, want_ema)); sys.exit(1)
    print("signal functions OK (mtr_energy, mtr_silent, mtr_slew_pow, mtr_ema_pow, mtr_onset_flux equal the Chladni and Layered Wave originals over 3000 random cases)")


DER_C = r'''
#include <stdio.h>
#include "meter_core.h"
int main(void) {
    unsigned s = 99u;
    #define R() (s = s * 1664525u + 1013904223u, s >> 8)
    for (int k = 0; k < 4000; k++) {
        const int quiet = (k % 5) == 0;
        uint64_t ll = quiet ? (R() %% 5000u) : ((uint64_t)R() << 24) % 0x10000000000ull, rr = quiet ? (R() %% 5000u) : ((uint64_t)R() << 24) % 0x10000000000ull;
        int64_t lr = (int64_t)((ll < rr ? ll : rr) / 1) * ((R() %% 3u) == 0 ? -1 : 1) / (1 + (int64_t)(R() %% 4u));
        if (k %% 11 == 0) { rr = ll; lr = (int64_t)ll; }
        if (k %% 13 == 0) { rr = ll; lr = -(int64_t)ll; }
        uint32_t peak = R() %% 32769u, rms = (k %% 17 == 0) ? 0u : 1u + R() %% 32768u;
        uint8_t lvl[16]; for (int b = 0; b < 16; b++) lvl[b] = (k %% 19 == 0) ? 0 : (uint8_t)(R() %% 256u);
        uint32_t v32 = R() << 8 | (R() & 255u);
        printf("%%llu %%llu %%lld %%u %%u %%u %%llu | %%u %%u %%d %%u %%u", (unsigned long long)ll, (unsigned long long)rr, (long long)lr, peak, rms, v32, (unsigned long long)ll,
               mtr_isqrt32(v32), mtr_rms(ll, 10), mtr_corr_q8(ll, rr, lr), mtr_crest_q8(peak, rms), mtr_centroid_q8(lvl, 16));
        printf(" |"); for (int b = 0; b < 16; b++) printf(" %%u", lvl[b]);
        printf(" | %%u\n", mtr_isqrt64((ll %% 0x40000000ull) * (rr %% 0x40000000ull)));
    }
    return 0;
}
'''.replace("%%", "%")


def check_derived():
    """Square roots, RMS, correlation, crest factor and centroid against exact Python integer references."""
    import math
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / "c.c"; c.write_text(DER_C); exe = Path(d) / "c"
        r = subprocess.run(["cc", "-O1", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    for k, ln in enumerate(out):
        a, b, bands, c3 = ln.split(" | ")
        ll, rr, lr, peak, rms, v32, _ = (int(x) for x in a.split())
        i32, grms, corr, crest, cen = (int(x) for x in b.split())
        lvl = [int(x) for x in bands.split()]
        sq64 = int(c3)
        want_corr = 0
        if ll and rr:
            sh = 0
            while (ll >> sh) >= 0x80000000 or (rr >> sh) >= 0x80000000: sh += 1
            den = math.isqrt((ll >> sh) * (rr >> sh))
            if den:
                q = min(256, (abs(lr) >> sh) * 256 // den)
                want_corr = -q if lr < 0 else q
        want_cen = 0
        if sum(lvl): want_cen = (sum(i * v for i, v in enumerate(lvl)) << 8) // sum(lvl)
        want = (math.isqrt(v32), math.isqrt((ll >> 10) & 0xFFFFFFFF), want_corr, 0 if not rms else min(0xFFFF, (peak << 8) // rms), want_cen)
        if (i32, grms, corr, crest, cen) != want or sq64 != math.isqrt((ll % 0x40000000) * (rr % 0x40000000)):
            print("derived MISMATCH at", k, (i32, grms, corr, crest, cen), want); sys.exit(1)
    print("derived measurements OK (isqrt32/64, rms, corr_q8, crest_q8, centroid_q8 equal the exact integer references over 4000 random cases)")


# ---- reference implementations (exact Python integers) of the newer core functions; the C is proven equal to these by the check_* tests above,
# and the JS twin (tau_core.js) is tested against the vectors made from them.
def _cdiv(a, b):
    q = abs(a) // abs(b); return q if (a >= 0) == (b > 0) else -q

def r_ramp(a, b, t, n):
    r = (((a >> 11) & 31) * (n - t) + ((b >> 11) & 31) * t) // n
    g = (((a >> 5) & 63) * (n - t) + ((b >> 5) & 63) * t) // n
    bl = ((a & 31) * (n - t) + (b & 31) * t) // n
    return (r << 11) | (g << 5) | bl

def r_mix256(a, b, t):
    ar, ag, ab, br, bg, bb = (a >> 11) & 31, (a >> 5) & 63, a & 31, (b >> 11) & 31, (b >> 5) & 63, b & 31
    return ((ar + (((br - ar) * t) >> 8)) << 11) | ((ag + (((bg - ag) * t) >> 8)) << 5) | (ab + (((bb - ab) * t) >> 8))

def r_ladder(lo, mid, hi, r, n):
    half = n // 2
    return r_ramp(lo, mid, r, half) if r < half else r_ramp(mid, hi, r - half, n - half)

def r_col_span(x0, w, n, i, gap):
    a, b = x0 + (i * w) // n, x0 + ((i + 1) * w) // n
    return [a, (b - a - gap) if b - a > gap else 1]

def r_col_cw(x0, w, n, i):
    a, b = x0 + (i * w) // n, x0 + ((i + 1) * w) // n
    return [a, (b - a) if b > a else 1]

def r_scale_u(v, h, full):
    return min(h, (v * h) // full)

def r_scale_s(v, ey, unit):
    return max(-ey, min(ey, _cdiv(v * ey, unit)))

def r_in_box(px, py, sz, x, y, w, h):
    return int(px >= x and px + sz <= x + w and py >= y and py + sz <= y + h)

def r_energy(lvl):
    return sum(lvl) // len(lvl)

def r_silent(lvl, peak):
    return int(peak == 0 and not any(lvl))

def r_slew(w, lvl, up, dn):
    out = []
    for b in range(len(lvl)):
        t = (lvl[b] * lvl[b]) >> 4; d = t - w[b]
        d = up if d > up else (-dn if d < -dn else d)
        out.append(w[b] + d)
    return out

def r_ema(e, lvl, div):
    return [e[b] + _cdiv(lvl[b] * lvl[b] - e[b], div) for b in range(len(lvl))]

def r_onset(prev, ema, lvl, sens, elapsed, refr):
    rise = sum(lvl[b] - prev[b] for b in range(len(lvl)) if lvl[b] > prev[b])
    fire = int(rise > ((sens * ema) >> 12) + 6 and elapsed > refr)
    d = ((rise << 8) - ema) & 0xFFFFFFFF
    d = d - (1 << 32) if d & 0x80000000 else d
    return fire, (ema + (d >> 5)) & 0xFFFFFFFF

def r_rms(s, lg):
    import math
    return math.isqrt((s >> lg) & 0xFFFFFFFF)

def r_corr(ll, rr, lr):
    import math
    if not ll or not rr: return 0
    sh = 0
    while (ll >> sh) >= 0x80000000 or (rr >> sh) >= 0x80000000: sh += 1
    den = math.isqrt((ll >> sh) * (rr >> sh))
    if not den: return 0
    q = min(256, (abs(lr) >> sh) * 256 // den)
    return -q if lr < 0 else q

def r_crest(peak, rms):
    return 0 if not rms else min(0xFFFF, (peak << 8) // rms)

def r_centroid(lvl):
    s = sum(lvl)
    return (sum(i * v for i, v in enumerate(lvl)) << 8) // s if s else 0


def extra_vectors():
    """Golden vectors for the JS twin: inputs from a fixed seed, outputs from the reference implementations above."""
    import math
    rg = random.Random(20261003)
    v = {"colour": [], "geometry": [], "signals": [], "derived": []}
    for _ in range(300):
        a, b = rg.randrange(65536), rg.randrange(65536); n = rg.randrange(1, 120); t = rg.randrange(n + 1); t256 = rg.randrange(257); r = rg.randrange(n)
        v["colour"].append({"a": a, "b": b, "n": n, "t": t, "t256": t256, "r": r, "ramp": r_ramp(a, b, t, n), "mix256": r_mix256(a, b, t256),
                            "ladder": r_ladder(a, b, a ^ b, r, n)})
    for _ in range(300):
        x0, w, n = rg.randrange(100), rg.randrange(1, 500), rg.randrange(1, 80); i = rg.randrange(n); gap = rg.randrange(8)
        h, full, val = rg.randrange(1, 400), rg.randrange(1, 40000), rg.randrange(70000)
        ey, unit, sv = rg.randrange(1, 200), rg.randrange(1, 32000), rg.randrange(-128, 128)
        px, py, sz = rg.randrange(-50, 550), rg.randrange(-50, 450), rg.randrange(1, 4)
        v["geometry"].append({"x0": x0, "w": w, "n": n, "i": i, "gap": gap, "span": r_col_span(x0, w, n, i, gap), "cw": r_col_cw(x0, w, n, i),
                              "h": h, "full": full, "val": val, "scale_u": r_scale_u(val, h, full), "ey": ey, "unit": unit, "sv": sv, "scale_s": r_scale_s(sv, ey, unit),
                              "px": px, "py": py, "sz": sz, "box": [x0, 20, w, h], "inbox": r_in_box(px, py, sz, x0, 20, w, h)})
    for k in range(300):
        quiet, zero = k % 7 == 0, k % 29 == 0
        lvl = [0 if zero else (rg.randrange(3) if quiet else rg.randrange(256)) for _ in range(16)]
        prev = [rg.randrange(256) for _ in range(16)]; w = [rg.randrange(4096) for _ in range(16)]; e = [rg.randrange(65000) for _ in range(16)]
        ema, sens, elapsed, refr = rg.randrange(5000), 8 + rg.randrange(40), rg.randrange(600), rg.randrange(300)
        up, dn, div = rg.randrange(300), rg.randrange(300), rg.randrange(1, 64); peak = rg.randrange(3) if k % 5 else 0
        fire, ema2 = r_onset(prev, ema, lvl, sens, elapsed, refr)
        v["signals"].append({"lvl": lvl, "prev": prev, "w": w, "e": e, "ema": ema, "sens": sens, "elapsed": elapsed, "refr": refr, "up": up, "dn": dn, "div": div, "peak": peak,
                             "energy": r_energy(lvl), "silent": r_silent(lvl, peak), "slew": r_slew(w, lvl, up, dn), "ema_out": r_ema(e, lvl, div), "fire": fire, "ema_new": ema2})
    for k in range(300):
        quiet = k % 5 == 0
        ll = rg.randrange(5000) if quiet else rg.randrange(0x10000000000); rr = rg.randrange(5000) if quiet else rg.randrange(0x10000000000)
        lr = int(min(ll, rr) * rg.random()) * (-1 if rg.randrange(3) == 0 else 1)
        if k % 11 == 0: rr, lr = ll, ll
        if k % 13 == 0: rr, lr = ll, -ll
        peak = rg.randrange(32769); rms = 0 if k % 17 == 0 else rg.randrange(1, 32769); v32 = rg.randrange(1 << 32)
        lvl = [0 if k % 19 == 0 else rg.randrange(256) for _ in range(16)]
        v["derived"].append({"ll": ll, "rr": rr, "lr": lr, "peak": peak, "rms_in": rms, "v32": v32, "lvl": lvl, "isqrt32": math.isqrt(v32), "rms": r_rms(ll, 10),
                             "corr": r_corr(ll, rr, lr), "crest": r_crest(peak, rms), "centroid": r_centroid(lvl),
                             "p64": [ll % 0x40000000, rr % 0x40000000], "isqrt64": math.isqrt((ll % 0x40000000) * (rr % 0x40000000))})
    return v


def main():
    rc, out = build_run(True)
    if rc:
        print("MISMATCH between meter_core.h and the original wviz_bars_tick code:\n" + out.splitlines()[0]); sys.exit(1)
    vecs = parse_vectors(out); vecs.update(extra_vectors())
    text = json.dumps(vecs, separators=(",", ":")) + "\n"
    if "--check" in sys.argv:
        if not VEC.exists() or VEC.read_text() != text:
            print("core_vectors.json is stale; run sim/test_meter_core.py --write"); sys.exit(1)
    elif "--write" in sys.argv:
        VEC.parent.mkdir(parents=True, exist_ok=True); VEC.write_text(text); print("wrote", VEC)
    check_colour()
    check_geometry()
    check_cache()
    check_signals()
    check_derived()
    print("meter core OK (equal to the original code; vectors " + ("checked" if "--check" in sys.argv else "computed") + ")")


if __name__ == "__main__":
    main()
