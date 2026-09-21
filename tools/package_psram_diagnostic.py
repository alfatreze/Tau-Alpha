#!/usr/bin/env python3
"""Build the side-by-side Pocket bundle for the PSRAM diagnostic core (B-004).

The bundle pairs the diagnostic ROM (fw/build.sh psram-diag) with an RBF fitted
with TAU_PSRAM_PROBE. There is deliberately NO default RBF: the caller must pass
--rbf and --rbf-sha256, and the packager refuses any mismatch, so a ROM can never
be paired with an unaudited or wrong bitstream (the ROM also checks the probe ID
register at run time and stops with a message if the RBF has no probe).

The core is a separate developer identity (alfatreze.TAU_PSRAM, platform
tau_psram) that installs beside the normal TAU core. It writes nothing to the SD
card except what APF itself writes when the core is Quit: the 16 interact
persist words (Settings/<core>/Interact/_core/interact_persist.json), decoded
with tools/decode_tau_diag_log.py --interact --psram.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_CORE = ROOT / "dist/Cores/alfatreze.TAU"
SOURCE_PLATFORM_IMAGE = ROOT / "dist/Platforms/_images/tau.bin"

PLATFORM_ID = "tau_psram"
CORE_ID = "alfatreze.TAU_PSRAM"
SHORTNAME = "TAU_PSRAM"
NAME = "TAU PSRAM Diagnostic"
DEFAULT_ROM = ROOT / "work/diagnostics/psram-diag/tau.rom"
DEFAULT_OUT = ROOT / "work/diagnostics/psram-diag/pocket"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def save_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=4) + "\n", encoding="utf-8")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bit_reverse(source: Path, destination: Path) -> None:
    table = bytes(int(f"{value:08b}"[::-1], 2) for value in range(256))
    destination.write_bytes(source.read_bytes().translate(table))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rbf", type=Path, required=True,
                        help="raw ap_core.rbf built with TAU_PSRAM_PROBE")
    parser.add_argument("--rbf-sha256", required=True,
                        help="expected SHA-256 of the raw RBF (from the audit entry)")
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    parser.add_argument("--rom-sha256", default=None,
                        help="optional expected SHA-256 of the ROM")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--variant", default="",
                        help="suffix for a separate identity (e.g. t7 -> alfatreze.TAU_PSRAM_T7, "
                             "platform tau_psram_t7), so several builds can sit on the card")
    args = parser.parse_args()
    if args.variant and not re.fullmatch(r"[A-Za-z0-9]{1,4}", args.variant):
        raise SystemExit("--variant must be 1-4 letters/digits")
    v = args.variant
    platform_id = PLATFORM_ID + (f"_{v.lower()}" if v else "")
    core_id = CORE_ID + (f"_{v.upper()}" if v else "")
    shortname = SHORTNAME + (f"_{v.upper()}" if v else "")
    name = NAME + (f" {v.upper()}" if v else "")

    raw_rbf = args.rbf if args.rbf.is_absolute() else ROOT / args.rbf
    rom = args.rom if args.rom.is_absolute() else ROOT / args.rom
    output = args.output if args.output.is_absolute() else ROOT / args.output
    for required in (SOURCE_CORE, SOURCE_PLATFORM_IMAGE, raw_rbf, rom):
        if not required.exists():
            raise SystemExit(f"missing PSRAM diagnostic input: {required}")
    rbf_digest = digest(raw_rbf)
    if rbf_digest != args.rbf_sha256.lower():
        raise SystemExit(f"refusing an unaudited RBF: expected {args.rbf_sha256}, got {rbf_digest}")
    rom_digest = digest(rom)
    if args.rom_sha256 and rom_digest != args.rom_sha256.lower():
        raise SystemExit(f"refusing an unexpected ROM: expected {args.rom_sha256}, got {rom_digest}")
    if len(platform_id) > 15 or not re.fullmatch(r"[a-z0-9][a-z0-9_]*", platform_id):
        raise ValueError(f"invalid Analogue Pocket platform shortname: {platform_id!r}")

    temp = output.with_name(output.name + ".tmp")
    if temp.exists():
        shutil.rmtree(temp)
    temp.mkdir(parents=True)

    core_dir = temp / "Cores" / core_id
    shutil.copytree(SOURCE_CORE, core_dir)
    bit_reverse(raw_rbf, core_dir / "bitstream.rbf_r")

    core = load_json(core_dir / "core.json")
    metadata = core["core"]["metadata"]
    metadata["platform_ids"] = [platform_id]
    metadata["shortname"] = shortname
    metadata["description"] = "TAU PSRAM diagnostic (developer build, not the player)"
    save_json(core_dir / "core.json", core)
    if core_dir.name != f'{metadata["author"]}.{metadata["shortname"]}':
        raise RuntimeError("core folder does not match metadata identity")

    # Only the ROM slot: no result-log slot, no music/playlist/settings slots.
    data = load_json(core_dir / "data.json")
    data["data"]["data_slots"] = [data["data"]["data_slots"][0]]
    save_json(core_dir / "data.json", data)

    input_config = load_json(core_dir / "input.json")
    # Names are limited to 19 characters (input.json spec).
    input_config["input"]["controllers"][0]["mappings"] = [
        {"id": 0, "name": "Run (default)", "key": "pad_btn_a"},
        {"id": 1, "name": "Slow run", "key": "pad_btn_b"},
        {"id": 2, "name": "Read +1", "key": "pad_btn_x"},
        {"id": 3, "name": "Read +2", "key": "pad_btn_y"},
        {"id": 4, "name": "CPU window suite", "key": "pad_trig_l"},
        {"id": 5, "name": "CPU window soak", "key": "pad_trig_r"},
    ]
    save_json(core_dir / "input.json", input_config)

    interact = load_json(core_dir / "interact.json")
    interact["interact"]["variables"] = [
        {
            "name": f"(diag) result word {i}",
            "id": 30 + i,
            "type": "slider_u32",
            "enabled": True,
            "persist": True,
            "address": f"0x{0x20000000 + 4 * i:08X}",
            "defaultval": 0,
            "graphical": {"signed": False, "min": 0, "max": 2147483647,
                          "adjust_small": 1, "adjust_large": 1},
        }
        for i in range(16)
    ]
    interact["interact"]["messages"] = []
    save_json(core_dir / "interact.json", interact)

    assets_common = temp / "Assets" / platform_id / "common"
    assets_instance = temp / "Assets" / platform_id / core_id
    assets_common.mkdir(parents=True)
    assets_instance.mkdir(parents=True)
    shutil.copy2(rom, assets_common / "tau.rom")
    save_json(assets_instance / f"{name}.json", {
        "instance": {
            "magic": "APF_VER_1",
            "variant_select": {"id": 0, "select": False},
            "data_path": "",
            "data_slots": [{"id": 1, "filename": "tau.rom"}],
            "memory_writes": [],
        }
    })

    platform_dir = temp / "Platforms"
    (platform_dir / "_images").mkdir(parents=True)
    shutil.copy2(SOURCE_PLATFORM_IMAGE, platform_dir / "_images" / f"{platform_id}.bin")
    save_json(platform_dir / f"{platform_id}.json", {
        "platform": {"category": "Media Players", "name": name, "year": 2026,
                     "manufacturer": "alfatreze"}
    })

    hashes = {
        "psram_probe_ap_core.rbf": rbf_digest,
        "packaged_bitstream.rbf_r": digest(core_dir / "bitstream.rbf_r"),
        "psram_diag_tau.rom": digest(assets_common / "tau.rom"),
    }
    (temp / "SHA256SUMS.txt").write_text(
        "".join(f"{v}  {k}\n" for k, v in hashes.items()), encoding="utf-8")
    (temp / "INSTALL.txt").write_text(
        f"{name} - DEVELOPER BUILD\n\n"
        "Copy the Cores, Assets and Platforms folders to the Pocket SD root. It installs\n"
        "beside the normal TAU core under Media Players. It REQUIRES the packaged\n"
        "TAU_PSRAM_PROBE RBF; the ROM stops with a message if the RBF has no probe.\n"
        "The test writes and reads the four PSRAM dies only (about 1 MiB each); it never\n"
        "touches SDRAM player data or any file on the card.\n"
        "A = default timing, X = read sample +1 clock, Y = read sample +2, B = slow dials (+3/+3).\n"
        "L = CPU-window suite (only in a build with TAU_PSRAM_WINDOW), R = CPU-window soak until a mode key is pressed.\n"
        "Photograph the screen, then QUIT the core to the menu: APF then writes the\n"
        "result words to Settings/<core>/Interact/_core/interact_persist.json.\n"
        "Decode: tools/decode_tau_diag_log.py --interact --psram <that file>\n",
        encoding="utf-8",
    )

    if output.exists():
        shutil.rmtree(output)
    temp.rename(output)
    print(f"wrote PSRAM diagnostic bundle: {output}")
    for name, value in hashes.items():
        print(f"  {value}  {name}")


if __name__ == "__main__":
    main()
