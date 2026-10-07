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
NEED = re.compile(rb"TAUFWNEED:([A-Z_,]*);")



def tree_core_version():
    m = re.search(r"CORE_VERSION\s*=\s*32'h([0-9A-Fa-f]{8})", (ROOT / "src/fpga/core/mp3_soc.v").read_text())
    if not m:
        sys.exit("cannot find CORE_VERSION in src/fpga/core/mp3_soc.v")
    return m.group(1).upper()


def rom_accepts(rom_bytes):
    m = MARKER.search(rom_bytes)
    return None if not m else m.group(1).decode().split(",")


def rom_needs(rom_bytes):
    """The bitstream features the firmware uses (TAUFWNEED in the ROM, B-653), or None for a ROM built before the marker."""
    m = NEED.search(rom_bytes)
    return None if not m else [x for x in m.group(1).decode().split(",") if x]


def check_features(pkg, macros, allow=()):
    """Each feature the ROM needs must be among the macros the bitstream was built with. Every feature degrades gracefully when missing (a boot probe reads NO UNIT), which is why
    a mismatch is otherwise silent. `allow` names features that may be missing on purpose (the fail-safe test builds)."""
    sys.path.insert(0, str(ROOT / "tools"))
    import fit_manifest
    errs = []
    for rom in sorted(Path(pkg).glob("Assets/*/common/tau.rom")):
        need = rom_needs(rom.read_bytes())
        if need is None:
            continue                                   # no marker: the version check already reports it
        for f in need:
            if f in allow:
                continue
            if fit_manifest.FEATURES.get(f, f) not in macros:
                errs.append(f"{rom}: the firmware uses {f} but the bitstream was built without {fit_manifest.FEATURES.get(f, f)} -- that feature would read NO UNIT. "
                            f"Pair it with a bitstream that has it, or build the firmware without it, or pass --allow-missing {f} for a deliberate fail-safe test")
    return errs


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
    ap.add_argument("--bitstream-manifest", help="the JSON next to a collected RBF (tools/vm_fit.py collect): also check the firmware's FEATURES against the macros it was built with")
    ap.add_argument("--allow-missing", default="", help="comma list of features that may be missing on purpose (HALCYON,LPC,POLY,SDRAM_BUSY)")
    a = ap.parse_args(argv)
    ver = (a.bitstream_version or os.environ.get("TAU_BITSTREAM_VERSION") or tree_core_version()).upper()
    errs = [e for p in a.packages for e in check(p, ver)]
    if a.bitstream_manifest:
        import json
        macros = set(json.loads(Path(a.bitstream_manifest).read_text())["macros"])
        errs += [e for p in a.packages for e in check_features(p, macros, tuple(x for x in a.allow_missing.split(",") if x))]
    for e in errs:
        print("PAIR MISMATCH:", e, file=sys.stderr)
    if errs:
        return 1
    print(f"PASS: firmware accepts the bitstream CORE_VERSION {ver} in {len(a.packages)} package(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
