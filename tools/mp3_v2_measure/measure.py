#!/usr/bin/env python3
"""MP3 v2 scoping measurement: build the CURRENT decoder (Helix, software polyphase, as in Player 2 minus the hardware window hook) for rv32im
with the project's vendored toolchain, run N frames of a file in tools/mp3_v2_measure/rvprof.c (an RV32IM simulator with a per-PC profile and a
VexRiscv-like cycle model), and print per-function and per-stage instruction counts, model cycles and instruction-class mixes.
Usage: measure.py <out-dir> <frames> <file.mp3> [more files]       (out-dir is scratch; nothing is written into the tree)
INSTRUCTION COUNTS are exact for the compiled code; CYCLES are a model (parameters at the top of rvprof.c) and are labelled as such."""
import re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
BIN = Path(subprocess.check_output([sys.executable, str(ROOT / "tools/toolchain.py"), "bin"], text=True).strip().splitlines()[-1])
H = ROOT / "third_party/libhelix-mp3"
STAGE = {  # function name (as Helix names it in the linked ELF) -> stage label
    "MP3FindSyncWord": "sync/header", "xmp3_UnpackFrameHeader": "sync/header", "xmp3_CheckPadBit": "sync/header", "xmp3_SetBitstreamPointer": "sync/header",
    "xmp3_UnpackSideInfo": "side-info", "xmp3_UnpackScaleFactors": "scalefactors", "xmp3_DecodeHuffman": "huffman (bit reader + tables)", "xmp3_GetBits": "huffman (bit reader + tables)",
    "xmp3_CalcBitsUsed": "side-info",
    "xmp3_Dequantize": "requantise", "xmp3_DequantChannel": "requantise", "xmp3_MidSideProc": "stereo (MS)", "xmp3_IntensityProcMPEG1": "stereo (intensity)", "xmp3_IntensityProcMPEG2": "stereo (intensity)",
    "xmp3_IMDCT": "antialias+IMDCT+window/overlap", "xmp3_FDCT32": "polyphase: DCT32", "xmp3_PolyphaseMono": "polyphase: window", "xmp3_PolyphaseStereo": "polyphase: window", "xmp3_Subband": "polyphase: glue",
    "MP3Decode": "frame glue (MP3Decode, buffers)", "xmp3_AllocateBuffers": "init",
}
def sh(cmd, **k):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **k)
def build(out, frames):
    out.mkdir(parents=True, exist_ok=True)
    gcc = str(BIN / "riscv-none-elf-gcc")
    srcs = [str(H / "mp3dec.c"), str(H / "mp3tabs.c")] + [str(H / f"real/{n}.c") for n in "bitstream buffers dct32 dequant dqchan huffman hufftabs imdct polyphase scalfact stproc subband trigtabs".split()]
    srcs += [str(ROOT / "fw2/platform/libc_min.c"), str(ROOT / "tools/mp3_v2_measure/harness.c"), str(ROOT / "tools/mp3_v2_measure/crt0.S")]
    elf = out / "harness.elf"
    sh([gcc, "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostartfiles", "-nostdlib", "-ffunction-sections", "-fdata-sections", "-w", f"-DFRAMES={frames}",
        "-I", str(H / "pub"), "-I", str(H / "real"), "-Wl,--gc-sections", "-T", str(ROOT / "tools/mp3_v2_measure/link.ld"), "-o", str(elf)] + srcs + ["-lgcc"])
    import os   # the host SDK on this machine needs an older sysroot to link (MacOSX27.2 libSystem.tbd is malformed for this linker)
    sdk = os.environ.get("MP3_SDKROOT", "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk")
    sh(["cc", "-isysroot", sdk, "-O2", "-o", str(out / "rvprof"), str(ROOT / "tools/mp3_v2_measure/rvprof.c")])
    return elf
def symbols(elf):
    o = sh([str(BIN / "riscv-none-elf-nm"), "-S", "-n", str(elf)]).stdout
    syms = []
    for l in o.splitlines():
        p = l.split()
        if len(p) == 4 and p[2] in "tTwW":
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    return sorted(syms)
def classify(ins):
    op = ins & 0x7f; f3 = (ins >> 12) & 7
    if op == 0x03: return "load"
    if op == 0x23: return "store"
    if op == 0x63: return "branch"
    if op in (0x6f, 0x67): return "jump"
    if op == 0x33 and ins & 0x02000000: return "mul" if f3 < 4 else "div"
    return "alu"
def analyse(out, elf, path, frames):
    prof = out / "prof.txt"
    r = subprocess.run([str(out / "rvprof"), str(elf), str(path), str(prof)], capture_output=True, text=True)
    info = r.stdout.strip(); tot = r.stderr.strip()
    syms = symbols(elf); starts = [s[0] for s in syms]
    import bisect
    per = {}
    for l in prof.read_text().splitlines():
        pc, ins, n, c, t = l.split(); pc = int(pc, 16); ins = int(ins, 16); n = int(n); c = int(c)
        i = bisect.bisect_right(starts, pc) - 1
        name = syms[i][2] if i >= 0 and pc < syms[i][0] + max(syms[i][1], 4) else "?"
        name = re.sub(r"\.(constprop|isra|part)\.\d+", "", name)
        d = per.setdefault(name, {"n": 0, "c": 0, "alu": 0, "load": 0, "store": 0, "branch": 0, "jump": 0, "mul": 0, "div": 0})
        d["n"] += n; d["c"] += c; d[classify(ins)] += n
    return info, tot, per
def main():
    out = Path(sys.argv[1]); frames = int(sys.argv[2]); files = sys.argv[3:]
    elf = build(out, frames)
    for f in files:
        info, tot, per = analyse(out, elf, f, frames)
        m = re.search(r"frames=(\d+)", info); nf = int(m.group(1)) if m else 1
        print(f"## {Path(f).name}: {info}  {tot}")
        stage = {}
        for fn, d in per.items():
            s = STAGE.get(fn, "other (" + fn + ")" if d["n"] > 0.002 * sum(x["n"] for x in per.values()) else "other (small)")
            if fn.startswith("xmp3_Polyphase") and fn not in STAGE: s = "polyphase: window"
            a = stage.setdefault(s, {k: 0 for k in d}); [a.__setitem__(k, a[k] + d[k]) for k in d]
        T = sum(d["c"] for d in per.values()); TI = sum(d["n"] for d in per.values())
        print(f"{'stage':44s} {'instr/frame':>11s} {'cyc/frame':>10s} {'%cyc':>6s} {'ld':>5s} {'st':>5s} {'br':>5s} {'mul':>5s} {'div':>4s}")
        for s, d in sorted(stage.items(), key=lambda kv: -kv[1]["c"]):
            print(f"{s:44s} {d['n']/nf:11.0f} {d['c']/nf:10.0f} {100*d['c']/T:6.1f} {d['load']/nf:5.0f} {d['store']/nf:5.0f} {d['branch']/nf:5.0f} {d['mul']/nf:5.0f} {d['div']/nf:4.0f}")
        print(f"{'TOTAL':44s} {TI/nf:11.0f} {T/nf:10.0f}")
        print("-- per function (top 14)")
        for fn, d in sorted(per.items(), key=lambda kv: -kv[1]["c"])[:14]:
            print(f"   {fn:36s} instr/frame {d['n']/nf:9.0f} cyc/frame {d['c']/nf:9.0f}")
main()
