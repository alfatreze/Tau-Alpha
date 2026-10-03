# Roadmap: the one ordered list of what is next

**Status: DRAFT, updated 2026-09-27 for the v0.5.0 release.** Proposed by Claude from the docs of every session; **the order is the owner's to set.** Items marked *(owner)* need a decision
before work starts. This is the only document that says what comes next. Every other plan or spec is a design reference for one item here
and must not carry its own "next" list (see section 6).

Current facts (card, release, open defects): `docs/CURRENT_STATUS.md`. History and evidence: `docs/AUDIT_TRAIL.md`.

## 1. Now (ready, no owner decision needed)

| # | Item | Status | Gate / next action | Reference |
|---|---|---|---|---|
| **0** | **Theme system, all 4 steps** (shipped in v0.5.0) | **DONE, released 2026-09-27.** Remaining: Omega's exporter waits for a real `tau-assets.bin` captured from a card | 0a role table with today's look as the default theme, no visible change; 0b Dark/Light switch plus a second built-in theme, with the text gamma check (`tools/gen_text_gamma.py --check`); 0c VU ladder, meters and thumbnails onto roles and CLUT-from-roles; 0d theme file (`TTHM`, new data slot) loader plus Omega exporter. Theme index and polarity persist across restarts (item 8, B-346/B-403 — corrected 2026-09-29: this row previously said "session-only until persist is widened," stale even at the time it was written since B-346 had already shipped the widening). Each step ends with a host test and a card run | `THEME_SPEC`, `HELIOS_SPEC` 7.4 |
| 1 | Close alpha.30 | Hardware-confirmed good and released in 0.5.0 (404,712 slots, 0 BAD, 1.75x clean) | Optional like-for-like unit-off run; find the speed where audio breaks; HarpMudd comparison later | B-308, B-309 |
| 2 | Batched drawing for the hardware wave/scope path | Compiled out since B-302 (256 columns cost ~21x a normal meter) | Design batching (few `fb_rect`/`OP_BAR`-style commands per frame), re-enable, prove with Meter Sweep | B-298..B-302, `HARDWARE_METER_IDEAS` #6 |
| 3 | Remove software paths made redundant by hardware | Spectrum cascade already removed | After 2: delete software level/scope in `meters_feed` and `wviz_scope_tick` | ALPHA22 handoff item 3 |
| 4 | Meter modules: **M0 to M5 built and host-verified (B-294, B-317..B-323)**; M6 (wrapping the legacy meters) deliberately not done | Winamp pair, Chladni and every future meter with parameters run on generated modules, a generic Configure page, `SR_T_METERCFG`, the METR presets file, the meter trace recorder and a preview lab whose ports are proven command-for-command against the firmware. Open: hardware run of alpha builds carrying it; reorder/hide meters from a file; a JS twin for Chladni; Omega's METR writer | `docs/AUDIT_TRAIL.md` B-317..B-323, `METER_MODULE_SPEC` |

## 2. Next (needs an owner call first)

| # | Item | Why it needs a decision | Reference |
|---|---|---|---|
| 5 | ~~Release the MP3 window unit~~ **DONE: v0.5.0, 2026-09-27** | Still true: the bitstream leaves only 4 free M10K blocks (304/308); decide before spending more block RAM on anything (see item 6) | `DECISIONS` (new), B-308/B-309 |
| 6 | ~~192 KB RAM shrink~~ **DONE, RTL and firmware both, hardware-confirmed (2026-09-30 correction)** | `TAU_RAM_192K=1` has been baked into every `0.6.0-alpha.N` bitstream since `A_17` (`glyphbuf-t200`, `CORE_VERSION` rev 26, refuses non-`RAM_192K_FW` firmware) — this is the bitstream the card has been running all session, hardware-confirmed repeatedly (B-450 among others). The "8.5 KB short" figure was stale (true 2026-09-25, B-236); cold-code moves since (B-244 onward) closed it with real margin: `release` 14,864 B / diagnostic 9,008 B / profile 6,704 B heap gap, all well over their floors, verified 2026-09-30. Nothing left to do here. | `RAM_SHRINK_192K_PLAN`, `PHASE_F_SPEC` section 4 |
| 7 | **H2 double buffering: built, two real bugs found and fixed, not yet re-tested (B-396/399/402, 2026-09-29)** | `DBUF_READY()` probe + a bracket around the chrome/art redraw seam; installed and hardware-confirmed the RTL/timing (`alfatreze.TAU_0_6_0_A_17`), then two real bugs found from an owner report and from code inspection (the bracket flipping onto stale content when the underlying draw was suppressed by `FB_HELD()` or `screen_blank`) and fixed, on `alfatreze.TAU_0_6_0_A_18`. Remaining: the owner's retest; then the now-playing redesign (Figma node 194:1630, parked for its own session, do after item 0 so it's written against roles) | `HELIOS_SPEC` sections 5 and 9, B-396/399/402 |
| 7b | **Light text weights in RTL**: built and simulated (B-316: `text_light` input, MMIO 0x114, table 0,1,1,2,3,3,4,5,6,7,8,9,10,12,13,16). **Fit done, both seeds close (B-316); shipped in v0.5.0**; can share the fit with persist widening (item 8). *(owner: launch the fit)* | B-311, `tools/gen_themes.py` report |
| 8 | ~~Persist widening in RTL~~ **Already built and present in the current tree (B-403, 2026-09-29)** | RTL (16->32 persist words, `mp3_soc.v`/`core_game.vh`, unconditional, no macro/version bump) and firmware (`persist32_probe()`, `fw/settings.inc`) both already committed since B-346 and running on `alfatreze.TAU_0_6_0_A_18`. Remaining: confirm theme + meter-preset choices survive a real Quit+relaunch on hardware | `THEME_SPEC` section 5, `METER_CONFIG_SPEC`, B-346, B-403 |
| 9 | Cover art: freeze the `TIM1` container | Reader is on by default and hardware-confirmed for MP3 and FLAC (B-329, B-330), shipped in v0.5.0; D-I05's precondition (a firmware reader exists) is met. Decide when to freeze. *(owner)* | `IMAGE_FORMATS`, `DECISIONS` D-I01..D-I05, `COVER_TIMG_READER` |
| 10 | Audio kernels beyond the MP3 window: **FLAC LPC reconstruction kernel** (retargeted from the bit reader, 2026-09-28 -- real hardware data found LPC dominates at 89-96% of channel 0's pass and channel 1 costs MORE than channel 0, not the bit-reader; FLAC decode overall measures ~99% of realtime, essentially no margin, and correlates with real reported audio clicks) | **DONE, hardware-confirmed (2026-09-29, B-376..B-386).** Built, fixed a real ALM-budget-blocking memory-inference bug (two attempts, the first a documented negative result), fit with real margin (18,064/18,480 ALMs, 98%), installed, hardware-confirmed via a causal A/B test (microstutter present with the unit disabled, gone with it enabled) and quantified (worst-case latency 11ms software vs 5-6ms hardware). Remaining: a sample-exact hardware-vs-software Check comparison (golden-vector testbench proves the arithmetic in simulation only). | `FLAC_LPC_KERNEL_DESIGN`, `FLAC_BITREADER_KERNEL_SCOPING` (superseded), `analogue-pocket-dev` skill KB-069 (local), B-360..B-386 |
| 11 | Two unverified MP3 numbers, cheap to measure (2026-09-28, B-365) | `CPU LOAD` reads 100% while `H+I+S+D+A+X` sums to only ~38% on the one real reading -- the ~62% gap was flagged in B-309 as "not usable as evidence" and never chased; likely UI/meter cost (ties to the `helios_audio_ok()` proposal, `HELIOS_SPEC` section 8.1), not hidden decode cost, but unmeasured. Separately, `S` (Subband, 22% even with the hardware window unit) has never been broken into "MMIO handoff" vs. "residual software work" post-deployment -- the design doc's ~400-cycle handoff estimate was never re-profiled on real hardware. Neither blocks anything; both are assumptions the FLAC work's own "measure, don't assume" lesson says are worth closing. | B-309, `MP3_FILTERBANK_KERNEL_DESIGN` section 2, `HELIOS_SPEC` section 8.1 |

| 12 | **Cymo: audiobook tempo (C7), test DEV 79 on a Pocket** (B-535..B-558) | Resampler hand-off fix hardware-confirmed (B-529). Tempo core, funnel and player wiring built and installed as `alfatreze.TAU_DEV_79` (MP3 only, Settings > Playback > TEMPO); **never run on a Pocket** | Owner tests DEV 79 (mono speech MP3 at 1.25/1.5/2.0x, a stereo MP3 at 1.25x, HEADROOM + UNDERRUNS after 10 s of plain playback, seek/pause/resume). Then: soft reset on resume, headroom guard, pause shortening (Off/Small/Medium/High, T3), persistence, tempo in the Diagnostic build; resolve issue 023 (rare spike) | `docs/handoffs/SESSION_HANDOFF_2026-10-03_CYMO_TEMPO.md`, `docs/features/CYMO_TEMPO_INTEGRATION.md`, `CYMO_AUDIO_ENGINE` section 7 |

## 3. Later and parked

- Meter module M6 (wrapping the legacy meters; M0-M5 are built); public preset gallery (parked, D-M12).
- Hardware meter helpers (beat detector, L/R correlation, Chladni field evaluator): saved ideas, none started (`HARDWARE_METER_IDEAS`).
- Blit ideas held on purpose: B5 alpha blend (pipelined version closes timing, B-327; firmware use is a 0.6 item), B13 gradient bar (cost bigger than scoped, B-241), B19 flip flags, B10 RLE blit, H2 double buffering.
- Library items waiting for RAM (`MEDIA_LIBRARY_0.4_SPEC` section 14) and the migration-across-versions question (section 15).
- Firmware modularization (`FIRMWARE_MODULARIZATION_PLAN`, parked until key features land).
- Winamp on-device Configure page: parked, may be replaced by Tau Omega authoring; do not extend.
- Tracker/MOD support, CJK/UTF-8 fonts, Chladni presets beyond Lattice/Shimmer, per-channel waveform presets.
- Resolution 720: **last** by owner decision, everything earlier must stay resolution-agnostic.
- Defects: `Track changes` Check failure; (boot-restore mismatch, `docs/issues/021`, closed as superseded 2026-10-02; heap-peak in Check done, B-514);
  BUG-001 accented names (`docs/issues/001`); some Layered Wave / meter settings problems the owner noted on 2026-10-02 (details to come, after the flicker work); the asynchronous present (poll the fence from the main loop instead of waiting) and a Helios input layer (`HELIOS_ARCHITECTURE_REVIEW` 8.1) are the open structural items from the Layered Wave work. Layered Wave flicker (`docs/issues/022`) is closed.

## 4. Standing decisions (still in force; ask before reversing)

Recorded in the docs named, not yet in one register (moving them into `docs/DECISIONS.md` is part of the consolidation):
no FFT (hardware octave/spectrum bank instead); no 3D GPU (2.5D on the 2D engine); licence stays MIT (GPL RTL may be studied, never copied);
720 is last; main RAM shrink is gated on meters going cold (met); Talos = blit engine, Helios = UI library over it; two seeds for every timing
claim; hot-to-cold calls need a `COLD_READY()` gate; card installs go through `tools/install_dev_core.py`; Tau and Tau Omega never share literal
files. Meter and image decisions: `docs/DECISIONS.md` D-M01..D-M13, D-I01..D-I05.

## 5. Sibling project (Tau Omega)

Owns card management, Check/QR decode, screenshots, presets authoring. Its own order is in `../Tau Omega/docs/STATUS_HANDOFF.md`. Items here that
create work there: TIM1 container freeze (item 9), meter presets and `tau-assets.bin` (item 4, M4), new persist ids or data slots (log in
`docs/CROSS_PROJECT_INTERFACE.md`).

## 6. Rules for keeping this true (several sessions and accounts write here)

1. **Only this file orders work.** A spec may say "status: built / not built" for its own design, never "next".
2. **One editor for this file** (the session doing the consolidation). Other sessions add ideas at the bottom under "Proposed" with their name and date;
   the owner accepts or rejects them.
3. Every session logs to `docs/AUDIT_TRAIL.md` (own id range, check the last id first) and updates the single latest handoff, not a new parallel one.
4. When a fact changes (card contents, release, an open defect), update `docs/CURRENT_STATUS.md` in the same turn.
5. Commit only your own files (hunk-stage mixed files); see `CLAUDE.md` section 3.

## Proposed (other sessions add here)

- **0.6 scope (owner, B-331):** persist widening so theme and meter settings are remembered (**done, B-346/B-403/B-408 — only a hardware Quit+relaunch confirmation remains, owner deferred to a later test pass, 2026-09-29**), alpha blend in firmware (translucent panel/fade), the `Track changes` fix. (Claude, 2026-09-27)
- **Talos 2 reimplementation, after the FLAC LPC work (owner, 2026-09-28):** plan in `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md` (phases P0-P4; P1 alone frees about 6,500 ALMs by putting the row buffer back in MLAB with blend on). Evidence: `docs/research/TALOS_REVIEW_2026-09-28.md`. (Claude, 2026-09-28)
