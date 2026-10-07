# Current status (one page)

Updated 2026-10-04 (evening). **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list, not yet updated with
the Cymo row — see below). Why things are the way they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is
`docs/archive/CURRENT_STATUS_history_2026-09-26.md`.
Full detail on this session: `docs/handoffs/SESSION_HANDOFF_2026-10-03_CYMO_TEMPO.md` — **read it first** (the 2026-10-01 resampler handoff is still correct for what it covers).

## Update 2026-10-07 (later): TAU_DEV_105 installed, hardware gain stage, headphone plan and measurement gates
`gain-b615` closed (all corners positive, DSP 21/66) and is installed as `TAU_DEV_105` (not yet run). The recording plan is `docs/features/CYMO_RECORDING_PLAN_DEV105.md`; the headphone analysis is `docs/features/CYMO_HEADPHONE_PLAN.md`; decision D-G01 gates the dependent developments (list in the recording plan, section 5). Existing recordings show an analogue floor of -76.4 dBFS and idle spurs; audit B-615.

## Update 2026-10-07: Cymo output chain, Halcyon EQ design, DSP review; TAU_DEV_104 installed, not yet run
Entry point: `docs/handoffs/SESSION_HANDOFF_2026-10-07_CYMO_HALCYON.md`; audit B-601..B-613. **On the card:** `TAU`, `TAU_DIAGNOSTIC` (alpha.3), `TAU_DEV_104` (16-bit I2S switch bitstream + main firmware; NOT yet run), the owner's barcode/meter cores. **Confirmed on hardware:** ReplayGain (it was never selectable until the Audio menu row count was fixed, B-605). **Built, default off:** 24-bit EQ coefficients (`TAU_EQ_COEF24`), runtime 16-bit I2S slot (in DEV 104). **Designed:** hardware gain stage, soft clipper + dither quantiser, **Halcyon** (the new EQ, named by the owner: six biquad stages, writable coefficients, presets as data, editable in Tau Omega). **Reviewed:** four open-source DSP projects (provenance rule D-P01, `docs/PROVENANCE.md`). **Next:** run DEV 104 + loopback A/B (15 vs 16 bit), synthesis-only count for 24-bit coefficients, Halcyon generator work (analog-matched tables, clamps, raw biquad presets), then gain stage and Halcyon RTL. Tempo at 1.75x is at the CPU limit (not a bug); Figma diagram `227:650` rebuilt.

## Update 2026-10-04 (evening): alpha.3 published, CPU headroom, dB volume, ReplayGain
Entry point: `docs/handoffs/SESSION_HANDOFF_2026-10-04_ALPHA3_CPU_VOLUME_RG.md`; audit B-576..B-599. **Published:** `v0.6.0-alpha.3` (CLUT RTL fix, pipelined Subband handoff and burst push: MP3 stereo CPU busy 55% to 44%, stress-pump noise fix, pixel-grid reports, load records, pairing gate); alpha.1 marked superseded. **On the card:** `TAU` and `TAU_DIAGNOSTIC` (alpha.3, smoke-tested), `TAU_DEV_93` (dB volume + ReplayGain + tempo build, NOT yet run on a Pocket), `TAU_DEV_BARCODE_04` and `TAU_DEV_METER_*` (the owner's, untouched). **Built, host-tested, not yet heard:** dB-tapered volume with a 5 ms ramp (default 94, step 3; saved volumes now sound quieter) and ReplayGain Off/Track/Album (Settings > Audio, Info row, attenuate-only, cold code). **Next:** test DEV 93, then alpha.4; Cymo gapless, output stage, EQ; `Track changes`. Check tags 24-27 are used; next free is 28.

## Update 2026-10-04 (barcode session): report codes as a pixel grid, merged from `barcode-study`
Entry point: `docs/handoffs/SESSION_HANDOFF_2026-10-04_BARCODE_TPG.md`; study `docs/features/BARCODE_STUDY.md`; audit B-594. The Check/Sweep/Info reports now have a **TPG pixel-grid view** (robust grid by default, lossless grid when the report is big, QR as the third view, X cycles) on all seven report pages, with one shared layout and caption set, and the Info page exports every row (`decode_tau_suite.py --grid shot.png --table`). Hardware-confirmed on the branch builds `TAU DEV BARCODE 01-04`; off by default (`TAU_TPG=0`, `fw/build.sh TPG=1`); the merged result has not been run on a Pocket. Record tags 26/27 are new (25 is `main`'s `SR_T_LOAD2`).

## Update 2026-10-04 (end of session): FLAC Rice, RAM work, three visual fixes confirmed, CLUT RTL fit running
Entry point: `docs/handoffs/SESSION_HANDOFF_2026-10-04_FLAC_RAM_CLUT.md`. `main` = `21f69ae`, pushed. **Hardware-confirmed this session (`TAU_DEV_85`):** FLAC Rice fast path (hard file idle 0% to 45%, stalls 12 to 0), CLUT start-index fix (covers and previews had a one-slot palette shift), meter previews through the CLUT blit now follow the double-buffer parity, boot splash TAU2 (256 colours). **Merged but NOT hardware-tested:** the JPEG-fallback buffers in PSRAM (+3,968 B heap in every build; owner's order, B-575). **In progress:** the proper CLUT RTL fix (`tau_clut_wr.sv`, firmware probes which behaviour it has) on branch `clut-rtl`, Quartus fit `clut-rtl-b576` running (launched 10:08 VM clock, expected about 11:50): collect, package as DEV 86, test, then merge. **Still owed:** a Pocket test of the JPEG fallback, a rebuilt and smoke-tested release (the alpha.2 zips are stale; the published alpha.1 normal core does not boot). Diagnostics RAM: the heap gap is unused policy margin, diagnostic floors now 2 KiB, `tools/ram_report.py` explains drops; parked: MP3 ring resize (4-7 KB), meter scratch (about 2 KB).

## Update 2026-10-03 (night): DEV 79 hardware result, tempo parked
DEV 79 ran on a Pocket (B-559): tempo sounds good to 1.75x, 2.00x clicks (idle 0%); the stretcher costs about 26-31% of the CPU (about 3x the simulator estimate; cold-code fetch suspected, unmeasured). Audiobook/tempo is nice-to-have and parked per owner; follow-ups listed in `docs/ROADMAP.md` row 12. Next candidate: the FLAC Rice decoder spec (meter-builder worktree).

## Update 2026-10-03 (late): Cymo hand-off confirmed, audiobook tempo built into the player, DEV 79 awaiting its first hardware test
The elastic hand-off fix (B-527) is **hardware-confirmed** (loopback: SINAD 54.5 dB with Cymo ON against 10.8 dB OFF, identical pitch, no added noise; B-529/B-531). Added: hold-to-repeat volume (shared `fw/key_repeat.h`), Cymo default ON in the Diagnostic Build with a 44.1 kHz-at-1.00x guard, one shared audio push path (`cymo_push()`), Info HEADROOM row and `UNDERRUNS n ALL m` (every FIFO stall; the old row counts one per flush), no speed changes for FLAC, speed list unlocked to 2.00x for MP3. **Cymo C7 audiobook tempo:** WSOLA host prototype and listening verdicts (B-535..B-537), fixed-point core + independent twin + bit-exact test (B-549/B-550, 5.4 M instructions per output second measured under rv32sim), streaming core v2 (B-556), funnel and player wiring behind `TEMPO=1` (B-557/B-558; Settings > Playback > TEMPO, MP3 only, stretcher in cold code, staging ring in PSRAM at +0x600000). **Installed as `alfatreze.TAU_DEV_79`, never run on a Pocket: its results are the next input.** Measured headroom (B-545, B-553): mono speech has plenty (to 2.00x), stereo MP3 128k about 1.25x, stereo FLAC music none; the 192 KB link leaves 13.9 KB of heap in the release build (not the 52 KB the default link reports). Open: issue 023 (rare audio spike), cold-code speed of the stretcher, soft reset on resume, headroom guard, pause shortening, persistence. `main` pushed to `d3d1801`.


## Update 2026-10-03: Cymo hand-off root cause found, fix built, fit running -- read `docs/handoffs/SESSION_HANDOFF_2026-10-03_LAYERED_WAVE_AND_CYMO_FEED.md` first
The `STALE 7580` on Info was the count kept from the last live period, and it exposed a structural flaw: the live hand-off from the track-rate tick to the resampler repeated about 11.5% of the input samples and lost as many (model, real-resampler simulation and the hardware counter agree). Fix: `src/fpga/core/tau_cymo_feed.sv`, a 4-entry elastic queue (commit `52dcc8a`, B-527; `make test-rtl` and `make test-host` pass; the old behaviour fails `make test-rtl-cymo-feed-mutation`). **Two-seed fit `cymo-feed-b527` launched on the VM 2026-10-03 00:11 local, expected 01:50-01:55.** Not yet on a Pocket: after the fit, package as TAU DEV 68, install (carry from TAU_DEV_67), read Info > CYMO RESAMP (`S0 D0 Q2-3` expected), then the 1 kHz toggle test. `main` is pushed to origin up to `356823d`; `52dcc8a` and the docs after it are local.

## Update 2026-10-02 (later): Layered Wave flicker closed, one way to draw a meter, merged to main
`lw-flicker` is merged into `main` (fast-forward, B-512..B-526). Hardware-confirmed on `alfatreze.TAU_DEV_67`: no flicker in the player screen, fullscreen or Settings > Meter > Configure, no corruption, no choppiness. What changed: every meter is drawn through one entry (`helios_meter()`); the full-repaint meters (Layered Wave, Chladni) are composed in the idle H2 back buffer and copied on (`helios_present`, skipping exclusion rects such as the fullscreen CPU% label); Layered Wave is throttled (45 ms, yields to a low audio FIFO, time-based stride); CPU LOAD works (the latch was compiled out); Check reports the heap peak (`SR_T_HEAP`); no library now says "Library file not found" and no track is loaded at boot; data slot 3 (legacy Playlist) is gone (library opens use slot 5 as the template); Start opens Settings on release, Start+Y jumps to Meter > Configure; issues 021 and 022 closed. Card: `TAU`, `TAU_DIAGNOSTIC`, `TAU_DEV_67`. Open: some meter settings problems the owner will describe; the asynchronous present; the Helios input layer (`HELIOS_ARCHITECTURE_REVIEW` 8.1); audio distortion under Layered Wave not re-confirmed gone (UNDERRUNS row not captured); the Cymo STALE counter reading 7580 while not live. Not pushed to origin.

## Headline: the Cymo resampler is built, fit, hardware-tested, and mid bug-fix cycle

Built the Cymo 44.1→48 kHz polyphase FIR resampler end to end this session (host model → golden model →
RTL/sim/mutation → synthesis check → real two-seed fit → live-audio wiring → firmware probe/toggle →
card install → **real hardware listening tests**). Two genuine hardware bugs were found by the owner's
own listening and fixed: a borrowed, imprecise tick divider causing an audible "vibrato" (B-484, fixed,
owner confirmed the re-test sounded better with lower noise floor and bigger soundstage), and a missing
reset-on-enable causing a different pitch on every toggle (B-488, fixed, re-fit and installed — **the
hardware re-test of this second fix has not happened yet as of this update**). Two new `analogue-pocket-
dev` skill entries (KB-083, KB-084) record both lessons. Full detail: B-471 through B-491, and the
handoff doc above.

**Separately this session**: a global Helios utility, `helios_wrap_index()`, fixing the Info/Stress-
Status pages' clamped (not wrapping) Up/Down scroll, and deduplicating two pre-existing copies of the
same wraparound logic elsewhere in `fw/settingsui.inc`. Unrelated to Cymo, a mid-session ask.

**Also resolved this session, before Cymo started** (carried over from the prior handoff's open items):
the OP_BAR 9-bit widen (fullscreen Winamp Bars past 127 rows) is **hardware-confirmed working** (B-459).
The Cymo 44.1 kHz SINAD investigation's MCLK-jitter hypothesis was **tested on hardware and ruled out**
(B-467 — a dedicated second PLL fixed a real but separate level anomaly, but did not change the SINAD
gap at all); the I2S clk_audio→clk_mclk CDC was **also tested and ruled out** (B-470). Both of those
negative results are what motivated building the real resampler instead of chasing the hold's own
SINAD gap further — see `docs/features/CYMO_AUDIO_ENGINE.md` section 15 for the full reasoning.

## 0.6.0 in progress (since v0.5.0)
FLAC LPC hardware kernel, T2-00 (`glyphbuf` ALM fix), Helios items 1-2, 4, 5, 7, Settings crossfade,
Chladni H2 buffer tracking, theme/mode persistence, Configure-page meter preview, OP_BAR 9-bit widen:
all built and **hardware-confirmed**. The Cymo resampler (above) is new work this session, not yet
part of any released or default-on configuration — it lives entirely behind a Diagnostic-Build-only,
off-by-default toggle. `docs/AUDIT_TRAIL.md`'s B-series currently ends at B-491.

## Released
- **v0.6.0-alpha.1** (2026-09-30, GitHub pre-release): the first 0.6.0 alpha cut, carrying the RAM
  shrink, clk66, alpha blend, and everything through B-461. `v0.3.0`/`v0.5.0` on GitHub are marked
  pre-release+retitled `(alpha)` per owner's own correction of their original release status.
- v0.5.0 (2026-09-27) is the previous full release tag. Changelog: `CHANGELOG.md`.

## On the Pocket card
`alfatreze.TAU`, `alfatreze.TAU_DIAGNOSTIC` (release, untouched throughout this session), and
`alfatreze.TAU_DEV_59` — the Cymo resampler test core, currently on `cymo-b488` seed 1 (the toggle-reset
fix, B-490/B-491), `RAM_192K=1 CLK66=1 SDRAM_BUSY=1 LPC_FW=1`. **Not yet confirmed working** — the real
hardware re-test of the toggle-reset fix is the very next step.

## Hardware-confirmed
- OP_BAR 9-bit widen: fullscreen Winamp Bars past 127 rows (B-459).
- Cymo tick fix (B-484/B-486): the reported "vibrato" is gone, music sounds better (lower noise floor,
  bigger soundstage) — owner-confirmed on real hardware.
- MCLK jitter and the I2S CDC are BOTH ruled out as causes of anything audio-quality-related (B-467,
  B-470) — the SINAD gap the whole Cymo investigation started from was never about the clock path.
- T2-00, the full `all6-combined` + FLAC LPC bundle, Settings crossfade, Chladni H2 tracking, theme/mode
  persistence, Configure-page meter preview: all previously confirmed, unaffected by anything this
  session touched.
- FLAC LPC hardware kernel, MP3 window unit, TIM1 covers: unaffected, previously confirmed.

## Known open evidence and defects
- **Cymo toggle-reset fix (B-488)**: fit-proven, installed, **not yet hardware re-tested**. Immediate
  next step.
- **Cymo "tiny constant noise" during ordinary music playback**: reported once, not yet confirmed fixed
  or separately diagnosed — may be the same B-488 transient if it coincides with track changes, may be a
  separate, still-open issue (candidate: the architecture's own inherent, normally-benign push/pop timing
  jitter between pcm_fifo's and the resampler's two independent accumulators — not yet investigated).
- Cymo is 44.1 kHz input ONLY by design (`Q_STEP=147` fixed) — engaging the live toggle on 48/22.05 kHz
  material will mis-resample, not fall back gracefully. Flagged to the owner, not yet guarded against.
- `CPU LOAD reads 100%` in every state, so it cannot show headroom. Use the per-stage decode percentages instead.
- `Track changes` Check fails (0 of 10 done): pre-existing, unexplained, unrelated to this session.
- Hardware wave/scope path is compiled out (`if (0 && wave_hw)`, B-302): unchanged.
- ~~Boot-restore mismatch between release and diagnostic builds~~ (`docs/issues/021`): closed as superseded 2026-10-02 (legacy playlist removed; likely stale library index, B-332).
- Meter-preset persistence: unchanged, still dropped to stay under the 16-entry `interact.json` cap.
- `docs/ROADMAP.md` has no Cymo row yet — not added this session, since the bug-fix cycle is still open
  and the owner's own ordered list shouldn't be reshuffled mid-investigation.

## Uncommitted or not mine
`docs/vendor/` (confidential vendor datasheet, deliberately left out of every commit, by design). Check `git status` before assuming anything
else is stale — this project has multiple concurrent sessions (today's session hit two real B-NNN audit-ID collisions with a concurrent
session, both resolved by renumbering, see B-480's own note in AUDIT_TRAIL.md).

## Sibling project
Tau Omega (`../Tau Omega/`, MIT OR Apache-2.0, Rust + Tauri) manages the card: sync, packages, diagnostics decode, screenshots. It keeps its own order in
its `docs/STATUS_HANDOFF.md`. The shared surface is `docs/CROSS_PROJECT_INTERFACE.md`; never share literal files.

## Where things are
| Need | File |
|---|---|
| Ordered plan | `docs/ROADMAP.md` |
| Latest handoff (sessions, traps, tools) | `docs/handoffs/SESSION_HANDOFF_2026-10-01_CYMO_RESAMPLER.md` |
| Cymo design doc | `docs/features/CYMO_AUDIO_ENGINE.md` (section 15 has the prior-art research + all status updates) |
| Decisions | `docs/DECISIONS.md` |
| Design references | `PHASE_F_SPEC`, `HELIOS_SPEC`, `HELIOS_ARCHITECTURE_REVIEW_2026-09-28`, `TALOS_REVIEW_2026-09-28`, `TALOS2_REIMPLEMENTATION_PLAN`, `METER_MODULE_SPEC`, `THEME_SPEC`, `MEDIA_LIBRARY_0.4_SPEC`, `PHASE_G_SPEC`, `TEST_SUITE_SPEC`, `MMIO_ALLOCATION`, `IMAGE_FORMATS`, `FLAC_LPC_KERNEL_DESIGN`, `CYMO_AUDIO_ENGINE`, `CYMO_AUDIO_ENGINE_REVIEW` |
| Card install | `docs/CARD_INSTALL_PROCEDURE.md`, `tools/install_dev_core.py` |
| Packaging with a specific firmware-flag combo | `tools/package_dev_build.py --build-flags` (B-448 — always use this, not a manual pre-build, for `RAM_192K`/`CLK66`/`SDRAM_BUSY`/`LPC_FW`) |
| VM fit launches | `tools/vm_fit.py launch/status/collect` — **always state an estimated duration and local finish clock time when launching (new rule, CLAUDE.md section 3)** |
| Skill knowledge | `analogue-pocket-dev` skill, KB-069 (memory-inference), KB-077 (build-clobber), KB-078 (JTAG polling limits), KB-079 (interact.json 16-entry cap), KB-080 (data.json reload-bit clutter), KB-083 (borrowed imprecise tick → resampler beat bug), KB-084 (reset-on-enable for toggled stateful units) |
