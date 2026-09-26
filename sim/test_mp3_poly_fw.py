#!/usr/bin/env python3
"""B-307 host check: proves the permanent TAU_POLY_FW capture hook added to third_party/libhelix-mp3/real/dct32.c (33 TAU_POLY_LOG(d, s) calls,
default no-op) is functionally identical to the already-proven scratch-copy WRLOG mechanism (tools/gen_mp3_poly_rom.py / sim/mp3_poly_map.c /
sim/mp3_poly_model.c) -- BEFORE either is wired into Subband(), the one decode path every MP3 track runs through.

Builds two host binaries against the real FDCT32, driven by the identical loop in sim/mp3_poly_fw_check_common.h:
  A. sim/mp3_poly_fw_capture.c   + the REAL, committed dct32.c, compiled with -DTAU_POLY_FW=1 (the permanent macro path).
  B. sim/mp3_poly_scratch_capture.c + a scratch copy of dct32.c with d[0] = d[8] = s; regex-replaced by WRLOG(d, s) (the scratch path).
Diffs their stdout line for line (each line is one FDCT32 call's raw wn + 33 logged values) -- any difference is a real divergence between the
two capture mechanisms. Also checks the skip-index-17 duplicate rule (wlog[17] == wlog[1]) holds on the permanent-macro side, matching
sim/mp3_poly_model.c's own assertion, and that TAU_POLY_FW left unset still compiles (documents the default-off no-op, no behavior claim beyond
that -- byte-identity of the shipped ROM/cold-image is checked separately by rebuilding the release firmware, not by this file)."""
import shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
H = ROOT / "third_party/libhelix-mp3"
fails = 0


def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok:
        fails += 1


def cc(out, sources, td, defines=()):
    cmd = ["cc", "-std=gnu11", "-O1", "-w"] + [f"-D{d}" for d in defines] + \
        ["-I", str(td), "-I", str(H / "pub"), "-I", str(H / "real"), "-o", str(out)] + [str(s) for s in sources]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(r.stderr)


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    for f in ("polyphase.c", "trigtabs.c"):
        shutil.copy(H / "real" / f, td / f)
    shutil.copy(ROOT / "sim/helix_host/assembly.h", td / "assembly.h")
    shutil.copy(ROOT / "sim/mp3_poly_fw_check_common.h", td / "mp3_poly_fw_check_common.h")
    # Copy the REAL, committed dct32.c into td too (not just reference it in place): its own directory carries a
    # non-portable real/assembly.h, and a quoted #include picks up the including file's own directory before -I td's
    # portable shim -- the same reason sim/test_mp3_poly_probe.py copies it, not just the shim, into a fresh temp dir.
    shutil.copy(H / "real/dct32.c", td / "dct32_real.c")

    # A: the permanent macro, against the real committed dct32.c, TAU_POLY_FW=1.
    exe_a = td / "capture_fw"
    cc(exe_a, [ROOT / "sim/mp3_poly_fw_capture.c", td / "dct32_real.c", td / "polyphase.c", td / "trigtabs.c"], td, defines=["TAU_POLY_FW=1"])
    out_a = subprocess.run([str(exe_a)], capture_output=True, text=True)
    check("permanent-macro capture binary runs cleanly", out_a.returncode == 0, out_a.stderr)

    # A2: the same real dct32.c with TAU_POLY_FW left at its default (off) -- must still compile (the no-op path).
    exe_a2 = td / "capture_off"
    cc(exe_a2, [ROOT / "sim/mp3_poly_fw_capture.c", td / "dct32_real.c", td / "polyphase.c", td / "trigtabs.c"], td)
    check("TAU_POLY_FW left unset still compiles (default-off no-op)", exe_a2.exists())

    # B: the scratch WRLOG patch (same mechanism gen_mp3_poly_rom.py/mp3_poly_model.c already use), independently reproduced here.
    s = (H / "real/dct32.c").read_text()
    n = s.count("d[0] = d[8] = s;")
    check("dct32.c still has exactly 33 raw d[0] = d[8] = s; stores (TAU_POLY_LOG did not remove/duplicate any)", n == 33, str(n))
    s = s.replace("d[0] = d[8] = s;", "WRLOG(d, s);")
    s = s.replace('#include "assembly.h"',
                  '#include "assembly.h"\nextern int wlog[64], wn;\n#define WRLOG(d, s) do { d[0] = d[8] = (s); wlog[wn++] = (s); } while (0)', 1)
    (td / "dct32_scratch.c").write_text(s)
    exe_b = td / "capture_scratch"
    cc(exe_b, [ROOT / "sim/mp3_poly_scratch_capture.c", td / "dct32_scratch.c", td / "polyphase.c", td / "trigtabs.c"], td)
    out_b = subprocess.run([str(exe_b)], capture_output=True, text=True)
    check("scratch WRLOG capture binary runs cleanly", out_b.returncode == 0, out_b.stderr)

    check("permanent-macro capture == scratch WRLOG capture, every call, byte for byte", out_a.stdout == out_b.stdout,
          "first divergent lines:\n" + "\n".join(
              f"  fw:      {a}\n  scratch: {b}" for a, b in zip(out_a.stdout.splitlines(), out_b.stdout.splitlines()) if a != b)[:2000])

    lines = out_a.stdout.splitlines()
    check("every call captured exactly 33 raw values (before the skip-17 rule)", lines and all(ln.split()[0] == "33" for ln in lines), str(lines[:3]))
    check("index 17 duplicates index 1 on every call (the documented skip-17 rule)",
          all(ln.split()[1:][17] == ln.split()[1:][1] for ln in lines), "")

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
