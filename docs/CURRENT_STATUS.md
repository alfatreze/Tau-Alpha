# Current status (one page)

Updated 2026-09-29. **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list). Why things are the way
they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is `docs/archive/CURRENT_STATUS_history_2026-09-26.md`.
Full detail on the 2026-09-29 session: `docs/handoffs/SESSION_HANDOFF_2026-09-29_LPC_HW_AND_HELIOS.md` — **read it first**, it
supersedes everything below about FLAC LPC and Helios.

## Headline: FLAC LPC is done and hardware-confirmed; Talos's ALM waste is the clear next step

The FLAC LPC hardware kernel is built, fixed (a real memory-inference bug, two attempts — the first a documented negative
result — before the real fix), fit with real margin (18,064/18,480 ALMs, 98%), installed, and hardware-confirmed via a causal
A/B test: a reported microstutter was present with the unit disabled and gone with it enabled, then quantified (worst-case
latency 11ms software vs 5-6ms hardware). Full story and evidence: the 2026-09-29 handoff, `docs/AUDIT_TRAIL.md` B-376..B-386,
`analogue-pocket-dev` skill KB-069 (local, hardware-validated).

**A concurrent session found the single largest ALM saving available on the whole chip, not yet built**: with
`TAU_BLIT_BLEND` on (present in the current bitstream, not yet used by any firmware), Talos's `glyphbuf` row buffer gets a
second read and write site the RTL never intended, so Quartus builds it from ~6,500 ALMs of registers instead of ~80 ALMs of
MLAB. Fix is already designed, low-risk, no new feature work: `docs/research/TALOS_REVIEW_2026-09-28.md` section 1a /
`docs/features/TALOS2_REIMPLEMENTATION_PLAN.md` (explicitly sequenced to start after the FLAC LPC work, which just landed).
**This is the recommended next step.**

## 0.6.0 in progress (since v0.5.0)
FLAC LPC hardware kernel: done (above). Helios review items 1-2 done (meter draw contract for the 4 live meters, incl. fixing
MASTER VU dead code since `8d529d1`; Settings' 3 dispatch chains collapsed to one function-pointer table), item 3 parked
(`D-H01`, no natural second `helios_excl[]` caller found). Five other RTL features each individually fit-proven earlier
(blend, 192 KB RAM shrink, clk66, Helios H2 double buffering, persist widen 16->32), combined into the `all6-combined`
bitstream, **fit confirmed successful** (setup +5.695ns/hold +0.037ns, resolved 2026-09-28) and now further combined with
FLAC LPC (98% ALM, see above). Firmware landed on `main`: rounded-rect corner-cut LUT fix (`8d529d1`), MASTER VU meter
(now actually wired and hardware-confirmed), Chladni EMBER/OCEAN presets, legacy `.m3u` playlist removed entirely.
`docs/AUDIT_TRAIL.md`'s numbered series currently ends at B-387.

## Released
- **v0.5.0** (2026-09-27, tagged, GitHub release published with both zips): `TAU` and `TAU_DIAGNOSTIC`. Themes (TAU/OCEAN, Dark/Light), TIM1 fast covers, MP3
  window unit in hardware (`POLY_FW=1` is the release default), Winamp/Chladni meters, full-screen menus with an action bar. Bitstream `gamma-b316` seed 1.
  Release heap gap 49,712 B. Changelog: `CHANGELOG.md`. Build/audit: B-331, B-332.
- Deferred to **0.6** (owner, B-331): theme and meter settings saved across restarts (needs persist widening), alpha blend in firmware, the `Track changes` fix.
- v0.4.0 (2026-09-22) is the previous release.

## On the Pocket card
`alfatreze.TAU`, `alfatreze.TAU_DIAGNOSTIC` (release, v0.5.0, unchanged), `alfatreze.TAU_0_6_0_A_16` (all6-combined + FLAC LPC
hardware, `LPC_FW=1`, hardware-confirmed), `alfatreze.TAU_DEV_52` (same bitstream, `LPC_FW=0`, the A/B control used to prove
LPC's effect — keep for now, or free the slot once the Talos fix needs a fresh install).

## Hardware-confirmed
- FLAC LPC hardware kernel: 0 timeouts across multiple Checks + stress, microstutter A/B-confirmed fixed, worst-case latency
  11ms (software) vs 5-6ms (hardware). Not yet sample-exact verified against software (no hw-vs-sw Check comparison built).
- MP3 window unit (alpha.30): 404,712 slots, 0 BAD, 1.75x plays cleanly (1.25x used to stutter); filterbank share of decode 22% at 1.0x (was 55-59%).
- TIM1 covers load in about 90 ms on MP3 and FLAC albums. E4 means an album has no `tau-art` file and falls back to the JPEG.
- Blit engine end to end, PSRAM cold code, library, SDRAM window, VBLANK about 60/S: unchanged and passing.

## Known open evidence and defects
- **ALM budget is now the tight resource, not M10K**: 98% on the current bitstream, 416 ALMs (2%) free. The Talos `glyphbuf`
  fix above would recover ~6,500 ALMs if built; until then, any new RTL feature is genuinely at risk of not fitting.
- **CPU LOAD reads 100%** in every state, so it cannot show headroom. Use the per-stage decode percentages and the speed at which audio breaks up.
- **`Track changes` Check fails** (0 of 10 done): pre-existing, unexplained; 0.6.
- **Hardware wave/scope path is compiled out** (`if (0 && wave_hw)`, B-302): drawing 256 columns cost about 21x a normal meter. The software scope runs instead.
- **Two live Talos correctness bugs, not yet fixed** (found in the concurrent-session review): `OP_BAR`'s 7-bit lit-row count wraps above 127 rows
  (not yet seen on hardware, nothing currently draws that tall); `fb_wait()` used as "engine finished" at 3 call sites when it only means "FIFO not full" (real races).
- Boot-restore mismatch between release and diagnostic builds (`docs/issues/021`), re-parked until the UI redesign.
- Settings page-dispatch collapse (B-387) not yet hardware-tested or installed.

## Uncommitted or not mine
`docs/vendor/` (confidential vendor datasheet, deliberately left out of every commit, by design). Check `git status` before assuming anything
else is stale — this project has multiple concurrent sessions; the Talos review docs above were one such concurrent landing, now merged into
this status.

## Sibling project
Tau Omega (`../Tau Omega/`, MIT OR Apache-2.0, Rust + Tauri) manages the card: sync, packages, diagnostics decode, screenshots. It keeps its own order in
its `docs/STATUS_HANDOFF.md`. The shared surface is `docs/CROSS_PROJECT_INTERFACE.md`; never share literal files.

## Where things are
| Need | File |
|---|---|
| Ordered plan | `docs/ROADMAP.md` |
| Latest handoff (sessions, traps, tools) | `docs/handoffs/SESSION_HANDOFF_2026-09-29_LPC_HW_AND_HELIOS.md` |
| Decisions | `docs/DECISIONS.md` |
| Design references | `PHASE_F_SPEC`, `HELIOS_SPEC`, `HELIOS_ARCHITECTURE_REVIEW_2026-09-28`, `TALOS_REVIEW_2026-09-28`, `TALOS2_REIMPLEMENTATION_PLAN`, `METER_MODULE_SPEC`, `THEME_SPEC`, `MEDIA_LIBRARY_0.4_SPEC`, `PHASE_G_SPEC`, `TEST_SUITE_SPEC`, `MMIO_ALLOCATION`, `IMAGE_FORMATS`, `FLAC_LPC_KERNEL_DESIGN` |
| Card install | `docs/CARD_INSTALL_PROCEDURE.md`, `tools/install_dev_core.py` |
| Skill knowledge | `analogue-pocket-dev` skill, KB-069 (local): the runtime-indexed-read memory-inference lesson |
