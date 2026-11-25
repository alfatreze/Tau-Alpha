#!/usr/bin/env python3
"""Table of the map results pulled by run_probe.py (work/talos_feat/*/). MIT licence.
M10K is derived from the report's per-RAM table (depth x width -> the cheapest of the M10K aspect ratios), because
quartus_map prints bits, not blocks: an approximation, marked as such in the deliverable."""
import math, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MODES = [(8192, 1), (4096, 2), (2048, 5), (1024, 10), (512, 20), (256, 40)]


def m10k(depth, width):
    return min(math.ceil(depth / d) * math.ceil(width / w) for d, w in MODES)


def row(d):
    t = (d / "output_files/talos_feat.map.rpt").read_text(errors="replace")
    g = lambda p: int(re.search(p + r"\s*;\s*([\d,]+)", t).group(1).replace(",", ""))
    blocks = auto = 0
    for m in re.finditer(r"^; (.*?ALTSYNCRAM)\s*; (M10K block|AUTO|MLAB)\s*; (\S[^;]*?)\s*; (\d+)\s*; (\d+)\s*;", t, re.M):
        b = m10k(int(m.group(4)), int(m.group(5)))
        if m.group(2) == "M10K block":
            blocks += b
        elif m.group(2) == "AUTO":
            auto += b   # the fitter chooses M10K or MLAB; counted here as M10K (upper bound)
    log = (d / "quartus-map.log").read_text(errors="replace")
    return dict(alm=g(r"Estimate of Logic utilization \(ALMs needed\)"), alut=g(r"Combinational ALUT usage for logic"),
                reg=g(r"Dedicated logic registers"), bits=g(r"Total block memory bits"), dsp=g(r"Total DSP Blocks"), m10k=f'{blocks}+{auto}auto',
                latch=len(re.findall(r"inferred latch|Inferred latch", log)), warn=len(re.findall(r"^Warning", log, re.M)),
                secs=(d / "wall_seconds.txt").read_text().strip())


def main():
    base = ROOT / "work/talos_feat"
    names = sys.argv[1:] or sorted(p.name for p in base.iterdir())
    print("| config | ALM est | ALUT | regs | block bits | M10K (derived) | DSP | latches | warnings | map s |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for n in names:
        r = row(base / n)
        print(f"| {n} | {r['alm']} | {r['alut']} | {r['reg']} | {r['bits']} | {r['m10k']} | {r['dsp']} | {r['latch']} | {r['warn']} | {r['secs']} |")


if __name__ == "__main__":
    main()
