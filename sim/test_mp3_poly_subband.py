#!/usr/bin/env python3
"""End-to-end host proof of the B-307 MP3 window redirect (sim/mp3_poly_subband_harness.c): Helix's REAL Subband() with TAU_POLY_FW=1 (window done by a
golden-model "hardware" stub, FDCT32 words captured through the real dct32.c hook) prints exactly the same PCM as the unmodified decoder, for
ordinary, loud/clipping and guard-bit-starved (es fixup) input, across a new decoder instance, with mono untouched, and when the unit "fails" at
various slots mid-track (the software fallback must finish the track identically). Also checks that with TAU_POLY_FW=1 the hardware path really ran."""
import shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_mp3_poly_rom as G
H = ROOT / "third_party/libhelix-mp3"
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1
taps, cf = G.build_map(), G.coefs()
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    for f in ("polyphase.c", "trigtabs.c", "dct32.c", "subband.c"): shutil.copy(H / "real" / f, td / f)
    shutil.copy(ROOT / "sim/helix_host/assembly.h", td / "assembly.h")
    shutil.copy(ROOT / "sim/mp3_poly_model_core.h", td / "mp3_poly_model_core.h")
    shutil.copy(ROOT / "fw/mp3_poly_hw.h", td / "mp3_poly_hw.h")
    (td / "tables.h").write_text(G.render_h(taps, cf))
    def build(name, fw):
        exe = td / name
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DTABLES_H=\"tables.h\"", f"-DTAU_POLY_FW={fw}", "-I", str(td), "-I", str(H / "pub"), "-I", str(H / "real"),
                            "-o", str(exe), str(ROOT / "sim/mp3_poly_subband_harness.c"), str(td / "dct32.c"), str(td / "polyphase.c"), str(td / "trigtabs.c"),
                            str(td / "subband.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return exe
    sw, hw = build("sw", 0), build("hw", 1)
    ref = subprocess.run([str(sw)], capture_output=True, text=True).stdout.splitlines()
    out = subprocess.run([str(hw)], capture_output=True, text=True).stdout.splitlines()
    total_stereo_slots = 2 * 48 * 18
    check("shipped decoder produced its PCM", len(ref) > 100 and ref[-1] == "HWSLOTS 0 MISMATCH 0", ref[-1:] and ref[-1])
    check("TAU_POLY_FW=1 with a healthy unit: PCM identical to the shipped decoder, every granule", out[:-1] == ref[:-1],
          next((f"granule {i}" for i, (a, b) in enumerate(zip(out, ref)) if a != b), "length differs"))
    hs = int(out[-1].split()[1])
    check(f"the hardware path really ran for all {total_stereo_slots} stereo slots (not silently software)", hs == total_stereo_slots, str(hs))
    check("a healthy unit never trips the self-check", out[-1].endswith("MISMATCH 0"), out[-1])
    # a unit that ANSWERS but is wrong: caught only inside each instance's verify window (first 8 slots), then disabled for the session
    for cs in (0, 3, 7):
        o = subprocess.run([str(hw), "-1", str(cs)], capture_output=True, text=True).stdout.splitlines()
        check(f"wrong output at slot {cs} (inside the verify window): detected, software result used, PCM identical, unit disabled",
              o[:-1] == ref[:-1] and o[-1] == f"HWSLOTS {cs + 1} MISMATCH 1", o[-1])
    o = subprocess.run([str(hw), "-1", "100"], capture_output=True, text=True).stdout.splitlines()
    check("wrong output at slot 100 (past the verify window): NOT detected -- the documented limit of the self-check", o[:-1] != ref[:-1], o[-1])
    for fs in (0, 1, 17, 18, 100, 431, 700):
        o = subprocess.run([str(hw), str(fs)], capture_output=True, text=True).stdout.splitlines()
        check(f"unit fails at slot {fs}: the rest of the track finishes in software, PCM still identical", o[:-1] == ref[:-1] and int(o[-1].split()[1]) == fs,
              o[-1])
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
