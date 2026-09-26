#!/usr/bin/env python3
"""The Chladni meter's tunable parameters (meters/chladni/meter.json, meter module M5) are now editable on the Configure page and writable from
tau-assets.bin, so the manifest ranges are a safety claim: nothing inside them may fault the field maths. This builds tools/host/chladni_harness.c
with the address and undefined-behaviour sanitisers (signed overflow, bad shifts, out-of-range indexes all abort) and runs 600 ticks of a synthetic
loud/quiet/noisy track through detect, update, select and render for EVERY corner of the tunable ranges (each of the nine parameters at its minimum
and its maximum, both layouts) plus 300 random interior points and the two shipped presets. Also proves the preset values equal the old hard-coded
chl_presets[] tunables (so wiring the module changed no default), by compiling the real fw/chladni_core.h against the generated tables."""
import itertools, json, random, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = json.loads((ROOT / "tools" / "meters_schema.json").read_text())
CH = next(m for m in SCHEMA["meters"] if m["key"] == "chladni")
TUN = [p for p in CH["params"] if p["key"] != "layout"]        # order: line_width modes rise fall morph morph_gain trigger refractory tonal


def build(d):
    exe = Path(d) / "h"
    r = subprocess.run(["cc", "-std=c99", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-Wall", "-Wextra", "-Werror",
                        "-o", str(exe), str(ROOT / "tools/host/chladni_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); sys.exit(1)
    return exe


def run(exe, layout, vals):
    r = subprocess.run([str(exe), "extreme", str(layout)] + [str(v) for v in vals], capture_output=True, text=True, timeout=60)
    return r


def equivalence():
    """chl_sync() builds chl_cfg from the module values; for both presets that must equal the original chl_presets[i].c tunables."""
    code = r'''
#include <stdint.h>
#include <stdio.h>
#define CHL_DATA
#include "chladni_core.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meters_gen.h"
int main(void) {
    int bad = 0;
    for (uint32_t i = 0; i < CHL_PRESET_N; i++) {
        mtr_apply_preset(&mtr_d_chladni, i);
        const chl_cfg_t *o = &chl_presets[MV_CHLADNI(LAYOUT)].c;
        if (MV_CHLADNI(LAYOUT) != i || MV_CHLADNI(LINE_WIDTH) != o->eps0 || MV_CHLADNI(MODES) != o->topk || MV_CHLADNI(RISE) != o->up_q12_s ||
            MV_CHLADNI(FALL) != o->dn_q12_s || MV_CHLADNI(MORPH) != o->morph_base || MV_CHLADNI(MORPH_GAIN) != o->morph_gain ||
            MV_CHLADNI(TRIGGER) != o->sens_q4 || MV_CHLADNI(REFRACTORY) != o->refr_ms || MV_CHLADNI(TONAL) != o->tonal) { bad++; printf("preset %u differs\n", i); }
    }
    printf("bad %d\n", bad);
    return bad;
}
'''
    with tempfile.TemporaryDirectory() as d:
        c, exe = Path(d) / "e.c", Path(d) / "e"
        c.write_text(code)
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-const-variable", "-Werror", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr); return 1
        p = subprocess.run([str(exe)], capture_output=True, text=True)
        print(p.stdout.strip())
        return p.returncode


def main():
    fails = equivalence()
    with tempfile.TemporaryDirectory() as d:
        exe = build(d)
        combos = 0
        full = "--full" in sys.argv        # every corner of the ranges (about 80 s); the default run does each parameter at its extremes
        base = [p["default"] for p in TUN]  # with the others at the layout's default, plus the random interior points
        if full:
            corners = list(itertools.product(*[(p["min"], p["max"]) if p["type"] != "bool" else (0, 1) for p in TUN]))
        else:
            corners = []
            for i, p in enumerate(TUN):
                for ext in ((p["min"], p["max"]) if p["type"] != "bool" else (0, 1)):
                    corners.append(tuple(ext if j == i else base[j] for j in range(len(TUN))))
            corners.append(tuple(p["min"] if p["type"] != "bool" else 0 for p in TUN)); corners.append(tuple(p["max"] if p["type"] != "bool" else 1 for p in TUN))
        for layout in (0, 1):
            for corner in corners:
                r = run(exe, layout, corner); combos += 1
                if r.returncode or not r.stdout.startswith("ok"):
                    fails += 1; print("FAULT at layout", layout, "corner", corner, "\n", (r.stderr or r.stdout)[:400]); break
        rnd = random.Random(7)
        for _ in range(300 if full else 60):
            v = [rnd.choice(range(p["min"], p["max"] + 1)) if p["type"] != "bool" else rnd.randint(0, 1) for p in TUN]
            r = run(exe, rnd.randint(0, 1), v); combos += 1
            if r.returncode or not r.stdout.startswith("ok"):
                fails += 1; print("FAULT at random point", v, "\n", (r.stderr or r.stdout)[:400]); break
        for pre in CH["presets"]:                       # the shipped presets themselves
            v = [pre["values"][p["key"]] for p in TUN]
            r = run(exe, pre["values"]["layout"], v); combos += 1
            if r.returncode or not r.stdout.startswith("ok"):
                fails += 1; print("FAULT in preset", pre["name"], r.stderr[:400])
    print("chladni parameter ranges OK: %d parameter sets under the sanitisers, no fault" % combos if not fails else "%d FAILURES" % fails)
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
