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
