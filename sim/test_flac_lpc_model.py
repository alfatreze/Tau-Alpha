#!/usr/bin/env python3
"""Golden-model test (B-364..B-366): sim/flac_lpc_model.c's sequential 45-bit-accumulator hardware model
matches an unbounded (int64_t) computation of fw/flac.c's own real-LPC arithmetic -- 20,000 random legal-
range trials plus 27 explicit worst-case corners, independently re-proving sim/test_flac_lpc_symmetry.py's
own result in C rather than only Python (this project's standing "prove it twice, differently" discipline
before trusting RTL against either alone). Also writes RTL testbench vectors to
build/rtl/flac_lpc_vectors.txt, mirroring sim/test_mp3_poly_model.py's exact pattern."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    out_dir = ROOT / "build/rtl"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / "flac_lpc_model"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                         "-o", str(exe), str(ROOT / "sim/flac_lpc_model.c")],
                        capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        return 1
    vectors = out_dir / "flac_lpc_vectors.txt"
    p = subprocess.run([str(exe), str(vectors)], capture_output=True, text=True)
    print(p.stdout, end="")
    ok = p.returncode == 0 and "mismatches 0, overflows 0" in p.stdout
    print("ok   flac_lpc_model.c matches the unbounded reference, 0 mismatches, 0 overflows"
          if ok else "FAIL flac_lpc_model.c")
    if ok:
        lines = sum(1 for _ in open(vectors))
        print(f"ok   {lines // 67} RTL vectors ({lines} words, 67/vector) written to {vectors.relative_to(ROOT)}")
    print("PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
