#!/usr/bin/env python3
"""Validate TAU's side-by-side Analogue Pocket package identity and assets."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
CORE_DIR = DIST / "Cores/alfatreze.TAU"
ASSET_DIR = DIST / "Assets/tau/common"
PLATFORM_JSON = DIST / "Platforms/tau.json"
PLATFORM_ART = DIST / "Platforms/_images/tau.bin"


def read_json(path):
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def main():
    core = read_json(CORE_DIR / "core.json")["core"]["metadata"]
    data = read_json(CORE_DIR / "data.json")["data"]["data_slots"]
    platform = read_json(PLATFORM_JSON)["platform"]

    assert core["author"] == "alfatreze"
    assert core["shortname"] == "TAU"
    assert core["platform_ids"] == ["tau"]
    # Pocket hardware does not render the superscript-alpha metadata glyph.
    # Keep OS-visible strings ASCII and carry the brand mark in artwork and in
    # Tau's own framebuffer, where the project controls glyph rendering.
    assert core["description"] == "TAU Music Player"
    assert platform["name"] == "TAU"
    assert platform["manufacturer"] == "alfatreze"

    firmware = next(slot for slot in data if slot["id"] == 1)
    splash = next(slot for slot in data if slot["id"] == 4)
    assert firmware["filename"] == "tau.rom"
    assert splash["filename"] == "tau-loading.bin"
    assert (ASSET_DIR / firmware["filename"]).is_file()
    assert (ASSET_DIR / splash["filename"]).is_file()
    assert PLATFORM_ART.stat().st_size == 521 * 165 * 2
    assert (CORE_DIR / "icon.bin").stat().st_size == 36 * 36 * 2

    old_paths = [
        DIST / "Cores/HarpMudd.Mp3Player",
        DIST / "Assets/mp3player",
        DIST / "Platforms/mp3player.json",
        DIST / "Platforms/_images/mp3player.bin",
    ]
    assert not any(path.exists() for path in old_paths)
    print("PASS: TAU package identity and side-by-side paths are consistent")


if __name__ == "__main__":
    main()
