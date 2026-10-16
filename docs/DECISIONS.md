# Decisions register

Numbered decisions with reasons, so an assessed option is never re-litigated. Rejected or parked options are recorded
too, with why. Newest last. Detail lives in the spec each entry points at.

## Meter module structure (2026-09-25, B-274) -- `docs/features/meters/METER_MODULE_SPEC.md` section 25

| # | Decision | Status |
|---|---|---|
| D-M01 | One `tau-assets.bin` container (themes, meters, later icons and fonts) instead of one slot per asset type | Decided |
| D-M02 | Generic on-device Configure page kept, Diagnostic-Build-only | Decided |
| D-M03 | Omega consumes a versioned preview bundle from a Tau-Alpha release and never forks it | Decided |
| D-M04 | Lab maths: JS port + golden vectors by default; WASM per meter only if a `wasm32` toolchain is proven | Decided |
| D-M05 | `role[]` theme roles are part of the meter contract from day one | Decided |
| D-M06 | A function enters the shared meter core when a second meter needs it | Decided |
| D-M07 | Legacy meters stay exactly as they are; any look change is opt-in per meter with owner approval | Decided |
| D-M08 | Cost-guard fallback is per-meter declared, defaulting to the `VIZ_BARS`-equivalent | Decided |
| D-M09 | Preset sharing formats: `.tmeter` JSON, `.tmeterpack` zip, `tau-meter:` text; binary only on device | Decided |
| D-M10 | Templates live in the firmware registry only; user variety comes from presets | Decided |
| D-M11 | Licence for shared presets and packs is MIT | Decided (owner) |
| D-M12 | Public preset gallery | **Parked** (owner) |
| D-M13 | Build order M0 to M6 as in the spec | Decided |
| D-M14 | A meter's persistent mutable state (1 KB or more, append-and-scan) lives in PSRAM via `MTR_PSRAM`, as a ring unwrapped into a hot scratch; this is the framework's strategy for all future meters | Decided (owner, 2026-10-02) |

### D-M12 detail: public preset gallery (parked)

What it is: a hosted, browsable collection of user-submitted `.tmeter` / `.tmeterpack` presets that Omega could search
and install from.

Why parked: it needs hosting, identity, moderation and takedown handling, none of which improve the device, and the
formats already support file and text-string exchange without any of it. Nothing built today depends on it.

Residual value kept so a later gallery is cheap: the content-addressed preset `id` (dedupe), the `licence` field
(MIT default), the optional `preview` block (trace + frame hash), and the compatibility fields (`meter`, `schema`,
`requires`, `min_firmware`). Revisit after M4 has real users; if revisited, start as a static, curated index in a git
repository (reviewed pull requests) rather than a service.

## Cover / thumbnail image format (2026-09-26, B-284) -- `docs/features/IMAGE_FORMATS.md`

| # | Decision | Status |
|---|---|---|
| D-I01 | Cover art variant is palette 256 (CLUT + 8-bit indices, `TIM1` container) at 128 px on the long side | Decided (owner) |
| D-I02 | Non-square covers scale proportionally, no crop and no letterbox; the file carries the real width and height | Decided (owner) |
| D-I03 | Better on-device codecs (WebP, AVIF, JPEG XL, HEIF) rejected: entropy decoding, loop filters and RAM compete with audio on a 60 MHz core without an FPU | Rejected |
| D-I04 | BC1 and pre-scaled JPEG kept as named variants, not defaults: on the owner's nine real covers palette beat BC1 on every one and pre-scaled JPEG is smudged at small sizes and 5x slower to show | Parked |
| D-I05 | Container `TIM1` and the `tau-art/` sidecar path stay unfrozen until a firmware reader exists | Open |


## Helios architecture review follow-ups (2026-09-28/29) -- `docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md`

| # | Decision | Status |
|---|---|---|
| D-H01 | `helios_excl[]` stays a one-caller mechanism (fullscreen's CPU%/hint label) rather than gaining a synthetic second caller to "prove it generalizes." Checked for a natural second use case (VU Master's overlay -- sits in its own reserved space below the ladders, no overlap; the stress HUD row -- occupies documented "otherwise unused" screen space, no conflict; Chladni's corner -- the mechanism's own comment already rules this out, tile-replication needs the source offsets adjusted too, separate larger work) and found none currently broken in the shape the mechanism exists to fix (a fill redrawing across a persistent on-top label, a beam-race glitch). Revisit when a real second need appears -- e.g. a future meter redesign adding a persistent corner element to the normal (non-fullscreen) player screen. | Parked (owner) |

### D-M14 detail: meter state in PSRAM (2026-10-02, B-509)

Trigger: merging the Layered Wave branch left the 192 KB Diagnostic Build 368 B under its 4,096 B heap-gap floor, and 2,424 B of that was one meter's
history buffer. Chosen: move it to PSRAM as a ring (`MTR_PSRAM`, `mtr_psram_ready()`, `.psram_state` region at `0xA4009000`), with a 404 B hot scratch. Result:
heap gap 3,728 B to 5,744 B. Full mechanism, rules, cost model, risks and suggestions: `docs/features/meters/METER_MODULE_SPEC.md` section 27.

Why it is a framework decision rather than a Layered Wave fix: nearly all meter state is history that is appended and scanned, which a ~32-cycle window serves
well and on-chip RAM is wasted on; and each meter owning its own PSRAM state removes any buffer sharing between meters.

Rejected, so it is not re-litigated: **shrink the buffer** (removes a capability to fix a layout problem; three places to change), **alias onto another meter's
buffers** (works because meters are exclusive, but every switch must invalidate the other meter's initialised flag, an invisible coupling), **gate the meter out of
the build that needs the RAM** (that build exists to carry it), **cold data** (read-only; this buffer is written every frame).

Open (not decided): the window-access budget of about 700 per frame is a judgement pending Meter Sweep data; migrating Chladni's `chl_half` is a candidate that
needs an access-pattern review and owner approval (D-M07).

## Report codes (2026-10-04, B-594) -- `docs/features/BARCODE_STUDY.md`

- **D-R01: carry reports as a pixel grid (TPG), not JAB Code or a 3-plane QR.** The path is a lossless screenshot read on a PC, so the area is better spent on data than on camera robustness. JAB: large implementation (ISO 23634, LDPC, palettes, docking), reference code under a licence that looks unfriendly to an MIT repo (unverified, from memory, check before any reuse), no Rust decoder, estimated 4-5 KB on this screen. 3-plane QR: about 3x a format that already fails at 2 px modules. Not built. Revisit only if a phone-camera path for large reports becomes a requirement.
- **D-R02: robust grid is the default view, lossless the automatic fallback, QR the third view.** Owner decision 2026-10-04. Robust (4x4 cells, 2 bits per channel, up to 6,059 B) survives JPEG q80+ and common resizing; lossless (up to 259,184 B) only a lossless PNG. A bad channel fails loudly (CRC32), never silently. **No error correction in v1**: Reed-Solomon in robust mode is the open option if the shareable path matters.
- **D-R03: one drawing path and one caption set for every code page** (`rep_draw()`): block centred, frame, title / view / detail at the top, controls at the bottom. Owner decision 2026-10-04 (captions had drifted apart across the seven pages).
- **D-R04 (REVERSED 2026-10-06 by the owner: the pixel grid is now the default, no build property needed; `fw/build.sh` defaults `TPG=1`, `TPG=0` gives the old QR-only pages; the Diagnostic Build only, the release ROM has no report pages). Original text: `TAU_TPG` stays off by default (hardware-tested only in the `TAU DEV BARCODE NN` builds).** Turning it on in the shipping Diagnostic Build is a one-line default flip (as `LPC_FW` was) and an owner decision; so is `TAU_QR_MAXV=14` (QR phone-scannable only), to wait until Tau Omega decodes grids. The QR encoder tables are not trimmed: that saves cold code only, not RAM.
- **D-R05: record tag numbers are claimed on `main`.** `SR_T_LOAD2` (25) was taken on `main` while this branch used 25 for `SR_T_INFOTEXT`; the branch moved to 26/27. Before adding a tag on a branch, read the enum on `main`. Captures made with the old numbers are container fixtures only.
- **Assessed and not done:** a unique device id in every report (privacy: makes all of one person's screenshots linkable; the APF gives cores no device id, the Cyclone V chip id needs RTL and a fit); wall-clock time from APF command 0x0090 (needs RTL; the capture time is already in the screenshot file name).


## Halcyon (Cymo C5), owner decisions 2026-10-07
- **D-S01: six biquad stages** (low shelf 100, bell 220, bell 1.8k, bell 3.5k, dip bell 6.5k, high shelf 10k); not five, not ten. 139 clocks per sample (10%), no extra DSP; stage count is a parameter.
- **D-S02: presets rebuilt from scratch as data** (name + six control positions), derived from the controls and a listening purpose, validated with the real coefficient generator; the old eight voicings retired; configurable in Tau Omega (`PRST`/`EQST` sections) and on the Pocket (sliders, MY SOUND, reset, restore defaults); defaults compiled in and resettable. Preamp is attenuate-only.
- **D-S03: preset recall by gain-dip first**; dual-bank crossfade only if the dip is audible in tests.
- **D-S04: de-esser firmware-only, after the basic layer is proven**, driven from the spectrum bank's 5-8 kHz means.
- **D-S05 (open): on-device persistence of edited presets** is limited by the 16-entry `interact.json` cap (14 used): one user slot now; more means retiring legacy ids 20-23.

## Provenance of DSP work, 2026-10-07
- **D-P01: copyleft code is read for ideas, never copied or transcribed.** Tau is MIT; GPL, AGPL and LGPL code, and the constants, tables and fitted numbers in it, stay out of the tree (the same one-way rule as GPL RTL, B-085). Designs come from published papers, public specifications, public-domain algorithms (RBJ cookbook) and our own derivations and measurements. Anything seen in, or substantially informed by, another project is recorded in `docs/PROVENANCE.md` and in the header of the file that uses it; permissively licensed code (for example the MIT `compressor.c` and BS2B in JamesDSP) may be reused only with an in-file attribution and a `PROVENANCE.md` entry.

## Halcyon, owner decisions 2026-10-07
- **D-H01: the new EQ (macro controls over six biquad stages, writable coefficient store, 24-bit coefficients, presets as data) is named Halcyon**, in docs, menus, Tau Omega and the diagram from now on. Old name "Sound Shaping" survives only in historical audit entries, file names and code identifiers until the rename task (`halcyon_model.py` and its test to `halcyon_*`, the spec file to `CYMO_HALCYON_SPEC.md`, the `SOUND` page title to `HALCYON`).
- **D-H02: follow the DSP review's recommendations** (`docs/features/CYMO_DSP_REVIEW.md` section 7): (1) analog-magnitude-matched refinement in the table generator, (2) keep 24-bit DF1 with the 18-bit SVF stage as documented fallback after a synthesis-only count, (3) clamp shelf slope and check stability at the real coefficient width in every tool that writes a shelf, (4) presets may carry a raw biquad list (AutoEQ / Equalizer APO import in Tau Omega), (5) hardware parallel-dip de-esser as an optional later step after the firmware prototype, (6) no FIR/FFT EQ, per-band saturation or oversampling.
- **D-H03: parked for a later review (owner, 2026-10-07): the non-EQ parts of the references** (crossfeed and stereo widening, bass boost, multiband compression and limiting as a feature, loudness metering and matching beyond ReplayGain). Not started; see ROADMAP row 15.

## Headphone measurement gates, 2026-10-07
- **D-G01: the headphone developments listed in `docs/features/CYMO_RECORDING_PLAN_DEV105.md` section 5 are gated on its recordings** (G-FLOOR, G-HWGAIN, G-A16, G-SPUR, G-LOAD, G-ISP). Nothing in a gated row is built, defaulted or shipped before its gate opens; the ungated list in the same section may proceed. A gate opens only when the owner's recordings have been analysed and the result recorded in `AUDIT_TRAIL.md` against the predictions written beforehand.

## Assets container, 2026-10-07
- **D-A01: `tau-assets.bin` stays ONE file in data slot 8 and its read limit is lifted from 1 KiB to 64 KiB by a chunked read into a PSRAM staging area (B-628).** Rejected for now: a new data slot (id 9) or a second bin for large Halcyon data, and sharing the cold-image file (it is strictly paired with the ROM by a layout id). **Owner: review this arrangement once Cymo is fully shipped** (in particular where the EQCO tables and a full correction-preset library should live); until then it stands.

## Cymo output stage, owner decisions 2026-10-14 (from the EVO 4 recordings, B-694..B-722)
- **D-G02: the 16-bit I2S slot is NOT adopted and is not the default; it stays a Diagnostics toggle (off at boot, not saved).** Evidence: it adds exactly 6.02 dB and the same SINAD at equal loudness (B-697); the Pocket's output compresses from -3 dBFS true peak with it ON and is clean to about +1 dBFS with it OFF (B-699), so the analogue stage has only about 1 to 3 dB of headroom above the 15-bit slot's full scale and the 15-bit slot is the safe setting. A "loudness-neutral offset" would only recreate the 15-bit level with less digital headroom. Rejected options: ON by default; ON with a -6 dB offset or a -12 dBFS clipper ceiling (the -12 figure in the first proposal could not be derived from the measured onset). Reopen only if a load measurement (G-LOAD) shows the amp has more headroom than the line-input numbers.
- **D-G03: the output-stage soft clipper and dither are deferred, not built.** Halcyon's auto-preamp already gives back every boost (worst overshoot +3.55 dB, B-718 confirms the offsets), firmware attenuates before the EQ, the analogue floor (-97 dBFS, before the system volume stage) already dithers quiet material, and positive ReplayGain is peak-limited by tag (D-G04), so the clipper has no demonstrated customer. Reopen if the cheap measurement (isp_hot at volume 100 with a boosting Halcyon preset, 16-BIT OFF) shows the EQ clamping, or for tracks with no peak tag. Starting point then: the true-peak limiter model (`tools/lab/`), not the knee curve of `CYMO_OUTPUT_STAGE_SPEC.md`.
- **D-G04: positive ReplayGain exists only against a peak tag and only in the hardware gain stage (B-720).** Total gain (volume x ReplayGain) may exceed unity up to the smaller of +6.02 dB (the stage's 16-bit target register, owner ceiling) and `0.944 / peak` (-0.5 dB margin for inter-sample peaks); no peak tag, no boost; the firmware gain path stays capped at unity. Rejected: a limiter or clipper to allow boosts without peak tags (D-G03); a lower ceiling (+3 dB).
- **D-G05: the HW GAIN ON-to-OFF click was the FIFO output register's held, unscaled sample exposed when the stage was disabled (B-719); fixed in firmware by waiting out the register's glide to zero with the stage still muted.** Not heard on a Pocket yet (DEV 112 carries it).

- **D-P2-01..D-P2-08, D-P2-10: Player 2 (owner, 2026-10-15; `docs/features/PLAYER_V2_SPEC.md` section 17).** Agreed as recommended: (1) a new `player2` firmware target beside the old player, old kept as the differential reference; (2) no support for bitstreams without the PCM ring and the hardware gain stage; (3) the inherited FLAC decoder is replaced, last (S8), differential against the old one, gated on CPU idle; (4) Helix stays the MP3 decoder (glue rewritten); (5) first-release gapless is same rate and codec only, prepared from the library index details (head read skipped); (6) tempo runs with the ring on (the timeline carries the speed ratio); (7) the optional CAPS register and free-running underrun counter are scheduled as one small fit before S3 without blocking on it; (8) UI scope of the first release is parity with today's screens, the Figma redesign follows as views; (10) a manual track change keeps its flush and fade-in. **D-P2-09 (owner, 2026-10-15): no LTO for the first release of Player 2; the tiny hot functions are `static inline` in headers and marked `HOT`; revisit with measurements at S6** (build with and without `-flto`; adopt only for a clear gain, about 2 KB or more of hot RAM or 3 points of CPU on MP3, with a stable heap gap; earlier if the module split costs more than about 2 KB or 2 points of CPU at S3/S6).

## FLAC decoder v2, owner decisions 2026-10-16 (docs/features/FLAC_DECODER_V2_REVIEW.md)
- **D-F01: the gate for replacing the FLAC decoder is "no regression against the current decoder (the Rice fast path, 45% idle on the hard 48 kHz file)", not "20% idle".** CRC-16 verification is ON by default (revisit only if it costs the hard file more than 3 points); a damaged frame is replaced by silence and the decoder resynchronises, giving up after 8 consecutive failures; nothing of a failed frame is ever audible (stage, then commit).
- **D-F02 (rejected): reusing a third-party FLAC decoder as product code.** libfoxenflac is LGPL-2.1 (out under D-P01); dr_flac (public domain / MIT-0) misses the pre-set gate (instructions -9% to -16%, +40 KB code, +21 KB RAM, whole-frame and blocking, no hooks for the LPC unit or budgeted steps) and at equal optimisation has the same instruction count as `fw/flac.c`. dr_flac stays a host oracle and speed yardstick.
- **D-F03: positions are 32-bit in the new FLAC core** (samples, file offsets, timeline); a frame header claiming a start past 2^32 samples is a false sync; a STREAMINFO total past 2^32 reads as unknown. Reason: the stream reader and `tl_open` are 32-bit and every 64-bit operation costs code on the 192 KB link.
- **D-F04: the LPC unit takes only the predictors the 32-bit software tier cannot do exactly (24-bit audio, long high-precision predictors).** For 16-bit music a proven 32-bit software loop costs about the same as the unit handshake.
