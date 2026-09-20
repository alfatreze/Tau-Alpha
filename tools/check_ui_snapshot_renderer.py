#!/usr/bin/env python3
"""Fast host check for the first deterministic Tau UI framebuffer fixtures."""

import sys
import re
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
    settings = ("settings-home", "settings-appearance", "settings-audio", "settings-playback",
                "settings-colour", "settings-meter", "settings-eq", "settings-repeat",
                "settings-blank", "settings-diagnostics", "settings-info", "settings-tests", "settings-stress",
                "settings-stress-level", "settings-soak", "settings-stress-status", "settings-speed")
    if len({checksums[name] for name in settings + ("now-playing", "playlist-browser")}) != 19:
        raise SystemExit("FAIL: settings fixtures are not distinct")
    if checksums["now-playing"] == checksums["playlist-browser"]:
        raise SystemExit("FAIL: playlist browser fixture did not draw its overlay")
    if len({checksums[name] for name in ("now-playing", "paused", "stopped", "seeking")}) != 4:
        raise SystemExit("FAIL: transport fixtures are not distinct")
    if len({checksums[name] for name in ("now-playing", "metadata-long", "metadata-missing", "toast")}) != 4:
        raise SystemExit("FAIL: metadata and toast fixtures are not distinct")
    diagnostic = ("sdram-diagnostic-running", "sdram-diagnostic-pass",
                  "sdram-diagnostic-fail", "sdram-diagnostic-version-mismatch")
    if len({checksums[name] for name in diagnostic}) != len(diagnostic):
        raise SystemExit("FAIL: SDRAM diagnostic fixtures are not distinct")
    cpu_diagnostic = ("sdram-cpu-diagnostic-running", "sdram-cpu-diagnostic-pass",
                      "sdram-cpu-diagnostic-fail", "sdram-cpu-diagnostic-version-mismatch")
    if len({checksums[name] for name in cpu_diagnostic}) != len(cpu_diagnostic):
        raise SystemExit("FAIL: CPU SDRAM diagnostic fixtures are not distinct")
    if checksums["sdram-diagnostic-running"] == checksums["sdram-cpu-diagnostic-running"]:
        raise SystemExit("FAIL: CPU SDRAM running fixture is not clearly identified")
    preflight = ("sdram-cpu-preflight-running", "sdram-cpu-preflight-fail",
                 "sdram-cpu-preflight-readback-fail")
    if len({checksums[name] for name in preflight}) != len(preflight):
        raise SystemExit("FAIL: CPU SDRAM preflight fixtures are not distinct")
    if checksums["sdram-cpu-probe-bar"] == checksums["sdram-cpu-diagnostic-running"]:
        raise SystemExit("FAIL: CPU SDRAM hardware-probe fixture did not render the overlay")
    # The FPGA overlay must render every recorded probe bit. A-063 initially
    # widened the recorder to 46 bits but left the visible bar at 19 cells;
    # inspect the authoritative scanout guards so a host-only preview cannot
    # accidentally hide that integration error again.
    core_game = (Path(__file__).resolve().parent.parent / "src/fpga/core/core_game.vh").read_text(
        encoding="utf-8")
    match = re.search(
        r"wire sdram_probe_pixel.*?sdram_probe_x < 9'd(\d+).*?"
        r"wire sdram_probe_bar.*?sdram_probe_x < 9'd(\d+)",
        core_game,
        flags=re.DOTALL,
    )
    if not match or match.group(1) != match.group(2) or match.group(1) != "392":
        raise SystemExit("FAIL: FPGA probe bar does not cover all 49 A-074 cells")
    print(f"PASS: UI snapshot renderer emits {len(frames)} deterministic "
          "400x360 RGB565 fixtures")


if __name__ == "__main__":
    main()
