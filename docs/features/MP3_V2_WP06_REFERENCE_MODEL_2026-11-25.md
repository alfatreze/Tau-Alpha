# MP3 v2 WP-06: the float64 back-end reference model, its gate and the two comparison points (2026-11-25)

**Status: report (work package WP-06 of `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`).** Host-only: no firmware, RTL, fit, card or VM was touched; `third_party/minimp3/` is unchanged. **Evidence labels:** **[MEASURED]** run in this session on the host; **[DERIVED]** my own derivation, checked by the named test; **[MEMORY]** the standard's formulas as I know them (the text of ISO/IEC 11172-3 is not available, D-C02 D14; the formulas are those the repository already relies on for the window, plus the usual Layer III formulas, and are proven here only by agreement with the oracle). Nothing ran on a Pocket.

## 1. What was added

| File | Role |
|---|---|
| `tools/mp3v2/ref_backend.py` | the model (numpy float64): alias reduction, direct O(n^2) IMDCT for long (36), short (12) and mixed blocks with the four windows, overlap-add, frequency inversion, direct matrixing, 512-tap synthesis with `tools/data/mp3_synthesis_window.txt`; the unit-arithmetic variant; the metric tool; the hard gate; `gate`, `metrics`, `library`, `domains` command lines |
| `tools/mp3_v2/oracle.c`, `oracle.py` | a stage dump added (`--stage`, build with `oracle.build_stage()`): an instrumented COPY of `minimp3.h` is written to `build/mp3_v2/stage/` (three hook calls at anchors that each occur exactly once; the vendored file is untouched), the stage build's PCM and float dump are byte-identical to the plain build (tested) |
| `sim/test_mp3v2_ref_backend.py` | gate on 9 generated streams, library files when present, transition matrix, 6 mutants, unit-arithmetic equality, metrics; in `make test-host` |

**The oracle had no stage dump** (it wrote PCM, float PCM and a summary only), so one was added. Per granule and channel it records the block type, the number of long subbands (mixed blocks), the band-width row, the requantised spectrum after stereo processing and before reordering (so the model reorders short blocks itself), and the subband samples after IMDCT, overlap and frequency inversion; plus a flag when the IMDCT overlap or the polyphase history was all zero before the granule (the state minimp3 resets on a header change), so the model follows resets without re-implementing the front end.

## 2. The formulas implemented (transcribed from the module docstring) [MEMORY, confirmed against the oracle]

- **Alias reduction**, for each subband boundary sb | sb+1 (31 boundaries for long and start/stop blocks, `n_long_bands - 1` for mixed, none for short) and i = 0..7: `lo = xr[18 sb + 17 - i]`, `up = xr[18 (sb+1) + i]`; `lo' = lo cs_i - up ca_i`, `up' = up cs_i + lo ca_i`; `cs_i = 1/sqrt(1 + c_i^2)`, `ca_i = c_i/sqrt(1 + c_i^2)`; c = -0.6 -0.535 -0.33 -0.185 -0.095 -0.041 -0.0142 -0.0037.
- **IMDCT**, direct: `x_i = sum_{k=0}^{n/2-1} X_k cos(pi/(2n) (2i + 1 + n/2)(2k + 1))`, i = 0..n-1, n = 36 (18 values) or 12 (6 values).
- **Windows**: normal `sin(pi/36 (i+1/2))`; start `sin(pi/36 (i+1/2))` for i < 18, 1 for 18..23, `sin(pi/12 (i-18+1/2))` for 24..29, 0 for 30..35; stop 0 for i < 6, `sin(pi/12 (i-6+1/2))` for 6..11, 1 for 12..17, `sin(pi/36 (i+1/2))` for 18..35; short `sin(pi/12 (i+1/2))`, i = 0..11. Short blocks: three windows placed at 6, 12, 18 of the 36 points and added; mixed blocks: the lowest `n_long_bands` subbands long with the normal window.
- **Overlap-add**: `out_i = z_i + prev_i` for i < 18, `prev = z_18..35`. **Frequency inversion**: odd subbands, odd time samples negated.
- **Synthesis** (per slot of 32 subband samples S): shift V by 64; `V_i = sum_k cos((16+i)(2k+1) pi/64) S_k`, i = 0..63; `U_{64a+j} = V_{128a+j}`, `U_{64a+32+j} = V_{128a+96+j}`; `out_j = sum_{i=0}^{15} U_{j+32i} D_{j+32i}` (D from the data file, multiples of 2^-16).
- **Unit arithmetic** (the ABI of `docs/features/MP3_WINDOW_UNIT_ABI_2026-11-25.md`): words `round(2^23 X_m(P))` (round half to even, as Python `round` in the ABI check) saturated to int32 and counted, `X_m = sum_k S_k cos(m (2k+1) pi/64)` in the unit's push order, V words by the cosine symmetries, exact integer sum `sum D_int word`, `out = clip16(floor((4 sum + 2^25)/2^26))` with the 64-bit sum >> 20 truncated to 32 bits as the RTL does.

## 3. The hard gate [MEASURED]

**Gate: the model fed with the oracle's requantised spectra reproduces the oracle's float PCM within 1e-5 of full scale.** The oracle's spectrum is the standard's times 1/2 (see the WP-09 report: `BITS_DEQUANTIZER_OUT` = -1; the model constant `MINIMP3_SPEC_UNIT` = 0.5 is fixed by the encode/decode test of `sim/test_mp3v2_tables.py` at a known gain, not fitted to the PCM).

| Input | Granules / samples | Max error (full scale) | Subband stage max |
|---|---|---|---|
| 9 generated streams (MPEG-1 stereo 44.1 kHz with long, start, short, stop in legal sequences incl. 1->3 and 3->1; joint stereo MS 48 kHz; mono 32 kHz; MPEG-2 LSF stereo 22.05 kHz and mono 16 kHz; MPEG-2.5 11.025 kHz; mixed blocks MPEG-1 and LSF; long/start/short) | 6,912 to 27,648 samples each, peaks 1.7 to 4.2 of full scale | 0.7e-6 to 2.1e-6 (worst 2.09e-6) | 0.5e-7 to 1.0e-7 |
| owner's library, 59 of 59 files, full length | 1,089,128,448 samples (1,890,848 channel-granules: 1,768,737 long, 33,128 start, 55,832 short, 33,151 stop) | worst 1.43e-6, all 59 pass | at most 3.1e-7 (largest printed per file) |

The gate is amplitude dependent because the oracle is float32: the float32 round-off (6e-8 relative) shows as about 3e-7 of full scale at the library's peaks (up to 1.19) and would pass 1e-5 up to peaks of about 30; the generated streams keep their peaks at 4 or below (a stream at peak 20 gave 1.4e-5 and was lowered, not the gate). Mixed-block and 8 kHz-mixed rows are covered only for the first sample rate indices of the generator.

**Transition matrix (a finding the plan did not have) [MEASURED, 23 pairs].** Model and minimp3 agree to 1e-7 on every legal block-type transition of the window-switching sequence: 0->0, 0->1, 1->2, 1->3, 2->2, 2->3, 3->0, 3->1 and mixed-short to mixed-short. They **differ** (stage error 0.08 to 0.18 of full scale in the subband samples) on 0->2, 0->3, 1->0, 1->1, 2->0, 2->1, 3->2 and 3->3, on the mixed variants of 0->2, 2->0, 2->1 and 3->2, and on the transitions between a start block and a mixed block (1->2 mixed) and between a mixed block and a stop block (2 mixed->3): 14 pairs in all. Reason (from reading the code and the data, not copied): minimp3 keeps the second half of the IMDCT output UNWINDOWED (9 values per subband) and applies the CURRENT granule's window to it at the next granule, which equals the standard's rule (each granule windowed with its own block type) only when the window shapes of the pair are complementary, as in legal sequences. The model applies each granule's own window literally. No encoder produces the differing pairs except possibly encoders using mixed blocks; **the owner's library contains no mixed block and no illegal transition (0 illegal transitions and 0 mixed granules in the 1,890,848 channel-granules above, counted per channel by `ref_backend.py library`)**, so the oracle and the standard agree on all of it. For streams with mixed blocks next to start or stop blocks, the oracle is not a safe reference; the model is the one that follows the formulas (unverified against the printed standard).

## 4. The two comparison points of the expert review, defined with this model [MEASURED]

`ref_backend.compare_domains` defines them; `ref_backend.py domains FILE` runs them on a file. One real file (Cowboy Bebop "Tank! (TV stretch)", 7,935 frames, 18,282,240 samples, 19,410 reference samples at or beyond full scale):

| Comparison | rms (LSB) | max (LSB) | count |
|---|---|---|---|
| (a) WIDE: unit arithmetic before the +1/2, floor and clip, against the float64 reference (both x 32768, unclipped) | 0.0016 | 0.0048 (the ABI document's bound is 0.0107) | none above 0.5 LSB |
| (b) 16-BIT: unit output against the reference rounded half up and clipped | 0.036 | 1 | 18,258,981 equal, 23,259 (0.127 %) differ by 1 LSB, 0 by more |
| (b') 16-BIT output against the unrounded reference (clipped to the 16-bit range), the ISO-style number | 0.2874 | 0.504 | |
| oracle's own 16-bit output (minimp3 float to s16) against the same unrounded reference | 0.2886 | 1.50 | 6,208 above 1 LSB (the oracle's s16 conversion is not round half up and clips differently; 31,866 samples differ from half-up rounding, one by 2 LSB) |
| ideal rounding to 16 bits, for scale | 0.2887 (2^-15/sqrt(12) of full scale) | 0.5 | |

This lets us define two of the review's flagged ISO items **without the standard text**: (1) **the comparison point**: gate the decoder chain in domain (a), where the unit's error is 0.0016 rms / 0.0048 max LSB and the budget of every earlier stage can be stated in LSB of that domain; use domain (b) as an end-to-end equality count (at least 99.8 % of samples equal to the rounded reference, none off by more than 1 LSB for the unit alone), because a 16-bit output measured against a wide reference sits AT the rms bound 2^-15/sqrt(12) (0.2874 against 0.2887) by construction of the rounding, so an rms gate in domain (b') cannot discriminate; (2) **the tolerance**: the recalled ISO figures (rms < 2^-15/sqrt(12), max <= 2^-14 = 2 LSB, [MEMORY], not checked) are met by the unit alone in domain (b') (0.2874 < 0.2887, max 0.504 < 2), with no margin on the rms: any added error in the decoder chain must be shown in domain (a), where it is separable. What stays open: the ISO figures themselves and the standard's -20 dB sine-sweep stream (D14); whether the "max" criterion applies to the clipped rails.

## 5. Unit-arithmetic switch [MEASURED]

The integer model equals `tools/mp3_v2/window_abi_check.py`'s `UnitPy` (itself proven bit-identical to the golden C model and to the RTL in WP-02) on 180 slots of quiet, ordinary, loud (2,470 of 5,760 samples clip), impulse and alternating-extreme data, and the words equal `words_for`. int32 saturation of the word conversion is implemented and counted (0 on the library file above: no word saturated). A model at scale 2^22 is not accepted by the same check (red case).

## 6. Speed [MEASURED]

The model alone: 18.3 M samples (all channels) in 3.8 s, about 4.8 M samples/s, 55 times real time on this host (whole 59-file library, 1.09 G samples, 269 s including the oracle runs and the dumps, about 4 M samples/s). A 7,935-frame file produces a 110 MB stage dump (temporary).

## 7. Mutants (red cases) [MEASURED]

Each makes the gate fail on the first generated stream (max error): wrong alias coefficient (c_2 = -0.35): 1.9e-2; stop block using the start window: 0.94; wrong overlap order (previous half reversed): 2.7; wrong short-block order (windows rotated): 2.2; frequency inversion missing: 3.2; one synthesis window value 2 % off (D[100]): 3.9e-4. The same stream passes with the unmutated model (green).

## 8. Contradictions with, or additions to, the plan

- The plan's gate text is met. **New facts the plan lacks:** (1) the oracle spectrum is half of the standard's, which WP-12's fixed-point format must decide about (a power-of-two exponent offset costs nothing); (2) minimp3 reproduces the standard's windowing only on legal block sequences (section 3), so "float minimp3 is the reference" holds for the library but not for synthetic streams that break those rules (the stream builder, WP-08, must emit legal sequences for oracle comparisons); (3) the review's "a decoder whose output is the unit's 16-bit clamp sits at the full-accuracy bound" is confirmed in numbers (0.2874 against 0.2887).
- Open (not done): the ISO text and vectors; mixed blocks at 8 kHz (see the WP-09 report); intensity stereo and MS are inside the oracle's front end, so the model sees their result only (their correctness is not proven by this model, only by the oracle's agreement with ffmpeg on four streams, WP-01).
