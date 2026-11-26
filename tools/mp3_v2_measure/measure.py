#!/usr/bin/env python3
"""MP3 v2 measurement: build the decoder AS PLAYER 2 BUILDS IT for rv32im with the project's vendored toolchain, run N frames of a file in tools/mp3_v2_measure/rvprof.c
(an RV32IM simulator with a per-PC profile, a VexRiscv-like cycle model and, with the cache model, the CPU's 4 KiB direct-mapped I$ and D$), and print per-function and per-stage
instruction counts, model cycles, cache misses and instruction-class mixes.

WHAT IS BUILT (WP-05; the first version of this tool built a configuration nothing ships and was corrected by the expert review): the Helix tree every consumer compiles
(tools/helix_tree.py: pristine upstream + third_party/libhelix-mp3/tau.patch), with the flags of fw2/build.sh (`-O2 -march=rv32im -mabi=ilp32 -mno-relax -ffunction-sections`,
`-DTAU_POLY_FW=1 -DTAU_POLY_REQUIRED=1`, `-I fw` for mp3_poly_hw.h), and in place of fw2/decode/poly_hw.c the RAM stub tools/mp3_v2_measure/poly_stub.c (same loops over RAM-backed
registers: the right number of words per slot, so the CPU-side work is counted; the unit's latency and compute time are not). `--software-window` builds the old configuration
(software polyphase window, no flags) for comparison with the scoping package.

Usage: measure.py <out-dir> <frames> [--no-cache] [--imiss N] [--dmiss N] [--software-window] [--stub shipped|review] [--review] <file.mp3> [more files]       (out-dir is scratch; nothing is written into the tree)
  --review   print the table of the expert review (Q1) next to the measured rows and the per-row ratio (the files are looked up in the owner's library; <frames> is ignored: each row uses the
             frame count that reproduces it; the review-shaped stub is used unless --stub shipped is given)
Library use: build(out, frames) -> elf; run(out, elf, path, ...) -> dict; INSTRUCTION COUNTS are exact for the compiled code; CYCLES are a model."""
import bisect, os, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from helix_tree import helix_tree
BIN = Path(subprocess.check_output([sys.executable, str(ROOT / "tools/toolchain.py"), "bin"], text=True).strip().splitlines()[-1])
ME = ROOT / "tools/mp3_v2_measure"
KNOWN_SDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk"
STAGE = {  # function name (as Helix names it in the linked ELF) -> stage label
    "MP3FindSyncWord": "sync/header", "xmp3_UnpackFrameHeader": "sync/header", "xmp3_CheckPadBit": "sync/header", "xmp3_SetBitstreamPointer": "sync/header",
    "xmp3_UnpackSideInfo": "side-info", "xmp3_UnpackScaleFactors": "scalefactors", "xmp3_DecodeHuffman": "huffman (bit reader + tables)", "xmp3_GetBits": "huffman (bit reader + tables)",
    "xmp3_CalcBitsUsed": "side-info",
    "xmp3_Dequantize": "requantise", "xmp3_DequantChannel": "requantise", "xmp3_MidSideProc": "stereo (MS)", "xmp3_IntensityProcMPEG1": "stereo (intensity)", "xmp3_IntensityProcMPEG2": "stereo (intensity)",
    "xmp3_IMDCT": "antialias+IMDCT+window/overlap", "xmp3_FDCT32": "polyphase: DCT32", "xmp3_PolyphaseMono": "polyphase: window", "xmp3_PolyphaseStereo": "polyphase: window", "xmp3_Subband": "polyphase: glue",
    "tau_poly_hw_start": "polyphase: unit push (stub)", "tau_poly_hw_finish": "polyphase: unit read (stub)", "tau_poly_hw_clear": "polyphase: unit push (stub)", "tau_poly_hw_slot": "polyphase: unit push (stub)",
    "MP3Decode": "frame glue (MP3Decode, buffers)", "xmp3_AllocateBuffers": "init",
}
# the expert review's table (docs/features/MP3_DECODER_V2_EXPERT_REVIEW_2026-11-25.md Q1, "What I measured instead": scratch rvprof + I$/D$ model, Helix compiled as Player 2 with
# TAU_POLY_FW/REQUIRED, the unit stubbed with RAM loops): (label, file relative to the library, instr/frame, model cyc/frame (CACHELESS model), I$ miss, D$ miss, stores, frames that reproduce it: the review says 1,000-3,000)
REVIEW_ROWS = [
    ("02 MacCunn 128 k 44.1 st", "Test Album/02 MacCunn MP3 128 44k.mp3", 274389, 328767, 1449, 1821, 38676, 1000),
    ("05 MacCunn 320 k 48 st", "Test Album/05 MacCunn MP3 320 48k.mp3", 293953, 352518, 1432, 2015, 38716, 1000),
    ("Tank! 320 k-class VBR 44.1 st", "Cowboy Bebop - Tank! THE! BEST!/01 - Tank! (TV stretch).mp3", 299400, 358136, 1953, 1996, 39989, 1000),
    ("06 Battle (B-591's guessed track)", "Nausicaä of the Valley of the Wind Soundtrack/06. Battle.mp3", 300189, 359090, 1641, 1965, 39646, 3000),
    ("11 Speech 128 k 44.1 mono", "Test Album/11 Speech MP3 128 44k (Five Boons of Life, Twain, LibriVox).mp3", 318139, 389774, 4572, 4837, 40766, 1000),
]
# the review's calibration groups (B-591's stages, Q1 "Calibration against B-591"): h = Huffman, side info, scale factors; d = requantisation and stereo; i = IMDCT (antialias, overlap,
# frequency inversion); s = subband (sub_fdct + sub_hw + glue); everything else (memcpy, MP3Decode glue, ...) is "other"
CALIB = {"xmp3_DecodeHuffman": "h", "xmp3_GetBits": "h", "xmp3_UnpackScaleFactors": "h", "xmp3_UnpackSideInfo": "h", "xmp3_UnpackFrameHeader": "h", "MP3FindSyncWord": "h",
         "xmp3_SetBitstreamPointer": "h", "xmp3_CheckPadBit": "h", "xmp3_CalcBitsUsed": "h",
         "DequantBlock": "d", "xmp3_DequantChannel": "d", "xmp3_Dequantize": "d", "xmp3_MidSideProc": "d", "xmp3_IntensityProcMPEG1": "d", "xmp3_IntensityProcMPEG2": "d",
         "xmp3_IMDCT": "i", "FreqInvertRescale": "i", "WinPrevious": "i",
         "xmp3_FDCT32": "s", "xmp3_Subband": "s", "tau_poly_hw_start": "s", "tau_poly_hw_finish": "s", "tau_poly_hw_clear": "s", "tau_poly_hw_slot": "s",
         "xmp3_PolyphaseStereo": "s", "xmp3_PolyphaseMono": "s"}
# the review's model figures for the Battle row (cacheless, cycles per frame, thousands) and the Pocket figures it derived from B-591 (1 % of the CPU = 17.4 k cycles per frame)
REVIEW_BATTLE_MODEL_K = {"h": 58, "d": 62, "i": 97, "s": 139}
REVIEW_BATTLE_POCKET_K = {"h": 70, "d": 70, "i": 122, "s": 209}
LIBRARIES = ["/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/test music", "/Users/abel.santos/Downloads/DEV PROJECTS/test music"]


def sh(cmd, **k):
    return subprocess.run([str(c) for c in cmd], check=True, text=True, capture_output=True, **k)


def sdk_args():
    sdk = os.environ.get("MP3_SDKROOT") or os.environ.get("SDKROOT") or (KNOWN_SDK if Path(KNOWN_SDK).is_dir() else "")
    return ["-isysroot", sdk] if sdk and sys.platform == "darwin" else []


def build_rvprof(out):
    """the simulator (host C; the host SDK on this machine needs an older sysroot to link)"""
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "rvprof"
    if not exe.exists() or exe.stat().st_mtime < (ME / "rvprof.c").stat().st_mtime:
        sh(["cc", *sdk_args(), "-O2", "-o", exe, ME / "rvprof.c"])
    return exe


def build(out, frames, mode="p2", stub="shipped", order_seed=None):
    """-> path of the ELF.
    mode "p2": Player 2's configuration today (TAU_POLY_FW/REQUIRED + the unit stub; mono also goes through the unit since the mono change, B-1172);
    mode "p2-premono": FW=1 without REQUIRED, which is what the review measured for the mono row (mono ran the software window then; stereo uses the stub);
    mode "software": the old software-window build (no flags), the scoping package's configuration.
    stub "shipped": poly_stub.c faithful to fw2/decode/poly_hw.c; stub "review": the shape of the review's scratch stub (see poly_stub.c), for --review only.
    order_seed: shuffle the link order of the Helix source files with this seed (code layout, hence I$ conflicts, change; instruction counts do not): the layout-sensitivity experiment."""
    poly = mode != "software"
    out.mkdir(parents=True, exist_ok=True)
    H = helix_tree()
    gcc = BIN / "riscv-none-elf-gcc"
    srcs = [H / "mp3dec.c", H / "mp3tabs.c"] + [H / f"real/{n}.c" for n in "bitstream buffers dct32 dequant dqchan huffman hufftabs imdct polyphase scalfact stproc subband trigtabs".split()]
    if order_seed is not None:
        import random
        random.Random(order_seed).shuffle(srcs)
    srcs += [ROOT / "fw2/platform/libc_min.c", ME / "harness.c", ME / "crt0.S"]
    defs, incs = [f"-DFRAMES={frames}"], ["-I", H / "pub", "-I", H / "real"]
    if poly:
        defs += ["-DTAU_POLY_FW=1"] + (["-DTAU_POLY_REQUIRED=1"] if mode == "p2" else []) + (["-DPOLY_STUB_REVIEW=1"] if stub == "review" else [])
        incs += ["-I", ROOT / "fw"]; srcs.append(ME / "poly_stub.c")
    elf = out / f"harness_{mode}_{stub}{'' if order_seed is None else '_o%d' % order_seed}.elf"
    # the flags of fw2/build.sh: -march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -nostartfiles -nostdlib -ffunction-sections -fdata-sections
    sh([gcc, "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostartfiles", "-nostdlib", "-ffunction-sections", "-fdata-sections", "-w", *defs, *incs,
        "-Wl,--gc-sections", "-Wl,--no-warn-rwx-segments", "-Wl,-Map," + str(elf.with_suffix(".map")), "-T", ME / "link.ld", "-o", elf, *srcs, "-lgcc"])
    return elf


def symbols(elf):
    o = sh([BIN / "riscv-none-elf-nm", "-S", "-n", elf]).stdout
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


def run(out, elf, path, cache=True, imiss=12, dmiss=12, rvprof=None):
    """run the profiler on one file -> {'info', 'frames', 'instr', 'cycles', 'imiss', 'dmiss', 'loads', 'stores', 'per': {function: {...}}, 'stage': {...}} (totals are whole run, divide by frames)"""
    rvprof = rvprof or build_rvprof(out)
    prof = out / "prof.txt"
    cmd = [rvprof, elf, path, prof] + (["--cache", "--imiss", str(imiss), "--dmiss", str(dmiss)] if cache else [])
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    info = r.stdout.strip(); tot = r.stderr.strip()
    m = re.search(r"\[instr (\d+) cycles\(model\) (\d+) exit (\d+) imiss (\d+) dmiss (\d+) loads (\d+) stores (\d+)", tot)
    if not m: raise RuntimeError(f"rvprof failed: {tot[:200]}")
    syms = symbols(elf); starts = [s[0] for s in syms]
    per = {}
    for l in prof.read_text().splitlines():
        f = l.split(); pc, ins, n, c = int(f[0], 16), int(f[1], 16), int(f[2]), int(f[3]); im_, dm_ = (int(f[5]), int(f[6])) if len(f) >= 7 else (0, 0)
        i = bisect.bisect_right(starts, pc) - 1
        name = syms[i][2] if i >= 0 and pc < syms[i][0] + max(syms[i][1], 4) else "?"
        name = re.sub(r"\.(constprop|isra|part)\.\d+", "", name)
        d = per.setdefault(name, {"n": 0, "c": 0, "alu": 0, "load": 0, "store": 0, "branch": 0, "jump": 0, "mul": 0, "div": 0, "imiss": 0, "dmiss": 0})
        d["n"] += n; d["c"] += c; d[classify(ins)] += n; d["imiss"] += im_; d["dmiss"] += dm_
    mf = re.search(r"frames=(\d+)", info)
    res = {"info": info, "frames": int(mf.group(1)) if mf else 1, "instr": int(m.group(1)), "cycles": int(m.group(2)), "imiss": int(m.group(4)), "dmiss": int(m.group(5)), "loads": int(m.group(6)),
           "stores": int(m.group(7)), "per": per}
    stage = {}
    total_n = sum(x["n"] for x in per.values())
    for fn, d in per.items():
        s = STAGE.get(fn, "other (" + fn + ")" if d["n"] > 0.002 * total_n else "other (small)")
        if fn.startswith("xmp3_Polyphase") and fn not in STAGE: s = "polyphase: window"
        a = stage.setdefault(s, {k: 0 for k in d}); [a.__setitem__(k, a[k] + d[k]) for k in d]
    res["stage"] = stage
    return res


def library_file(rel):
    for lib in LIBRARIES:
        p = Path(lib) / rel
        if p.exists(): return p
    import unicodedata
    for lib in LIBRARIES:      # the folder names may be stored decomposed (the diaeresis of "Nausicaa")
        for form in ("NFC", "NFD"):
            p = Path(lib) / unicodedata.normalize(form, rel)
            if p.exists(): return p
    return None


def reproduce(out, cache=True, imiss=12, dmiss=12, stub="review"):
    """measure the five rows of the review -> list of (label, measured dict or None, review row). The review's 'model cyc/frame' is the CACHELESS model (the plan labels it 'caches off'),
    so it is compared with cycles - misses x penalty (exact: a miss only adds its penalty). The four stereo rows use Player 2's build with the review-shaped stub; the mono row uses the
    build the review had (mode p2-premono: mono ran the software window before the mono change). Frames per row: the count that reproduces the review's instruction column
    (1,000, and 3,000 for the Battle row; the review says 1,000-3,000)."""
    rows = []; elfs = {}
    for row in REVIEW_ROWS:
        f = library_file(row[1])
        if not f: rows.append((row[0], None, row)); continue
        key = (row[7], "p2-premono" if "mono" in row[0] else "p2", stub)
        if key not in elfs: elfs[key] = build(out, *key)
        r = run(out, elfs[key], f, cache, imiss, dmiss)
        r["cycles_cacheless"] = r["cycles"] - (imiss * r["imiss"] + dmiss * r["dmiss"] if cache else 0)
        rows.append((row[0], r, row))
    return rows


def calibration(r, imiss=12, dmiss=12):
    """-> {'h','d','i','s','other'}: cacheless model cycles per frame and the same with the misses (keys 'h+' ...) for the review's four calibration groups"""
    nf = r["frames"]; g = {}
    for fn, d in r["per"].items():
        k = CALIB.get(fn, "other")
        g[k] = g.get(k, 0) + (d["c"] - imiss * d["imiss"] - dmiss * d["dmiss"]) / nf
        g[k + "+"] = g.get(k + "+", 0) + d["c"] / nf
    return g


def review_values(r):
    """per frame: instr, cacheless model cycles, I$ misses, D$ misses, stores"""
    nf = r["frames"]
    return [r["instr"] / nf, r["cycles_cacheless"] / nf, r["imiss"] / nf, r["dmiss"] / nf, r["stores"] / nf]


def main():
    a = sys.argv[1:]
    def opt(name, default=None):
        if name in a:
            i = a.index(name); v = a[i + 1]; del a[i:i + 2]; return v
        return default
    imiss, dmiss = int(opt("--imiss", 12)), int(opt("--dmiss", 12))
    cache = "--no-cache" not in a; a = [x for x in a if x != "--no-cache"]
    mode = "software" if "--software-window" in a else "p2"; a = [x for x in a if x != "--software-window"]
    stub = opt("--stub", "shipped")
    review = "--review" in a; a = [x for x in a if x != "--review"]
    out = Path(a[0]); frames = int(a[1]) if len(a) > 1 else 1000; files = a[2:]
    if review:
        print(f"{'row':34s} {'instr/fr':>9s} {'(review)':>9s} {'cyc/fr':>9s} {'(review)':>9s} {'I$ miss':>8s} {'(rev)':>6s} {'D$ miss':>8s} {'(rev)':>6s} {'stores':>7s} {'(rev)':>6s}   ratios instr cyc imiss dmiss stores")
        for label, r, row in reproduce(out, True, imiss, dmiss, stub if "--stub" in sys.argv else "review"):
            if r is None: print(f"{label:34s} (file not in the library)"); continue
            v = review_values(r)
            print(f"{label:34s} {v[0]:9.0f} {row[2]:9d} {v[1]:9.0f} {row[3]:9d} {v[2]:8.0f} {row[4]:6d} {v[3]:8.0f} {row[5]:6d} {v[4]:7.0f} {row[6]:6d}   " + " ".join(f"{x / y:5.3f}" for x, y in zip(v, row[2:7])))
        return
    elf = build(out, frames, mode, stub)
    for f in files:
        r = run(out, elf, f, cache, imiss, dmiss)
        nf = r["frames"]; per = r["per"]; stage = r["stage"]
        print(f"## {Path(f).name}: {r['info']}  [instr {r['instr']} cycles(model) {r['cycles']} imiss {r['imiss']} dmiss {r['dmiss']} stores {r['stores']} cache {'on' if cache else 'off'}]")
        T = sum(d["c"] for d in per.values()); TI = sum(d["n"] for d in per.values())
        print(f"{'stage':44s} {'instr/frame':>11s} {'cyc/frame':>10s} {'%cyc':>6s} {'ld':>5s} {'st':>5s} {'br':>5s} {'mul':>5s} {'div':>4s} {'I$ms':>5s} {'D$ms':>5s}")
        for s, d in sorted(stage.items(), key=lambda kv: -kv[1]["c"]):
            print(f"{s:44s} {d['n']/nf:11.0f} {d['c']/nf:10.0f} {100*d['c']/T:6.1f} {d['load']/nf:5.0f} {d['store']/nf:5.0f} {d['branch']/nf:5.0f} {d['mul']/nf:5.0f} {d['div']/nf:4.0f} {d['imiss']/nf:5.0f} {d['dmiss']/nf:5.0f}")
        print(f"{'TOTAL':44s} {TI/nf:11.0f} {T/nf:10.0f}")
        print("-- per function (top 14)")
        for fn, d in sorted(per.items(), key=lambda kv: -kv[1]["c"])[:14]:
            print(f"   {fn:36s} instr/frame {d['n']/nf:9.0f} cyc/frame {d['c']/nf:9.0f} I$ {d['imiss']/nf:6.0f} D$ {d['dmiss']/nf:6.0f}")


if __name__ == "__main__":
    main()
