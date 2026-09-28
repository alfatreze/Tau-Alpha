#!/usr/bin/env python3
"""Native tests for fw/rc_lut.h (the B11 hardware corner-cut LUT loader), through
tools/host/rc_lut_harness.c.

Root cause: mp3_soc.v's rc_cut_lut_r resets to all-zero and nothing in firmware ever wrote
R_RC_IDX/R_RC_DATA, so every hardware-drawn OP_RRECT rendered square corners (docs/AUDIT_TRAIL.md).
This checks rc_lut_cut()'s per-dy cut values against an independent Python re-implementation of the
same quarter-circle search fb_round_rect()/fb_round_rect_on() use in fw/player.c, for radii 0, 3, 5,
9 and 15 (0 = the fast-path/no-load case, 5/9 = the two real radii player.c and fw/settingsui.inc's
call sites actually use, 3/15 cover a small and the LUT's max radius), and checks rc_lut_prepare()'s
caching decision (loads only on a genuine radius change, never for r=0)."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools/host/rc_lut_harness.c")
EXE = os.path.join(tempfile.gettempdir(), "rc_lut_harness_test")

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


def run(args):
    out = subprocess.run([EXE, *map(str, args)], capture_output=True, text=True, timeout=10)
    if out.returncode:
        sys.exit("harness failed: " + out.stderr)
    return out.stdout


def cuts_ref(r):
    """Independent re-implementation of fw/player.c's fb_round_rect()/fb_round_rect_on() quarter-
    circle search, evaluated per dy (1..r) exactly as the RTL comment says the hardware LUT is
    indexed ("dy = r - row"). dy=0 and dy>r are never queried by the RTL for this radius."""
    out = []
    for dy in range(16):
        if r == 0 or dy < 1 or dy > r:
            out.append(0)
            continue
        inner = 0
        while (inner + 1) ** 2 + dy ** 2 <= r ** 2:
            inner += 1
        out.append(max(0, r - inner))
    return out


def cuts(r):
    return [int(v) for v in run(["cuts", r]).splitlines()]


def main():
    build()

    for r in (0, 1, 3, 5, 9, 15):
        got = cuts(r)
        want = cuts_ref(r)
        ok(f"radius {r}: 16 cut(dy) entries match the independent Python reference", got == want)
        ok(f"radius {r}: every entry fits the 5-bit hardware field (0..31)", all(0 <= v <= 31 for v in got))

    ok("radius 0: every entry is 0 (no corner engine armed, matches fb_rrect()'s r=0 fast path)",
       cuts(0) == [0] * 16)

    # rc_lut_prepare() caching: loads only on a genuine radius change; r=0 never loads (and, since
    # it never touches the cache, doesn't force a reload of the radius that follows it either).
    lines = run(["loads", "5", "5", "9", "9", "5", "0", "0", "5"]).splitlines()
    got_loaded = [line.split()[1] == "1" for line in lines]
    want_loaded = [True, False, True, False, True, False, False, False]
    ok("caching: loads on 5, 9, 5 (the 3 genuine radius changes); repeats and r=0 never load, "
       "including the final 5 which is still the cached radius from call 5",
       got_loaded == want_loaded)

    print("rc_lut OK" if fails == 0 else f"rc_lut: {fails} FAILED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
