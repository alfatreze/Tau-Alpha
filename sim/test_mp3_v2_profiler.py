#!/usr/bin/env python3
"""MP3 v2 WP-05: the cache-aware per-PC profiler (tools/mp3_v2_measure/: rvprof.c, measure.py, poly_stub.c, layout_ab.c, alias_check.py, patterns.py).

1. The cache model of rvprof.c (4 KiB direct-mapped I$ and D$, 32-byte lines, write-through, no write-allocate, penalty per miss) against hand-built RV32 programs with exactly known
   miss counts: straight-line code, a loop that fits, two code blocks 4 KiB apart, sequential and repeated loads, a store to a cold line followed by a load (no write-allocate),
   a store to a present line, two addresses 4 KiB apart (conflict) and 32 bytes apart (none), additive penalties, instruction counts independent of the cache. RED: four mutants of
   rvprof.c (write-allocate stores, a 64-byte I$ line, no D$ penalty, a two-line D$ index) must each fail at least one of those checks; GREEN: the real model fails none.
2. The D$ layout A/B and the I$ alias pair (layout_ab.c): [slot][subband] costs the same as column-strided when nothing aliases and about 2.4x fewer misses when scratch data aliases the
   output array; two functions on the same I$ sets cost more than 10x the misses of the same two on disjoint sets.
3. alias_check.py reads the ELF symbols and the profile: the aliased pair is flagged (exposure above 30 %), the disjoint pair is not (below 1 %), the static mode lists the pair.
4. measure.py builds the decoder as Player 2 does: TAU_POLY_FW/REQUIRED with the unit stub (the software window absent from the image, the stub present), and the old configuration
   when asked; the profile has the miss columns and patterns.py still reads it. On a generated stream the Player 2 build executes fewer instructions per frame than the software-window build.
5. The expert review's table (Q1): the five rows reproduced within 2 % on instructions per frame, cacheless model cycles per frame and stores (the owner's library; skipped when absent),
   and the faithful stub differs from the review-shaped one by the documented constant (212 instructions and 32 stores per slot, 36 slots per frame)."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/mp3_v2_measure")); sys.path.insert(0, str(ROOT / "tools/mp3_v2"))
import measure as M, layout_ab as L, alias_check as A, synth_stream as S
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail)))
    if not ok: fails += 1

td = Path(tempfile.mkdtemp(prefix="profiler_"))
rv = M.build_rvprof(td)

# ---------------------------------------------------------------- 1. the cache model
GCC = M.BIN / "riscv-none-elf-gcc"
def program(name, body, data=""):
    """assemble and link a bare RV32 program at address 0 (link.ld); body falls through to an exit store. -> elf"""
    src = td / f"{name}.S"
    src.write_text(f""".section .text._start
.global _start
_start:
    la sp, _stack_top
{body}
    li t0, 0xF0000004
last_instr:
    sw zero, 0(t0)
1:  j 1b
.global last_instr
{data}
""")
    elf = td / f"{name}.elf"
    M.sh([GCC, "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-nostartfiles", "-nostdlib", "-Wl,--no-warn-rwx-segments", "-T", M.ME / "link.ld", "-o", elf, src])
    return elf
def prun(exe, elf, *opts):
    r = subprocess.run([str(exe), str(elf), "/dev/null", str(td / "p.prof"), *opts], capture_output=True, text=True)
    m = re.search(r"\[instr (\d+) cycles\(model\) (\d+) exit (\d+) imiss (\d+) dmiss (\d+) loads (\d+) stores (\d+)", r.stderr)
    return {k: int(v) for k, v in zip(("instr", "cycles", "exit", "imiss", "dmiss", "loads", "stores"), m.groups())} if m else None
def sym(elf, name):
    o = M.sh([M.BIN / "riscv-none-elf-nm", elf]).stdout
    return next(int(l.split()[0], 16) for l in o.splitlines() if l.split()[-1] == name)
def lines_to(elf):
    return (sym(elf, "last_instr") >> 5) + 1       # I$ lines from address 0 to the last executed instruction, all cold
BUF = ".bss\n.balign 4096\nbufA: .space 8192\n.balign 4096\nbufB: .space 8192\n"
def suite(exe):
    """-> {check name: bool} on a profiler executable"""
    r = {}
    e = program("iseq", "    .rept 512\n    nop\n    .endr")
    p = prun(exe, e, "--cache"); r["straight-line code: one cold miss per 32-byte line"] = p["imiss"] == lines_to(e) and p["dmiss"] == 0
    e = program("iloop", "    li t2, 10\n1:  .rept 100\n    nop\n    .endr\n    addi t2, t2, -1\n    bnez t2, 1b")
    p = prun(exe, e, "--cache"); r["a loop that fits is fetched cold once (10 iterations, no further miss)"] = p["imiss"] == lines_to(e) and p["instr"] > 1000
    e = program("dseq", "    la a0, bufA\n    li t1, 2\n2:  li t2, 64\n    mv a1, a0\n1:  lw t3, 0(a1)\n    addi a1, a1, 4\n    addi t2, t2, -1\n    bnez t2, 1b\n    addi t1, t1, -1\n    bnez t1, 2b", BUF)
    p = prun(exe, e, "--cache"); r["64 sequential words twice: 8 cold line misses, none on the second pass"] = p["dmiss"] == 8 and p["loads"] == 128
    e = program("wna", "    la a0, bufA\n    li t2, 8\n    mv a1, a0\n1:  sw t2, 0(a1)\n    addi a1, a1, 4\n    addi t2, t2, -1\n    bnez t2, 1b\n    lw t3, 0(a0)", BUF)
    p = prun(exe, e, "--cache"); r["a store to a cold line does not allocate: the load after 8 stores still misses (1)"] = p["dmiss"] == 1 and p["stores"] == 8
    e = program("wth", "    la a0, bufA\n    lw t3, 0(a0)\n    sw t3, 0(a0)\n    lw t3, 0(a0)\n    sw t3, 4(a0)\n    lw t3, 4(a0)", BUF)
    p = prun(exe, e, "--cache"); r["a store to a present line keeps it: load, store, load, store, load = 1 miss"] = p["dmiss"] == 1
    e = program("conf", "    la a0, bufA\n    li t5, 4096\n    add a2, a0, t5\n    li t1, 10\n1:  lw t3, 0(a0)\n    lw t4, 0(a2)\n    addi t1, t1, -1\n    bnez t1, 1b", BUF)
    p = prun(exe, e, "--cache"); r["two addresses 4 KiB apart evict each other: 20 loads, 20 misses"] = p["dmiss"] == 20
    e = program("noconf", "    la a0, bufA\n    li t1, 10\n1:  lw t3, 0(a0)\n    lw t4, 32(a0)\n    addi t1, t1, -1\n    bnez t1, 1b", BUF)
    p = prun(exe, e, "--cache"); r["two addresses 32 bytes apart share nothing: 2 misses in 20 loads"] = p["dmiss"] == 2
    p0 = prun(exe, e); p1 = prun(exe, e, "--cache", "--imiss", "7", "--dmiss", "11")
    r["cache off: no miss counted, cache on: instruction counts equal"] = p0["imiss"] == 0 and p0["dmiss"] == 0 and p0["instr"] == p1["instr"]
    r["penalties are additive: cycles(on) - cycles(off) = 7 x I$ misses + 11 x D$ misses"] = p1["cycles"] - p0["cycles"] == 7 * p1["imiss"] + 11 * p1["dmiss"] and p1["imiss"] > 0 and p1["dmiss"] > 0
    q = prun(exe, e, "--cache", "--dcache-bytes", "8192")
    r["an 8 KiB D$ is accepted and the 4 KiB aliases vanish (conflict program: 20 -> 2 misses)"] = prun(exe, program("conf8", "    la a0, bufA\n    li t5, 4096\n    add a2, a0, t5\n    li t1, 10\n1:  lw t3, 0(a0)\n    lw t4, 0(a2)\n    addi t1, t1, -1\n    bnez t1, 1b", BUF), "--cache", "--dcache-bytes", "8192")["dmiss"] == 2 and q is not None
    return r

green = suite(rv)
for k, v in green.items(): check(k, v)
# RED: mutants of the simulator must be caught
src = (M.ME / "rvprof.c").read_text()
mutants = {
    "write-allocate stores": ("if (ad < MMIO) n_stores++;", "if (ad < MMIO) { n_stores++; if (CACHE) { uint32_t l2 = ad >> 5; dtag[l2 & (dnl - 1)] = l2; } }"),
    "64-byte I$ lines": ("uint32_t ln = pc >> 5, ix = ln & (inl - 1);", "uint32_t ln = pc >> 6, ix = ln & (inl - 1);"),
    "no D$ penalty": ("c += P_DMISS; dm = 1;", "dm = 1;"),
    "two-set D$ index": ("ix = ln & (dnl - 1);", "ix = ln & 1;"),
}
for name, (a, b) in mutants.items():
    assert src.count(a) == 1, name
    ms = td / f"rvprof_mut.c"; ms.write_text(src.replace(a, b))
    exe = td / "rvprof_mut"
    M.sh(["cc", *M.sdk_args(), "-O2", "-w", "-o", exe, ms])
    bad = [k for k, v in suite(exe).items() if not v]
    check(f"red: the mutant '{name}' is caught by the cache-model checks ({len(bad)} failing)", len(bad) >= 1)

# ---------------------------------------------------------------- 2. synthetic A/B
lay = L.layout_table(td / "ab", 100, 12)
per = 200
d = lambda lay_, ss: lay[(lay_, ss)]["dmiss"] / per
check("layout A/B, nothing aliasing: both layouts miss the same (within 2 %)", abs(d(0, 100) - d(1, 100)) < 0.02 * d(1, 100) and abs(d(0, -1) - d(1, -1)) < 0.02 * d(1, -1), (d(0, 100), d(1, 100)))
check("layout A/B, scratch aliasing the output array: column-strided misses at least 2x as often as [slot][subband]", d(0, 0) >= 2.0 * d(1, 0), (d(0, 0), d(1, 0)))
check("layout A/B: the aliasing scratch costs [slot][subband] under 30 % more misses than without it", d(1, 0) <= 1.3 * d(1, 100), (d(1, 0), d(1, 100)))
check("layout A/B: the same instruction count within 2 % (the layouts differ in cache behaviour, not in work)", abs(lay[(0, 0)]["instr"] - lay[(1, 0)]["instr"]) < 0.02 * lay[(1, 0)]["instr"])
ia = L.ialias_table(td / "ab")
check("I$ alias: the pair on the same sets misses more than 10x the pair on disjoint sets", ia[1]["imiss"] > 10 * ia[0]["imiss"], (ia[1]["imiss"], ia[0]["imiss"]))
check("I$ alias: identical instruction counts, the aliased pair is more than 2x slower in model cycles", ia[1]["instr"] == ia[0]["instr"] and ia[1]["cycles"] > 2 * ia[0]["cycles"])

# ---------------------------------------------------------------- 3. alias checker
ca = A.check(ia[1]["elf"], ia[1]["prof"]); cd = A.check(ia[0]["elf"], ia[0]["prof"])
check("alias checker (dynamic): the aliased pair is flagged, exposure above 30 %", ca["exposure"] > 0.30 and any({p["a"], p["b"]} == {"f1", "f2"} for p in ca["pairs"]), (ca["exposure"], ca["pairs"][:2]))
check("alias checker (dynamic): the disjoint pair is not (exposure below 5 %, only the driver's own few lines share a set with f1; no f1/f2 pair)", cd["exposure"] < 0.05 and not any({p["a"], p["b"]} == {"f1", "f2"} for p in cd["pairs"]), (cd["exposure"], cd["pairs"][:2]))
sa = A.check(ia[1]["elf"]); sd = A.check(ia[0]["elf"])
check("alias checker (static, from the ELF symbols only): lists f1/f2 with 32 shared sets when aliased and nothing when disjoint",
      any({p["a"], p["b"]} == {"f1", "f2"} and p["shared_sets"] == 32 for p in sa["pairs"]) and not any({p["a"], p["b"]} == {"f1", "f2"} for p in sd["pairs"]), (sa["pairs"][:2], sd["pairs"][:2]))

# ---------------------------------------------------------------- 4. measure.py builds Player 2's configuration
b = td / "m"
elf_p2, elf_sw = M.build(b, 40, "p2", "shipped"), M.build(b, 40, "software")
names_p2 = {n for _, _, n in M.symbols(elf_p2)}; names_sw = {n for _, _, n in M.symbols(elf_sw)}
check("Player 2 build: the window-unit API is linked and the software window is not in the image", {"tau_poly_hw_start", "tau_poly_hw_finish", "xmp3_FDCT32"} <= names_p2 and "xmp3_PolyphaseStereo" not in names_p2, sorted(n for n in names_p2 if "oly" in n or "tau" in n))
check("software-window build (the old configuration): the software window is there and the unit API is not", "xmp3_PolyphaseStereo" in names_sw and "tau_poly_hw_start" not in names_sw)
check("the Player 2 image carries the stub's RAM registers (poly_regs), the unit stub replaced poly_hw.c", "poly_regs" in M.sh([M.BIN / "riscv-none-elf-nm", elf_p2]).stdout and "poly_regs" not in M.sh([M.BIN / "riscv-none-elf-nm", elf_sw]).stdout)
with tempfile.TemporaryDirectory() as t2:
    syn = Path(t2) / "syn.mp3"; syn.write_bytes(S.stream(nframes=80, seed=5, gain=185, mode=0, pairs=60, br_idx=14))
    rp = M.run(b, elf_p2, syn); rs = M.run(b, elf_sw, syn)
check("generated stream: 40 frames decoded by both builds", rp["frames"] == 40 and rs["frames"] == 40, (rp["frames"], rs["frames"]))
check("generated stream: the Player 2 build executes fewer instructions per frame than the software window (window removed, stub loops added)", rp["instr"] < rs["instr"] and rp["instr"] / 40 > 50000, (rp["instr"] / 40, rs["instr"] / 40))
check("generated stream: cache model on, misses counted (I$ and D$ both above 0), cycles include them", rp["imiss"] > 0 and rp["dmiss"] > 0 and rp["cycles"] > rp["instr"])
prof_lines = (b / "prof.txt").read_text().splitlines()
check("profile lines carry 7 columns (pc, instruction, count, cycles, taken, I$ misses, D$ misses)", all(len(l.split()) == 7 for l in prof_lines[:200]))
r = subprocess.run([sys.executable, str(M.ME / "patterns.py"), str(elf_p2), str(b / "prof.txt"), "40"], capture_output=True, text=True)
check("patterns.py still reads the extended profile", r.returncode == 0 and "xmp3_IMDCT" in r.stdout, r.stderr[-200:])
g = M.calibration(rp)
check("calibration groups cover the Helix stages (h, d, i, s each above 0) and sum to the cacheless total", all(g.get(k, 0) > 0 for k in "hdis") and abs(sum(v for k, v in g.items() if not k.endswith("+")) - (rp["cycles"] - 12 * (rp["imiss"] + rp["dmiss"])) / 40) < 1.0)

with tempfile.TemporaryDirectory() as t3:
    syn2 = Path(t3) / "syn.mp3"; syn2.write_bytes(S.stream(nframes=80, seed=5, gain=185, mode=0, pairs=60, br_idx=14))
    ro = [M.run(b, M.build(b, 40, "p2", "shipped", sd), syn2) for sd in (1, 2, 3)]
check("link order changes the code layout: same instruction count, different I$ misses (layout sensitivity is real and measured)", len({r["instr"] for r in ro}) == 1 and len({r["imiss"] for r in ro}) >= 2, [(r["instr"], r["imiss"]) for r in ro])

# ---------------------------------------------------------------- 5. the review's table
bad = []
rows = M.reproduce(td / "rev")
if all(r is None for _, r, _ in rows):
    print("skip: the owner's library is not present (the review's five rows are not reproduced)")
else:
    for label, r, row in rows:
        if r is None: print(f"skip: {label} not in the library"); continue
        v = M.review_values(r)
        dev = [abs(v[0] / row[2] - 1), abs(v[1] / row[3] - 1), abs(v[4] / row[6] - 1)]
        check(f"review row '{label}': instructions, cacheless model cycles and stores within 2 % ({dev[0] * 100:.2f} %, {dev[1] * 100:.2f} %, {dev[2] * 100:.2f} %)", max(dev) < 0.02, dev)
    # the faithful stub against the review-shaped one, on one stereo file
    lab, r_rev, row = rows[0]
    if r_rev is not None:
        f = M.library_file(row[1])
        r_f = M.run(td / "rev", M.build(td / "rev", 1000, "p2", "shipped"), f, True, 12, 12)
        d_instr = (r_rev["instr"] - r_f["instr"]) / r_rev["frames"]
        d_store = (r_rev["stores"] - r_f["stores"]) / r_rev["frames"]
        check("faithful stub: 36 slots x 212 instructions fewer than the review-shaped stub (7,632 per frame, within 2 %) and 1,152 stores more", abs(d_instr - 7632) < 0.02 * 7632 and abs(d_store + 1152) < 0.02 * 1152, (d_instr, d_store))
print("FAILED %d" % fails if fails else "ALL PASSED")
sys.exit(1 if fails else 0)
