#!/usr/bin/env python3
"""Summarise a finished Quartus fit: resources, timing slack, PSRAM pad-register packing, RBF hash.

Works on the `output_files` directory of a build (on the VM, or a copy of it), read-only.
It replaces the ad-hoc snippets used for B-006..B-013 so results are extracted the same way
every time.

  quartus_fit_summary.py <output_files_dir> [--log quartus-xyz.log] [--expect-rbf-sha256 HEX]

Prints: fitter status, ALMs/registers/RAM/DSP, elapsed time and error count (with --log), worst
setup and hold slack over all corners and the number of negative-slack entries, the I/O-cell
register columns of the CRAM pins (Input/Output/Output Enable Register), the packing warnings
(176279), and the RBF SHA-256 (compared with --expect-rbf-sha256 when given).
Exit status 1 if there is a negative slack entry, an error, or an RBF hash mismatch.
"""
import argparse
import hashlib
import re
import sys
from collections import Counter
from pathlib import Path


def _table(lines, title):
    for i, line in enumerate(lines):
        if line.startswith("; " + title) and i and lines[i - 1].startswith("+"):
            for j in range(i + 1, i + 6):
                if lines[j].startswith("; Name"):
                    header = [c.strip() for c in lines[j].strip("; ").split(";")]
                    rows, k = [], j + 2
                    while k < len(lines) and lines[k].startswith(";"):
                        rows.append([c.strip() for c in lines[k].strip("; ").split(";")])
                        k += 1
                    return header, rows
    return None, []


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output_files", type=Path)
    ap.add_argument("--log", type=Path)
    ap.add_argument("--expect-rbf-sha256")
    args = ap.parse_args()
    out = args.output_files
    bad = 0

    fit_summary = out / "ap_core.fit.summary"
    if fit_summary.exists():
        text = fit_summary.read_text(errors="replace")
        for key in ("Fitter Status", "Logic utilization", "Total registers", "Total RAM Blocks", "Total DSP"):
            m = re.search(key + r".*", text)
            print("  ", m.group(0) if m else f"{key}: (missing)")
    else:
        print("   fit summary missing: the fit has not finished")
        bad = 1

    if args.log and args.log.exists():
        log = args.log.read_text(errors="replace")
        elapsed = re.findall(r"Elapsed time: ([\d:]+)", log)
        errors = log.count("\nError (")
        final = re.findall(r"Quartus Prime Shell was successful[^\n]*", log)
        print(f"   elapsed: {elapsed[-1] if elapsed else '?'} | errors: {errors} | final: {final[-1] if final else '(not finished)'}")
        print(f"   packing warnings 176279: {log.count('(176279)')} | 176225: {log.count('(176225)')}")
        bad |= errors > 0

    sta = out / "ap_core.sta.summary"
    if sta.exists():
        entries = re.findall(r"Type\s*:\s*(.*?)\nSlack\s*:\s*(-?[\d.]+)", sta.read_text())
        worst = {}
        for typ, slack in entries:
            m = re.match(r"(Slow|Fast) 1100mV (\d+C) Model (Setup|Hold) (.*)", typ.strip().strip("'"))
            if m and (m.group(3) not in worst or float(slack) < worst[m.group(3)][0]):
                worst[m.group(3)] = (float(slack), f"{m.group(1)} {m.group(2)}", m.group(4)[-40:])
        negative = sum(float(s) < 0 for _, s in entries)
        print(f"   negative slack entries: {negative}")
        for kind in ("Setup", "Hold"):
            if kind in worst:
                print(f"   worst {kind.lower()}: {worst[kind][0]:+.3f} ns [{worst[kind][1]}] {worst[kind][2]}")
        bad |= negative > 0
    else:
        print("   timing summary missing: timing analysis has not finished")

    fit_rpt = out / "ap_core.fit.rpt"
    if fit_rpt.exists():
        lines = fit_rpt.read_text(errors="replace").split("\n")
        for title in ("Bidir Pins", "Output Pins"):
            header, rows = _table(lines, title)
            if not header:
                continue
            cols = [c for c in ("Input Register", "Output Register", "Output Enable Register") if c in header]
            counts, unpacked = Counter(), set()
            for r in rows:
                if r[0].startswith("cram"):
                    counts[tuple(r[header.index(c)] for c in cols)] += 1
                    if "Output Register" in header and r[header.index("Output Register")] == "no":
                        unpacked.add(re.sub(r"\[\d+\]", "[*]", r[0]))
            print(f"   {title} {cols}: {dict(counts)}")
            if title == "Output Pins" and unpacked:
                print("     output register not packed:", sorted(unpacked), "(cram*_clk/cre are constants)")

    rbf = out / "ap_core.rbf"
    if rbf.exists():
        digest = hashlib.sha256(rbf.read_bytes()).hexdigest()
        print("   RBF sha256:", digest)
        if args.expect_rbf_sha256:
            same = digest == args.expect_rbf_sha256.lower()
            print("   RBF matches the expected hash:", "YES (bit-identical)" if same else "NO")
            if not same:
                bad = 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
