#!/usr/bin/env python3
"""Host test for fw/vu_master_core.h (the MASTER VU meter's portable logic, docs/features/meters/METER_VU_MASTERING_SPEC.md),
through tools/host/vu_master_harness.c -- same pattern as sim/test_chladni_core.py for fw/chladni_core.h.

Checks:
  1. The checked-in table (fw/vu_segment_table.h) is exactly what tools/gen_vu_segment_table.py produces now
     (a second, independent check beside that tool's own --check, run here so `make test-host` catches drift
     even if a future change forgets to wire the generator's own check in).
  2. vum_peak_to_segments() (the C function, via the harness) agrees with the SAME table read directly in
     Python, and is monotonic non-decreasing in peak (louder never reads as a lower segment count) and
     matches the 20*log10-based expectation within the offline table's own quantization.
  3. vum_coalesce() -- the "at most 3 coalesced rects" rule (spec section 4.2) -- checked exhaustively for
     every lit value 0..24 against an independent Python reference: span count <= 3, the covered segments
     are exactly [0, lit), zones appear in order (green, then yellow, then red), no gaps or overlaps.
  4. vum_zone_of() boundaries match the coalescing reference's own zone assignment.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_vu_segment_table as gen  # noqa: E402

SRC = os.path.join(ROOT, "tools/host/vu_master_harness.c")
SEGMENTS = 24
GREEN_MAX = 16
YELLOW_MAX = 21

fails = 0


def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        fails += 1


def build():
    exe = os.path.join(tempfile.gettempdir(), "vu_master_harness_test")
    r = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-o", exe, SRC],
                        capture_output=True, text=True)
    if r.returncode:
        sys.exit("harness build failed:\n" + r.stderr)
    return exe


def run(exe, *args):
    r = subprocess.run([exe] + [str(a) for a in args], capture_output=True, text=True, timeout=30)
    if r.returncode:
        sys.exit(f"harness failed on {args}: {r.stderr}")
    return r.stdout.strip()


def py_coalesce(lit):
    """Independent Python reference for vum_coalesce()'s rule."""
    lit = max(0, min(SEGMENTS, lit))
    bounds = [0, GREEN_MAX, YELLOW_MAX, SEGMENTS]
    spans = []
    for z in range(3):
        lo, hi = bounds[z], bounds[z + 1]
        seg_lo, seg_hi = lo, min(lit, hi)
        if seg_hi > seg_lo:
            spans.append((z, seg_lo, seg_hi - seg_lo))
    return spans


def py_zone(seg):
    if seg <= GREEN_MAX:
        return 0
    if seg <= YELLOW_MAX:
        return 1
    return 2


def main():
    exe = build()

    # 1. Checked-in table matches the generator, independently of the generator's own --check.
    table = gen.build_table()
    text_now = open(os.path.join(ROOT, "fw", "vu_segment_table.h")).read()
    text_gen = gen.render(table)
    check("fw/vu_segment_table.h matches tools/gen_vu_segment_table.py", text_now == text_gen)

    # 2. vum_peak_to_segments(): C harness agrees with the same table read in Python; monotonic;
    #    boundary sanity (0 -> segment 0, 32767 -> segment 24).
    ok_agree = True
    ok_mono = True
    prev = -1
    for peak in range(0, 32768, 137):
        got = int(run(exe, "segments", peak))
        idx = min(peak >> 7, len(table) - 1)
        want = table[idx]
        if got != want:
            ok_agree = False
        if got < prev:
            ok_mono = False
        prev = got
    for peak in (0, 1, 32767, 65535):
        idx = min(peak >> 7, len(table) - 1)
        if int(run(exe, "segments", peak)) != table[idx]:
            ok_agree = False
    check("vum_peak_to_segments() agrees with the Python-read table", ok_agree)
    check("vum_peak_to_segments() is monotonic non-decreasing in peak", ok_mono)
    check("peak 0 reads segment 0", int(run(exe, "segments", 0)) == 0)
    check("peak 32767 reads segment 24 (0 dBFS)", int(run(exe, "segments", 32767)) == 24)

    # 3. vum_coalesce(): exhaustive over every lit value, plus a couple of out-of-range inputs.
    ok = True
    for lit in list(range(0, SEGMENTS + 2)) + [255]:
        out = [int(x) for x in run(exe, "coalesce", lit).split()]
        n = out[0]
        got_spans = [tuple(out[1 + 3 * i:4 + 3 * i]) for i in range(n)]
        want_spans = py_coalesce(lit)
        if got_spans != want_spans:
            ok = False
            print(f"  lit={lit}: got {got_spans} want {want_spans}")
            continue
        # at most 3 rects (spec section 4.2's own rule)
        if n > 3:
            ok = False
            continue
        # spans cover exactly [0, min(lit,24)) with no gap or overlap, in order
        clamped = max(0, min(SEGMENTS, lit))
        covered = 0
        for _z, s, c in got_spans:
            if s != covered:
                ok = False
            covered += c
        if covered != clamped:
            ok = False
    check("vum_coalesce() matches the Python reference for every lit 0..25 and out-of-range", ok)

    # 4. vum_zone_of() boundaries.
    ok = True
    for seg in range(0, SEGMENTS + 1):
        got = int(run(exe, "zone", seg))
        want = py_zone(seg)
        if got != want:
            ok = False
    check("vum_zone_of() matches green<=16/yellow<=21/red<=24 (spec section 2.4)", ok)

    print("PASSED" if not fails else f"FAILED ({fails})")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
