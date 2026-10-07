# Work that proceeds alongside the DEV 105 recordings (approach saved 2026-10-07, B-618)

Rule (D-G01): nothing in a gated row of `CYMO_RECORDING_PLAN_DEV105.md` section 5 is built, defaulted or shipped before its gate opens. Everything below is outside those rows. Status column is updated as work lands.

## A. Host or firmware only (no card, no VM)
| # | Item | Notes | Status |
|---|---|---|---|
| A1 | Round instead of floor in the 24-to-16 FLAC reduction (`fw/flac.c:804`) | one line plus a host test; removes -0.5 LSB DC and signal-following distortion on 24-bit files | **DONE B-619** (host-tested, not on a Pocket) |
| A2 | Ramp the loudness-changing toggles (16-bit slot, live resampler) | removes the 6 dB step; the default policy of the 16-bit mode stays gated on G-A16 | **DONE B-620** (builds clean, cold code, not host-testable, not on a Pocket) |
| A3 | Crossfeed model, mono mode, A/B listening WAVs | host model of the difference form plus a classic form for comparison by ear | **DONE B-621** (model, integer reference, 12 checks, 12 listening WAVs in `test music/Audio Lab/crossfeed_ab/`; waiting for the owner's listening verdict; D corrected 13 to 4 samples) |
| A4 | Gapless groundwork | load-latency counter (file end to first new sample), LAME delay/padding parsing with host tests; FLAC no-flush boundary and absolute priming threshold as design notes | **LAME delay/padding parsing DONE B-624** (`fw/lame_tag.h`, checked against Apple's gapless decoder on 6 real MP3s); the load-latency counter is NOT done (needs a diagnostic build and a Pocket) |
| A5 | Halcyon firmware and data layer | six-control to stage-gain macro layer, `EQCO`/`PRST` data format with golden vectors, AutoEQ `ParametricEQ.txt` import spec for Tau Omega, tuning-lab HTML page | **PRST format + APO importer + reference tool DONE B-626** (`tools/halcyon_assets.py`, `docs/features/HALCYON_DATA_FORMAT.md`); NOT done: firmware macro layer and loader, EQCO (open decision: ROM table recommended), tuning-lab page |
| A6 | Info row for the hardware gain (target, current, fading) | diagnostic aid for session 2; reaches the card with the next build | **DONE B-625** (Info row 36, host fixture updated; not on a card) |
| A7 | Spec edits: infrasonic stage 15-20 Hz, 17-stage engine sizing, peak-safe preamp default | documentation | **DONE B-623** |
| A8 | Host models for the gated branches: final quantiser with ceiling parameter, true-peak limiter (4x oversampled detection) | cheap on the host; ready when G-FLOOR / G-ISP open | **True-peak limiter DONE B-622** (model + 8 checks); the final quantiser with ceiling parameter already exists as `cymo_out_model.py` (dither, soft clip); a ceiling-aware quantiser model waits for G-FLOOR |

## B. VM fits (one at a time)
| # | Item | Status |
|---|---|---|
| B1 | Two-seed fit with `TAU_EQ_COEF24` on the gain-stage bundle (64-bit accumulate timing) | **RUNNING** `eq24-b618`, launched 08:44 local 2026-10-07, expected about 10:15-10:20 |
| B2 | Synthesis-only: EQ state and coefficient store to MLAB/M10K, measure ALMs saved (prerequisite of the Halcyon RTL) | **ANALYSED B-627, experiment deferred**: the current module cannot be switched by an attribute (4 async reads, a reset loop over the array, combinational ROM address); the measurement belongs to the rewritten engine (Halcyon spec, section 'Finding while preparing the MLAB experiment') |
| B3 | Resampler output widened to 18 bits into the 24-bit EQ input (RTL, simulation, mutants, then a fit) | |
| B4 | Timing experiments: STANDARD FIT, Rapid Recompile | |

## C. Release
C1 alpha.4 candidate on the alpha.3 bitstream plus firmware fixes (rounding, ReplayGain range, dB volume); excludes hardware gain, 16-bit adoption and anything gated; needs the owner's smoke test.

## D. Other roadmap items unrelated to audio
Tau Omega decoders and parsers; one Pocket run of the merged pixel-grid tree (owner); `Track changes` Check failure, BUG-001, asynchronous meter present, Helios input layer; MP3 ring and meter scratch RAM; library migration question and cover-art container freeze (owner decision); meter module M6, Chladni presets; decisions register consolidation and stale duplicate docs.

## Order of work chosen by the owner (2026-10-07)
Start with A1 (FLAC rounding), A2 (toggle ramps), A3 (crossfeed model and WAVs), the B1 fit and B2 experiment on the free VM, and A8 (host models), in that order where they do not conflict; items 1 and 3 and the VM fit run at the same time.

## Added 2026-10-07
A9 **Assets read limit lifted to 64 KiB** (chunked PSRAM read, B-628, D-A01): DONE on the host and in firmware (cold code, release heap gap unchanged); needs a Pocket test with a file over 4 KiB. The owner will review the one-container arrangement after Cymo is fully shipped.
