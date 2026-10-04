# Handoff 2026-10-04: report codes as a pixel grid (TPG)

Branch `barcode-study` (worktree `../tau-alpha-barcode`), merged with `main` at B-593 and ready to merge into `main`. Audit entry B-594, decisions D-R01..D-R05 (`docs/DECISIONS.md`), study with all numbers `docs/features/BARCODE_STUDY.md`.

## What it is
Reports (Check, Decode Sweep, Meter Sweep, Meter Trace, Blit Test, Info export, Meter Config export) leave the Pocket as screenshots. A screenshot is a lossless 400x360 PNG of exact RGB565 pixels (hardware-confirmed), so the record is written straight into the pixels (**TPG**, `tools/tpg.py` docstring) instead of a QR code. Each code page now offers three views, X cycles: **robust grid** (the default: a centred square of 4x4-pixel cells, 2 bits per channel, up to 6,059 B, survives JPEG q80+ and common resizing), **lossless grid** (1 px per 16 bits, up to 259,184 B, used automatically when the report is too big for robust) and the **QR code**. All seven pages share one drawing path and caption set (`rep_draw()` in `fw/suite.inc`). The Info page exports every row as text: `python3 tools/decode_tau_suite.py --grid shot.png --table`.

## Files
- Format and codec: `tools/tpg.py`, `fw/tpg.h` (C and Python equal pixel for pixel), decoder `tools/decode_tau_suite.py --grid/--table`, mock-up `tools/lab/tpg_preview.py`, host lab `tools/lab/barcode_lab.py`, pixel fidelity test page `fw/pixgrid.h` + `tools/pixgrid_check.py`.
- Tests (in `make test-host`): `sim/test_tpg.py`, `sim/test_tpg_fw.py`, `sim/test_pixgrid.py`.
- Firmware: `fw/suite.inc` (`rep_draw`, `rep_context`, the `REP_*` macros), `fw/info_export.inc`, `fw/wvcfg_export.inc`, `fw/suite_core.h` (tags 26 `SR_T_INFOTEXT`, 27 `SR_T_NOWPLAYING`). Build switch `TPG=1` (`TAU_TPG`), default off; `TAU_QR_MAXV` (default 38).
- Packaging: `tools/package_dev_build.py --barcode NN` makes `TAU DEV BARCODE NN` (platform id `tau_devbarNN`).

## State
- Hardware-confirmed on the branch builds `TAU DEV BARCODE 01-04` (all pre-merge): pixel fidelity (4 patterns 100% exact), a real Check record identical across robust/lossless/QR, Info export (34 rows), context block, Decode Sweep, centred layout, robust default, identical captions.
- **The merged tree has not run on a Pocket.** The merge only changed record tag numbers (this branch's two new tags moved from 25/26 to 26/27 because `main` took 25 for `SR_T_LOAD2`) and combined the Makefile tests; `make test-host` passes and both `TPG=0` and `TPG=1` link (heap gap 6,912 / 6,704 B; `TPG=0` equals `main`'s baseline, so default builds are unchanged).
- Tau Omega: the six TPG2 captures in `../Tau Omega/testdata/screenshots/` (`20261004_1947*`-`1949*`) were made before the merge and carry the two entries as tags 25 and 26. They are good fixtures for the container (TPG2), not for the entry tags. Those files and the README change are **uncommitted in the Omega repo**.

## Next (ROADMAP row 13)
1. Package a build of `main` (after merge) with `TPG=1` (`tools/package_dev_build.py --barcode 5 --build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1,TPG=1 --rbf <b576 seed 2 RBF> --rbf-sha256 a44c9f99...3da4`, install with `tools/install_dev_core.py --carry-from alfatreze.TAU`), run Info > A and a Check, press X through the views, and decode with `--grid`; replace the pre-merge captures in Omega's testdata.
2. Owner decisions: default `TAU_TPG` on in the Diagnostic Build (D-R04); `TAU_QR_MAXV=14` once Omega decodes grids.
3. Tau Omega grid decoder (support TPG1 and TPG2, test against the real captures).
4. Reed-Solomon for robust mode if the shareable path matters (a heavy bilinear 50% downscale corrupts about 14% of cells; detected by CRC, not repaired).
5. Cheap context items (firmware only): settings snapshot, uptime, hardware feature bits, decoded-file facts, last 8 error codes. Needs RTL: wall-clock time (APF command 0x0090). A device id was assessed and not recommended (privacy).
6. Heavier records: Check FULL and sweeps over 1 KB were only run on the host; Info rows longer than 28 characters are cut in the export.

## Traps found
- `tools/install_dev_core.py` needs the card mounted and prints a dry run first; the `Pock` volume name is the card.
- `fw/build.sh` is not executable: run it with `bash`.
- A git worktree needs the untracked `toolchain` symlink (`ln -s ../tau-alpha/toolchain toolchain`) for the firmware host tests; it is not committed.
- Platform ids are limited to 15 characters (`tau_devbarcode01` was refused).
- Record tags: read `main`'s enum in `fw/suite_core.h` before adding one on a branch (D-R05).
- Keep `README.md`'s version string and `fw/build.sh`'s regex in step (unrelated here, but it bit alpha.1).
