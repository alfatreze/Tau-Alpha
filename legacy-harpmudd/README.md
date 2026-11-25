# legacy-harpmudd/

The retired old player (`fw/`) and the tools, tests and harnesses that only served it. Mostly inherited from HarpMudd's MP3 player (Tau baseline `7ef8f0fb`, v1.4.0). Nothing here is built, tested or shipped: Player 2 (`fw2/`) is the only firmware that ships. Moved here on 2026-11-25 by decision D-T20 (phase 1: "retire what is not needed, rewrite what is"). The first folder of this name (18 files, phase 0) was deleted by D-T20 phase 2 (`removal.patch`); this one holds the phase 1 move.

Original paths are mirrored: `legacy-harpmudd/fw/player.c` was `fw/player.c`, `legacy-harpmudd/tools/check_heap_gap.py` is the old (old-player) `tools/check_heap_gap.py` (rule 6 now has a Player 2 `tools/check_heap_gap.py`). Plain `git mv`, so history is kept (`git log --follow`).

What is here:
- `fw/`: `player.c`, `build.sh`, `flac.h` and the 25 `.inc` files of the old player (the 27 headers `fw2/` includes, the generated meter headers `tools/gen_meters.py` writes and `pcm_ring_fw.inc` stay in `fw/`).
- `tools/`: `check_cold_calls.py`, `check_targets_link.py`, `diag_cost.py`, `meter_cost_estimate.py`, `ui_snapshot_renderer.py`, `theme_preview.py`, `visual_review.py`, `package_psram_diagnostic.py`, `player2/globals_map.py`, the old `check_heap_gap.py` with its `heap_gap_baseline.json`, the old `make_release.py` (a copy: `tools/make_release.py` now ships Player 2 only), `lab/flacperf/` (the old decoder's cycle lab), the FLAC and art host harnesses in `host/`.
- `package.py` (the old release packager) and, in `sim/`, the tests of old-player code only: `test_clk66`, `test_ram192k`, `test_clut_contract`, `test_fw_pair_check`, `test_theme`, `test_splash_asset` (+ `splash_harness.c`), `test_sdram_map_overlap`, `test_art_decode` (+ `gen_art_fixtures.py`), `test_tau_timg`.

How to restore one file: `git mv legacy-harpmudd/tools/check_cold_calls.py tools/check_cold_calls.py`. It will not run against today's tree without its old-player sources (`legacy-harpmudd/fw/`).

`sim/test_diag_features.py` still reads `legacy-harpmudd/fw/` (the register `fw/diag_features.json` lists features that exist only in the old player): when this folder is deleted, drop those register entries with it.

Deletion needs the owner's approval; nothing live reads this folder except that test.
