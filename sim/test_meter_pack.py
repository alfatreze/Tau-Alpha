#!/usr/bin/env python3
"""Meter pack prototype (docs/features/meters/METER_PACKS.md): Layered Wave built as a loadable pack, loaded through the firmware's portable loader and run
on the RISC-V simulator, must draw exactly what the built-in meter draws, and the loader must refuse every kind of bad file.
  1. the pack builds, is freestanding (no undefined symbols) and links at a PSRAM-window address as well as at the simulator's slot;
  2. native build of fw/layered_wave.inc over a fixed input trace: reference command counts and hashes;
  3. the loader + pack under tools/rv32sim.py over the same trace: identical counts and hashes (the pack really runs, through the host table only);
  4. refusal matrix: wrong magic, ABI, meter id, origin, truncated, trailing byte, flipped body bit (CRC), entry out of range, oversize, missing file;
  5. the ABI fingerprint (mtr_in_t and mtr_host_api_t) matches the tracked value, so changing either without bumping MTR_PACK_ABI fails here."""
import hashlib, os, re, struct, subprocess, sys, tempfile, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm

SIM_ORG = 0x00400000
FINGERPRINT = "e2d98dff"      # sha256 of the normalised mtr_in_t + mtr_host_api_t definitions, first 8 hex digits
fails = 0


def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("  " + info if info and not cond else ""))
    fails += 0 if cond else 1


def abi_fingerprint():
    txt = (ROOT / "fw/meter.h").read_text() + (ROOT / "fw/meter_pack.h").read_text()
    parts = []
    for m in re.finditer(r"typedef struct \{.*?\} (mtr_in_t|mtr_host_api_t);", txt, re.S):
        body = re.sub(r"/\*.*?\*/", "", m.group(0), flags=re.S)
        parts.append(re.sub(r"\s+", " ", body).strip())
    return hashlib.sha256("\n".join(parts).encode()).hexdigest()[:8]


def run_native(tmp):
    exe = tmp / "native"
    r = subprocess.run(["cc", "-O1", "-w", "-I", str(ROOT / "fw"), "-I", str(ROOT / "sim"), "-o", str(exe), str(ROOT / "sim/lw_pack_native.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); sys.exit(1)
    return [ln for ln in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines() if ln.startswith("S ")]


def build_harness(tmp, frames=None):
    elf = tmp / ("pack_harness%s.elf" % ("" if frames is None else "_%d" % frames))
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-Wall", "-Wno-unused-function", "-Wno-unused-variable",
           "-Wno-unused-const-variable", "-Wno-comment", "-DPACK_ORG=0x%X" % SIM_ORG] + (["-DLW_NFRAMES=%d" % frames] if frames is not None else []) + ["-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"),
           str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/pack_harness.c"), "-I", str(ROOT / "fw"), "-o", str(elf), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); sys.exit(1)
    return elf


def run_sim(elf, blob, tmp, counts=(), want_err=False):
    f = tmp / "pack.tmpk"; f.write_bytes(blob)
    r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), str(elf), str(f)] + ["--count=%s:%d:%d" % c for c in counts], capture_output=True, text=True)
    out = [ln for ln in r.stdout.splitlines() if not ln.startswith("[")]
    return (out, r.stderr) if want_err else out


def main():
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        blob, info = pm.build("layered_wave", SIM_ORG, tmp / "lw.elf")
        check("the pack builds freestanding (no undefined symbols) at the simulator slot", len(blob) > pm_hdr and info["entry"] == 0)
        check("working state is small enough for the on-chip scratch area and the history ring stays in the slot", 0 < info["data"] + info["bss"] <= 1024 and info["pstate"] >= 2000, str(info))
        blob_hw, info_hw = pm.build("layered_wave", 0x24A00000, tmp / "lw_hw.elf")
        check("the same pack links at a PSRAM code-window address", info_hw["load"] == info["load"] and info_hw["org"] == 0x24A00000)
        print("     pack: %d B image in the slot, %d B state in the slot (history ring), %d B working state in on-chip scratch, %d B file" % (info["load"], info["pstate"], info["data"] + info["bss"], len(blob)))
        want = run_native(tmp)
        check("the native reference trace produced 3 scenarios with draw commands", len(want) == 3 and all(int(w.split()[2]) > 100 for w in want), str(want))
        elf = build_harness(tmp)
        got = run_sim(elf, blob, tmp)
        check("the loader accepts the pack", got[:1] == ["LOAD E0"], str(got[:3]))
        check("the pack run on rv32sim draws exactly what the built-in meter draws (command counts and hashes, 3 scenarios)", got[1:] == want, "\n got  %s\n want %s" % (got[1:], want))

        def bad(name, mut, code):
            out = run_sim(elf, mut(bytearray(blob)), tmp)
            check("refused: " + name + " (E%d, nothing run)" % code, out[:1] == ["LOAD E%d" % code] and len(out) == 1, str(out[:2]))

        def set32(b, off, v):
            b[off:off + 4] = struct.pack("<I", v); return b
        bad("wrong magic", lambda b: (b.__setitem__(0, b[0] ^ 1), b)[1], 21)
        bad("wrong ABI", lambda b: (b.__setitem__(4, 9), b)[1], 22)
        bad("wrong meter id", lambda b: (b.__setitem__(6, 3), b)[1], 23)
        bad("linked for another slot", lambda b: set32(b, 12, SIM_ORG + 0x1000), 24)
        bad("a flipped bit in the body", lambda b: (b.__setitem__(60, b[60] ^ 0x10), b)[1], 26)
        bad("a truncated file", lambda b: b[:-1], 25)
        bad("a trailing byte", lambda b: b + b"\0", 25)
        bad("an entry offset outside the body", lambda b: set32(b, 16, len(blob)), 27)
        bad("a body larger than the slot", lambda b: set32(b, 8, 0x00100000), 25)
        bad("a pstate region that does not fit the slot", lambda b: set32(b, 44, 0x00100000), 25)
        bad(".data larger than the image", lambda b: set32(b, 28, 0x00100000), 25)
        bad("a scratch origin the firmware does not use", lambda b: set32(b, 32, 0x00310000), 28)
        bad("working state larger than the scratch area", lambda b: set32(b, 36, 0x00010000), 29)
        bad("an empty file", lambda b: b"", 20)
        # CPU cost of where the working state lives, counted on the simulator (exact access counts, priced with the measured PSRAM window costs)
        regions = [("scratch", 0x300000, 0x301000), ("slot_ro", SIM_ORG, SIM_ORG + info["load"]), ("slot_state", SIM_ORG + info["load"], SIM_ORG + 0x40000)]
        full, err_full = run_sim(elf, blob, tmp, regions, True)
        elf0 = build_harness(tmp, 0)
        _, err0 = run_sim(elf0, blob, tmp, regions, True)
        def parse(err):
            ic = int(re.search(r"\[([\d,]+) instructions", err).group(1).replace(",", ""))
            acc = {m.group(1): (int(m.group(2)), int(m.group(3))) for m in re.finditer(r"\[access (\w+) reads (\d+) writes (\d+)\]", err)}
            return ic, acc
        ic1, a1 = parse(err_full); ic0, a0 = parse(err0)
        frames = 3 * 48
        per = lambda k, i: (a1[k][i] - a0[k][i]) / frames
        RD, WR = 31, 25          # extra cycles over an on-chip access: uncached PSRAM window read ~32, write ~26 (KB-040), minus the 1 an on-chip access costs
        instr = (ic1 - ic0) / frames
        split = per("slot_ro", 0) * RD + per("slot_state", 0) * RD + per("slot_state", 1) * WR
        naive = split + per("scratch", 0) * RD + per("scratch", 1) * WR
        print("     per frame: %.0f instructions; data accesses scratch %.0f R / %.0f W, slot rodata %.0f R, slot state %.0f R / %.0f W" % (instr, per("scratch", 0), per("scratch", 1), per("slot_ro", 0), per("slot_state", 0), per("slot_state", 1)))
        print("     extra PSRAM cycles per frame: %.0f with the working state in scratch (this design), %.0f if it lived in the slot (%.1fx)" % (split, naive, naive / max(split, 1)))
        check("the working state is the hot data: scratch is accessed far more often than the slot's state", per("scratch", 0) + per("scratch", 1) > 5 * (per("slot_state", 0) + per("slot_state", 1)))
        check("keeping the working state in scratch is cheaper than keeping it in the PSRAM slot", split < naive)
    check("the ABI fingerprint matches the tracked value (bump MTR_PACK_ABI and this constant together when mtr_in_t or the host table changes)", abi_fingerprint() == FINGERPRINT, "now %s" % abi_fingerprint())
    print("PASSED (0 failures)" if not fails else "FAILED (%d)" % fails)
    sys.exit(1 if fails else 0)


pm_hdr = 32
main()
