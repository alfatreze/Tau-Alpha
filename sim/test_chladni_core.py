#!/usr/bin/env python3
"""Native tests for fw/chladni_core.h (the Chladni meter's portable logic), through tools/host/chladni_harness.c.

Checks: cosine table accuracy, Q14 field against a float reference (the overflow check), exact tile periodicity for
same-parity mode mixes (the property the tiled meter depends on), zero placement of a pure mode, determinism,
the state machine on a synthetic track (refractory time, mode pool rules, never blank), and the draw-command budget.
Not a hardware measurement: docs/CHLADNI_METER_SPEC.md says which numbers still need one."""
import math, os, random, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools/host/chladni_harness.c")
EXE = os.path.join(tempfile.gettempdir(), "chl_harness_test")


def build():
    r = subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2", "-o", EXE, SRC], capture_output=True, text=True)
    if r.returncode:
        sys.exit("harness build failed:\n" + r.stderr)


def run(args, stdin="", err=False):
    r = subprocess.run([EXE] + [str(a) for a in args], input=stdin, capture_output=True, text=True, timeout=60)
    if r.returncode:
        sys.exit("harness failed: " + r.stderr)
    return (r.stdout, r.stderr) if err else r.stdout


def fref(modes, x, y):
    f = 0.0
    for m, n, w, s in modes:
        f += w / 16384 * (math.cos(m * math.pi * x) * math.cos(n * math.pi * y)
                          + s / 16384 * math.cos(n * math.pi * x) * math.cos(m * math.pi * y))
    return f


def field(Rx, Ry, modes):
    args = ["field", Rx, Ry, len(modes)] + [v for md in modes for v in md]
    return [[int(v) for v in ln.split()] for ln in run(args).splitlines()]


def main():
    build()
    fails = 0

    def check(name, ok, detail=""):
        nonlocal fails
        print(("ok   " if ok else "FAIL ") + name + (" " + detail if detail and not ok else ""))
        fails += 0 if ok else 1

    cs = [int(v) for v in run(["cos"]).split()]
    err = max(abs(cs[i] - round(16384 * math.cos(i * 2 * math.pi / 1024))) for i in range(1024))
    check("cos table within 1 LSB of round(16384 cos)", err <= 1, f"max err {err}")

    rng = random.Random(3)
    worst = 0.0
    for cls in (0, 1):
        pool = [(m, n) for m in range(2 + cls, 12, 2) for n in range(cls, m, 2)]
        for _ in range(8):
            picks = rng.sample(pool, 4)
            ws = [rng.randint(1, 100) for _ in picks]
            tot = sum(ws)
            modes = [(m, n, round(16384 * w / tot), rng.choice([-16384, -5000, 0, 9000, 16384])) for (m, n), w in zip(picks, ws)]
            Rx, Ry = 33, 37
            F = field(Rx, Ry, modes)
            for j in range(Ry):
                for i in range(Rx):
                    worst = max(worst, abs(F[j][i] / 16384 - fref(modes, (i + .5) / Rx, (j + .5) / Ry)))
                    # periodicity: one tile over, only the sign may change
                    a = fref(modes, (i + .5) / Rx + 1, (j + .5) / Ry)
                    b = fref(modes, (i + .5) / Rx, (j + .5) / Ry)
                    assert abs(abs(a) - abs(b)) < 1e-9, "same-parity mix is not tile-periodic"
    check("Q14 field matches float reference (no overflow) on 16 random mixes", worst < 0.006, f"worst {worst:.4f}")
    check("same-parity mixes repeat per tile up to sign", True)

    # a mixed-parity pair must NOT be periodic: the reason the meter restricts itself to one class
    mixed = [(2, 0, 8000, 16384), (3, 1, 8000, 16384)]
    d = max(abs(abs(fref(mixed, x / 40, .3)) - abs(fref(mixed, x / 40 + 1, .3))) for x in range(40))
    check("mixed parity classes are not periodic (restriction is needed)", d > 0.05, f"{d:.3f}")

    # pure mode (2,0,-): F = cos 2 pi x - cos 2 pi y, zero on both diagonals, large elsewhere
    Rx = Ry = 36
    rows = run(["plane", Rx, Ry, 120, 1, 2, 0, 16384, -16384]).splitlines()
    diag = all(rows[i][i] != "0" and rows[i][Rx - 1 - i] != "0" for i in range(Rx))
    quiet = rows[Ry // 4][Rx // 2] == "0" and rows[Ry // 2][Rx // 4] == "0"
    check("pure mode draws both diagonals", diag)
    check("pure mode leaves the antinodes empty", quiet)

    # symmetry identities the meter can exploit (float reference; these are properties of the formula)
    rs = random.Random(11)
    def mix(cls, same_s=None):
        pool = [(m, n) for m in range(2 + cls, 12, 2) for n in range(cls, m, 2)]
        pk = rs.sample(pool, 3)
        return [(m, n, 5000 + 300 * i, same_s if same_s is not None else rs.choice([-16384, -6000, 9000, 16384])) for i, (m, n) in enumerate(pk)]
    def worst_of(fn, modes):
        w = 0.0
        for _ in range(40):
            x, y = rs.random(), rs.random()
            w = max(w, fn(modes, x, y))
        return w
    edge = worst_of(lambda md, x, y: max(abs(fref(md, -x, y) - fref(md, x, y)), abs(fref(md, 2 - x, y) - fref(md, x, y)),
                                         abs(fref(md, x, -y) - fref(md, x, y))), [(1, 0, 8000, 5000), (2, 1, 8384, -9000)])
    check("edge mirror holds for ANY modes, mixed parity included (mirror tiling needs no parity class)", edge < 1e-9, f"{edge}")
    cen = max(worst_of(lambda md, x, y: abs(abs(fref(md, 1 - x, y)) - abs(fref(md, x, y))), mix(c)) for c in (0, 1))
    check("centre mirror holds for one parity class with any family blend (quarter compute is exact)", cen < 1e-9, f"{cen}")
    cenx = worst_of(lambda md, x, y: abs(abs(fref(md, 1 - x, y)) - abs(fref(md, x, y))), [(2, 0, 8000, 9000), (3, 1, 8384, 9000)])
    check("centre mirror fails across classes", cenx > 0.05, f"{cenx:.3f}")
    dg = max(worst_of(lambda md, x, y: abs(abs(fref(md, y, x)) - abs(fref(md, x, y))), mix(c, same_s=sg)) for c in (0, 1) for sg in (-16384, 16384))
    check("diagonal mirror holds when every mode shares s = +-1 (eighth compute is exact)", dg < 1e-9, f"{dg}")
    dgx = worst_of(lambda md, x, y: abs(abs(fref(md, y, x)) - abs(fref(md, x, y))), [(2, 0, 8000, 16384), (4, 2, 8384, 9000)])
    check("diagonal mirror fails with a continuous family blend", dgx > 0.05, f"{dgx:.3f}")

    # quarter fold (D2): same levels as the full path, exactly symmetric, and about a quarter of the multiply-adds
    rf = random.Random(21)
    worst_mis, worst_ratio, asym = 0.0, 0.0, 0
    for cls in (0, 1):
        pool = [(m, n) for m in range(2 + cls, 12, 2) for n in range(cls, m, 2)]
        for (Rx, Ry) in ((33, 37), (25, 28), (37, 37), (26, 29)):
            for K in (1, 3, 4):
                picks = rf.sample(pool, K)
                ws = [rf.randint(1, 60) for _ in picks]
                tot = sum(ws)
                md = [v for (m, n), w in zip(picks, ws) for v in (m, n, round(16384 * w / tot), rf.choice([-16384, -7000, 3000, 16384]))]
                a_out, a_err = run(["plane", Rx, Ry, 120, K] + md, err=True)
                b_out, b_err = run(["planefold", Rx, Ry, 120, K] + md, err=True)
                A, B = a_out.split(), b_out.split()
                mis = sum(1 for x, y in zip("".join(A), "".join(B)) if x != y) / (Rx * Ry)
                worst_mis = max(worst_mis, mis)
                worst_ratio = max(worst_ratio, int(b_err.split()[1]) / int(a_err.split()[1]))
                for j in range(Ry):
                    asym += (B[j] != B[j][::-1]) + (B[j] != B[Ry - 1 - j])
    print(f"     fold: worst level mismatch {worst_mis*100:.2f} percent of cells, worst multiply-add ratio {worst_ratio:.3f}")
    check("quarter fold gives the same figure as the full path (levels differ in under 1 percent of cells)", worst_mis < 0.01, f"{worst_mis:.4f}")
    check("quarter fold output is exactly mirror symmetric", asym == 0, f"{asym} rows")
    check("quarter fold uses under 35 percent of the multiply-adds", worst_ratio < 0.35, f"{worst_ratio:.3f}")

    # state machine on a synthetic track
    def track(seconds, silent=False):
        lines = []
        for t in range(int(seconds * 1000 / 26.3)):
            ms = t * 26.3
            beat = ms / 508.5
            lv = [0] * 16
            if not silent:
                ke = math.exp(-(beat % 1) * 7)
                for b in range(0, 4):
                    lv[b] = int(235 * ke * (1 - b * .2))
                for b in range(4, 16):
                    lv[b] = int(60 + 90 * math.exp(-((beat * 4) % 1) * 4) * ((int(beat * 4) + b) % 5 == 0))
            lines.append(" ".join(str(min(255, v)) for v in lv))
        return "\n".join(lines) + "\n"

    for preset, refr in ((0, 600), (1, 350)):
        out = run(["run", preset], track(30)).splitlines()
        again = run(["run", preset], track(30)).splitlines()
        check(f"preset {preset}: deterministic", out == again)
        trig = [int(l.split()[0]) for l in out if l.split()[1] == "1"]
        gaps = [(b - a) * 26.3 for a, b in zip(trig, trig[1:])]
        check(f"preset {preset}: {len(trig)} triggers in 30 s, refractory {refr} ms respected",
              len(trig) > 3 and all(g >= refr - 30 for g in gaps), f"min gap {min(gaps) if gaps else 0:.0f}")
        rects = [int(l.split()[3]) for l in out if l.split()[2] != "0"]
        # Informational, and the reason the module draws a plane through the blit engine: one rect per run of equal level
        # is hundreds of commands per update (VIZ_BARS is 36 per frame). Only a sanity bound is asserted.
        print(f"     preset {preset}: rect-per-run cost {sum(rects)/len(rects):.0f} mean, {max(rects)} max commands per tile update")
        pr = (33, 37, 3) if preset == 0 else (25, 28, 4)
        mac_ratio = max(int(l.split()[4]) / (pr[0] * pr[1] * int(l.split()[2]) * 2) for l in out if l.split()[2] != "0")
        check(f"preset {preset}: shipped preset runs folded (multiply-adds per update <= 35 percent of full)", mac_ratio <= 0.35, f"{mac_ratio:.3f}")
        check(f"preset {preset}: rect-run count is bounded by the cell count", max(rects) <= 33 * 37, f"max {max(rects)}")
        okp = True
        for l in out:
            f = l.split()
            if f[2] == "0":
                continue
            ms_ = [tuple(int(v) for v in x.split(",")) for x in f[5:]]
            par = {m % 2 for m, n, w, s in ms_}
            okp &= len(par) == 1 and all((m - n) % 2 == 0 and m <= (11 if preset == 0 else 8) and n < m for m, n, w, s in ms_)
            okp &= abs(sum(w for m, n, w, s in ms_) - 16384) <= 8
        check(f"preset {preset}: modes stay in one parity class, within the order limit, weights sum to 1", okp)

    sil = run(["run", 0], track(6, silent=True)).splitlines()
    check("silence still draws a figure (never blank)", all(l.split()[2] == "1" and int(l.split()[3]) > 0 for l in sil))

    print("FAILED" if fails else "chladni core OK")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
