#!/usr/bin/env python3
"""Host test for fw/meter_module.h + the generated fw/meters_gen.h (M2). The generic parameter logic (adjust, preset step, custom marking)
is compared with a verbatim copy of the ORIGINAL hand-written Configure-page code (wvcfg_adjust and the two preset tables, as they stood
before M2) over 300,000 random operations on both meters: every value and the preset index must match after every operation. Also checks
the `when` visibility rule and that the generated defaults equal the default preset.
"""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "meter_gen_enum.h"
#include "meter_module.h"
#ifndef COLD_DATA
#define COLD_DATA
#endif
#include "meters_gen.h"

/* ---- verbatim from fw/player.c and fw/settingsui.inc before M2 ------------------------------------ */
typedef struct { uint8_t bands, ease_mode, attack, release, peak_on, peak_gravity; uint16_t peak_hold_ms; uint8_t peak_fall; } ref_bars_t;
typedef struct { uint8_t scope_smooth, scope_trail; } ref_scope_t;
static const ref_bars_t ref_bars_presets[5] = { { 16, 2, 55, 22, 1, 1, 200, 35 }, { 16, 0, 100, 100, 1, 0, 0, 60 }, { 12, 3, 60, 25, 1, 1, 250, 30 },
                                                 { 8, 2, 30, 12, 1, 0, 500, 15 }, { 16, 1, 90, 90, 1, 1, 100, 70 } };
static const ref_scope_t ref_scope_presets[5] = { { 35, 30 }, { 0, 0 }, { 45, 20 }, { 55, 40 }, { 15, 5 } };
static ref_bars_t ref_bars = { 16, 2, 55, 22, 1, 1, 200, 35 };
static ref_scope_t ref_scope = { 35, 30 };
static uint8_t ref_idx_bars, ref_idx_scope;
enum { WVR_PRESET = 0, WVR_MODE, WVR_BANDS, WVR_EASE, WVR_ATTACK, WVR_RELEASE, WVR_PEAK_ON, WVR_PEAK_GRAV, WVR_PEAK_HOLD, WVR_PEAK_FALL, WVR_SCOPE_SMOOTH, WVR_SCOPE_TRAIL };
static void ref_adjust(int scope, uint32_t row, int32_t dir)
{
    if (row == WVR_PRESET) {
        uint8_t *pidx = scope ? &ref_idx_scope : &ref_idx_bars;
        int32_t p = (*pidx < 5) ? (int32_t)*pidx : 0;
        p += dir;
        if (p < 0) p = 4; else if (p >= 5) p = 0;
        *pidx = (uint8_t)p;
        if (scope) ref_scope = ref_scope_presets[*pidx]; else ref_bars = ref_bars_presets[*pidx];
        return;
    }
    if (scope) ref_idx_scope = 0xFFu; else ref_idx_bars = 0xFFu;
    int32_t v;
    switch (row) {
    case WVR_BANDS:  v = (int32_t)ref_bars.bands + dir; if (v < 4) v = 4; else if (v > 16) v = 16; ref_bars.bands = (uint8_t)v; break;
    case WVR_EASE:   v = (int32_t)ref_bars.ease_mode + dir; if (v < 0) v = 3; else if (v > 3) v = 0; ref_bars.ease_mode = (uint8_t)v; break;
    case WVR_ATTACK: v = (int32_t)ref_bars.attack + dir * 5; if (v < 1) v = 1; else if (v > 100) v = 100; ref_bars.attack = (uint8_t)v; break;
    case WVR_RELEASE: v = (int32_t)ref_bars.release + dir * 5; if (v < 1) v = 1; else if (v > 100) v = 100; ref_bars.release = (uint8_t)v; break;
    case WVR_PEAK_ON: ref_bars.peak_on = ref_bars.peak_on ? 0u : 1u; break;
    case WVR_PEAK_GRAV: ref_bars.peak_gravity = ref_bars.peak_gravity ? 0u : 1u; break;
    case WVR_PEAK_HOLD: v = (int32_t)ref_bars.peak_hold_ms + dir * 20; if (v < 0) v = 0; else if (v > 800) v = 800; ref_bars.peak_hold_ms = (uint16_t)v; break;
    case WVR_PEAK_FALL: v = (int32_t)ref_bars.peak_fall + dir * 5; if (v < 1) v = 1; else if (v > 100) v = 100; ref_bars.peak_fall = (uint8_t)v; break;
    case WVR_SCOPE_SMOOTH: v = (int32_t)ref_scope.scope_smooth + dir * 5; if (v < 0) v = 0; else if (v > 90) v = 90; ref_scope.scope_smooth = (uint8_t)v; break;
    default: v = (int32_t)ref_scope.scope_trail + dir * 5; if (v < 0) v = 0; else if (v > 80) v = 80; ref_scope.scope_trail = (uint8_t)v; break;
    }
}
/* --------------------------------------------------------------------------------------------------- */

static uint32_t rng = 99u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static long bad;
static void same_bars(const char *why) {
    const uint16_t *v = mtr_v_winamp_bars;
    if (v[0] != ref_bars.bands || v[1] != ref_bars.ease_mode || v[2] != ref_bars.attack || v[3] != ref_bars.release || v[4] != ref_bars.peak_on ||
        v[5] != ref_bars.peak_gravity || v[6] != ref_bars.peak_hold_ms || v[7] != ref_bars.peak_fall || mtr_pi_winamp_bars != ref_idx_bars) { bad++; if (bad < 5) printf("bars differ after %s\n", why); }
}
static void same_scope(const char *why) {
    const uint16_t *v = mtr_v_winamp_scope;
    if (v[0] != ref_scope.scope_smooth || v[1] != ref_scope.scope_trail || mtr_pi_winamp_scope != ref_idx_scope) { bad++; if (bad < 5) printf("scope differ after %s\n", why); }
}
int main(void) {
    ref_idx_bars = 0; ref_idx_scope = 0;
    same_bars("start"); same_scope("start");
    for (long n = 0; n < 300000; n++) {
        int32_t dir = (rnd() & 1) ? 1 : -1;
        if (rnd() % 3 == 0) {                       /* preset step */
            mtr_preset_step(&mtr_d_winamp_bars, dir); ref_adjust(0, WVR_PRESET, dir); same_bars("preset");
            mtr_preset_step(&mtr_d_winamp_scope, dir); ref_adjust(1, WVR_PRESET, dir); same_scope("preset");
        } else if (rnd() & 1) {
            uint32_t i = rnd() % 8;                  /* bars param i  <-> original row WVR_BANDS + i */
            mtr_adjust(&mtr_d_winamp_bars, i, dir); ref_adjust(0, WVR_BANDS + i, dir); same_bars("adjust");
        } else {
            uint32_t i = rnd() % 2;
            mtr_adjust(&mtr_d_winamp_scope, i, dir); ref_adjust(1, WVR_SCOPE_SMOOTH + i, dir); same_scope("adjust");
        }
    }
    /* generated defaults equal the default preset (the generator also enforces it) */
    for (int i = 0; i < 8; i++) if (mtr_p_winamp_bars[i].def != mtr_pre_winamp_bars[i]) bad++;
    /* visibility: dependent rows follow peak_on */
    mtr_apply_preset(&mtr_d_winamp_bars, 0);
    if (!mtr_visible(&mtr_d_winamp_bars, MP_WINAMP_BARS_PEAK_HOLD_MS)) bad++;
    mtr_v_winamp_bars[MP_WINAMP_BARS_PEAK_ON] = 0;
    if (mtr_visible(&mtr_d_winamp_bars, MP_WINAMP_BARS_PEAK_HOLD_MS) || mtr_visible(&mtr_d_winamp_bars, MP_WINAMP_BARS_PEAK_FALL) ||
        mtr_visible(&mtr_d_winamp_bars, MP_WINAMP_BARS_PEAK_GRAVITY) || !mtr_visible(&mtr_d_winamp_bars, MP_WINAMP_BARS_BANDS)) bad++;
    printf("mismatches %ld\n", bad);
    return bad != 0;
}
'''


def main():
    with tempfile.TemporaryDirectory() as d:
        c, exe = Path(d) / "h.c", Path(d) / "h"
        c.write_text(HARNESS)
        r = subprocess.run(["cc", "-O2", "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-const-variable", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr); sys.exit(1)
        p = subprocess.run([str(exe)], capture_output=True, text=True)
    print(p.stdout.strip())
    if p.returncode:
        print("FAIL: the generic parameter logic differs from the original Configure page"); sys.exit(1)
    print("meter module OK (generic logic equals the original Configure page over 300,000 operations)")


if __name__ == "__main__":
    main()
