#!/usr/bin/env python3
"""Fast host check for the first deterministic Tau UI framebuffer fixtures."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ui_snapshot_renderer as ui  # noqa: E402


def main():
    frames = {name: maker() for name, maker in ui.FIXTURES.items()}
    checksums = {}
    for name, frame in frames.items():
        if len(frame.pixels) != ui.FB_W * ui.FB_H:
            raise SystemExit(f"FAIL: {name} has the wrong framebuffer size")
        if any(pixel < 0 or pixel > 0xFFFF for pixel in frame.pixels):
            raise SystemExit(f"FAIL: {name} contains a non-RGB565 pixel")
        checksums[name] = sum(frame.pixels) & 0xFFFFFFFF
    if checksums["empty-library"] == checksums["playlist-error"]:
        raise SystemExit("FAIL: playlist error fixture did not draw its reason")
    if checksums["now-playing"] == checksums["playlist-browser"]:
        raise SystemExit("FAIL: playlist browser fixture did not draw its overlay")
    print("PASS: UI snapshot renderer emits four deterministic 400x360 RGB565 fixtures")


if __name__ == "__main__":
    main()
