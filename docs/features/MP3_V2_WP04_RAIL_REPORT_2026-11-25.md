# MP3 v2 WP-04: rail and headroom count on the library (2026-11-25)

**Status: measurement report (work package WP-04 of `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`).** Host-only, read-only on the owner's library (the 59 `.mp3` files of the `test music` folder, the same set as WP-03). No firmware, RTL, fit, card or VM was touched; nothing ran on a Pocket. **Evidence labels:** **[MEASURED]** run on the host by `tools/mp3_v2/rail_count.py`; **[DERIVED]** arithmetic on measured values; **[ESTIMATE]** an approximation stated as such. Counts only: no file names, folders are lettered as in `docs/features/MP3_LIBRARY_SCAN_2026-11-25.md` (A = 12 files, B = 12, C = 11, D = 13, E = the project's Test Album, 7; F = the four test-signal files that WP-03 lumped as F, G, H); tracks are numbered inside a folder in file-name order.

## 1. Result in one paragraph

**Helix creates essentially no clipping of its own: 298,058 samples sit at a 16-bit rail in Helix's output (as Player 2 builds it) against 298,043 in the float oracle (these 298,043 reproduce WP-03 exactly, as do the 297,131 float samples beyond +-1.0 and the 1.1935 peak). 298,008 samples are at the rail in both. At most 50 are Helix-only (0.017 %; none of them is more than 2 LSB inside the oracle's range) and 35 are oracle-only.** The rails are in the stream: a master pushed to 0 dBFS plus the codec's overshoot decodes above full scale, so the 16-bit conversion clips it, and any decoder with 16-bit output does the same. The question for D11 is therefore only where a negative gain is applied. **Applied before the window (a pre-synthesis gain), a gain of -1.54 dB or more removes every rail sample on the library, with an error against the ideal of at most 3 LSB (0.20 LSB RMS: the difference between the two decoders). Applied after the clamp, the same gain leaves 291,073 to 295,124 samples (depending on the gain) more than 2 LSB from the ideal, flat tops up to 5,314 LSB high (3.06 LSB RMS over the library at the estimated ReplayGain).** Recommendation: **D11 = pre-synthesis gain** (section 5).

## 2. Method

`tools/mp3_v2/rail_count.py` (library `analyse_file`, CLI over a folder; tests `sim/test_mp3_v2_rail_count.py`, in `make test-host`) runs three decoders per track:

1. **The float oracle** (WP-01, `tools/mp3_v2/oracle.py`, pristine minimp3, 1.0 = 16-bit full scale) for the peak, the samples beyond +-1.0, the 16-bit rails and a float dump used as the reference.
2. **`rail_xr.c`**: the same decoder compiled from a **probed copy of the header** (`build/mp3_v2/xr_probe/minimp3.h`: two insertions in `L3_decode`; the python side refuses to build unless removing them gives back the vendored file byte for byte, and the test checks it) for the largest `|xr|` after requantisation and MS/intensity, before reorder, antialias and IMDCT, and a histogram of the per-granule-channel maximum. Its float peak equals the oracle's (tested).
3. **`rail_count.c`**: Helix (the tree every consumer compiles, `tools/helix_tree.py`) **as Player 2 builds it: `TAU_POLY_FW=1 TAU_POLY_REQUIRED=1`, no software window**, the window unit replaced by its golden model (`sim/mp3_poly_model_core.h`, proven equal to the RTL by WP-02), one model instance per gain: instance 0 is the shipped decoder; instance k pushes every one of the 64 words of a slot multiplied by g_k and rounded to nearest, which is exactly where a decoder pre-gain acts (the window is linear; folding g into the requantiser instead changes the result by rounding only). Helix and the oracle align at lag 0 with the same sample count in all 59 files (no frame is skipped by either).

**Definitions** (per output sample, both channels; o16 = oracle float x 32768; rail = 16-bit value 32767 or -32768):

| Quantity | Meaning |
|---|---|
| Helix rail / oracle rail | the 16-bit output is at a rail (oracle: `o16 >= 32766.5` or `<= -32767.5`, minimp3's own conversion) |
| both | at a rail in both decoders: clipping inherent in the stream |
| **Helix only** | Helix at a rail, oracle not. **This is the decoder-created clipping estimate and it is an UPPER bound:** the two decoders differ by 1-2 LSB at ordinary samples (up to 4 LSB at 327 of 1.09 billion samples), so a sample at 32766.4 against 32766.6 falls on opposite sides of the line. |
| Helix only (2 LSB clear) | the same with the oracle at least 2 LSB inside the range: 0 in every track |
| **Oracle only** | oracle at a rail, Helix not (Helix lost a clip: a lower bound of the opposite error; 0 with a 2 LSB margin) |
| ideal(g) | `clamp16(round(o16 x g))`: what a perfect decoder with a pre-gain g produces |
| pre-gain error > 2 LSB | samples where Helix with pre-gain g differs from ideal(g) by more than 2 LSB (Helix and the oracle agree within 2 LSB on ordinary samples, so more is a defect, not rounding) |
| **post-clamp damage** | samples where `round(Helix_16bit x g)` differs from ideal(g) by more than 2 LSB: the cost of applying g AFTER the 16-bit clamp (the clipped tops stay flat; the second rounding adds at most 1 LSB and is below this threshold) |
| estimated ReplayGain | **[ESTIMATE]** `min(-18 LUFS - loudness, 0 dB)` with the integrated loudness of ITU-R BS.1770 (K-weighting and the two gates, written from the standard; the C code is tested against an independent implementation and a -3.01 LUFS reference sine). The library has **no ReplayGain tags** (only one test file carries them), so this stands in for the value Player 2 would read. The older ReplayGain 1.0 reference is about 4 dB louder (4 dB less attenuation): the tables give the loudness so the reader can shift it. |
| max window word | the largest `|word|` Helix pushes to the window unit (word = V x 2^23, WP-02; signed 32-bit range 2^31) |
| xr | minimp3 scales its dequantiser output by `2^(BITS_DEQUANTIZER_OUT)` = 1/2 against the standard (`minimp3.h` lines 80 and 732), so the standard's own scale is **twice** the printed minimp3 number [DERIVED]; 1.0 in the standard's scale is the PCM full scale |

Limits: sample peaks, not inter-sample (true) peaks; the oracle is minimp3 in float, not the ISO reference decoder (neither is bit-exact to the other; Helix against the oracle is within 2 LSB except 327 samples, 4 LSB at worst); no listening; the estimated ReplayGain is not the tag value.

## 3. Library totals [MEASURED]

| Item | Value |
|---|---|
| files / output samples (both channels) | 59 / 1,089,128,448 |
| files with a rail sample (Helix = oracle) | 36 / 36 |
| rail samples: Helix / oracle | 298,058 / 298,043 |
| float samples beyond +-1.0 / largest float peak | 297,131 / 1.1935 (+1.54 dB) |
| both at a rail / Helix only / oracle only | 298,008 / 50 / 35 (tracks with any Helix-only: 9; with any oracle-only: 9) |
| Helix-only with a 2 LSB margin / oracle-only with a 2 LSB margin | 0 / 0 |
| Helix against oracle: samples more than 1 LSB apart / more than 2 LSB / worst | 59,802 / 327 / 4 LSB |
| frames or samples differing between Helix and oracle, lag other than 0 | 0 files |
| largest spectral magnitude, minimp3 units / standard's scale | 0.7612 / 1.5225 (log2 = 0.61), in a folder-A track |
| largest window word | 11,209,632 = 2^23.42 (V = 1.336), 7.58 bits below the signed 32-bit limit |
| loudness range (BS.1770) | -26.3 to -6.5 LUFS (folder A, the hot masters: -13.6 to -8.4) |

By folder: A: 12 files, 12 with rails, 289,177 Helix rail samples (289,161 oracle), peak 1.1935; B: 12, 6, 2,149 (2,149), 1.0772; C: 11, 5, 27 (27), 1.0423; D: 13, 12, 6,702 (6,703), 1.0622; E: 7, 1, 3 (3), 1.0249; F: 4, 0, 0, 0.4786. (Folders match the WP-03 table: files and files-with-rails 12/12, 12/6, 11/5, 13/12, 7/1, 4/0.) Folder A holds 97 % of the rail samples; one track alone has 98,218.

## 4. The pre-gain against the post-clamp gain [MEASURED, whole library]

All counts are samples; the gains are applied to every track. Instances 1-4 are fixed gains, "no-clip" is the largest gain that keeps each track's own oracle peak off the rail (0 to -1.54 dB), "estimated ReplayGain" is section 2's estimate.

| Gain | Rail samples left (pre-gain) | Pre-gain error > 2 LSB (worst) | Post-clamp damage, samples > 2 LSB (files) | Post-clamp worst error (LSB) |
|---|---|---|---|---|
| 0 dB (the shipped decoder) | 298,058 | 327 (4) | not applicable | not applicable |
| -1 dB | 21 (6 files) | 104 (3) | 294,802 (36) | 3,563 |
| -3 dB | 0 | 6 (3) | 294,580 (36) | 4,491 |
| -6 dB | 0 | 0 (2) | 293,472 (36) | 3,179 |
| -9 dB | 0 | 0 (2) | 291,073 (36) | 2,251 |
| no-clip gain, per track | 0 | 99 (3) | 295,124 (36) | 5,314 |
| estimated ReplayGain (median of the 36 clipping tracks -5.4 dB, range -9.6 to 0) | 2 (1 track) | 2 (3) | 292,101 (35) | 3,112 |

RMS error against the ideal over the library: pre-gain 0.20 LSB (the two decoders' own difference); post-clamp 5.5 LSB at -3 dB and 3.06 LSB at the estimated ReplayGain. The post-clamp damage count is nearly independent of the gain (it is the number of clipped samples with a real overshoot): attenuating after the clamp scales the flat tops, it does not give them back. The pre-gain has no such floor: 35 of the 36 clipping tracks are completely clean at the estimated ReplayGain (the exception is the one quiet track, C1, whose estimated gain is 0 dB while its peak is 1.0025: two rail samples, which a gain of -0.02 dB removes).

## 5. Recommendation for decision D11 (where negative ReplayGain is applied)

**Apply min(ReplayGain, 0 dB) as a pre-synthesis gain inside the decoder (before the window), not after the 16-bit clamp.** The numbers:

- **Why a gain is needed at all, and why it cannot be done after the unit:** 36 of 59 files clip and 298,058 samples are on a rail, 99.98 % of them in the float reference too. The window unit clamps to 16 bits, so a gain applied to its output attenuates a waveform whose tops are already cut (292,101 samples left more than 2 LSB from the ideal at the estimated ReplayGain, up to 3,112 LSB, 3.06 LSB RMS).
- **What the pre-gain achieves:** every rail removed on 35 of the 36 clipping tracks at the estimated ReplayGain, all 298,058 at any fixed gain of -3 dB or more; error against the ideal within the decoder noise (0.20 LSB RMS; at most 3 LSB). The attenuation needed is small: **at most 1.54 dB** (peak 1.1935), so any negative ReplayGain of that size or more clears the track; of the 36 clipping tracks only C1 has an estimated gain (0 dB) below its need (-0.02 dB).
- **What it costs:** [DERIVED] if the gain is a Q15 multiply of the 64 words per slot it is 36 x 64 = 2,304 multiplies per stereo frame, which the review put at about 1 % of the CPU (that is the exact-multiply form run here on the host; its cost on the Pocket was not measured). A cheaper form is to fold the gain into the requantiser exponent: the dequantiser already applies `2^((global_gain - 210)/4)`, so a gain in steps of 2^(1/4) = 1.505 dB costs nothing per sample, rounded toward more attenuation so the no-clip condition holds (an extra Q15 trim covers the remainder at one multiply per line). That folded form was not run in this report (the measured pre-gain is the exact-multiply form); it is a design note for WP-12, not a result.
- **When it does not apply:** ReplayGain of 0 dB or a boost (positive gain) stays after the clamp in the output stage: a boost cannot undo clipping and the pre-gain mechanism (default unity) is not involved. Tracks that need less attenuation than the track's own gain asks for are not the decoder's concern.
- **Decoder-created clipping (U3):** at most 50 samples in 298,058 (0.017 %), an upper bound, 0 with a 2 LSB margin: the integer pipeline is not the source of the clipping and the case for pre-gain does not depend on Helix's accuracy.
- **Upper bounds and caveats:** Helix-only, oracle-only, and every count of "damage" are bounded by the 2 LSB threshold and the two decoders' agreement; the estimated ReplayGain replaces tags the library does not have, so the fraction of tracks that would really receive a gain large enough is [ESTIMATE]; sample peaks understate the true peak; nothing was listened to (the audibility of 0.027 % of samples clipped by up to 19 % of full scale is not measured; folder A is 97 % of them); the pre-gain mechanism "ships either way" as D-C02 states, so this recommendation fixes only what the firmware does with it.

## 6. Spectral headroom and the window-word range (input to WP-12) [MEASURED]

Largest `|xr|` after requantisation and stereo processing over the library: **0.7612 in minimp3 units = 1.5225 in the standard's scale** (log2 0.61). Histogram of the per-granule-channel maximum over all 1,890,848 granule-channels of the library (bucket e: `2^(e-20) <= max < 2^(e-19)` in minimp3 units; the standard's scale is twice): the granule maxima concentrate in `2^-5` to `2^-3` (buckets 15-17: 1,101,349 granule-channels, 58.2 %); 99.96 % are below `2^-1` (0.5 in minimp3 units, 1.0 in the standard's scale); 716 granule-channels (0.038 %) lie between 0.5 and 1.0 in minimp3 units and none reaches 1.0. 17,088 (0.9 %) are below `2^-20` (silence or near it). So **a spectrum format needs one integer bit above 1.0 in the standard's scale (range +-2 holds the library with 1.3x on the largest), and the quiet end sets the precision side: 12.6 % of granule-channels have a maximum below `2^-7` in minimp3 units and 0.9 % are below `2^-20`.** This is the library's maximum, not the format's bound: the largest value a stream can code is far higher (8,206^(4/3) times the gain term), and WP-12 must still choose a saturation rule and a counter for it.

Largest window word 11,209,632 (V = 1.336 in the unit's scale): **7.58 bits below the signed 32-bit limit**; the quietest music tracks have 1.9 bits more (9.5 bits). WP-02's stimulus maximum (2^27.3, loud random bands) is a synthetic bound, 4 bits above the library maximum.

| Histogram bucket | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Granule-channels | 17,088 | 379 | 436 | 843 | 1,560 | 3,808 | 8,246 | 10,374 | 16,998 | 15,102 | 22,213 | 44,713 | 95,612 | 188,264 | 349,772 | 511,845 | 457,918 | 131,586 | 13,375 | 716 |

## 7. Per-track tables (counts only) [MEASURED]

Table 1: rail counts. Table 2: spectral and window-word headroom (the no-clip gain is the attenuation that keeps this track's oracle peak off the rail). Table 3: fixed pre-gains and the damage of applying them after the clamp. Table 4: estimated ReplayGain (loudness, the gain, what the pre-gain leaves, what the post-clamp gain leaves).


| Track | Frames | Float peak | Float samples > 1.0 | Helix rail samples | Oracle rail samples | Both | Helix only | Helix only (2 LSB clear) | Oracle only | Frames/lag differ | Samples off by > 2 LSB |
|---|---|---|---|---|---|---|---|---|---|---|---|
| A1 | 7,935 | 1.1080 | 19,410 | 19,484 | 19,485 | 19,482 | 2 | 0 | 3 | no | 1 |
| A2 | 6,304 | 1.1274 | 16,893 | 16,927 | 16,925 | 16,924 | 3 | 0 | 1 | no | 7 |
| A3 | 9,528 | 1.1520 | 97,995 | 98,218 | 98,215 | 98,204 | 14 | 0 | 11 | no | 90 |
| A4 | 9,249 | 1.0624 | 120 | 121 | 121 | 121 | 0 | 0 | 0 | no | 0 |
| A5 | 6,722 | 1.1802 | 1,164 | 1,165 | 1,165 | 1,165 | 0 | 0 | 0 | no | 0 |
| A6 | 6,430 | 1.1039 | 4,887 | 4,928 | 4,927 | 4,926 | 2 | 0 | 1 | no | 2 |
| A7 | 7,643 | 1.1402 | 22,490 | 22,583 | 22,582 | 22,582 | 1 | 0 | 0 | no | 2 |
| A8 | 13,785 | 1.1935 | 17,587 | 17,645 | 17,641 | 17,640 | 5 | 0 | 1 | no | 1 |
| A9 | 11,658 | 1.0751 | 6,684 | 6,709 | 6,709 | 6,708 | 1 | 0 | 1 | no | 0 |
| A10 | 14,544 | 1.1295 | 85,042 | 85,307 | 85,302 | 85,288 | 19 | 0 | 14 | no | 178 |
| A11 | 10,899 | 1.0248 | 658 | 659 | 659 | 659 | 0 | 0 | 0 | no | 0 |
| A12 | 12,258 | 1.1008 | 15,376 | 15,431 | 15,430 | 15,428 | 3 | 0 | 2 | no | 2 |
| B1 | 7,423 | 0.9338 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| B2 | 10,269 | 1.0459 | 434 | 435 | 435 | 435 | 0 | 0 | 0 | no | 4 |
| B3 | 8,329 | 1.0772 | 599 | 602 | 602 | 602 | 0 | 0 | 0 | no | 6 |
| B4 | 13,203 | 1.0408 | 480 | 481 | 481 | 481 | 0 | 0 | 0 | no | 4 |
| B5 | 11,660 | 0.6947 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| B6 | 6,996 | 1.0360 | 542 | 547 | 547 | 547 | 0 | 0 | 0 | no | 8 |
| B7 | 13,478 | 0.8157 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| B8 | 12,145 | 1.0041 | 34 | 34 | 34 | 34 | 0 | 0 | 0 | no | 4 |
| B9 | 8,055 | 1.0373 | 50 | 50 | 50 | 50 | 0 | 0 | 0 | no | 0 |
| B10 | 4,042 | 0.6290 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| B11 | 9,956 | 0.5730 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| B12 | 6,186 | 0.5518 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| C1 | 7,277 | 1.0025 | 2 | 2 | 2 | 2 | 0 | 0 | 0 | no | 0 |
| C2 | 9,009 | 0.8637 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| C3 | 9,492 | 1.0123 | 2 | 2 | 2 | 2 | 0 | 0 | 0 | no | 0 |
| C4 | 12,722 | 0.9193 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| C5 | 11,084 | 0.9862 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| C6 | 9,943 | 0.7658 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| C7 | 8,379 | 1.0423 | 7 | 7 | 7 | 7 | 0 | 0 | 0 | no | 0 |
| C8 | 8,777 | 1.0080 | 2 | 2 | 2 | 2 | 0 | 0 | 0 | no | 0 |
| C9 | 6,248 | 0.5966 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| C10 | 9,951 | 1.0274 | 14 | 14 | 14 | 14 | 0 | 0 | 0 | no | 0 |
| C11 | 4,382 | 0.4836 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| D1 | 10,676 | 1.0229 | 85 | 86 | 86 | 86 | 0 | 0 | 0 | no | 0 |
| D2 | 5,944 | 1.0606 | 710 | 714 | 714 | 714 | 0 | 0 | 0 | no | 0 |
| D3 | 7,404 | 1.0267 | 4 | 4 | 4 | 4 | 0 | 0 | 0 | no | 0 |
| D4 | 7,329 | 1.0605 | 1,935 | 1,944 | 1,944 | 1,944 | 0 | 0 | 0 | no | 3 |
| D5 | 7,971 | 1.0314 | 421 | 427 | 427 | 427 | 0 | 0 | 0 | no | 0 |
| D6 | 7,388 | 1.0479 | 901 | 904 | 904 | 904 | 0 | 0 | 0 | no | 5 |
| D7 | 3,795 | 1.0066 | 14 | 14 | 14 | 14 | 0 | 0 | 0 | no | 0 |
| D8 | 5,885 | 0.8200 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| D9 | 8,827 | 1.0622 | 1,372 | 1,385 | 1,385 | 1,385 | 0 | 0 | 0 | no | 10 |
| D10 | 2,968 | 1.0278 | 286 | 290 | 290 | 290 | 0 | 0 | 0 | no | 0 |
| D11 | 7,980 | 1.0143 | 24 | 24 | 24 | 24 | 0 | 0 | 0 | no | 0 |
| D12 | 6,702 | 1.0134 | 71 | 72 | 72 | 72 | 0 | 0 | 0 | no | 0 |
| D13 | 8,728 | 1.0270 | 833 | 838 | 839 | 838 | 0 | 0 | 1 | no | 0 |
| E1 | 6,694 | 1.0249 | 3 | 3 | 3 | 3 | 0 | 0 | 0 | no | 0 |
| E2 | 6,694 | 0.9731 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| E3 | 7,285 | 0.9866 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| E4 | 6,379 | 0.3747 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| E5 | 6,379 | 0.3936 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| E6 | 6,943 | 0.3936 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| E7 | 12,652 | 0.9995 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| F1 | 1,151 | 0.0000 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| F2 | 463 | 0.4784 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| F3 | 461 | 0.4786 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |
| F4 | 6,379 | 0.3936 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | 0 |

| Track | max abs xr (minimp3 units) | xr in the standard's scale (x2) | log2 of that | max abs window word | word / 2^31 | word headroom (bits) | no-clip gain (dB) |
|---|---|---|---|---|---|---|---|
| A1 | 0.4883 | 0.9766 | -0.03 | 10,616,308 | 0.0049 | 7.66 | -0.89 |
| A2 | 0.5571 | 1.1142 | 0.16 | 10,272,740 | 0.0048 | 7.71 | -1.04 |
| A3 | 0.7612 | 1.5225 | 0.61 | 11,115,532 | 0.0052 | 7.59 | -1.23 |
| A4 | 0.4092 | 0.8184 | -0.29 | 8,361,184 | 0.0039 | 8.00 | -0.53 |
| A5 | 0.4212 | 0.8424 | -0.25 | 9,687,493 | 0.0045 | 7.79 | -1.44 |
| A6 | 0.4858 | 0.9716 | -0.04 | 9,869,732 | 0.0046 | 7.77 | -0.86 |
| A7 | 0.5414 | 1.0828 | 0.11 | 11,001,362 | 0.0051 | 7.61 | -1.14 |
| A8 | 0.5000 | 0.9999 | -0.00 | 10,247,768 | 0.0048 | 7.71 | -1.54 |
| A9 | 0.3552 | 0.7103 | -0.49 | 10,250,038 | 0.0048 | 7.71 | -0.63 |
| A10 | 0.6364 | 1.2727 | 0.35 | 11,209,632 | 0.0052 | 7.58 | -1.06 |
| A11 | 0.3965 | 0.7930 | -0.33 | 9,227,942 | 0.0043 | 7.86 | -0.21 |
| A12 | 0.5444 | 1.0888 | 0.12 | 10,462,042 | 0.0049 | 7.68 | -0.83 |
| B1 | 0.4450 | 0.8900 | -0.17 | 7,896,032 | 0.0037 | 8.09 | 0.00 |
| B2 | 0.5694 | 1.1388 | 0.19 | 9,347,035 | 0.0044 | 7.84 | -0.39 |
| B3 | 0.6223 | 1.2446 | 0.32 | 9,145,556 | 0.0043 | 7.88 | -0.65 |
| B4 | 0.6398 | 1.2796 | 0.36 | 9,324,048 | 0.0043 | 7.85 | -0.35 |
| B5 | 0.2342 | 0.4685 | -1.09 | 5,603,387 | 0.0026 | 8.58 | 0.00 |
| B6 | 0.5851 | 1.1702 | 0.23 | 9,285,740 | 0.0043 | 7.85 | -0.31 |
| B7 | 0.4495 | 0.8989 | -0.15 | 6,740,006 | 0.0031 | 8.32 | 0.00 |
| B8 | 0.6635 | 1.3269 | 0.41 | 9,771,470 | 0.0046 | 7.78 | -0.04 |
| B9 | 0.4332 | 0.8664 | -0.21 | 9,502,982 | 0.0044 | 7.82 | -0.32 |
| B10 | 0.2109 | 0.4219 | -1.25 | 4,823,228 | 0.0022 | 8.80 | 0.00 |
| B11 | 0.2223 | 0.4446 | -1.17 | 4,306,774 | 0.0020 | 8.96 | 0.00 |
| B12 | 0.2021 | 0.4042 | -1.31 | 4,284,738 | 0.0020 | 8.97 | 0.00 |
| C1 | 0.2792 | 0.5583 | -0.84 | 6,119,622 | 0.0028 | 8.45 | -0.02 |
| C2 | 0.2792 | 0.5583 | -0.84 | 6,827,194 | 0.0032 | 8.30 | 0.00 |
| C3 | 0.2089 | 0.4178 | -1.26 | 7,975,871 | 0.0037 | 8.07 | -0.11 |
| C4 | 0.2341 | 0.4681 | -1.09 | 7,194,880 | 0.0034 | 8.22 | 0.00 |
| C5 | 0.2102 | 0.4204 | -1.25 | 6,548,291 | 0.0030 | 8.36 | 0.00 |
| C6 | 0.2196 | 0.4393 | -1.19 | 6,499,275 | 0.0030 | 8.37 | 0.00 |
| C7 | 0.2086 | 0.4171 | -1.26 | 9,423,686 | 0.0044 | 7.83 | -0.36 |
| C8 | 0.2297 | 0.4593 | -1.12 | 7,866,128 | 0.0037 | 8.09 | -0.07 |
| C9 | 0.2138 | 0.4276 | -1.23 | 5,075,720 | 0.0024 | 8.72 | 0.00 |
| C10 | 0.3532 | 0.7065 | -0.50 | 8,322,452 | 0.0039 | 8.01 | -0.23 |
| C11 | 0.1265 | 0.2530 | -1.98 | 3,535,803 | 0.0016 | 9.25 | 0.00 |
| D1 | 0.3821 | 0.7642 | -0.39 | 9,012,209 | 0.0042 | 7.90 | -0.20 |
| D2 | 0.3863 | 0.7725 | -0.37 | 8,989,680 | 0.0042 | 7.90 | -0.51 |
| D3 | 0.3204 | 0.6408 | -0.64 | 7,779,850 | 0.0036 | 8.11 | -0.23 |
| D4 | 0.4496 | 0.8992 | -0.15 | 9,400,916 | 0.0044 | 7.84 | -0.51 |
| D5 | 0.3833 | 0.7665 | -0.38 | 9,335,321 | 0.0043 | 7.85 | -0.27 |
| D6 | 0.4502 | 0.9005 | -0.15 | 9,427,498 | 0.0044 | 7.83 | -0.41 |
| D7 | 0.3149 | 0.6298 | -0.67 | 8,167,330 | 0.0038 | 8.04 | -0.06 |
| D8 | 0.2170 | 0.4340 | -1.20 | 6,823,809 | 0.0032 | 8.30 | 0.00 |
| D9 | 0.5450 | 1.0900 | 0.12 | 9,479,736 | 0.0044 | 7.82 | -0.52 |
| D10 | 0.3829 | 0.7657 | -0.39 | 8,561,464 | 0.0040 | 7.97 | -0.24 |
| D11 | 0.3092 | 0.6184 | -0.69 | 9,020,260 | 0.0042 | 7.90 | -0.12 |
| D12 | 0.2634 | 0.5268 | -0.92 | 8,658,936 | 0.0040 | 7.95 | -0.12 |
| D13 | 0.3498 | 0.6996 | -0.52 | 9,358,817 | 0.0044 | 7.84 | -0.23 |
| E1 | 0.2821 | 0.5643 | -0.83 | 7,887,475 | 0.0037 | 8.09 | -0.21 |
| E2 | 0.3013 | 0.6025 | -0.73 | 8,376,332 | 0.0039 | 8.00 | 0.00 |
| E3 | 0.2661 | 0.5321 | -0.91 | 8,637,424 | 0.0040 | 7.96 | 0.00 |
| E4 | 0.1535 | 0.3069 | -1.70 | 2,979,618 | 0.0014 | 9.49 | 0.00 |
| E5 | 0.1612 | 0.3224 | -1.63 | 3,110,858 | 0.0014 | 9.43 | 0.00 |
| E6 | 0.1349 | 0.2698 | -1.89 | 3,102,426 | 0.0014 | 9.44 | 0.00 |
| E7 | 0.3261 | 0.6521 | -0.62 | 8,840,854 | 0.0041 | 7.92 | 0.00 |
| F1 | 0.0000 | 0.0000 | -29.90 | 0 | 0.0000 | 31.00 | 0.00 |
| F2 | 0.2640 | 0.5280 | -0.92 | 4,211,844 | 0.0020 | 8.99 | 0.00 |
| F3 | 0.2844 | 0.5688 | -0.81 | 4,513,864 | 0.0021 | 8.89 | 0.00 |
| F4 | 0.1612 | 0.3224 | -1.63 | 3,110,858 | 0.0014 | 9.43 | 0.00 |

| Track | Rails at -3 dB pre-gain | Post-clamp damage at -3 dB (samples off by > 2 LSB) | Post-clamp damage at -6 dB | Post-clamp damage at -9 dB | Pre-gain error > 2 LSB at -6 dB | Rails at no-clip pre-gain | Post-clamp damage at the no-clip gain |
|---|---|---|---|---|---|---|---|
| A1 | 0 | 19,252 | 19,179 | 19,032 | 0 | 0 | 19,269 |
| A2 | 0 | 16,738 | 16,661 | 16,515 | 0 | 0 | 16,776 |
| A3 | 0 | 97,412 | 97,143 | 96,568 | 0 | 0 | 97,530 |
| A4 | 0 | 118 | 116 | 114 | 0 | 0 | 118 |
| A5 | 0 | 1,155 | 1,151 | 1,142 | 0 | 0 | 1,158 |
| A6 | 0 | 4,808 | 4,773 | 4,706 | 0 | 0 | 4,814 |
| A7 | 0 | 22,291 | 22,206 | 21,994 | 0 | 0 | 22,310 |
| A8 | 0 | 17,431 | 17,351 | 17,200 | 0 | 0 | 17,476 |
| A9 | 0 | 6,629 | 6,603 | 6,541 | 0 | 0 | 6,642 |
| A10 | 0 | 84,195 | 83,839 | 83,089 | 0 | 0 | 84,339 |
| A11 | 0 | 650 | 645 | 639 | 0 | 0 | 652 |
| A12 | 0 | 15,206 | 15,149 | 15,003 | 0 | 0 | 15,242 |
| B1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B2 | 0 | 427 | 425 | 417 | 0 | 0 | 440 |
| B3 | 0 | 593 | 589 | 586 | 0 | 0 | 603 |
| B4 | 0 | 471 | 471 | 462 | 0 | 0 | 479 |
| B5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B6 | 0 | 534 | 533 | 526 | 0 | 0 | 544 |
| B7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B8 | 0 | 34 | 34 | 34 | 0 | 0 | 47 |
| B9 | 0 | 50 | 50 | 50 | 0 | 0 | 50 |
| B10 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B11 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| B12 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C1 | 0 | 2 | 2 | 2 | 0 | 0 | 2 |
| C2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C3 | 0 | 2 | 2 | 2 | 0 | 0 | 2 |
| C4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C6 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C7 | 0 | 7 | 7 | 7 | 0 | 0 | 7 |
| C8 | 0 | 2 | 2 | 2 | 0 | 0 | 2 |
| C9 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| C10 | 0 | 14 | 14 | 14 | 0 | 0 | 14 |
| C11 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| D1 | 0 | 85 | 85 | 85 | 0 | 0 | 85 |
| D2 | 0 | 709 | 708 | 704 | 0 | 0 | 711 |
| D3 | 0 | 4 | 4 | 4 | 0 | 0 | 4 |
| D4 | 0 | 1,913 | 1,910 | 1,882 | 0 | 0 | 1,922 |
| D5 | 0 | 413 | 407 | 403 | 0 | 0 | 419 |
| D6 | 0 | 887 | 883 | 872 | 0 | 0 | 898 |
| D7 | 0 | 14 | 14 | 13 | 0 | 0 | 14 |
| D8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| D9 | 0 | 1,345 | 1,334 | 1,309 | 0 | 0 | 1,359 |
| D10 | 0 | 277 | 277 | 271 | 0 | 0 | 279 |
| D11 | 0 | 24 | 24 | 24 | 0 | 0 | 24 |
| D12 | 0 | 70 | 69 | 68 | 0 | 0 | 70 |
| D13 | 0 | 815 | 809 | 790 | 0 | 0 | 820 |
| E1 | 0 | 3 | 3 | 3 | 0 | 0 | 3 |
| E2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| E3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| E4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| E5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| E6 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| E7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| F1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| F2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| F3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| F4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

| Track | Loudness (LUFS) | Estimated ReplayGain 2.0 (dB, capped at 0) | Rails at that pre-gain | Pre-gain error > 2 LSB | Post-clamp damage (> 2 LSB) | Post-clamp rails | Gain needed to clear the rails (dB) |
|---|---|---|---|---|---|---|---|
| A1 | -8.4 | -9.6 | 0 | 0 | 18,988 | 0 | -0.89 |
| A2 | -8.7 | -9.3 | 0 | 0 | 16,538 | 0 | -1.04 |
| A3 | -9.7 | -8.3 | 0 | 0 | 96,756 | 0 | -1.23 |
| A4 | -13.6 | -4.4 | 0 | 0 | 117 | 0 | -0.53 |
| A5 | -12.4 | -5.6 | 0 | 0 | 1,155 | 0 | -1.44 |
| A6 | -9.6 | -8.4 | 0 | 0 | 4,709 | 0 | -0.86 |
| A7 | -9.9 | -8.1 | 0 | 0 | 22,068 | 0 | -1.14 |
| A8 | -8.6 | -9.4 | 0 | 0 | 17,236 | 0 | -1.54 |
| A9 | -9.0 | -9.0 | 0 | 0 | 6,560 | 0 | -0.63 |
| A10 | -10.3 | -7.7 | 0 | 0 | 83,675 | 0 | -1.06 |
| A11 | -11.1 | -6.9 | 0 | 0 | 646 | 0 | -0.21 |
| A12 | -9.2 | -8.8 | 0 | 0 | 15,009 | 0 | -0.83 |
| B1 | -17.1 | -0.9 | 0 | 0 | 0 | 0 | 0.00 |
| B2 | -16.8 | -1.2 | 0 | 0 | 435 | 0 | -0.39 |
| B3 | -16.9 | -1.1 | 0 | 2 | 596 | 0 | -0.65 |
| B4 | -15.0 | -3.0 | 0 | 0 | 471 | 0 | -0.35 |
| B5 | -17.9 | -0.1 | 0 | 0 | 0 | 0 | 0.00 |
| B6 | -13.8 | -4.2 | 0 | 0 | 534 | 0 | -0.31 |
| B7 | -19.9 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| B8 | -17.4 | -0.6 | 0 | 0 | 48 | 0 | -0.04 |
| B9 | -13.7 | -4.3 | 0 | 0 | 50 | 0 | -0.32 |
| B10 | -19.0 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| B11 | -21.4 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| B12 | -20.3 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| C1 | -21.8 | 0.0 | 2 | 0 | 0 | 2 | -0.02 |
| C2 | -14.0 | -4.0 | 0 | 0 | 0 | 0 | 0.00 |
| C3 | -13.0 | -5.0 | 0 | 0 | 2 | 0 | -0.11 |
| C4 | -20.3 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| C5 | -17.7 | -0.3 | 0 | 0 | 0 | 0 | 0.00 |
| C6 | -18.3 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| C7 | -15.1 | -2.9 | 0 | 0 | 7 | 0 | -0.36 |
| C8 | -13.0 | -5.0 | 0 | 0 | 2 | 0 | -0.07 |
| C9 | -20.0 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| C10 | -14.3 | -3.7 | 0 | 0 | 14 | 0 | -0.23 |
| C11 | -26.0 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| D1 | -13.2 | -4.8 | 0 | 0 | 85 | 0 | -0.20 |
| D2 | -11.3 | -6.7 | 0 | 0 | 708 | 0 | -0.51 |
| D3 | -13.3 | -4.7 | 0 | 0 | 4 | 0 | -0.23 |
| D4 | -9.4 | -8.6 | 0 | 0 | 1,894 | 0 | -0.51 |
| D5 | -10.1 | -7.9 | 0 | 0 | 403 | 0 | -0.27 |
| D6 | -11.4 | -6.6 | 0 | 0 | 878 | 0 | -0.41 |
| D7 | -11.9 | -6.1 | 0 | 0 | 14 | 0 | -0.06 |
| D8 | -16.1 | -1.9 | 0 | 0 | 0 | 0 | 0.00 |
| D9 | -12.6 | -5.4 | 0 | 0 | 1,331 | 0 | -0.52 |
| D10 | -12.5 | -5.5 | 0 | 0 | 275 | 0 | -0.24 |
| D11 | -14.2 | -3.8 | 0 | 0 | 24 | 0 | -0.12 |
| D12 | -13.0 | -5.0 | 0 | 0 | 69 | 0 | -0.12 |
| D13 | -10.0 | -8.0 | 0 | 0 | 797 | 0 | -0.23 |
| E1 | -15.6 | -2.4 | 0 | 0 | 3 | 0 | -0.21 |
| E2 | -15.2 | -2.8 | 0 | 0 | 0 | 0 | 0.00 |
| E3 | -15.2 | -2.8 | 0 | 0 | 0 | 0 | 0.00 |
| E4 | -26.3 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| E5 | -25.9 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| E6 | -25.9 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| E7 | -20.7 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| F1 | -70.0 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |
| F2 | -6.5 | -11.5 | 0 | 0 | 0 | 0 | 0.00 |
| F3 | -6.5 | -11.5 | 0 | 0 | 0 | 0 | 0.00 |
| F4 | -25.9 | 0.0 | 0 | 0 | 0 | 0 | 0.00 |

## 8. Reproduce

```
SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk python3 tools/mp3_v2/rail_count.py [--json out.json] [--markdown out.md] [PATH ...]
```

Without a PATH it uses the owner's known library locations (13 seconds for the 59 files on a 14-core Mac). `sim/test_mp3_v2_rail_count.py` (31 checks, in `make test-host`) covers generated streams: the exact scaling of the spectral maximum and the window words between two global-gain values, rails in both decoders, pre-gain against post-clamp, mono through the unit, the loudness against an independent implementation, the probed header, and red checks (a wrong reference, a missing probe anchor, a mutated loudness).
