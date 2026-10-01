# Current status (one page)

Updated 2026-10-01. **What is true right now.** What to do next is in `docs/ROADMAP.md` (the only ordered list, not yet updated with
the Cymo row — see below). Why things are the way they are is in `docs/AUDIT_TRAIL.md`. The old, long version of this file is
`docs/archive/CURRENT_STATUS_history_2026-09-26.md`.
Full detail on this session: `docs/handoffs/SESSION_HANDOFF_2026-10-01_CYMO_RESAMPLER.md` — **read it first.**

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
- Boot-restore mismatch between release and diagnostic builds (`docs/issues/021`), re-parked until the UI redesign.
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
