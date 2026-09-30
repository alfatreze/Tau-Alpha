# Current status (one page)

Updated 2026-09-30 (later session). **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list). Why things
are the way they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is `docs/archive/CURRENT_STATUS_history_2026-09-26.md`.
Full detail on this session: `docs/handoffs/SESSION_HANDOFF_2026-09-30_METER_PREVIEW_PERSIST_AND_MCLK.md` — **read it first.**

## Headline: three investigations closed hardware-confirmed, two RTL fits queued on the VM

**Scope trail, Configure-page meter preview, and settings persistence — all closed, hardware-confirmed.**
Winamp Scope trail accumulation (B-450), Bars/Scope/Chladni/VU Master frozen in the Configure preview
(B-452/B-453), and theme/mode never surviving a restart (B-455/B-456) were all real, independently-caused
bugs, all found and fixed this session, all confirmed working on real hardware. The persistence bug in
particular was not a firmware bug at all — `interact.json` hard-caps at 16 shown UI entries (Analogue's
own documented limit), and a first fix attempt pushed it to 17, silently breaking both display and
persistence for the overflow entries. New skill knowledge: `analogue-pocket-dev` KB-079 (the 16-entry
cap) and KB-080 (an unrelated `data.json` bit that was cluttering Core Settings with unwanted reload
actions on all 7 data slots, not just the 2 the owner had noticed).

**Winamp Bars fullscreen clamp: real RTL fix built and simulation-verified, Quartus fit in progress.**
`OP_BAR`'s lit-row count widened 7→9 bits (a new field, no width change to anything CHAR/RRECT use).
No hardware-capability probe — owner's call, since alpha-stage firmware/bitstream are always paired
together. Fit `bar-hi-b454` was still running on the VM at end of session. Full detail: handoff section 3.

**Cymo 44.1 kHz: a real, well-reasoned MCLK jitter fix built, queued behind the same fit.** The FIFO
hand-off is twice-proven clean; found the actual audio master clock (MCLK) was synthesised via a phase
accumulator with a non-integer division ratio — real jitter, invisible to RTL simulation. Replaced with
a proper PLL fractional-N output (reusing the existing shared clock generator). Verified functionally
equivalent in simulation (no regression). **Not yet a proven fix** — needs the real fit plus a hardware
A/B recording to confirm the SINAD gap narrows. Full detail: handoff section 4.

## 0.6.0 in progress (since v0.5.0)
FLAC LPC hardware kernel: done, hardware-confirmed. T2-00 (`glyphbuf` single-write-port ALM fix): done,
fit-confirmed, hardware-confirmed. Helios items 1-2, 4, 5, 7 done. Settings crossfade, Chladni H2 buffer
tracking, theme/mode persistence, Configure-page meter preview: all built and hardware-confirmed working.
`docs/AUDIT_TRAIL.md`'s B-series currently ends at B-457.

## Released
- **v0.5.0** (2026-09-27, tagged, GitHub release published with both zips): `TAU` and `TAU_DIAGNOSTIC`. Themes (TAU/OCEAN, Dark/Light), TIM1 fast covers, MP3
  window unit in hardware (`POLY_FW=1` is the release default), Winamp/Chladni meters, full-screen menus with an action bar. Release heap gap 49,712 B.
  Changelog: `CHANGELOG.md`. Build/audit: B-331, B-332.
- v0.4.0 (2026-09-22) is the previous release.

## On the Pocket card
`alfatreze.TAU`, `alfatreze.TAU_DIAGNOSTIC` (release, v0.5.0, unchanged), `alfatreze.TAU_DEV_54`/
`alfatreze.TAU_DEV_56` (earlier item-7 iterations on the old pre-T2-00 bitstream, free to remove),
`alfatreze.TAU_0_6_0_A_43` — `glyphbuf-t200` bitstream (RBF `b089b82871d7f441e2d68665f18a9a130691598726cb9cd7a828fd1ee2195a7e`),
carrying B-456's persistence fix (theme/mode + the data.json reload-slot cleanup) on top of every earlier
fix this session (B-450/452/453). **Hardware-confirmed working.** No alpha carrying B-454 (OP_BAR RTL
widen) or B-457 (Cymo MCLK) has been packaged/installed yet — both wait on the `bar-hi-b454` fit.

## Hardware-confirmed
- T2-00 (`glyphbuf` ALM fix), the full `all6-combined` + FLAC LPC bundle, Settings crossfade, Chladni H2
  tracking, theme/mode persistence, Configure-page meter preview (all four meters): all confirmed on real silicon.
- FLAC LPC hardware kernel: 0 timeouts across multiple Checks + stress, microstutter A/B-confirmed fixed.
- MP3 window unit: 404,712 slots, 0 BAD; filterbank share of decode 22% at 1.0x (was 55-59%).
- Cymo: real Altera `dcfifo` simulation matches the ideal-hold prediction (27.71 dB SINAD) exactly (the FIFO hand-off, not the MCLK fix).
- TIM1 covers load in about 90 ms on MP3 and FLAC albums.

## Known open evidence and defects
- **Winamp Bars fullscreen past 127 rows**: real RTL fix built, fit in progress — not yet on hardware.
- **Cymo 44.1 kHz SINAD**: RTL fix built (MCLK jitter), not yet fit or hardware-tested — not a proven fix yet.
- **CPU LOAD reads 100%** in every state, so it cannot show headroom. Use the per-stage decode percentages instead.
- **`Track changes` Check fails** (0 of 10 done): pre-existing, unexplained.
- **Hardware wave/scope path is compiled out** (`if (0 && wave_hw)`, B-302): drawing 256 columns cost about 21x a normal meter. The software scope runs instead.
- Boot-restore mismatch between release and diagnostic builds (`docs/issues/021`), re-parked until the UI redesign.
- Meter-preset persistence (Bars/Scope/Chladni's chosen preset) has no persistence path — deliberately dropped from `interact.json` to stay under the 16-entry cap (B-456), never reported as an issue.

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
| Latest handoff (sessions, traps, tools) | `docs/handoffs/SESSION_HANDOFF_2026-09-30_METER_PREVIEW_PERSIST_AND_MCLK.md` |
| Decisions | `docs/DECISIONS.md` |
| Design references | `PHASE_F_SPEC`, `HELIOS_SPEC`, `HELIOS_ARCHITECTURE_REVIEW_2026-09-28`, `TALOS_REVIEW_2026-09-28`, `TALOS2_REIMPLEMENTATION_PLAN`, `METER_MODULE_SPEC`, `THEME_SPEC`, `MEDIA_LIBRARY_0.4_SPEC`, `PHASE_G_SPEC`, `TEST_SUITE_SPEC`, `MMIO_ALLOCATION`, `IMAGE_FORMATS`, `FLAC_LPC_KERNEL_DESIGN`, `CYMO_AUDIO_ENGINE`, `CYMO_AUDIO_ENGINE_REVIEW` |
| Card install | `docs/CARD_INSTALL_PROCEDURE.md`, `tools/install_dev_core.py` |
| Packaging with a specific firmware-flag combo | `tools/package_dev_build.py --build-flags` (B-448 — always use this, not a manual pre-build, for `RAM_192K`/`CLK66`/`SDRAM_BUSY`/`LPC_FW`) |
| VM fit status | `python3 tools/vm_fit.py status bar-hi-b454` (running at end of session) |
| Skill knowledge | `analogue-pocket-dev` skill, KB-069 (memory-inference), KB-077 (build-clobber), KB-078 (JTAG polling limits), KB-079 (interact.json 16-entry cap), KB-080 (data.json reload-bit clutter) |
