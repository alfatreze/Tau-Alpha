#!/usr/bin/env python3
"""Shared fixtures for the Tau Omega contract (RELEASE_SYSTEM_REVIEW_2026-10-08 H1). Deterministic; `--check` fails if the checked-in
copies differ from what this script makes (wired into sim/test_tau_compat.py).

  docs/schemas/fixtures/tau-assets-roundtrip.bin   a TAUA v1 container with THEM + METR + PRST, built with the reference packers.
      Rule for any writer (Omega's theme editor): read it, change the themes, write it back -> the METR and PRST section bytes must be
      identical and the file must still parse here (tools/tau_assets.py dump). The two other sections hold user meter and EQ presets.

  python3 tools/make_compat_fixtures.py [--check]
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_assets as ta          # noqa: E402
import halcyon_assets as ha      # noqa: E402

OUT = ROOT / "docs/schemas/fixtures/tau-assets-roundtrip.bin"
PRESETS = [{"name": "MY CONTROLS", "controls": {"warmth": 2, "bass": 1, "vocal": 0, "punch": -1, "sibilance": 3, "air": -2}},
           {"name": "SOFT", "controls": {"warmth": 1, "air": -1}}]


def roundtrip_assets():
    ex = ROOT / "themes/user_examples"
    return ta.pack_container([(b"THEM", ta.pack_themes([json.loads((ex / "sunset.json").read_text())])),
                              (b"METR", ta.pack_meters(json.loads((ex / "meters_example.json").read_text()))),
                              (b"PRST", ha.pack_presets(PRESETS))])


def main():
    blob = roundtrip_assets()
    if "--check" in sys.argv:
        ok = OUT.is_file() and OUT.read_bytes() == blob
        print(("ok   " if ok else "FAIL ") + f"{OUT.relative_to(ROOT)} matches the reference packers")
        return 0 if ok else 1
    OUT.write_bytes(blob)
    print(f"wrote {OUT.relative_to(ROOT)} ({len(blob)} bytes, sections THEM METR PRST)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
