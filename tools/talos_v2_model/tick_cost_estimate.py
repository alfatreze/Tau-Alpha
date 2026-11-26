#!/usr/bin/env python3
"""ORACLE-SIDE estimate (audit B-1181): engine-busy cycles per meter tick of the seven shipped packs, from the lab twin's
host-call logs (meter_tick_logs.js) and the per-class costs of the shipped engine on the realistic memory path
(sim/sdram_sched_a/run_gfx_cost.sh output). An ESTIMATE: each host call is costed with a per-row/per-word model fitted to the
measured classes; the CPU's own work per tick is not included.
    python3 tools/talos_v2_model/tick_cost_estimate.py cost.txt tick_logs.json
"""
import json
import math
import re
import sys


def load_cost(path):
    c = {}
    for line in open(path):
        m = re.match(r"COST (.+?) n=\d+ min=(\d+) p50=(\d+) p95=(\d+) max=(\d+)", line)
        if m:
            c[m.group(1).strip()] = int(m.group(3))
    return c


def model(c):
    # RECT: per row r0 + k*w (from 1x1, 511x1), checked against 16x16 / 127x16
    k = (c["RECT 511x1"] - c["RECT 1x1"]) / 510.0
    r0 = c["RECT 1x1"] - k
    # BLIT: per row b0 + kb*w from 16x16 and 127x16
    kb = (c["BLIT 127x16"] - c["BLIT 16x16"]) / (16 * 111.0)
    b0 = c["BLIT 16x16"] / 16.0 - 16 * kb
    kbl = (c["BLIT 127x16 blend"] / 16.0 - b0) / 127.0
    kcb = (c["CBLIT 127x16"] / 16.0 - b0) / 127.0
    spx = c["SBLIT 40x40 2x"] / 6400.0
    ch = c["CHAR 1x"]
    rect = lambda w, h: h * (r0 + k * w)                                         # noqa: E731
    blit = lambda w, h, kk=kb: sum(h * (b0 + kk * min(127, w - x)) for x in range(0, w, 127))   # noqa: E731
    return {"rect": rect, "blit": blit, "kbl": kbl, "kcb": kcb, "spx": spx, "char": ch,
            "fit": "RECT row %.1f + %.3f/word; BLIT row %.1f + %.3f/word (blend %.3f, CBLIT %.3f); SBLIT %.2f/output px; CHAR 1x %d" % (r0, k, b0, kb, kbl, kcb, spx, ch)}


def cost_of(line, m):
    t = line.split()
    op, a = t[0], [int(x) if re.match(r"-?\d+$", x) else x for x in t[1:]]
    if op in ("rect", "rect_stash"):
        return m["rect"](a[2], a[3])
    if op == "bar":
        return m["rect"](a[2], a[3]) + 2
    if op in ("blit", "blit_abs", "copy"):
        return m["blit"](a[4], a[5])
    if op == "blend_blit":
        return m["blit"](a[4], a[5], m["kbl"])
    if op == "cblit":
        return m["blit"](a[4], a[5], m["kcb"])
    if op == "sblit":
        s = a[6]
        return m["spx"] * min(127, a[4] * s) * min(127, a[5] * s)
    if op == "text":
        return m["char"] * len(str(a[2]))
    if op in ("scroll_down", "scroll_left"):
        w, h, d = a[2], a[3], a[4]
        return m["blit"](w - d if op == "scroll_left" else w, h - d if op == "scroll_down" else h)
    return 0                                                                      # fence, mb_*, clut_load, blend_on/off: CPU side


def main():
    c = load_cost(sys.argv[1])
    m = model(c)
    print("cost model:", m["fit"])
    print("| meter | box | commands/tick | engine cycles/tick (clk_sdram, est.) | as clk_sys cycles |")
    print("|---|---|---|---|---|")
    for case in json.load(open(sys.argv[2])):
        ticks = [sum(cost_of(l, m) for l in f) for f in case["logs"]]
        mean = sum(ticks) / max(1, len(ticks))
        cmds = sum(case["perFrame"][10:]) / max(1, len(case["perFrame"][10:]))
        name = case["key"] + ("" if not case["params"] else " " + json.dumps(case["params"]))
        print("| %s | %s | %.1f | %.0f | %.0f |" % (name, case["box"], cmds, mean, mean * 66.667 / 100.0))


if __name__ == "__main__":
    main()
