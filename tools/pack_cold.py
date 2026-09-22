#!/usr/bin/env python3
"""Phase G1: build tau-cold.bin from a firmware ELF and bind it to the ROM.

  pack_cold.py pack  fw.elf OUT_DIR [--rom OUT_DIR/tau.rom]   writes OUT_DIR/tau-cold.bin and patches the layout id into the ROM
  pack_cold.py info  tau-cold.bin                              header fields

The cold image is the linked contents of the `.cold_data` section (VMA 0xA4800000, PSRAM), loaded at boot by the firmware
(fw/cold_core.h). Header, 20 bytes little-endian: magic `TCLD`, version u16 = 1, flags u16 = 0, size u32, crc32 u32 of the body,
layout id u32. The layout id is the CRC32 (31 bits) of every `.cold_data` symbol's (name, address, size): it changes whenever
anything about where cold objects live changes, and the same value is written into the ROM (variable `tau_cold_id`), so the
firmware refuses an image that does not belong to it. Contents are covered by the body CRC.
"""
import argparse
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLCHAIN = ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
MAGIC = 0x444C4354          # 'TCLD'
COLD_BASE = 0xA4800000
CODE_BASE = 0x24800000
COLD_CAP = 0x100000


def tool(name):
    return str(TOOLCHAIN / f"riscv-none-elf-{name}")


def layout_id(elf):
    out = subprocess.run([tool("nm"), "-S", "--defined-only", str(elf)], capture_output=True, text=True, check=True).stdout
    items = []
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 4:
            addr, size = int(p[0], 16), int(p[1], 16)
            if COLD_BASE <= addr < COLD_BASE + COLD_CAP or CODE_BASE <= addr < CODE_BASE + COLD_CAP:
                items.append((p[3], addr, size))
    blob = b"".join(f"{n}:{a:08X}:{s:X};".encode() for n, a, s in sorted(items))
    return zlib.crc32(blob) & 0x7FFFFFFF, len(items)


def symbol(elf, name):
    out = subprocess.run([tool("nm"), str(elf)], capture_output=True, text=True, check=True).stdout
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 3 and p[2] == name:
            return int(p[0], 16)
    raise SystemExit(f"symbol {name} not found in {elf}")


def build_image(body, lid):
    return struct.pack("<IHHIII", MAGIC, 1, 0, len(body), zlib.crc32(body) & 0xFFFFFFFF, lid) + body


def section_bytes(elf, name, out_dir):
    path = Path(out_dir) / f"tau-cold.{name}"
    subprocess.run([tool("objcopy"), "-O", "binary", "-j", f".{name}", str(elf), str(path)], check=True)
    data = path.read_bytes() if path.exists() else b""
    if path.exists():
        path.unlink()
    return data


def pack(elf, out_dir, rom):
    """Image body = .cold_data, zero padding to 32 bytes, .cold_text (the code, linked at the instruction alias directly behind
    the data in PSRAM). Layout id covers the symbols of both."""
    out_dir = Path(out_dir)
    data = section_bytes(elf, "cold_data", out_dir)
    code = section_bytes(elf, "cold_text", out_dir)
    pad = (-len(data)) % 32
    body = data + b"\0" * pad + code
    lid, n = layout_id(elf)
    if len(body) > COLD_CAP:
        raise SystemExit(f"cold image {len(body)} B exceeds the {COLD_CAP} B region")
    want = symbol(elf, "_cold_img_size")
    if want != len(body):
        raise SystemExit(f"linker says the cold image is {want} B, extracted {len(body)} B")
    if code and symbol(elf, "_cold_text_start") != CODE_BASE + len(data) + pad:
        raise SystemExit("cold code does not start where the padded data ends")
    (out_dir / "tau-cold.bin").write_bytes(build_image(body, lid))
    # bind the ROM: the firmware compares tau_cold_id with the header's layout id
    addr = symbol(elf, "tau_cold_id")
    rom = Path(rom)
    rbytes = bytearray(rom.read_bytes())
    old = struct.unpack_from("<I", rbytes, addr)[0]
    if old != 0xC01DC01D:
        raise SystemExit(f"tau_cold_id placeholder not found at 0x{addr:X} (found 0x{old:08X})")
    struct.pack_into("<I", rbytes, addr, lid)
    rom.write_bytes(bytes(rbytes))
    print(f"cold image: {len(data)} B data + {pad} pad + {len(code)} B code = {len(body)} B, {n} symbols, layout id 0x{lid:08X}; ROM patched at 0x{addr:X}")


def info(path):
    d = Path(path).read_bytes()
    m, v, f, size, crc, lid = struct.unpack_from("<IHHIII", d, 0)
    ok = m == MAGIC and len(d) == 20 + size and zlib.crc32(d[20:]) & 0xFFFFFFFF == crc
    print(f"magic {m:08X} version {v} flags {f} size {size} crc {crc:08X} layout {lid:08X} -> {'OK' if ok else 'BAD'}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("elf")
    p.add_argument("out_dir")
    p.add_argument("--rom")
    i = sub.add_parser("info")
    i.add_argument("bin")
    a = ap.parse_args()
    if a.cmd == "pack":
        pack(a.elf, a.out_dir, a.rom or Path(a.out_dir) / "tau.rom")
    else:
        info(a.bin)


if __name__ == "__main__":
    main()
