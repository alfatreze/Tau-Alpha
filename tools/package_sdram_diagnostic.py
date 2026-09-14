#!/usr/bin/env python3
"""Build a side-by-side Analogue Pocket bundle for the Phase 1 SDRAM test.

The diagnostic uses its own core/platform identifiers, so installing it does
not overwrite the normal TAU player.  Inputs are staged artifacts: the fresh
Quartus RBF copied from the VM and ``fw/build.sh sdram-diag`` output.
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE_CORE = ROOT / "dist/Cores/alfatreze.TAU"
SOURCE_PLATFORM_IMAGE = ROOT / "dist/Platforms/_images/tau.bin"
RAW_RBF = ROOT / "work/diagnostics/sdram/fpga/ap_core.rbf"
DIAG_ROM = ROOT / "work/diagnostics/sdram/tau.rom"
OUTPUT = ROOT / "work/diagnostics/sdram/pocket"

PLATFORM_ID = "tau_sdram_diag"
CORE_ID = "alfatreze.TAU_SDRAM_DIAG"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def save_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=4) + "\n", encoding="utf-8")


def bit_reverse(source: Path, destination: Path) -> None:
    table = bytes(int(f"{value:08b}"[::-1], 2) for value in range(256))
    destination.write_bytes(bytes(source.read_bytes()).translate(table))


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    for required in (SOURCE_CORE, SOURCE_PLATFORM_IMAGE, RAW_RBF, DIAG_ROM):
        if not required.exists():
            raise SystemExit(f"missing diagnostic input: {required}")

    temp = OUTPUT.with_name(OUTPUT.name + ".tmp")
    if temp.exists():
        shutil.rmtree(temp)
    temp.mkdir(parents=True)

    core_dir = temp / "Cores" / CORE_ID
    shutil.copytree(SOURCE_CORE, core_dir)
    bit_reverse(RAW_RBF, core_dir / "bitstream.rbf_r")

    core = load_json(core_dir / "core.json")
    metadata = core["core"]["metadata"]
    metadata["platform_ids"] = [PLATFORM_ID]
    if len(PLATFORM_ID) > 15 or not re.fullmatch(r"[a-z0-9][a-z0-9_]*", PLATFORM_ID):
        raise ValueError(f"invalid Analogue Pocket platform shortname: {PLATFORM_ID!r}")
    # Analogue requires Cores/Author.Core to match metadata author/shortname
    # exactly.  Spaces here while the folder used underscores produced a
    # Pocket "Load error in 'core' / Error in core setup" before firmware ran.
    metadata["shortname"] = "TAU_SDRAM_DIAG"
    metadata["description"] = "TAU Phase 1 SDRAM mailbox diagnostic"
    save_json(core_dir / "core.json", core)
    expected_core_id = f'{metadata["author"]}.{metadata["shortname"]}'
    if core_dir.name != expected_core_id:
        raise RuntimeError(
            f"core folder {core_dir.name!r} does not match metadata identity "
            f"{expected_core_id!r}"
        )

    data = load_json(core_dir / "data.json")
    data["data"]["data_slots"] = [data["data"]["data_slots"][0]]
    save_json(core_dir / "data.json", data)

    input_config = load_json(core_dir / "input.json")
    input_config["input"]["controllers"][0]["mappings"] = [{
        "id": 0, "name": "Run diagnostic again", "key": "pad_btn_a"
    }]
    save_json(core_dir / "input.json", input_config)

    interact = load_json(core_dir / "interact.json")
    interact["interact"]["variables"] = []
    interact["interact"]["messages"] = []
    save_json(core_dir / "interact.json", interact)

    assets_common = temp / "Assets" / PLATFORM_ID / "common"
    assets_instance = temp / "Assets" / PLATFORM_ID / CORE_ID
    assets_common.mkdir(parents=True)
    assets_instance.mkdir(parents=True)
    shutil.copy2(DIAG_ROM, assets_common / "tau.rom")
    save_json(assets_instance / "TAU SDRAM Diagnostic.json", {
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
    shutil.copy2(SOURCE_PLATFORM_IMAGE, platform_dir / "_images" / f"{PLATFORM_ID}.bin")
    save_json(platform_dir / f"{PLATFORM_ID}.json", {
        "platform": {
            "category": "Media Players",
            "name": "TAU SDRAM Diagnostic",
            "year": 2026,
            "manufacturer": "alfatreze",
        }
    })

    hashes = {
        "source_ap_core.rbf": digest(RAW_RBF),
        "packaged_bitstream.rbf_r": digest(core_dir / "bitstream.rbf_r"),
        "diagnostic_tau.rom": digest(assets_common / "tau.rom"),
    }
    (temp / "SHA256SUMS.txt").write_text(
        "".join(f"{value}  {name}\n" for name, value in hashes.items()),
        encoding="utf-8",
    )
    (temp / "INSTALL.txt").write_text(
        "TAU SDRAM DIAGNOSTIC - DEVELOPER BUILD\n\n"
        "Copy the Cores, Assets, and Platforms folders to the Pocket SD root.\n"
        "This installs beside the normal TAU core under Media Players.\n"
        "Launch TAU SDRAM Diagnostic and photograph PASS or the complete FAIL screen.\n"
        "Press A to repeat the diagnostic.\n",
        encoding="utf-8",
    )

    if OUTPUT.exists():
        shutil.rmtree(OUTPUT)
    temp.rename(OUTPUT)
    print(f"wrote side-by-side Pocket diagnostic bundle: {OUTPUT}")
    for name, value in hashes.items():
        print(f"  {value}  {name}")


if __name__ == "__main__":
    main()
