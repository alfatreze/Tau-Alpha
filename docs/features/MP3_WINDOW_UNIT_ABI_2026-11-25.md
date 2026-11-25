# MP3 window unit: the interface, specified from the standard (2026-11-25)

**Status: specification plus an executable proof (work package WP-02 of `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`, section 3.4).** No firmware, RTL, fit or card was touched. The proof is `tools/mp3_v2/window_abi_check.py`, run by `sim/test_mp3_v2_window_abi.py` in `make test-host`.
**Evidence labels.** **[MEASURED]** run in this session on the host: the unit's golden model (`sim/mp3_poly_model_core.h`, compiled through `tools/mp3_v2/window_model_cli.c`), the RTL `src/fpga/core/tau_mp3_poly.sv` under iverilog (`tools/mp3_v2/window_abi_tb.v`), and a float64 implementation of the standard's synthesis filter. **[READ]** source read. **[DERIVED]** derived here and checked by the named test. Nothing here ran on a Pocket.
**Sources read:** `src/fpga/core/tau_mp3_poly.sv`, `tau_mp3_poly_rom.svh`, `sim/mp3_poly_model.c`, `mp3_poly_model_core.h`, `mp3_poly_map.c`, `fw2/decode/poly_hw.c`, `fw/mp3_poly_hw.h`, `tools/mp3_window_derive.py`, `tools/gen_mp3_poly_rom.py`, `docs/features/MP3_WINDOW_TABLE_PROVENANCE_2026-11-25.md`, and the mono change (branch `claude/mono-poly-unit-d49ef6`, commits a302d6c4 and 03d0475c; it is merged into main as of this work: `third_party/libhelix-mp3/tau.patch`, `sim/test_mp3_poly_mono.py`). The standard itself was not available: its formulas below are the ones the repository's own derivation (`tools/mp3_window_derive.py`, B-1168) and the window data file already rely on, written out in float64 and tested; the text of ISO/IEC 11172-3 was not re-read (decision D14 of the plan).

## 1. Result in one paragraph

For every slot (32 subband samples S[k] of one channel, in the standard's units where 1.0 produces a full-scale 16-bit output) the unit expects **32 words**, `word[P] = round(2^23 * X[m(P)])`, where `X[m] = sum_k S[k] cos(m(2k+1)pi/64)` is the unnormalised 32-point DCT-II and `m(P)` is the unit's push order (P0 = X[0], P1..P16 = X[16..31], P17..P31 = X[15..1]). The unit then outputs the standard's synthesis filterbank result times 32768, rounded half up and clipped to 16 bits. **The plan's word scale 2^23 is confirmed** by 41,472 samples (648 slots x 2 channels x 32) against the float64 standard: maximum difference 1 LSB, and equal everywhere except within 0.0053 LSB of a rounding tie (202 samples; the bound is the rounding of the words themselves, derived in section 5) **[MEASURED]**. Mono pushes the one channel's 32 words as both channels; the right half then equals the left in every slot.

## 2. The standard's synthesis filterbank (what is being implemented)

Per slot, for one channel [DERIVED from the formulas the repository uses; implemented as `StdSynth` in the check]:
1. Shift the 1,024-entry vector V by 64 (V[i] = V[i-64], i = 1023..64).
2. Matrix the 32 new samples: `V[i] = sum_{k=0..31} cos((16+i)(2k+1)pi/64) S[k]`, i = 0..63.
3. Build `U[64i+j] = V[128i+j]` and `U[64i+32+j] = V[128i+96+j]`, i = 0..7, j = 0..31.
4. Window and sum: `out[j] = sum_{i=0..15} U[j+32i] D[j+32i]`, j = 0..31 (the 32 output samples of the slot, in time order).
5. The 16-bit sample is `out[j] * 32768`, rounded and clipped.

`D[i]` is Table 3-B.3 (`tools/data/mp3_synthesis_window.txt`, 512 values that are exact multiples of 2^-16; peak D[256] = 75038/65536 = 1.144989014; provenance in `docs/features/MP3_WINDOW_TABLE_PROVENANCE_2026-11-25.md`). This check trusts that data file; the file's own verification is `sim/test_mp3_poly_derive.py`.

## 3. The 32 unique words and the push order

With `X[m] = sum_k S[k] cos(m(2k+1)pi/64)`, V[n] = X[16+n] for n = 0..63, and X extended by the cosine symmetries (X[32] = 0, X[64-q] = -X[q], X[64+q] = -X[q]). The check finds, numerically, for every n which pushed word (and sign) V[n] equals, and compares the result with this closed form: all 64 agree **[MEASURED]**.

| V index n | equals | push index P |
|---|---|---|
| 0..15 | +X[16..31] | P1..P16 (P = n + 1) |
| 16 | 0 | none |
| 17..31 | -V[32-n] = -X[31..17] | P16..P2 (P = 33 - n), sign -1 |
| 32 | -V[0] = -X[16] | P1, sign -1 |
| 33..47 | -X[15..1] | P17..P31 (P = n - 16), sign -1 |
| 48 | -X[0] | P0, sign -1 |
| 49..63 | V[96-n] = -X[1..15] | P = 80 - n (P31..P17), sign -1 |

Plan claims, **confirmed**: V[32-i] = -V[i] for i = 0..16 (so V[16] = 0); V[96-i] = V[i] for i = 33..63; the unique set is V[0..15] and V[33..48] (32 words); V[48] = -X[0] (B-1168's "row 16 uses V[48] = -X[0]"). The 63 non-zero V entries use exactly the 32 push indices (P0 once, the others twice or more).

**Push order (an ABI of the hardware, not of the standard).** The unit's ROM (`tau_mp3_poly_rom.svh`, generated from the standard-derived tables; unchanged) addresses the history by (age, P) with P0 = X[0], P1..P16 = X[16..31], P17..P31 = X[15..1] (the order Helix's FDCT32 final stage produced). **Recommendation kept: do not change the order** (a different order is a ROM change and a fit). The decoder's DCT32 produces the 32 numbers X[0..31] and stores them at index P = {0 for m = 0; m - 15 for m = 16..31; 32 - m for m = 1..15}. If the numerics review (C3, question 5) finds the order costs DCT32 instructions, that is the only trigger for a fit.

## 4. The word scale (numeric proof)

Arithmetic of the unit [READ RTL and model, confirmed by the integer model in the check]: coefficient `c = 4 * D[i] * 65536 = D * 2^18` (ROM, up to 300,152 = 1.145 * 2^18); sum `S = 2^25 + sum(w * c)` in 64 bits; `out = clip16( (int32)(S >> 20) >> 6 )`, i.e. `out = clip16( floor( (sum(w c) + 2^25) / 2^26 ) )`. Therefore `out = floor( sum(w D) / 2^8 + 1/2 )`. The standard's output times 32768 is `32768 * sum(V D)`. They are equal for **w = V * 32768 * 256 = V * 2^23**. Measured proof: the check pushes `round(2^23 X[m(P)])` for 648 slots of random (quiet, normal, full-scale), impulse, alternating-extreme, swept and silent subband samples (16,136 of the 41,472 output samples are clipped, so the saturation path is exercised) and compares the model's PCM with `floor(32768 * out_standard + 1/2)` clipped: max |difference| = 1; exact in 41,448 samples; the 24 differing samples are all within 0.0053 LSB of a rounding tie **[MEASURED]**. The RTL, run in iverilog on 84 slots (random, +-2^31 extreme and ABI words), matches the integer model word for word.
**Largest word seen** in the stimulus: 169,250,368 = 2^27.3 (loud random full-scale bands). The legal range is the signed 32-bit word: |V| up to 256.

Red cases (the test fails if the ABI is changed, `sim/test_mp3_v2_window_abi.py`): scale 2^22, scale 2^24, natural order X[0..31] instead of the push order, sign of P17..P31 negated, history one slot late: each is detected by three checks.

## 5. Rounding, saturation, wrap

- **Rounding:** exact integer sum, add 2^25, arithmetic shift right by 26: **round half up** (a tie goes toward +infinity; negative ties too: -0.5 -> 0). Engineered ties are tested (k = -2, -1, 1, 2). minimp3's float path rounds ties away from zero for negatives; the two differ only on exact ties and by 1 LSB.
- **Saturation:** symmetric 16-bit rails, +32767 and -32768.
- **Wrap (the plan's "64-bit sum below 2^51"):** the unit keeps bits 51:20 of the 64-bit sum as a signed 32-bit value (the model's `(int)(sum >> 20)`). The sum wraps only if |sum| reaches 2^51. The largest sum of absolute coefficients per output is 715,784 = 2.73 * 2^18 **[MEASURED]**, so with 32-bit words |sum| < 2^31 * 715,784 + 2^25 = 0.68 * 2^51. **Correction to the plan:** no wrap is possible for any 32-bit word values; the only wrap risk is in the decoder's own conversion of its DCT accumulator to the int32 word, which must saturate at +-(2^31-1) (and count) instead of truncating. The model and the integer model agree bit for bit on random words and +-2^31 extremes (120 slots).
- **Error budget for the DCT32 [DERIVED, MEASURED bound]:** an error of e words (in w units) in every word changes an output by at most `e * 0.0107` LSB (twice the 0.0053 bound of rounding to integers, because the bound is 0.5 * max sum|D| / 256 per word of rounding error). A DCT with relative error 2^-20 of V (8 units at the loudest measured words) costs under 0.1 LSB. The words must be rounded (round to nearest), not truncated: truncation biases every output by up to 0.011 LSB.

## 6. Protocol and registers (what the firmware must do) [READ, MEASURED in RTL]

Registers (`src/fpga/core/tau_soc.v`, `fw2/decode/poly_hw.c`): `0x80000100` CTL (bit 0 clear, bit 1 go), `0x80000104` PUSH, `0x80000108` IDX, `0x8000010C` OUT `{R, L}` 16 bits each, `0x80000110` ST (bit 0 built in, bit 1 busy, bits 31:16 slots computed).
1. **clear** (new track, seek discontinuity, rate or channel change): zeroes the whole history (both channels, 16 slots each), busy 1,024 + 1 clocks (measured 1,025; 15 us). A pending partial push is discarded. The standard's synthesis starts from V = 0, so a decoder that clears sees what the standard sees.
2. **push**: exactly 64 words per slot: channel 0's P0..P31, then channel 1's. Pushes while busy and a 65th push are ignored (no error). The head of a channel's ring advances at that channel's first word.
3. **go**: accepted only when 64 words are pending and the unit is idle; otherwise ignored (the poll then times out). Busy until done.
4. **read**: write IDX = k, then read OUT (the bench waits two clocks): `{R sample k, L sample k}`, k = 0..31 in time order. The result stays until the next go.
5. **timeouts** (firmware, `fw2/decode/poly_hw.c`): clear polls 200,000 times, finish polls 400,000; on expiry the unit is disabled for the session (counted). Today's first-8-slots comparison with the software window (`TAU_POLY_VERIFY_SLOTS`) disappears in v2: replace by the self-test of section 9.
6. Words are never "carry-over": the unit holds the last 16 slots per channel (age 0 = newest), exactly what the standard's V vector needs (1,024 = 16 x 64 entries, of which only 32 unique words per slot are kept).

## 7. Latency [DERIVED from the sequencer, MEASURED in iverilog]

Per stereo slot the sequencer is busy 4,691 clocks (two channels x 2,345 + 1: group 0 = 97, group 16 = 73, 15 groups x 145); the bench counts 4,692. At the 66.67 MHz of the shipped link that is 70.4 us per slot, 2.53 ms for the 36 slots of a 44.1 kHz frame (9.7 % of real time, 26.1 ms per frame). The file header's "about 4,400 clocks" is an underestimate by 6.6 %. The unit is stereo regardless of the stream: mono costs the same unit time. The 64 MMIO pushes and 32 reads per slot are CPU work in both cases.

## 8. Mono

Mono change (merged as of this work; read from `third_party/libhelix-mp3/tau.patch`): the mono branch computes the one channel's words once and calls `tau_poly_hw_start(w0, w0)`: the same 32 words are pushed as channel 0 and channel 1; the result is taken from the left (`pcm[2*j]`). ABI consequence: **R equals L, the second channel's history is a copy and is harmless** (checked in the C model with the `mono` mode and in the RTL with `+mono`: the right half equals the left in every slot, and the left equals a stereo run with the same words on both sides). v2 must keep this: a mono decode pushes 64 words, not 32. The failure rule is that of the stereo path (counted silence, `tau_poly_stat_silent`). The unit-time saving of mono does not exist; the mono gain of the change is the removed software `PolyphaseMono` (242 k model cycles per frame, plan section 2.1).

## 9. Self-test (replaces today's software-window comparison)

Stimulus: after `clear`, push 16 slots of fixed pseudo-random words (LCG `s = s * 1664525 + 1013904223 mod 2^32`, `s0 = 0x5EED`, word = `((s >> 8) mod 2^23) - 2^22` for the 64 words of a slot in order; `selftest_slots()` in the check), read the PCM of the 16th slot (64 values, 128 bytes for both channels; 64 B per channel). `python3 tools/mp3_v2/window_abi_check.py --selftest` prints them:

```
ch0: 17707, -20795, -5139, 12130, -4634, -17987, -8860, -12975, -6533, 10762, -18211, 4612, -21220, -3969, -27952, 7443,
     9106, 15020, -3624, 25127, 2156, -21804, -3302, -2792, -8878, -15643, -7218, 6883, 9232, -30964, 19592, 9254
ch1: -10339, -14857, 8240, 23240, -27905, -11761, -22830, 9070, -16404, -1968, -2422, -17833, 18309, -5064, 4567, -10546,
     9814, 10054, -18021, 12395, -17442, -12131, 12928, 14688, -13240, 15086, -6319, -11199, 1085, -4214, -8650, 7470
```
Sensitivity [MEASURED]: no value is clipped, so every tap contributes; an error of +-256 (0.09 % of the largest coefficient) in any of the 264 ROM coefficients changes the result; a wrong age (age xor 1) or wrong word (P xor 1) in any tap-map entry changes it except 3 of 528 variants, all on taps whose coefficients are 0 or at most 64 (tap (0,0) multiplies the coefficient 0; tap (9,8) has coefficient 44: a possible error of about 0.7 LSB at this amplitude). A stuck or absent unit differs everywhere. The self-test costs 16 x 64 pushes + 16 computes = about 1.1 ms; run it once per `clear` of a new track only if the owner wants it every track, otherwise once per boot.

## 10. The test specification (what `sim/test_mp3_v2_window_abi.py` checks)

1. V[n] map equals the closed form; 63 non-zero entries use exactly the 32 push indices; the symmetries of section 3.
2. Every pushed word fits int32 at the nominal scale.
3. Model PCM equals `floor(32768 * standard + 1/2)` clipped: max |difference| <= 1 LSB; exact except within the rounding bound of a tie; the stimulus clips and is mostly ordinary.
4. Integer model of the stated arithmetic equals the C model bit for bit (random words, +-2^31 extremes, impulses).
5. No 64-bit-sum wrap is possible with 32-bit words.
6. Single-word arithmetic and engineered ties (round up).
7. Mono: right equals left, and left equals the stereo run.
8. Self-test vector: equals the integer model; detects the ROM and tap-map errors above.
9. RTL (iverilog; skipped with a note if iverilog is missing): equals the integer model on 84 slots; 4,691 busy states per slot; clear 1,025 clocks; mono right equals left.
10. Red mutants: scale 2^22 and 2^24, natural order, sign flip of P17..P31, history one slot late: each must fail check 3.
Unchanged by this work: `sim/test_mp3_poly_*.py`, `test-rtl-mp3-poly` (the RTL and the ROM are not touched).

## 11. Plan claims, marked

| Plan statement (section 3.4 and 3.2 "Headroom") | Verdict |
|---|---|
| 32 unique words per slot, V[32-i] = -V[i], V[96-i] = V[i] for i = 33..63, unique set V[0..15] and V[33..48], V[16] = 0, V[48] = -X[0] | **confirmed** (section 3) |
| Push order P0 = X[0], P1..16 = X[16..31], P17..31 = X[15..1]; keep it | **confirmed** |
| Scale w = V_std x 2^23 | **confirmed** (section 4) |
| Rounding constant 2^25, shift 26, clip to int16, 32-bit truncation, 16 slots of history | **confirmed** |
| "Words must stay below about 2^31 and the 64-bit sum below 2^51 so the unit's 32-bit cast never wraps; DCT output saturated and counted" | **corrected**: with 32-bit words the sum cannot reach 2^51 (0.68 x 2^51 at worst); the saturation of the decoder's word conversion to int32 is still required, but wrap in the unit is impossible |
| Mono: push the channel twice, read the L half; R equals L | **confirmed** (the change is merged; section 8) |
| Test: float64 matrixing and textbook synthesis vs the unit model: at most 1 LSB difference, exact except near a rounding boundary of 2^-10 LSB | **corrected**: the boundary is 0.0053 LSB, not 2^-10 LSB = 0.001, because the words are rounded integers; the measured result is within it |
| Self-check redefinition: impulse slot, 32 words, 64 B constant | **refined**: an impulse after clear reads only age 0; the self-test uses 16 slots (all ages) and 64 PCM values (section 9) |
| Latency not stated in the plan | **4,691 clocks per stereo slot** (the unit header says about 4,400) |
| File name `MP3_POLY_UNIT_ABI_2026-xx.md`, test `sim/test_mp3_poly_abi.py` | renamed to this file and `sim/test_mp3_v2_window_abi.py` (work-package brief) |

## 12. Open points

- The standard's text was not re-read; the formulas are those the repository's derivation already uses and the window data's provenance (section 2). A reviewer with ISO/IEC 11172-3 should confirm sections 2 and 3 (C3, question 1).
- The unit's file header comment still says the ROM comes from Helix and "about 4,400 clocks" (the `.svh`/`.sv` header comment is left alone by owner decision B-1168; this document supersedes both statements).
- The self-test is specified, not implemented in firmware (v2 work package WP-17/WP-19).
