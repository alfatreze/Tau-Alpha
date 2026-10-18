# HarpMudd code discontinuation: status at v0.6.0 (2026-10-18)

Continues the 2026-10-12/13 analysis (`HARPMUDD_COMPARISON_AND_REWRITE_2026-10-12.md` and `provenance-2026-10-12/`, kept in the main checkout, not on this branch). Same tool, `measure.py` of that folder, run read-only on pinned revisions;
output in `provenance-2026-10-18/` (`summary-v0.6.0.json` for tag v0.6.0 = `0e0b1d2`, `summary.json` and `files.csv` for branch `archive-old-fw` = `4c0dcc2`). Source lines = nonblank, non-comment; origin = last textual origin by copy-aware blame (`-w -M -C -C`), not creative ownership.

## What ships in v0.6.0 and where HarpMudd-origin lines remain

| Part | Source lines | HarpMudd-origin | Ships |
|---|---|---|---|
| `fw2/` (Player 2 firmware) | 12,752 | 20 (0.2%) | yes |
| `fw/` headers that `fw2/` includes (27 files) | 3,307 | 0 | yes |
| `fw/` rest (the old player) | 13,301 | 3,758 (28%) | no |
| `src/fpga/core` (the FPGA design) | 24,281 | 6,411 (26%) | yes |
| `src/fpga/rtl` (VexRiscv, `sdram_fb.sv`) | 6,911 | 6,843 (99%) | yes (vendored) |
| `third_party` (Helix MP3 decoder) | 7,173 | 6,963 (97%) | yes (vendored) |

Player 2 dropped HarpMudd's player code: the shipped firmware has about 20 HarpMudd-origin lines of 12,752. What still ships from HarpMudd's lineage is the shared FPGA design (about a quarter of `src/fpga/core`) and vendored third-party code.
The diagnostic features (Tests, Stress, Check, sweeps, reports, Info) are Tau's own work; HarpMudd's player had none. They live in the old player's `fw/` and, rebuilt, in `fw2/diagnostics/`.

## Repo-wide, v0.6.0 against the branch below

| | v0.6.0 | `archive-old-fw` |
|---|---|---|
| First-party code (firmware, RTL, tooling, tests, framework), HarpMudd share | 80,959 lines, 14,690 (18.1%) | 77,261 lines, 13,996 (18.1%) |
| Whole tree incl. vendor and generated, HarpMudd share | 29.6% | 30.3% |
| Firmware lines / HarpMudd firmware lines | 16,874 / 4,398 | 15,405 / 3,743 |
| Original HarpMudd production lines still present (unique origins) | 5,440 of 7,788 | 4,841 |

Authors by first-party source lines at v0.6.0: Abel Santos 65,667, HarpMudd 13,228, Claude 592.

## What was archived (branch `archive-old-fw`, not merged)

Moved outside the repo to `tau-alpha-archive/old-firmware-2026-10-18/` (with a README; also in git history, tag v0.6.0): the `archive/` folder (cassette meter, player2 bring-up, superseded builds, QR encoder), `fw/alloc.c`, `flac.c` (old FLAC decoder),
`link.ld`, `pcm_ring.h`, `psram_diag.c`, `start.S`, `sysio.c`, and 30 host tests and harnesses of the old player that nothing else references. 40 lines were dropped from `make test-host`; the rest passes, `fw2/build.sh` is unchanged.

## Why the rest of the old player is still in the tree

About 50 old files were restored after a first attempt to move 57 `fw/` files, because six live checks read `fw/player.c`, `fw/settingsui.inc` or `fw/build.sh`, and `player.c` includes most of the old player:

| Tool | What it reads |
|---|---|
| `tools/gen_themes.py --check` | the accent palette and `th_accent_of()` from `fw/player.c`; writes `fw/theme_data.h`, which `fw2/` includes |
| `sim/test_splash_asset.py` | `ui_splash_asset()` and the `TAU_SPLASH_*` constants cut out of `fw/player.c` |
| `sim/test_meter_core.py --check` | `fw/meter_core.h` against a verbatim copy of the old `wviz_bars_tick()` |
| `tools/meters/preview/build.py --check` | `fw/player.c` (the meter preview stack) |
| `tools/gen_meters.py --check` | `fw/player.c`, `fw/settingsui.inc`; its outputs `fw/meter_gen*.h`, `meters_gen.h` feed the Player 2 preset generators |
| `tools/gen_diag_features.py --check` | `fw/diag_features.json`, `fw/build.sh`; the generated header is included by `fw2/` |

(`tools/fit_manifest.py check-bundles` only names `player.c` in a comment and passes without it.)

## Next, to finish the discontinuation

1. Point the palette and theme source of `gen_themes`, the meter tables of `gen_meters`/the preview build, and the diagnostic-feature register at `fw2/` equivalents; archive `test_splash_asset` and `test_meter_core` (they test old-player code).
2. Archive the rest of `fw/` (`player.c`, the `*.inc` files, `build.sh`) and the release tooling's legacy non-Player-2 path (`make_release.py` without `--player2`, `package.py`, `check_heap_gap.py`, `check_targets_link.py`).
3. Re-run `measure.py`; expected first-party HarpMudd share falls by the old player's 3,758 lines (to about 14%).
4. What remains after that is the shared FPGA design (6,411 lines) and the vendored code; reducing those is the RTL rewrite of the 2026-10-12 plan, not firmware work.
