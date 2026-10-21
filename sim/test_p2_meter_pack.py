#!/usr/bin/env python3
"""Meter packs on Player 2 (docs/features/meters/METER_PACKS.md): Winamp Bars, Winamp Scope, Master VU, Chladni and Layered Wave built as loadable packs, loaded through the firmware's portable loader and run on the RISC-V simulator, must draw exactly what the
built-in meter draws, and the loader must refuse every kind of bad file.
  1. the pack builds, is freestanding (no undefined symbols) and links at the real PSRAM slot and the real scratch address as well as at the simulator's;
  2. native build of fw2/ui/meter_winamp_bars.c over a fixed input trace: reference command counts and hashes;
  3. the loader + pack under tools/rv32sim.py over the same trace: identical counts and hashes (the pack runs through the host table only);
  4. refusal matrix: wrong magic, ABI, meter id, origin, truncated, trailing byte, flipped body bit (CRC), entry out of range, oversize, missing file;
  5. bundles: several packs, a skipped unknown meter, a corrupt pack, a truncated file, a non-bundle;
  6. the ABI fingerprint (mtr_in_t and mtr_host_api_t) matches the tracked value: changing either without bumping MTR_PACK_ABI fails here."""
import hashlib, re, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2")); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm
import pack_bundle as pb
SIM_ORG, SIM_SCRATCH = 0x00400000, 0x00300000
FINGERPRINT = "14555172"
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("  " + info if info and not cond else ""))
    fails += 0 if cond else 1
def abi_fingerprint():
    txt = re.sub(r"/\*.*?\*/", "", (ROOT / "fw2/ui/meter.h").read_text() + (ROOT / "fw2/ui/meter_pack.h").read_text(), flags=re.S)
    parts = [re.sub(r"\s+", " ", m.group(0)).strip() for m in re.finditer(r"typedef struct \{[^{}]*\} (?:mtr_in_t|mtr_host_api_t);", txt)]
    assert len(parts) == 2, parts
    return hashlib.sha256("\n".join(parts).encode()).hexdigest()[:8]
METERS = {"winamp_bars": "PACK_BARS", "winamp_scope": "PACK_SCOPE", "vu_master": "PACK_VU", "chladni": "PACK_CHL", "layered_wave": "PACK_LW"}
SIMREGS = ["-include", str(ROOT / "sim/p2_pack_sim_regs.h")]
def run_native(tmp, key):
    exe = tmp / ("native_" + key)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-D" + METERS[key], "-I", str(ROOT / "fw"), "-o", str(exe), str(ROOT / "sim/p2_pack_native.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return [ln for ln in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines() if ln.startswith("S ")]
def build_harness(tmp, key, bundle=False):
    slot = pm.SLOT[key]
    elf = tmp / ("harness_%s%s.elf" % (key, "_bundle" if bundle else ""))
    cmd = [pm.tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-const-variable", "-Wno-comment",
           "-DPACK_ORG=0x%X" % SIM_ORG, "-DSCRATCH_ORG=0x%X" % SIM_SCRATCH, "-D" + METERS[key], "-DPACK_METER=%d" % pm.METER_IDS[key], "-DPACK_SLOT=%d" % slot] + (["-DPACK_BUNDLE"] if bundle else []) + ["-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"),
           str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/p2_pack_harness.c"), "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2/cores"), "-o", str(elf), "-lgcc"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return elf
def run_sim(elf, blob, tmp):
    f = tmp / "pack.tmpk"; f.write_bytes(blob)
    r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), "--dev=" + str(ROOT / "tools/p2_dev_model.py"), str(elf), str(f)], capture_output=True, text=True)
    return [ln for ln in r.stdout.splitlines() if not ln.startswith("[")]
def set32(b, off, v): b[off:off + 4] = struct.pack("<I", v); return b
def main():
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        built = {}
        for key in METERS:
            blob_k, info = pm.build(key, SIM_ORG, tmp / (key + ".elf"), SIMREGS, scratch=SIM_SCRATCH)
            check("%s: the pack builds freestanding (no undefined symbols) at the simulator slot" % key, len(blob_k) > 48 and info["entry"] == 0)
            check("%s: the working state fits the on-chip scratch area (4 KB) (Chladni is the largest user)" % key, 0 < info["data"] + info["bss"] <= 3072 and info["pstate"] <= 4096, str(info))
            blob_hw, info_hw = pm.build(key, pm.SLOT_BASE + pm.SLOT[key] * pm.SLOT_SIZE, tmp / (key + "_hw.elf"))
            check("%s: the same pack links at its real slot address and the real scratch address (the ABI defaults)" % key, struct.unpack_from("<I", blob_hw, 32)[0] == pm.SCRATCH_ORG and info_hw["org"] == pm.SLOT_BASE + pm.SLOT[key] * pm.SLOT_SIZE)
            print("     %s: %d B image in the slot, %d B working state in on-chip scratch, %d B file" % (key, info["load"], info["data"] + info["bss"], len(blob_k)))
            want_k = run_native(tmp, key)
            check("%s: the native reference trace produced 4 scenarios with draw commands" % key, len(want_k) == 4 and all(int(w.split()[2]) > 50 for w in want_k), str(want_k))
            elf_k = build_harness(tmp, key)
            got = run_sim(elf_k, blob_k, tmp)
            check("%s: the loader accepts the pack" % key, got[:1] == ["LOAD E0"], str(got[:3]))
            check("%s: the pack run on rv32sim draws exactly what the built-in meter draws (command counts and hashes, 4 scenarios)" % key, got[1:] == want_k, "\n got  %s\n want %s" % (got[1:], want_k))
            built[key] = (blob_k, elf_k, want_k)
        blob, elf, want = built["winamp_bars"]
        def bad(name, mut, code):
            out = run_sim(elf, mut(bytearray(blob)), tmp)
            check("refused: " + name + " (E%d, nothing run)" % code, out[:1] == ["LOAD E%d" % code] and len(out) == 1, str(out[:2]))
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
        belf = build_harness(tmp, "winamp_bars", True)
        good = pb.bundle([blob])
        out = run_sim(belf, good, tmp)
        check("a bundle with the pack installs it and runs identically", out[:2] == ["BUNDLE E0 SEEN 1", "SLOT 0 E0"] and out[6:] == want, str(out[:6]))
        multi = []
        for key in METERS:
            bk, _ = pm.build(key, SIM_ORG + pm.SLOT[key] * pm.SLOT_SIZE, tmp / (key + "_b.elf"), SIMREGS, scratch=SIM_SCRATCH)
            multi.append(bk)
        for key in METERS:
            be = build_harness(tmp, key, True)
            out = run_sim(be, pb.bundle(multi), tmp)
            check("%s: in a bundle of all the packs it installs and runs identically" % key, out[0] == "BUNDLE E0 SEEN %d" % len(METERS) and out[1 + pm.SLOT[key]] == "SLOT %d E0" % pm.SLOT[key] and out[1 + 5:] == built[key][2], str(out[:6]))
        unknown = bytearray(blob); unknown[6] = 77
        out = run_sim(belf, pb.bundle([bytes(unknown), blob]), tmp)
        check("a pack for a meter that cannot be a pack is skipped and the next one still installs", out[:2] == ["BUNDLE E0 SEEN 2", "SLOT 0 E0"] and out[6:] == want, str(out[:6]))
        flipped = bytearray(blob); flipped[60] ^= 0x10
        out = run_sim(belf, pb.bundle([bytes(flipped)]), tmp)
        check("a corrupt pack in a bundle is refused (E26) and nothing runs", out == ["BUNDLE E0 SEEN 1", "SLOT 0 E26"] + ["SLOT %d none" % i for i in range(1, 5)], str(out))
        out = run_sim(belf, good[:-100], tmp)
        check("a truncated bundle installs nothing and does not crash", out[:2] == ["BUNDLE E0 SEEN 0", "SLOT 0 none"], str(out[:3]))
        out = run_sim(belf, b"NOPE" + good[4:], tmp)
        check("a file that is not a bundle is refused (E30)", out[:1] == ["BUNDLE E30 SEEN 0"], str(out[:1]))
        out = run_sim(belf, b"TPKB" + struct.pack("<HH", 1, 0), tmp)
        check("a bundle with no packs installs none", out[:1] == ["BUNDLE E0 SEEN 0"], str(out[:1]))
    # delivery: the bundle rides in tau-assets.bin as the MPKB section; the reference reader checks the pack headers and CRCs
    import tau_assets as ta
    ta_blob = ta.pack_container([(b"MPKB", good)])
    r = ta.parse(ta_blob)
    check("tau-assets.bin carries the bundle as an MPKB section and reads it back", r["packs"] == [{"meter": 0, "abi": 3, "size": len(blob) - 48, "entry": 0}], str(r.get("packs")))
    bad_blob = bytearray(good); bad_blob[60] ^= 0x10
    try: ta.parse(ta.pack_container([(b"MPKB", bytes(bad_blob))])); ok = False
    except ValueError: ok = True
    check("a pack with a flipped bit inside the container is refused by the reference reader", ok)
    fp = abi_fingerprint()
    check("the ABI fingerprint matches the tracked value (bump MTR_PACK_ABI and this value together)", fp == FINGERPRINT, "fingerprint is %s" % fp)
    print("PASSED" if not fails else "FAILED (%d)" % fails)
    sys.exit(1 if fails else 0)
main()
