# Current status (one page)

Updated 2026-09-30. **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list). Why things are the way
they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is `docs/archive/CURRENT_STATUS_history_2026-09-26.md`.
Full detail on the 2026-09-30 session: `docs/handoffs/SESSION_HANDOFF_2026-09-30_SCOPE_BLEND_AND_CYMO.md` — **read it first.**

## Headline: two open investigations, one real build-tooling bug found and fixed

**Winamp Scope "trail accumulation" bug: still open.** Every architectural/register-level check
(strip content, sticky bases, H2 buffer selection, firmware AND hardware alpha value) reads correct
across six diagnostic builds (`A_29` through `A_38`). Live JTAG polling has reached a genuine, proven
dead end for this question (a real arithmetic property makes low-contrast pixel pairs
blend-indistinguishable for any alpha, and real decay converges too fast for opportunistic polling to
catch — `analogue-pocket-dev` skill KB-078). Built a firmware-only frame-by-frame decay logger
(PIXHIST, on the Info page) instead; it has been relocated twice chasing where the accumulated mass
actually sits on screen and has not yet been read during a confirmed-active repro at its current
location. **Next step and full context:** the 2026-09-30 handoff, section 1.

**Cymo 44.1 kHz audio investigation: RTL cleared, real hardware evidence needed next.** The decisive
simulation (real Altera `dcfifo` model, not the behavioural stand-in) reproduces the ideal-hold
prediction almost exactly — this rules out the RTL/FIFO hand-off as the cause of the real hardware
recordings' ~17 dB worse SINAD. Next: the serializer (`sound_i2s.v`) or the recording/analog capture
path itself, neither examined yet. Full context: the 2026-09-30 handoff, section 2.

**A real, previously-undiscovered build-clobber bug shipped a broken alpha (`A_36`), found and fixed.**
`tools/check_heap_gap.py` rebuilding a flagged target with no flags of its own, landing at the same
output path as the real build, silently swapped in the wrong firmware variant — same reported sizes,
genuinely different bytes, a total black-screen boot with zero diagnostic signal. Fixed at the tool
level: `tools/package_dev_build.py --build-flags` now builds the firmware itself as the literal last
step before packaging. **Use it for every future alpha build with `RAM_192K`/`CLK66`/`SDRAM_BUSY`/
`LPC_FW`** — see the 2026-09-30 handoff, section 3, for the exact command.

## 0.6.0 in progress (since v0.5.0)
FLAC LPC hardware kernel: done, hardware-confirmed. T2-00 (`glyphbuf` single-write-port ALM fix): done,
fit-confirmed with real margin, hardware-confirmed — this is the `glyphbuf-t200` bitstream every current
alpha build uses. Helios items 1-2, 4, 5, 7 done. Settings hardware alpha-blend crossfade, Chladni H2
buffer tracking, theme/mode persistence: built and hardware-confirmed working (session of 2026-09-29,
`docs/AUDIT_TRAIL.md` B-405 through B-416). `cymo` branch (audio-engine research/tooling) merged into
`main` 2026-09-30. `docs/AUDIT_TRAIL.md`'s B-series currently ends at B-449.

## Released
- **v0.5.0** (2026-09-27, tagged, GitHub release published with both zips): `TAU` and `TAU_DIAGNOSTIC`. Themes (TAU/OCEAN, Dark/Light), TIM1 fast covers, MP3
  window unit in hardware (`POLY_FW=1` is the release default), Winamp/Chladni meters, full-screen menus with an action bar. Release heap gap 49,712 B.
  Changelog: `CHANGELOG.md`. Build/audit: B-331, B-332.
- v0.4.0 (2026-09-22) is the previous release.

## On the Pocket card
`alfatreze.TAU`, `alfatreze.TAU_DIAGNOSTIC` (release, v0.5.0, unchanged), `alfatreze.TAU_DEV_54`/
`alfatreze.TAU_DEV_56` (earlier item-7 iterations on the old pre-T2-00 bitstream, free to remove),
`alfatreze.TAU_0_6_0_A_38` — `glyphbuf-t200` bitstream (RBF `b089b82871d7f441e2d68665f18a9a130691598726cb9cd7a828fd1ee2195a7e`),
carrying every Scope-blend diagnostic built this session (STRIP/BASES/DBUF/ALPHA/PIXHIST Info rows) plus
the B-445 crash fix and B-446 auto-repeat fix. **Confirmed booting correctly on real hardware.**

## Hardware-confirmed
- T2-00 (`glyphbuf` ALM fix), the full `all6-combined` + FLAC LPC bundle, Settings crossfade, Chladni H2
  tracking, theme/mode persistence: all confirmed on real silicon.
- FLAC LPC hardware kernel: 0 timeouts across multiple Checks + stress, microstutter A/B-confirmed fixed.
- MP3 window unit: 404,712 slots, 0 BAD; filterbank share of decode 22% at 1.0x (was 55-59%).
- Cymo: real Altera `dcfifo` simulation matches the ideal-hold prediction (27.71 dB SINAD) exactly.
- TIM1 covers load in about 90 ms on MP3 and FLAC albums.

## Known open evidence and defects
- **Winamp Scope trail accumulation**: open, see headline above and the 2026-09-30 handoff.
- **Cymo 44.1 kHz SINAD**: open, RTL cleared, serializer/analog-path not yet examined.
- **CPU LOAD reads 100%** in every state, so it cannot show headroom. Use the per-stage decode percentages instead.
- **`Track changes` Check fails** (0 of 10 done): pre-existing, unexplained.
- **Hardware wave/scope path is compiled out** (`if (0 && wave_hw)`, B-302): drawing 256 columns cost about 21x a normal meter. The software scope runs instead.
- Boot-restore mismatch between release and diagnostic builds (`docs/issues/021`), re-parked until the UI redesign.

## Uncommitted or not mine
`docs/vendor/` (confidential vendor datasheet, deliberately left out of every commit, by design). Check `git status` before assuming anything
else is stale — this project has multiple concurrent sessions.

## Sibling project
Tau Omega (`../Tau Omega/`, MIT OR Apache-2.0, Rust + Tauri) manages the card: sync, packages, diagnostics decode, screenshots. It keeps its own order in
its `docs/STATUS_HANDOFF.md`. The shared surface is `docs/CROSS_PROJECT_INTERFACE.md`; never share literal files.

## Where things are
| Need | File |
|---|---|
| Ordered plan | `docs/ROADMAP.md` |
| Latest handoff (sessions, traps, tools) | `docs/handoffs/SESSION_HANDOFF_2026-09-30_SCOPE_BLEND_AND_CYMO.md` |
| Decisions | `docs/DECISIONS.md` |
| Design references | `PHASE_F_SPEC`, `HELIOS_SPEC`, `HELIOS_ARCHITECTURE_REVIEW_2026-09-28`, `TALOS_REVIEW_2026-09-28`, `TALOS2_REIMPLEMENTATION_PLAN`, `METER_MODULE_SPEC`, `THEME_SPEC`, `MEDIA_LIBRARY_0.4_SPEC`, `PHASE_G_SPEC`, `TEST_SUITE_SPEC`, `MMIO_ALLOCATION`, `IMAGE_FORMATS`, `FLAC_LPC_KERNEL_DESIGN`, `CYMO_AUDIO_ENGINE`, `CYMO_AUDIO_ENGINE_REVIEW` |
| Card install | `docs/CARD_INSTALL_PROCEDURE.md`, `tools/install_dev_core.py` |
| Packaging with a specific firmware-flag combo | `tools/package_dev_build.py --build-flags` (B-448 — always use this, not a manual pre-build, for `RAM_192K`/`CLK66`/`SDRAM_BUSY`/`LPC_FW`) |
| Skill knowledge | `analogue-pocket-dev` skill, KB-069 (local, memory-inference), KB-077 (local, build-clobber), KB-078 (local, JTAG polling limits) |
