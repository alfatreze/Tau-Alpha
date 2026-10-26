#!/usr/bin/env python3
"""RAM report for a linked firmware ELF (docs/features/DIAGNOSTICS_RESOURCE_ANALYSIS.md, B-565).

Answers "where did the on-chip RAM go, and what grew?" from the linked ELF instead of from a printed
heap-gap number: hot sections (.text .rodata .data .bss, which share the 192/256 KB of on-chip RAM) and the
cold image (.cold_text .cold_data, in PSRAM, no hot-RAM cost), per section, per feature group and per symbol.

  ram_report.py ELF [--top N]                 sections, groups, the N largest hot symbols
  ram_report.py --diff A B [--top N]          what changed from A to B (each an ELF or a snapshot .json)
  ram_report.py --snapshot ELF OUT.json       write a compact snapshot (sections + symbol sizes) for later diffs

tools/check_heap_gap.py keeps one snapshot per tracked target in tools/ram_snapshots/ and prints the diff
when a target's heap gap falls, so the cause is printed instead of rediscovered. Standard library only; the
toolchain is found the way fw/build.sh finds it (RISCV_TOOLCHAIN_BIN, else the vendored xpack, else PATH).
"""
import json
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOT = (".text", ".rodata", ".data", ".bss")
COLD = (".cold_text", ".cold_data")
KNOWN = HOT + COLD + (".psram", ".psram_state")

# Feature groups by symbol-name prefix: approximate (names, not source files), good enough to see which area moved.
GROUPS = [
    ("audio buffers", r"^(arena|pcm$|ring|tag_)"),
    ("helix mp3", r"^(xmp3_|MP3|mp3|Mp3)"),
    ("flac", r"^(flac|fl_|rice|unary|bits$|sbits|need$|byte$|fill$|tau_lpc|hw_lpc|subframe|residual|frame_header)"),
    ("art/jpeg", r"^(art_|pjpeg|picojpeg|g[A-Z]|timg|jpeg|decode_mcu|idct|upsample)"),
    ("meters", r"^(chl|lw_|wviz|scope|wf|vu|vum|viz|mtr_|meter|spec_|peak|wave|bars|mw_)"),
    ("settings/ui", r"^(set_|settings|th_|theme|ov_|ui_|pl_|plist|toast|helios|fb_|font|hl_)"),
    ("library", r"^(lib|favs|queue)"),
    ("diag/check", r"^(chk|sw_|bt_|dg_|stress|qr|sr_|info_|mt_|dbg|ur_|hr_|headroom|prof|vblank)"),
    ("tempo/cymo", r"^(tempo|ws2|wsola|ws_|cymo|resamp)"),
]


def group_of(name):
    for g, pat in GROUPS:
        if re.match(pat, name):
            return g
    return "main/other"


def tool(name):
    base = os.environ.get("RISCV_TOOLCHAIN_BIN", "")
    vend = ROOT / "toolchain" / "xpack-riscv-none-elf-gcc-15.2.0-1" / "bin"
    if not base and (vend / "riscv-none-elf-gcc").exists():
        base = str(vend)
    prefix = os.environ.get("RISCV_PREFIX", "riscv-none-elf-")
    return (base + "/" if base else "") + prefix + name


def parse_sections(text):
    """readelf -S -W output -> {name: (addr, size)}."""
    out = {}
    for line in text.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", line)
        if m:
            out[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return out


def parse_symbols(text, sections):
    """nm -S -C --defined-only output -> {name: (section, size)}; same-name symbols (statics) add up."""
    def sec_of(addr):
        for n in KNOWN:
            if n in sections:
                base, size = sections[n]
                if base <= addr < base + max(size, 1):
                    return n
        return "?"
    out = {}
    for line in text.splitlines():
        p = line.split(None, 3)
        if len(p) < 4:
            continue
        try:
            addr, size = int(p[0], 16), int(p[1], 16)
        except ValueError:
            continue
        sec = sec_of(addr)
        if sec == "?" or not size:
            continue
        old = out.get(p[3], (sec, 0))
        out[p[3]] = (sec, old[1] + size)
    return out


def load(path):
    """-> (sections, symbols) from an ELF or a snapshot .json."""
    path = str(path)
    if path.endswith(".json"):
        d = json.loads(Path(path).read_text())
        return ({k: tuple(v) for k, v in d["sections"].items()},
                {k: tuple(v) for k, v in d["symbols"].items()})
    sec = parse_sections(subprocess.run([tool("readelf"), "-S", "-W", path], capture_output=True, text=True,
                                        check=True).stdout)
    nm = subprocess.run([tool("nm"), "-S", "-C", "--defined-only", path], capture_output=True, text=True,
                        check=True).stdout
    return sec, parse_symbols(nm, sec)


def save_snapshot(sections, symbols, out):
    Path(out).write_text(json.dumps({"sections": sections, "symbols": symbols}, sort_keys=True,
                                    separators=(",", ":")) + "\n")


def group_totals(symbols):
    hot, cold = Counter(), Counter()
    for name, (sec, size) in symbols.items():
        (hot if sec in HOT else cold if sec in COLD else Counter())[group_of(name)] += size
    return hot, cold


def report(sections, symbols, top=20):
    lines = ["sections (bytes):"]
    for n in KNOWN:
        if n in sections:
            lines.append(f"  {n:14s}{sections[n][1]:>9d}" + ("   hot" if n in HOT else "   cold (PSRAM)" if n in COLD else ""))
    lines.append(f"  hot total     {sum(sections[n][1] for n in HOT if n in sections):>9d}  (.text+.rodata+.data+.bss)")
    hot, cold = group_totals(symbols)
    lines.append("by feature group (hot / cold bytes):")
    for g in sorted(set(hot) | set(cold), key=lambda g: -(hot[g] + cold[g])):
        lines.append(f"  {g:14s}{hot[g]:>9d}{cold[g]:>10d}")
    big = sorted(((s, n, sec) for n, (sec, s) in symbols.items() if sec in HOT), reverse=True)[:top]
    lines.append(f"largest hot symbols (top {top}):")
    lines += [f"  {s:>7d} {sec:8s} {n[:70]}" for s, n, sec in big]
    return "\n".join(lines)


def diff(a, b, top=15, min_delta=16):
    sa, ya = a
    sb, yb = b
    lines = ["section deltas (B minus A, bytes):"]
    for n in KNOWN:
        if n in sa or n in sb:
            d = sb.get(n, (0, 0))[1] - sa.get(n, (0, 0))[1]
            if d:
                lines.append(f"  {n:14s}{d:+9d}" + ("   hot" if n in HOT else ""))
    ha, ca = group_totals(ya)
    hb, cb = group_totals(yb)
    rows = [(hb[g] - ha[g], cb[g] - ca[g], g) for g in set(ha) | set(hb) | set(ca) | set(cb)]
    rows = [r for r in rows if r[0] or r[1]]
    if rows:
        lines.append("feature group deltas (hot, cold):")
        lines += [f"  {g:14s}{h:+9d}{c:+10d}" for h, c, g in sorted(rows, key=lambda r: -abs(r[0]))]
    sym = []
    for n in set(ya) | set(yb):
        sec = (yb.get(n) or ya.get(n))[0]
        d = (yb.get(n, (sec, 0))[1]) - (ya.get(n, (sec, 0))[1])
        if sec in HOT and abs(d) >= min_delta:
            sym.append((d, n, sec, n not in ya, n not in yb))
    sym.sort(key=lambda r: -abs(r[0]))
    if sym:
        lines.append(f"hot symbols that changed (top {top}, |delta| >= {min_delta} B):")
        for d, n, sec, new, gone in sym[:top]:
            lines.append(f"  {d:+7d} {sec:8s} {n[:60]}" + ("  (new)" if new else "  (gone)" if gone else ""))
    return "\n".join(lines)


def main(argv):
    top = 20
    if "--top" in argv:
        i = argv.index("--top")
        top = int(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    if len(argv) == 4 and argv[1] == "--snapshot":
        sec, sym = load(argv[2])
        save_snapshot(sec, sym, argv[3])
        print(f"snapshot: {len(sym)} symbols -> {argv[3]}")
    elif len(argv) == 4 and argv[1] == "--diff":
        print(diff(load(argv[2]), load(argv[3]), top=top))
    elif len(argv) == 2 and not argv[1].startswith("-"):
        print(report(*load(argv[1]), top=top))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
