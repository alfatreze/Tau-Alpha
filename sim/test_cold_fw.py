#!/usr/bin/env python3
"""Phase G1 host test: the firmware cold-image loader (fw/cold_core.h, built by the vendored RISC-V toolchain, run in tools/rv32sim.py)
against images written by tools/pack_cold.py: a good image, and every refusal (bad magic, version, size, layout id, CRC, truncation, extra bytes, no file)."""
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pack_cold as P

GCC = ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc"
fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {info}" if info and not cond else ""))


def build(out, size, lid):
    cmd = [str(GCC), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-Wall", "-Wextra",
           "-Wno-unused-function", f"-DWANT_SIZE={size}u", f"-DWANT_ID={lid}u", "-Wl,--no-warn-rwx-segments",
           "-T", str(ROOT / "tools/host/link.ld"), str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/cold_harness.c"), "-o", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or "warning" in r.stderr:
        print(r.stderr)
        raise SystemExit("harness build failed or warned")


def run(elf, blob, tmp):
    f = Path(tmp) / "img.bin"
    f.write_bytes(blob)
    r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), str(elf), str(f)], capture_output=True, text=True)
    return [ln for ln in r.stdout.splitlines() if not ln.startswith("[")]


def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def main():
    if not GCC.exists():
        print("SKIP: vendored RISC-V toolchain not found")
        return 0
    body = bytes((i * 7 + 3) & 255 for i in range(5136))          # a 5,136 B body like the real one (not multiple of 4096)
    lid = 0x04AC7C06
    good = P.build_image(body, lid)
    with tempfile.TemporaryDirectory() as td:
        elf = Path(td) / "cold.elf"
        build(elf, len(body), lid)
        out = run(elf, good, td)
        check("good image loads", out[:1] == ["COLD E0"], out[:1])
        check("copied bytes equal the body", len(out) > 1 and out[1] == "HASH 0x%08X" % fnv(body), out)

        def flip(b, off):
            b = bytearray(b)
            b[off] ^= 0xFF
            return bytes(b)
        cases = [
            ("bad magic -> E11", flip(good, 0), 11), ("bad version -> E11", good[:4] + struct.pack("<H", 2) + good[6:], 11),
            ("size field wrong -> E12", good[:8] + struct.pack("<I", len(body) + 4) + good[12:], 12),
            ("truncated -> E12", good[:-8], 12), ("extra bytes -> E12", good + b"\0" * 8, 12),
            ("layout id of another build -> E14", good[:16] + struct.pack("<I", lid ^ 1) + good[20:], 14),
            ("flipped body byte -> E13", flip(good, 20 + 100), 13),
            ("stored CRC wrong -> E13", good[:12] + struct.pack("<I", (zlib.crc32(body) ^ 1) & 0xFFFFFFFF) + good[16:], 13),
            ("header shorter than 20 bytes -> E11", good[:10], 11), ("empty slot -> E10", b"", 10),
        ]
        for name, blob, want in cases:
            o = run(elf, blob, td)
            check(name, o[:1] == [f"COLD E{want}"], o[:1])
    print("FAILED: " + ", ".join(fails) if fails else "all cold image tests passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
