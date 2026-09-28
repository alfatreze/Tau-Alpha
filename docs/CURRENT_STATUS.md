# Current status (one page)

Updated 2026-09-28. **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list). Why things are the way
they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is `docs/archive/CURRENT_STATUS_history_2026-09-26.md`.
Full detail on the 2026-09-27/28 night: `docs/handoffs/SESSION_HANDOFF_2026-09-28_0.6.0_COMBINED.md` (read it before trusting any
claim about a combined six-feature hardware fit, an `alfatreze.TAU_0_6_0_A_11` install, or a seed-corruption story — none of those
three are documented anywhere in this repo as of this update, see that doc's items 4/5/13).

## 0.6.0 in progress (since v0.5.0)
Five RTL features each individually fit-proven (blend, 192 KB RAM shrink, clk66, Helios H2 double buffering, persist widen 16->32).
RAM shrink + clk66 combined into one CORE_VERSION rev 26 interlock (`2c09524`). A committed `all6-combined` qsf bundle exists
(`tools/blit_g3_poly_blend_ram192_clk66_dbuf_qsf_append.txt`) but **its fit result is not recorded anywhere in this repo** — confirm
on the VM before trusting it. Firmware landed on `main`: rounded-rect corner-cut LUT fix (`8d529d1`, hardware never loaded the
table so `OP_RRECT` always drew square corners — not yet hardware-confirmed after the fix), MASTER VU meter (`8d529d1`, not yet
hardware-run), Chladni EMBER/OCEAN presets (`8fb74f3`), legacy `.m3u` playlist removed entirely (library-only now, `45630c3`). A
build-breaking regression (VIZ_VU_MASTER overflowing the hardware-blit thumbnail-stash budget, breaking every firmware target
including plain `release`) was found and fixed (`a5fc0d1`) — caught only by a real rebuild, not `make test-host` alone.
`docs/AUDIT_TRAIL.md`'s numbered series currently ends at B-346 (persist widening, packaged as `alfatreze.TAU_0_6_0_A_6`, not
installed). An "audio-first track load" spec+tooling exists in a separate, unmerged worktree branch (`ae8c284`) — check whether it
has landed before assuming either way.

## Released
- **v0.5.0** (2026-09-27, tagged, GitHub release published with both zips): `TAU` and `TAU_DIAGNOSTIC`. Themes (TAU/OCEAN, Dark/Light), TIM1 fast covers, MP3
  window unit in hardware (`POLY_FW=1` is the release default), Winamp/Chladni meters, full-screen menus with an action bar. Bitstream `gamma-b316` seed 1.
  Release heap gap 49,712 B. Changelog: `CHANGELOG.md`. Build/audit: B-331, B-332.
- Deferred to **0.6** (owner, B-331): theme and meter settings saved across restarts (needs persist widening), alpha blend in firmware, the `Track changes` fix.
- v0.4.0 (2026-09-22) is the previous release.

## On the Pocket card
`TAU` and `TAU_DIAGNOSTIC` at v0.5.0 (media and library index re-synced in B-332 after a stale Sep-22 index showed duplicates) and `TAU_0_5_0_A_35`
(pre-release with the same features). The v0.5.0 release ROM had its smoke test in B-332; the re-synced media has not been re-tested.

## Hardware-confirmed
- MP3 window unit (alpha.30): 404,712 slots, 0 BAD, 1.75x plays cleanly (1.25x used to stutter); filterbank share of decode 22% at 1.0x (was 55-59%).
- TIM1 covers load in about 90 ms on MP3 and FLAC albums (B-329, B-330). E4 means an album has no `tau-art` file and falls back to the JPEG.
- Blit engine end to end, PSRAM cold code, library, SDRAM window, VBLANK about 60/S: unchanged and passing.

## Known open evidence and defects
- **CPU LOAD reads 100%** in every state, so it cannot show headroom. Use the per-stage decode percentages and the speed at which audio breaks up.
- **`Track changes` Check fails** (0 of 10 done): pre-existing, unexplained; 0.6.
- **Hardware wave/scope path is compiled out** (`if (0 && wave_hw)`, B-302): drawing 256 columns cost about 21x a normal meter. The software scope runs instead.
- Boot-restore mismatch between release and diagnostic builds (`docs/issues/021`), re-parked until the UI redesign.
- M10K: the shipped bitstream uses 304 of 308 blocks. The 192 KB RAM shrink (RTL fit-proven, B-235) would release 64 blocks; firmware does not fit yet
  (`docs/RAM_SHRINK_192K_PLAN.md`; uncommitted B-333 work adds a CORE_VERSION interlock).
- Alpha blend closes timing when pipelined (B-327, seed 1 all corners positive, RAM 304/308, DSP 17); not packaged, no firmware use yet (0.6).
- Speeds above 1.20x are Diagnostic Build only.

## Uncommitted or not mine
Not committed: `dist/` ROM and cold image. Belongs to the other sessions and left alone: `tools/sync_media.py`, `tools/tau_image.py`, `tools/lab/`,
`sim/test_tau_image.py`, `docs/DECISIONS.md`, `docs/IMAGE_FORMATS.md`, `docs/CHLADNI_METER_SPEC.md`, `docs/METER_MODULE_SPEC.md`, `docs/THEME_SPEC.md`, `docs/vendor/`.
`main` and tag `v0.5.0` were pushed at `63ea499`. Another session has uncommitted work in `README.md` (a rewrite), `fw/`, `mp3_soc.v` and `dist/`; leave it.

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
