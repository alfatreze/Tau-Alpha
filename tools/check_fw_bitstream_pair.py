#!/usr/bin/env python3
"""Refuse a firmware/bitstream pairing the Pocket would refuse (B-581).

The boot interlock in fw/player.c (VERSION_OK) accepts only the CORE_VERSION values its build flags allow. A ROM built without
RAM_192K=1 CLK66=1 is refused by the 192 KB / 66.667 MHz bitstream and the Pocket shows a black screen with no message
(B-394, B-448, B-563, B-580 -- the same mistake four times). The ROM carries the list it accepts as text
("TAUFWPAIR:4D50331A;"); this tool reads it out of the ROM that is actually in the package and compares it with the
bitstream's CORE_VERSION (mp3_soc.v in this tree, or --bitstream-version / env TAU_BITSTREAM_VERSION when packaging a deliberately older bitstream).

  python3 tools/check_fw_bitstream_pair.py PACKAGE_DIR [PACKAGE_DIR ...] [--bitstream-version 4D50331A]

A package dir has Assets/<platform>/common/tau.rom (the one under dist/ works too). Exit 1 on any mismatch or a ROM with
no marker (an old or unknown build cannot be vouched for).
"""
import argparse, os, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARKER = re.compile(rb"TAUFWPAIR:([0-9A-F]{8}(?:,[0-9A-F]{8})*);")


def tree_core_version():
    m = re.search(r"CORE_VERSION\s*=\s*32'h([0-9A-Fa-f]{8})", (ROOT / "src/fpga/core/mp3_soc.v").read_text())
    if not m:
        sys.exit("cannot find CORE_VERSION in src/fpga/core/mp3_soc.v")
    return m.group(1).upper()


def rom_accepts(rom_bytes):
    m = MARKER.search(rom_bytes)
    return None if not m else m.group(1).decode().split(",")


def check(pkg, bitstream_version):
    roms = sorted(Path(pkg).glob("Assets/*/common/tau.rom"))
    if not roms:
        return [f"{pkg}: no Assets/*/common/tau.rom"]
    errs = []
    for rom in roms:
        acc = rom_accepts(rom.read_bytes())
        if acc is None:
            errs.append(f"{rom}: no TAUFWPAIR marker (built before B-581 or not a Tau player ROM); rebuild the firmware")
        elif bitstream_version not in acc:
            errs.append(f"{rom}: accepts CORE_VERSION {','.join(acc)} but the bitstream reports {bitstream_version} -- the Pocket "
                        "would refuse this ROM (black screen). Rebuild with the matching flags (release: RAM_192K=1 CLK66=1 SDRAM_BUSY=1)")
    return errs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("packages", nargs="+")
    ap.add_argument("--bitstream-version", help="8 hex digits; default: CORE_VERSION in src/fpga/core/mp3_soc.v")
    a = ap.parse_args(argv)
    ver = (a.bitstream_version or os.environ.get("TAU_BITSTREAM_VERSION") or tree_core_version()).upper()
    errs = [e for p in a.packages for e in check(p, ver)]
    for e in errs:
        print("PAIR MISMATCH:", e, file=sys.stderr)
    if errs:
        return 1
    print(f"PASS: firmware accepts the bitstream CORE_VERSION {ver} in {len(a.packages)} package(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
