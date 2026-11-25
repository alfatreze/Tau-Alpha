#!/usr/bin/env python3
"""WP-01 report: compile the PRISTINE float minimp3 for rv32im with the project's riscv-none-elf-gcc (MINIMP3_ONLY_MP3, MINIMP3_NO_SIMD),
print code/rodata/data/bss, per-function code sizes, the static stack figure (-fstack-usage), the measured stack watermark, and the soft-float
(libgcc) calls per frame and instructions per frame from the project's RV32IM simulator (tools/mp3_v2_measure/rvprof.c, used read-only).
Usage: rv32_size.py <scratch-dir> <frames> <file.mp3> [more.mp3] [--opt -Os|-O2] [--inc dir-with-a-variant-minimp3.h]       (nothing is written into the tree)"""
import re, subprocess, sys, os, bisect
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
BIN = Path(subprocess.check_output([sys.executable, str(ROOT / "tools/toolchain.py"), "bin"], text=True).strip().splitlines()[-1])
M = ROOT / "tools/mp3_v2_measure"
FLOATFN = re.compile(r"^__(add|sub|mul|div|neg|eq|ne|lt|le|gt|ge|cmp|unord|float|fix|extend|trunc)[a-z]*[sd][fi]\d?$|^__(float|fix)(un)?s[id]s[fi]$|^__[a-z]*sf[a-z0-9]*$|^__[a-z]*df[a-z0-9]*$")

def sh(cmd, **k):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **k)

def build(out, opt, frames, extra=(), inc=None):
    out.mkdir(parents=True, exist_ok=True)
    elf = out / f"mp3{opt}.elf"
    sh([str(BIN / "riscv-none-elf-gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", opt, "-ffreestanding", "-nostartfiles", "-nostdlib",
        "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-w", f"-DFRAMES={frames}", "-DMINIMP3_ONLY_MP3", "-DMINIMP3_NO_SIMD", *extra,
        "-I", str(inc or ROOT / "third_party/minimp3"), "-Wl,--gc-sections", "-Wl,-Map=" + str(out / f"mp3{opt}.map"), "-T", str(M / "link.ld"), "-o", str(elf),
        str(ROOT / "tools/mp3_v2/rv32_harness.c"), str(ROOT / "fw2/platform/libc_min.c"), str(M / "crt0.S"), "-lgcc"], cwd=out)
    return elf

def build_rvprof(out):
    exe = out / "rvprof"
    sdk = os.environ.get("SDKROOT", "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk")
    sh(["cc", "-isysroot", sdk, "-O2", "-o", str(exe), str(M / "rvprof.c")])
    return exe

def syms(elf):
    o = sh([str(BIN / "riscv-none-elf-nm"), "-S", "-n", str(elf)]).stdout
    r = []
    for l in o.splitlines():
        p = l.split()
        if len(p) == 4:
            r.append((int(p[0], 16), int(p[1], 16), p[2], p[3]))
    return r

def main():
    a = sys.argv[1:]; opt = "-Os"; inc = None
    if "--inc" in a:   # measurement variant: a scratch copy of minimp3.h (never the vendored file), e.g. with the window loop removed
        i = a.index("--inc"); inc = a[i + 1]; del a[i:i + 2]
    if "--opt" in a:
        i = a.index("--opt"); opt = a[i + 1]; del a[i:i + 2]
    out = Path(a[0]); frames = int(a[1]); files = a[2:]
    elf = build(out, opt, frames, inc=inc)
    size = sh([str(BIN / "riscv-none-elf-size"), "-A", str(elf)]).stdout
    sec = {m.group(1): int(m.group(2)) for m in re.finditer(r"^(\.\w+)\s+(\d+)", size, re.M)}
    S = syms(elf)
    code = sec.get(".text", 0); ro = sec.get(".rodata", 0); data = sec.get(".data", 0); bss = sec.get(".bss", 0)
    print(f"## build {opt} rv32im ilp32, MINIMP3_ONLY_MP3 + NO_SIMD (int16 output, float internals)")
    print(f"code(.text incl. libgcc soft-float and harness) {code}  rodata {ro}  data {data}  bss {bss}")
    fn = sorted(((s, n) for _, s, t, n in S if t in "tTwW" and s), reverse=True)
    mp = [(s, n) for s, n in fn if not n.startswith("__") and n not in ("main", "memcpy", "memmove", "memset", "memcmp", "strlen", "malloc", "free", "_start")]
    lg = sum(s for s, n in fn if n.startswith("__"))
    print(f"libgcc soft-float/support code {lg}; minimp3 functions: {sum(s for s, _ in mp)} bytes in {len(mp)} symbols (inlining merges many into mp3dec_decode_frame)")
    for s, n in mp[:12]: print(f"  {n:34s} {s:6d}")
    rod = sorted(((s, n) for _, s, t, n in S if t in "rR" and s), reverse=True)
    print("largest rodata symbols: " + ", ".join(f"{n} {s}" for s, n in rod[:8]))
    # static stack figure from -fstack-usage
    for su in sorted(out.glob("*.su")):
        for l in su.read_text().splitlines():
            m = re.search(r"(\w+)\s+(\d+)\s+(static|dynamic)", l)
            if m and int(m.group(2)) >= 512: print(f"  stack-usage {m.group(1)} {m.group(2)} {m.group(3)}")
    rvp = build_rvprof(out)
    starts = [x[0] for x in S if x[2] in "tTwW"]; table = {x[0]: x for x in S if x[2] in "tTwW"}
    for f in files:
        prof = out / "prof.txt"
        r = subprocess.run([str(rvp), str(elf), str(f), str(prof)], capture_output=True, text=True)
        print(f"## {Path(f).name}: {r.stdout.strip()}  {r.stderr.strip()}")
        m = re.search(r"frames=(\d+)", r.stdout); nf = int(m.group(1)) if m else 1
        cnt = {}; total = 0
        for l in prof.read_text().splitlines():
            pc, ins, n, c, t = l.split(); pc = int(pc, 16); n = int(n); total += n; cnt[pc] = n
        print(f"instructions/frame {total / nf:.0f}")
        calls = {}
        for pc, (_, sz, t, name) in table.items():
            if name.startswith("__") and cnt.get(pc, 0):
                calls[name] = cnt[pc]
        tot = sum(calls.values())
        print(f"soft-float/libgcc calls per frame: {tot / nf:.0f}   (" + ", ".join(f"{k} {v / nf:.0f}" for k, v in sorted(calls.items(), key=lambda kv: -kv[1])[:10]) + ")")
        inl = 0
        names = sorted(table.items())
        per = {}
        for pc, n in cnt.items():
            i = bisect.bisect_right(starts, pc) - 1
            name = table[starts[i]][3] if i >= 0 else "?"
            per[name] = per.get(name, 0) + n
        libg = sum(v for k, v in per.items() if k.startswith("__"))
        print(f"instructions inside libgcc routines: {libg / nf:.0f}/frame ({100 * libg / total:.1f} %)")
main()
