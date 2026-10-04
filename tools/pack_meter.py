#!/usr/bin/env python3
"""Build a meter pack (.tmpk): one meter's drawing code as a freestanding blob linked at a fixed address (docs/features/meters/METER_PACKS.md).

  python3 tools/pack_meter.py layered_wave --org 0x24A00000 --out build/packs/layered_wave.tmpk [--elf-out file.elf]

The pack source is fw/meter_pack_<meter>.c. It is compiled for rv32im with no libc and no firmware symbols, linked at --org (the slot's address in the
PSRAM code window), checked to have no undefined symbols, and written as a 32-byte header (fw/meter_pack_core.h) followed by the load image. Needs the
vendored RISC-V toolchain (toolchain/ or RISCV_TOOLCHAIN_BIN)."""
import argparse, os, re, struct, subprocess, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ABI = int(re.search(r"#define MTR_PACK_ABI (\d+)u", (ROOT / "fw/meter_pack.h").read_text()).group(1))      # read from the firmware header: one source of truth
METER_IDS = {"layered_wave": 16, "winamp_bars": 12}      # the VIZ_* id of the meter (fw/meter_gen_enum.h)
SLOT = {"layered_wave": 0, "winamp_bars": 1}              # the slot of each meter (fw/meter_pack.h mtr_pack_slot_of)
SLOT_BASE, SLOT_SIZE = 0x24840000, 0x10000


def tool(name):
    tb = os.environ.get("RISCV_TOOLCHAIN_BIN")
    base = Path(tb) if tb else ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
    return str(base / ("riscv-none-elf-" + name))


def symbols(elf):
    out = subprocess.run([tool("nm"), str(elf)], capture_output=True, text=True, check=True).stdout
    return {ln.split()[-1]: (int(ln.split()[0], 16), ln.split()[1]) for ln in out.splitlines() if len(ln.split()) == 3}


SCRATCH_ORG = 0x00027400      # the ABI scratch address (fw/meter_pack.h MTR_PACK_SCRATCH_ORG); host tests pass their own


def build(meter, org, elf_out=None, extra=(), scratch=None):
    src = ROOT / "fw" / ("meter_pack_%s.c" % meter)
    if not src.exists():
        raise SystemExit("no pack source %s" % src)
    elf = Path(elf_out) if elf_out else Path(os.environ.get("TMPDIR", "/tmp")) / ("pack_%s.elf" % meter)
    scratch = SCRATCH_ORG if scratch is None else scratch
    alias = 0x80000000 if org >= 0x24000000 else 0     # rodata/pstate at the data alias of the PSRAM window (the instruction alias cannot be loaded from)
    cmd = [tool("gcc"), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles", "-fno-pic", "-mcmodel=medany",
           "-ffunction-sections", "-fdata-sections", "-Wall", "-Wno-unused-function", "-Wno-comment", "-Wno-unused-variable", "-Wno-unused-const-variable", "-I", str(ROOT / "fw"),
           "-Wl,--gc-sections", "-Wl,--no-warn-rwx-segments", "-Wl,--defsym=PACK_ORG=0x%X" % org, "-Wl,--defsym=DATA_ALIAS=0x%X" % alias, "-Wl,--defsym=SCRATCH_ORG=0x%X" % scratch, "-T", str(ROOT / "fw/meter_pack.ld"), str(src), "-lgcc", "-o", str(elf)] + list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True)
    stderr = "\n".join(l for l in r.stderr.splitlines() if "dot moved backwards" not in l)   # benign: sections at explicit scratch/alias addresses
    if r.returncode or "warning" in stderr:
        sys.stderr.write(r.stderr); raise SystemExit("pack build failed or warned")
    undef = subprocess.run([tool("nm"), "-u", str(elf)], capture_output=True, text=True, check=True).stdout.split()
    if undef:
        raise SystemExit("pack names symbols it does not define: %s" % ", ".join(undef))
    sy = symbols(elf)
    binf = elf.with_suffix(".bin")
    subprocess.run([tool("objcopy"), "-O", "binary", str(elf), str(binf)], check=True)
    body = binf.read_bytes()
    load_end = sy["_pack_load_end"][0] - org
    data_lma, data_start, data_end = sy["_pack_data_lma"][0] - org, sy["_pack_data_start"][0], sy["_pack_data_end"][0]
    bss_start, bss_end = sy["_pack_bss_start"][0], sy["_pack_bss_end"][0]
    pst_off, pst_end = sy["_pack_pstate_start"][0] - alias - org, sy["_pack_pstate_end"][0] - alias - org
    if load_end - 3 <= len(body) < load_end:        # objcopy stops at the last byte of content; the link script rounds the end up to 4
        body += b"\0" * (load_end - len(body))
    if len(body) != load_end:
        raise SystemExit("load image %d B but sections end at %d" % (len(body), load_end))
    entry = sy["mtr_pack_entry"][0] - org
    if bss_start != data_end:
        raise SystemExit(".bss does not follow .data in the scratch area")
    hdr = struct.pack("<IHHIIIIIIIIII", 0x4B504D54, ABI, METER_IDS[meter], len(body), org, entry, zlib.crc32(body) & 0xFFFFFFFF, data_lma, data_end - data_start,
                      data_start, bss_end - bss_start, pst_off, pst_end - pst_off)
    return hdr + body, {"load": len(body), "data": data_end - data_start, "bss": bss_end - bss_start, "pstate": pst_end - pst_off, "entry": entry, "org": org, "scratch": scratch}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("meter"); ap.add_argument("--org", type=lambda s: int(s, 0), default=None, help="the slot address (default: this meter's slot in the PSRAM code window, MTR_PACK_SLOT_BASE + slot x MTR_PACK_SLOT_SIZE)"); ap.add_argument("--out", required=True); ap.add_argument("--elf-out"); ap.add_argument("--scratch", type=lambda s: int(s, 0), default=SCRATCH_ORG)
    ap.add_argument("-D", action="append", default=[], help="extra -D define for the pack build")
    a = ap.parse_args()
    if a.org is None: a.org = SLOT_BASE + SLOT[a.meter] * SLOT_SIZE
    blob, info = build(a.meter, a.org, a.elf_out, ["-D" + d for d in a.D], a.scratch)
    Path(a.out).parent.mkdir(parents=True, exist_ok=True); Path(a.out).write_bytes(blob)
    print("%s: %d B file (%d B image), slot state %d B, scratch %d B (data %d + bss %d), entry +0x%X, org 0x%X, scratch 0x%X" % (a.out, len(blob), info["load"], info["pstate"], info["data"] + info["bss"], info["data"], info["bss"], info["entry"], info["org"], info["scratch"]))


if __name__ == "__main__":
    main()
