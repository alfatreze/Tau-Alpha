#!/usr/bin/env python3
"""MP3 v2 WP-05: build and run the synthetic cache experiments of tools/mp3_v2_measure/layout_ab.c under rvprof --cache and print the A/B tables.

  layout_ab.py <out-dir> [--miss N]      D$ layout A/B (column-strided vs [slot][subband], scratch absent / on other sets / aliasing the output array) and the I$ alias pair (same sets or not)
Library use: build(out, defs) -> elf; run(out, defs, ...) -> {'instr','cycles','imiss','dmiss','stores','loads','prof'}. Nothing is written into the tree."""
import os, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/mp3_v2_measure"))
import measure
ME = measure.ME


def build(out, defs, name):
    out.mkdir(parents=True, exist_ok=True)
    elf = out / f"{name}.elf"
    measure.sh([measure.BIN / "riscv-none-elf-gcc", "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostartfiles", "-nostdlib", "-ffunction-sections", "-fdata-sections", "-w",
                *[f"-D{d}" for d in defs], "-Wl,--gc-sections", "-Wl,--no-warn-rwx-segments", "-T", ME / "link.ld", "-o", elf, ME / "layout_ab.c", ME / "crt0.S", "-lgcc"])
    return elf


def run(out, defs, name, miss=12, cache=True):
    elf = build(out, defs, name)
    rv = measure.build_rvprof(out)
    prof = out / f"{name}.prof"
    cmd = [rv, elf, "/dev/null", prof] + (["--cache", "--imiss", str(miss), "--dmiss", str(miss)] if cache else [])
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    m = re.search(r"\[instr (\d+) cycles\(model\) (\d+) exit (\d+) imiss (\d+) dmiss (\d+) loads (\d+) stores (\d+)", r.stderr)
    if not m: raise RuntimeError(r.stderr[:200])
    return {"out": r.stdout.strip(), "instr": int(m.group(1)), "cycles": int(m.group(2)), "imiss": int(m.group(4)), "dmiss": int(m.group(5)), "loads": int(m.group(6)), "stores": int(m.group(7)), "prof": prof, "elf": elf}


def layout_table(out, ngr=200, miss=12):
    """-> {(layout, scr_set): result}; layout 0 column-strided, 1 [slot][subband]; scr_set -1 no scratch, 100 scratch on other sets than the output array, 0 scratch aliasing its lines"""
    return {(lay, ss): run(out, ["EXP=1", f"NGR={ngr}", f"LAYOUT={lay}", f"SCR_SET={ss}"], f"lay{lay}_s{ss}", miss) for lay in (0, 1) for ss in (-1, 100, 0)}


def ialias_table(out, miss=12):
    return {al: run(out, ["EXP=2", f"IALIAS={al}"], f"ial{al}", miss) for al in (0, 1)}


def main():
    a = sys.argv[1:]
    miss = 12
    if "--miss" in a: i = a.index("--miss"); miss = int(a[i + 1]); del a[i:i + 2]
    out = Path(a[0])
    ngr = 200
    gr = ngr * 2     # granule-channels
    print(f"D$ layout A/B ({ngr} granules x 2 channels; per granule-channel; miss penalty {miss}; scratch = 24 lines read once per slot):")
    print(f"{'layout':28s} {'scratch':10s} {'instr':>8s} {'cycles':>8s} {'D$ miss':>8s} {'loads':>7s} {'stores':>7s}")
    for (lay, ts), r in layout_table(out, ngr, miss).items():
        print(f"{'column-strided [sb][slot]' if lay == 0 else '[slot][subband]':28s} { {-1: 'none', 100: 'disjoint', 0: 'aliasing'}[ts]:10s} {r['instr'] / gr:8.0f} {r['cycles'] / gr:8.0f} {r['dmiss'] / gr:8.1f} {r['loads'] / gr:7.0f} {r['stores'] / gr:7.0f}")
    print("\nI$ alias (two 1 KiB functions called alternately 4,000 times):")
    for al, r in ialias_table(out, miss).items():
        print(f"{'same sets' if al else 'disjoint sets':14s} instr {r['instr']:>9,} cycles {r['cycles']:>10,} I$ misses {r['imiss']:>8,}")


if __name__ == "__main__":
    main()
