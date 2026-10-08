#!/usr/bin/env python3
"""Where a Tau core's build-bound files live on the card (RELEASE_SYSTEM_REVIEW_2026-10-08 H4).

The firmware ROM, the cold image and the boot splash belong to ONE build. Since 2026-10-08 they are core-specific: data slots 1, 4
and 6 set parameter bit 1, so the Pocket reads them from /Assets/<platform>/<Author.Core>/ (Analogue data.json docs), and two cores
on the same platform can never load each other's firmware. User and generated data (library index, tau-assets.bin, covers, media)
stays in /Assets/<platform>/common/, shared by every core of the platform.

Readers accept both places: packages and cards made before the move keep the files in common/.
"""
from pathlib import Path

BUILD_BOUND = ("tau.rom", "tau-cold.bin", "tau-loading.bin")
CORE_SPECIFIC = 0x2          # data.json parameter bit 1: "file specific to this core only"


def core_dir(base, platform, core_id):
    """<base>/Assets/<platform>/<core_id>: the folder the build-bound files go in (base = a package dir or a card root)."""
    return Path(base) / "Assets" / platform / core_id


def find(base, name, platform=None, core_id=None):
    """The build-bound file `name` under a package or card: the core-specific copy first, else the legacy common/ one. With
    platform/core_id unknown, any Assets/*/*/name (core folder or common). Returns a Path or None."""
    base = Path(base)
    if platform and core_id:
        for p in (core_dir(base, platform, core_id) / name, base / "Assets" / platform / "common" / name):
            if p.is_file():
                return p
        return None
    hits = sorted(base.glob(f"Assets/*/*/{name}"))
    core = [h for h in hits if h.parent.name != "common"]
    return (core or hits or [None])[0]


# ---- dev channel (B-672/B-673, owner decision 2026-10-08: option a) ----------------------------------------------------------------
# Dev builds live on ONE platform, `tau_dev` ("TAU Dev"), and also declare `tau` so they can read TAU's library, tau-assets.bin and
# music in place: the Pocket only opens files on the platforms a core declares (B-673). Consequence, accepted: every dev build is also
# listed in TAU's Select Core list (shortname + version). Build-bound files stay core-specific under Assets/tau_dev/<core>/.
DEV_PLATFORM, DEV_PLATFORM_NAME, MEDIA_PLATFORM = "tau_dev", "TAU Dev", "tau"
SHARED_SLOT_FILES = ("tau-library.tdb", "tau-assets.bin")    # user/generated data read from the media platform
PLATFORM_INDEX_SHIFT = 24                                     # data.json parameter bits [25:24]: index into platform_ids


def slot_platform_index(parameters):
    return (int(str(parameters), 16) >> PLATFORM_INDEX_SHIFT) & 3


def read_shared_media(data_json, index=1):
    """Point the shared-data slots (library index, tau-assets.bin) at platform_ids[index] (TAU's common folder)."""
    for sl in data_json["data"]["data_slots"]:
        if sl.get("filename") in SHARED_SLOT_FILES:
            p = int(str(sl.get("parameters", "0")), 16) & ~(3 << PLATFORM_INDEX_SHIFT)
            sl["parameters"] = "0x%X" % (p | (index << PLATFORM_INDEX_SHIFT))
    return data_json


def slot_dir(base, slot, platforms, core_id):
    """The folder the Pocket reads a data slot's file from: Assets/<platform_ids[bits 25:24]>/<core_id or common>."""
    params = int(str(slot.get("parameters", "0")), 16)
    plat = platforms[min(slot_platform_index(params), len(platforms) - 1)]
    return Path(base) / "Assets" / plat / (core_id if params & CORE_SPECIFIC else "common")
