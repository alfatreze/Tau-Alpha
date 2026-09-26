#!/usr/bin/env python3
"""Native tests for fw/helios_rect.h (rect-minus-rect, the Helios exclusion primitive), through
tools/host/helios_rect_harness.c.

Checks: no-overlap passthrough, hole fully covers R, corner overlap (the real fullscreen CPU%
label case: a small rect in a big fill's top-right corner), interior hole (4-piece split), area
conservation and no-overlap-between-output-pieces for a spread of random cases."""
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools/host/helios_rect_harness.c")
EXE = os.path.join(tempfile.gettempdir(), "helios_rect_harness_test")

fails = 0


def ok(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        fails += 1


def build():
    r = subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", "-o", EXE, SRC],
                        capture_output=True, text=True)
    if r.returncode:
        sys.exit("harness build failed:\n" + r.stderr)


def subtract(r, h):
    out = subprocess.run([EXE, "subtract", *map(str, r), *map(str, h)],
                          capture_output=True, text=True, timeout=10)
    if out.returncode:
        sys.exit("harness failed: " + out.stderr)
    rects = []
    for line in out.stdout.splitlines():
        x, y, w, hh = map(int, line.split())
        rects.append((x, y, w, hh))
    return rects


def area(rect):
    return rect[2] * rect[3]


def overlap_area(a, b):
    ax0, ay0, aw, ah = a
    bx0, by0, bw, bh = b
    ix0, iy0 = max(ax0, bx0), max(ay0, by0)
    ix1, iy1 = min(ax0 + aw, bx0 + bw), min(ay0 + ah, by0 + bh)
    return max(0, ix1 - ix0) * max(0, iy1 - iy0)


def main():
    build()

    # No overlap: R passes through untouched.
    r = (0, 0, 400, 322)
    h = (350, 400, 40, 20)   # entirely below R
    out = subtract(r, h)
    ok("no overlap: R passes through as one rect", out == [r])

    # Hole fully covers R: nothing left.
    out = subtract((10, 10, 5, 5), (0, 0, 100, 100))
    ok("hole fully covers R: zero output rects", out == [])

    # The real case: fullscreen figure (0,0,400,322) minus the CPU% label corner (top-right).
    r = (0, 0, 400, 322)
    h = (320, 0, 80, 22)   # top-right corner, touching the top and right edges of R
    out = subtract(r, h)
    total_area = sum(area(o) for o in out)
    ok("corner hole: area conservation (R area - overlap area)",
       total_area == area(r) - overlap_area(r, h))
    ok("corner hole: no output rect overlaps the hole",
       all(overlap_area(o, h) == 0 for o in out))
    ok("corner hole: no two output rects overlap each other",
       all(overlap_area(out[i], out[j]) == 0
           for i in range(len(out)) for j in range(i + 1, len(out))))
    ok("corner hole touching two edges: at most 2 pieces (not the full 4)", len(out) <= 2)

    # Interior hole: a hole strictly inside R needs all 4 strips.
    r = (0, 0, 100, 100)
    h = (30, 30, 20, 20)
    out = subtract(r, h)
    ok("interior hole: exactly 4 pieces", len(out) == 4)
    ok("interior hole: area conservation", sum(area(o) for o in out) == area(r) - area(h))
    ok("interior hole: no output rect overlaps the hole",
       all(overlap_area(o, h) == 0 for o in out))
    ok("interior hole: no two output rects overlap each other",
       all(overlap_area(out[i], out[j]) == 0
           for i in range(len(out)) for j in range(i + 1, len(out))))

    # Randomised spread: area conservation and non-overlap always hold, for any R/H pair.
    random.seed(1)
    all_area_ok, all_disjoint_ok, all_inside_r_ok = True, True, True
    for _ in range(500):
        r = (random.randint(-20, 20), random.randint(-20, 20),
             random.randint(1, 60), random.randint(1, 60))
        h = (random.randint(-20, 20), random.randint(-20, 20),
             random.randint(1, 60), random.randint(1, 60))
        out = subtract(r, h)
        if sum(area(o) for o in out) != area(r) - overlap_area(r, h):
            all_area_ok = False
        if any(overlap_area(out[i], out[j]) != 0
               for i in range(len(out)) for j in range(i + 1, len(out))):
            all_disjoint_ok = False
        for o in out:
            if not (r[0] <= o[0] and o[0] + o[2] <= r[0] + r[2]
                    and r[1] <= o[1] and o[1] + o[3] <= r[1] + r[3]):
                all_inside_r_ok = False
    ok("random spread (500 cases): area conservation always holds", all_area_ok)
    ok("random spread (500 cases): output pieces never overlap each other", all_disjoint_ok)
    ok("random spread (500 cases): every output piece stays inside R", all_inside_r_ok)

    print("helios rect OK" if fails == 0 else f"helios rect: {fails} FAILED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
