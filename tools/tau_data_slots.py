#!/usr/bin/env python3
"""Shared data-slot / persist-variable declarations for a packaged Tau core, used by both package.py (the shipped
release, dist/) and tools/package_dev_build.py (numbered test builds and the Diagnostic Build). Kept in one place
so the two never drift: the ids, filenames and persist words for the library index and the cold image must be
identical wherever a core declares them."""
import json
from pathlib import Path


def save(p: Path, v: dict) -> None:
    p.write_text(json.dumps(v, indent=4) + "\n")


def add_library_slot(core_dir: Path) -> None:
    """Data slot 5: the host-built index, read at boot (deferload, optional -- shipped cores never bundle the file
    itself, since it is built from the user's own music by tools/sync_media.py). Persist words 24-26: what was
    playing (kind/id/position), the index build it refers to, and the Shuffle All seed.

    Id 27 ("Library off (restart)") is RETIRED: the library on/off switch was removed along with legacy
    playlist mode (the library is now mandatory, since there is no fallback left to switch to). Per this
    project's own persist-id convention (fw/settings.inc), id 27 is left dead -- never declared again here,
    never reused for a new variable."""
    dj = json.loads((core_dir / "data.json").read_text())
    slots = dj["data"]["data_slots"]
    existing = next((x for x in slots if x["id"] == 5), None)
    if existing is not None:
        # Idempotent: the source this was copied from (e.g. dist/, once the release itself carries the library)
        # may already declare it identically -- only a genuinely different slot 5 is a real conflict.
        expect = {"name": "Media library index", "id": 5, "required": False, "deferload": True,
                  "parameters": "0x0", "filename": "tau-library.tdb"}
        assert existing == expect, f"data slot 5 already declared, and differently: {existing}"
    else:
        slots.append({"name": "Media library index", "id": 5, "required": False, "deferload": True,
                      "parameters": "0x0", "filename": "tau-library.tdb"})
        save(core_dir / "data.json", dj)

    ij = json.loads((core_dir / "interact.json").read_text())
    vs = ij["interact"]["variables"]
    if any(v["id"] in (24, 25, 26) for v in vs):
        return   # already declared (see above)
    gr = {"signed": False, "min": 0, "max": 2147483647, "adjust_small": 1, "adjust_large": 1}
    for i, nm in enumerate(("(internal) library 1", "(internal) library 2", "(internal) library 3")):
        vs.append({"name": nm, "id": 24 + i, "type": "slider_u32", "enabled": True, "persist": True,
                   "address": "0x%08X" % (0x20000030 + 4 * i),
                   "defaultval": 0, "graphical": gr})
    save(core_dir / "interact.json", ij)


def add_cold_slot(core_dir: Path) -> None:
    """Data slot 6: the cold image (Phase G), shipped with the core -- it is a firmware asset, not user data, so
    unlike the library index it is required for the cold-code features to be available (the fail-safe still holds
    if it is ever missing or fails its check)."""
    dj = json.loads((core_dir / "data.json").read_text())
    existing = next((x for x in dj["data"]["data_slots"] if x["id"] == 6), None)
    if existing is not None:
        expect = {"name": "Cold image", "id": 6, "required": False, "deferload": True,
                  "parameters": "0x0", "filename": "tau-cold.bin"}
        assert existing == expect, f"data slot 6 already declared, and differently: {existing}"
        return
    dj["data"]["data_slots"].append({"name": "Cold image", "id": 6, "required": False, "deferload": True,
                                      "parameters": "0x0", "filename": "tau-cold.bin"})
    save(core_dir / "data.json", dj)


def add_cover_slot(core_dir: Path) -> None:
    """Data slot 7: the pre-converted cover image (TAU_ART_TIMG, B-285, docs/COVER_TIMG_READER.md). Opened BY NAME at run time
    (the same 0192 mechanism as the playlist slot); no filename, so nothing is loaded at boot. Only cores built with the
    ART_TIMG firmware need it."""
    dj = json.loads((core_dir / "data.json").read_text())
    expect = {"name": "Cover image", "id": 7, "required": False, "deferload": True, "parameters": "0x0", "extensions": ["timg"]}
    existing = next((x for x in dj["data"]["data_slots"] if x["id"] == 7), None)
    if existing is not None:
        assert existing == expect, f"data slot 7 already declared, and differently: {existing}"
        return
    dj["data"]["data_slots"].append(expect)
    save(core_dir / "data.json", dj)



def add_assets_slot(core_dir: Path) -> None:
    """Data slot 8: tau-assets.bin (theme step 0d, docs/THEME_FILE_FORMAT.md): the one optional file of user-supplied assets (extra
    themes today; meter config and icons later). Never shipped with the core: written by tools/tau_assets.py or Tau Omega. A missing
    file is not an error, exactly like the library index."""
    dj = json.loads((core_dir / "data.json").read_text())
    expect = {"name": "Assets", "id": 8, "required": False, "deferload": True, "parameters": "0x0", "filename": "tau-assets.bin"}
    existing = next((x for x in dj["data"]["data_slots"] if x["id"] == 8), None)
    if existing is not None:
        assert existing == expect, f"data slot 8 already declared, and differently: {existing}"
        return
    dj["data"]["data_slots"].append(expect)
    save(core_dir / "data.json", dj)
