#!/usr/bin/env python3
"""Runs the real firmware QR encoder (fw/qrcode.h, vendored RISC-V toolchain, tools/rv32sim.py) and compares the symbol
module for module with segno (byte mode, level L: screenshots are pixel exact, so the extra capacity is worth more than error correction) for every
version 1..38 on a payload filling the version, with forced masks (all eight with --full) and the automatic choice at a few versions. When OpenCV is installed (work/venv-qr) it also decodes rendered images.
Needs segno: run with work/venv-qr/bin/python; skips with a notice otherwise."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GCC = ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc"
try:
    import segno
except ImportError:
    print("SKIP test_qr (segno not installed; use work/venv-qr/bin/python)")
    raise SystemExit(0)

import segno.consts as _c
CAP = {v: (8 * sum(e.num_blocks * e.num_data for e in _c.ECC[v][_c.ERROR_LEVEL_L]) - (12 if v < 10 else 20)) // 8 for v in range(1, 39)}
AUTO_AT = (1, 7, 11, 20, 30, 38)
fails = []


def build(out):
    cmd = [str(GCC), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles",
           "-Wall", "-Wextra", "-Wno-unused-function", "-Wl,--no-warn-rwx-segments",
           "-T", str(ROOT / "tools/host/link.ld"), str(ROOT / "tools/host/start.S"),
           str(ROOT / "tools/host/qr_harness.c"), "-o", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or "warning" in r.stderr:
        print(r.stderr)
        raise SystemExit("qr harness build failed or warned")


def fw(elf, tmp, minv, maxv, mask, payload):
    f = Path(tmp) / "in.bin"
    f.write_bytes(bytes([minv, maxv, mask + 1]) + payload)
    r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), str(elf), str(f)], capture_output=True, text=True)
    for ln in r.stdout.splitlines():
        if ln.startswith("QR "):
            _, v, m, sz, bits = ln.split()
            return int(v), int(m), int(sz), bits
    return None


def ref_bits(payload, ver, mask):
    q = segno.make(payload, mode="byte", error="l", version=ver, mask=mask, boost_error=False, micro=False)
    s = "".join(str(b) for row in q.matrix for b in row)
    s += "0" * (-len(s) % 4)
    return "".join("%x" % int(s[i:i + 4], 2) for i in range(0, len(s), 4)), q.symbol_size(border=0)[0]


def payload_for(ver):
    n = CAP[ver]
    return bytes((i * 131 + ver * 17 + 7) & 0xFF for i in range(n))


def check(name, ok, info=""):
    if not ok:
        fails.append(name)
    print(("ok   " if ok else "FAIL ") + name + (f"  {info}" if info and not ok else ""))


def main():
    with tempfile.TemporaryDirectory() as tmp:
        elf = Path(tmp) / "qr.elf"
        build(elf)
        masks = range(8) if "--full" in sys.argv else (0,)
        for ver in range(1, 39):
            p = payload_for(ver)
            for m in masks:
                got = fw(elf, tmp, ver, ver, m, p)
                want = ref_bits(p, ver, m)
                check(f"v{ver} mask{m}", got is not None and (got[3], got[2]) == want, f"got={got and got[:3]}")
            if ver not in AUTO_AT:
                continue
            got = fw(elf, tmp, 1, 38, -1, p)
            # the firmware's penalty score may pick a different (equally valid) mask than segno; the symbol must be the
            # exact segno symbol for the mask the firmware chose
            check(f"v{ver} auto", got is not None and (got[3], got[2]) == ref_bits(p, ver, got[1]), f"mask {got and got[1]}")
        # a payload one byte over the capacity of version 38 must be refused
        check("too big", fw(elf, tmp, 1, 38, 0, bytes(CAP[38] + 1)) is None)
        try:
            import cv2
            import numpy as np
            sys.path.insert(0, str(ROOT / "tools"))
            import decode_tau_suite as D
            for ver in (3, 12, 20, 38):
                p = bytes((i * 7 + 3) & 0x7F | 0x20 for i in range(CAP[ver] - 2))
                got = fw(elf, tmp, 1, 38, 0, p)
                sz = got[2]
                bits = bin(int(got[3], 16))[2:].zfill(len(got[3]) * 4)[:sz * sz]
                m = np.array([int(c) for c in bits], dtype=np.uint8).reshape(sz, sz)
                px = 2 if sz > 120 else 4          # the firmware draws 2 px modules for the densest codes, never smoothed
                img = np.pad(255 * (1 - m), 4, constant_values=255).repeat(px, 0).repeat(px, 1).astype(np.uint8)
                cv2.imwrite(str(Path(tmp) / "qr.png"), img)
                txt = D.qr_text(str(Path(tmp) / "qr.png"))       # the same path the host tool uses on screenshots
                check(f"opencv decode v{got[0]}", txt.encode("latin1", "replace") == p, f"len {len(txt)}")
        except ImportError:
            print("note: OpenCV not installed; decode check skipped")
    print("PASSED" if not fails else f"FAILED {len(fails)}: {fails[:5]}")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
