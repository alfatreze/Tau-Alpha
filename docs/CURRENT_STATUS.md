# Current status (one page)

Updated 2026-09-26. **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list). Why things are the way
they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is `docs/archive/CURRENT_STATUS_history_2026-09-26.md`.

## Released
- **v0.4.0** (2026-09-22): `TAU` and `TAU_DIAGNOSTIC`, media library, cold code, PSRAM. Both zips built. Not touched since.
- `dist/` holds an **uncommitted newer release build** (`release` target, ROM `f19b6202...`); it is not a version bump and has not been on a Pocket as a release.

## On the Pocket card
`TAU`, `TAU_DIAGNOSTIC` (v0.4.0) and **`TAU_0_5_0_A_30`**: bitstream `poly-b298` seed 1 (MP3 window unit, hardware wave/scope block, spectrum bank,
frame counter, beam position, blit engine incl. rounded rect) and diagnostic firmware with `POLY_FW=1`.

## Hardware-confirmed (alpha.30, 2026-09-26)
- MP3 window unit: Info > MP3 WINDOW `HW 404712 SLOTS 0 BAD 0 TMO`; USER CHECK at 1.75x and STANDARD at 1.0x all clean, 0 late underruns, audio continuous.
- 1.75x plays cleanly (1.25x used to stutter). Filterbank share of decode 22% at 1.0x (was 55-59%).
- Blit engine end to end (B1/B2/B4/B5/B6/B8, Blit Test 24 windows pass), PSRAM cold code, library, SDRAM window, VBLANK ~60/S: all unchanged and passing.

## Known open evidence and defects
- **CPU LOAD reads 100%** in every state, so it cannot show headroom. Use the per-stage decode percentages and the speed at which audio breaks up.
- No true unit-off baseline on alpha.30 (alpha.29 is in `work/card-backups/20260926-170019`). A HarpMudd comparison is wanted later (same track, speed, meter).
- **`Track changes` Check fails** (0 of 10 done): pre-existing, unexplained.
- **Hardware wave/scope path is compiled out** (`if (0 && wave_hw)`, B-302): drawing 256 columns cost about 21x a normal meter and caused audio jitter. The software scope runs instead. Needs batched drawing.
- Boot-restore mismatch between release and diagnostic builds (`docs/issues/021`), re-parked until the UI redesign.
- M10K: the poly bitstream uses 304 of 308 blocks. The 192 KB RAM shrink (RTL fit-proven, B-235) would release 64 blocks, but the firmware does not fit yet (`docs/RAM_SHRINK_192K_PLAN.md`).

## Uncommitted or not mine
Not committed: `dist/` ROM and cold image. Belongs to the other sessions and left alone: `tools/sync_media.py`, `tools/tau_image.py`, `tools/lab/`,
`sim/test_tau_image.py`, `docs/DECISIONS.md`, `docs/IMAGE_FORMATS.md`, `docs/CHLADNI_METER_SPEC.md`, `docs/METER_MODULE_SPEC.md`, `docs/THEME_SPEC.md`, `docs/vendor/`.
Local `main` was pushed at `43d970f`.

## Sibling project
Tau Omega (`../Tau Omega/`, MIT OR Apache-2.0, Rust + Tauri) manages the card: sync, packages, diagnostics decode, screenshots. It keeps its own order in
its `docs/STATUS_HANDOFF.md`. The shared surface is `docs/CROSS_PROJECT_INTERFACE.md`; never share literal files.

## Where things are
| Need | File |
|---|---|
| Ordered plan | `docs/ROADMAP.md` |
| Latest handoff (sessions, traps, tools) | `docs/SESSION_HANDOFF_2026-09-26_ALPHA29_MPOLY.md` (section 8 is newer than section 3) |
| Decisions | `docs/DECISIONS.md` (meters and images so far; older ones are listed in `docs/ROADMAP.md` section 4) |
| Design references | `PHASE_F_SPEC`, `HELIOS_SPEC`, `METER_MODULE_SPEC`, `THEME_SPEC`, `MEDIA_LIBRARY_0.4_SPEC`, `PHASE_G_SPEC`, `TEST_SUITE_SPEC`, `MMIO_ALLOCATION`, `IMAGE_FORMATS` |
| Card install | `docs/CARD_INSTALL_PROCEDURE.md`, `tools/install_dev_core.py` |
