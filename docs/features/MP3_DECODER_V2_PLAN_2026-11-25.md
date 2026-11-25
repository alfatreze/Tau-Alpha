# MP3 decoder v2: revised plan under D-A01 option (d) (2026-11-25)

**Status: PLAN ONLY.** No decoder code, no RTL, no fit, no card, no VM, nothing pushed or merged. Written on a worktree branch from main (after `D-A01`, the scoping and the expert review landed).
**Evidence labels used below.** **[MEASURED]** host measurement made by the scoping (B-1169) or the expert review (B-1170); I did not re-run it. **[READ]** I read the repository source or document in this session. **[FETCH]** read from `lieff/minimp3` at commit `48e3a085f0e1a99446b34614e1e7f17f09dc2e82` (the commit B-1168 pinned) through a summarising fetch tool, not by diff and not compiled: treat names and counts as probable, and re-verify them in work package WP-01 against the downloaded file. **[MODEL]** cycle model. **[EST]** estimate. **[MEMORY]** my recollection of a standard or a library, not checked. **[DERIVED]** my own derivation in this document, to be checked by the named work package.
**Inputs read:** scoping, expert review, requirements CSV, clean-room process, window-table provenance, `STAGE0_RESULTS.md` (repo root), `docs/DECISIONS.md` (D-A01 MP3, D-T12, D-T20), `docs/PROJECT_RULES.md`, audit B-1168..B-1170, `tools/mp3_v2_measure/` (listing), `fw2/decode/poly_hw.c`, `fw2/aout/aout.c`, `fw2/cores/mp3_frames_core.h`, `fw2/cores/mp3_trim.h`, `fw2/transport/mp3play.c` (grep), `src/fpga/core/tau_mp3_poly.sv` and `sim/mp3_poly_model_core.h` (model and header), `.gitignore`, `docs/ATTRIBUTIONS.md`.
**Audience:** owner and coordinator. Under (d) there is no clean-room wall, so this plan, the scoping and the review may be given to the work-package authors, with one exception in section 3A.5 (Helix back-end source stays unread).

---

## 1. One-page summary

**What is built.** A fixed-point Layer III decoder for Player 2 (`fw2/`), in two parts:
- a **front end** converted from lieff/minimp3 (CC0 1.0): header and side info, bit reader and bit reservoir, scale factors, Huffman decoding with requantisation fused in, stereo (MS, intensity), reorder; converted from float to fixed point and restructured into one arena;
- an **own back end** written from the standard: antialias, IMDCT with overlap and window switching, frequency inversion, 32-point DCT, and the glue that pushes the 32 unique V words per channel and slot to the hardware window unit `tau_mp3_poly.sv` (stereo, and mono by pushing the channel twice), with an optional pre-synthesis gain folded into requantisation and a PCM output contract (rate tag, source pair index, discontinuity/concealment flags, reserved PCM24).

Helix stays as the end-to-end differential oracle and, behind a source build flag (`MP3V2=0|1`), as the fallback for two releases. Float minimp3 (pristine, host only) is the second, licence-clean oracle. No custom instructions, no rate conversion in the decoder (D-A01).

**Corrected baseline (the review's numbers, not the scoping's).**

| Item | Value | Label |
|---|---|---|
| Stereo frame, Helix as Player 2 ships it | 274-300 k instructions, 329-359 k model cycles | MEASURED (review) |
| Mono speech frame (software window still linked for mono) | 318 k instructions, 390 k model cycles, 242 k of it the software window | MEASURED (review) |
| Model to Pocket | x1.3 without a cache model, x1.15 with a 4 KiB I$/D$ model (+/-10 %) | MEASURED vs B-591 |
| Hot RAM taken by Helix | 66.8 KB (code 28,844 B, tables 12,614 B, data 640 B, arena 24,576 B, unit glue 160 B), not 71.5 KB | MEASURED (review) |
| Heap gap, DIAG=0 | 13,568 B | MEASURED (both) |
| Glue between Helix and the unit | about 66 k instructions per stereo frame (24 % of decode); any new decoder removes it | MEASURED (review) |

**Targets and gates** (section 5 gives them per phase).

| Gate | Value |
|---|---|
| Instructions per stereo frame, rv32 host run, unit stubbed | at most 200 k (aim 165-185 k) |
| Mono frame | at most 150 k (aim 110-130 k) [EST] |
| Model cycles with the I$/D$ model | at most 240 k stereo; that is about 16 % of the CPU at 44.1 kHz [DERIVED: 240 k x 1.15 x 38.28 / 66.67 M] |
| Pocket `SR_T_LOAD2.dec_pct`, 1.00x | at most 20 % stereo and mono (Helix: about 24 % modelled for stereo, 27-30 % in the B-591 profile build) |
| Hot RAM freed against Helix's 66.8 KB | at least 22 KB (aim 28-33 KB); heap gap at least 35.5 KB DIAG=0 |
| Worst stack | at most 1.5 KB (Helix peak 1,672 B, B-230), checked statically |
| Accuracy | limited accuracy everywhere; full accuracy in the pre-rounding domain (section 4.2, **statement to verify against ISO text**) |
| Output | every frame emits its samples; six-file gapless equality with the FLACs; v2 vs Helix aligned at lag 0 |
| Provenance | every derived function listed in a map against the pinned upstream commit; notices in place |

**Plan.** 7 phases, **expected 21 working sessions (range 19-26; target 18-25)**, calendar **6-9 weeks** (Pocket gates and owner time dominate; front end and back end run in parallel). Two small fixes run first as separate sessions (the 48 kHz drain in `fw2/aout/aout.c`, mono through the unit) and are referenced, not duplicated. 24 work packages are ready to brief (section 5.3); the first three are WP-01 (vendor pristine minimp3 and build the float oracle), WP-02 (the window-unit ABI document from the standard) and WP-03 (header scan of the owner's library). Everything before P4 is host-only: no firmware, RTL, fit or card.

---

## 2. Corrected baseline

### 2.1 CPU per frame and per stage

Frame rates: 38.28/s (44.1 kHz, 1,152 samples), 41.67/s (48 kHz), LSF 22.05/24 kHz 576 samples per frame at 38.28/41.67 per second. 1 % of the CPU = 17.4 k cycles per 44.1 kHz frame.

**Helix with the unit, as shipped, per stereo frame, MEASURED (review, Battle 320 k VBR; model cycles incl. caches off):**

| Stage (function) | Model cycles | Pocket (B-591, 1 % = 17.4 k) |
|---|---|---|
| IMDCT incl. antialias and overlap (`IMDCT`) | 89.9 k | i 7 % = about 122 k (x1.25 of this model) |
| FDCT32 with push log (`FDCT32`) | 77.2 k | s 12 % = about 209 k (x1.5 incl. `sub_hw`) |
| Huffman + bit reader | 53.3 k | h 4 % = about 70 k (x1.2) |
| Requantisation (`DequantBlock` etc.) | 47.2 k | d 4 % = about 70 k (x1.1) |
| `Subband` glue, push, read | 28.2 k + 20.9 k + 13.0 k | inside s |

**Per file (review, with a 4 KiB direct-mapped I$/D$ model, 1,000-3,000 frames):**

| File | Instr/frame | Model cycles/frame | I$ miss | D$ miss | Stores |
|---|---|---|---|---|---|
| 02 MacCunn 128 k 44.1 stereo | 274,389 | 328,767 | 1,449 | 1,821 | 38,676 |
| 05 MacCunn 320 k 48 stereo | 293,953 | 352,518 | 1,432 | 2,015 | 38,716 |
| Tank! 320 k-class VBR | 299,400 | 358,136 | 1,953 | 1,996 | 39,989 |
| 06 Battle | 300,189 | 359,090 | 1,641 | 1,965 | 39,646 |
| 11 Speech 128 k **mono** | 318,139 | 389,774 | 4,572 | 4,837 | 40,766 |

**With and without the window unit** (stereo 44.1 kHz 128 k, MEASURED): software window build 602,894 instructions / 762,972 model cycles (window 67 %); with the unit about 274 k / 329 k. Mono without a working unit path: software `PolyphaseMono` is 242 k cycles of 390 k. **Mono never uses the unit today** (`tau_mp3_poly.sv` header: stereo only); the running fix routes mono through it (review estimate: about 200 k model cycles per frame saved, about 12 % of the CPU model, about 15 % on a Pocket [EST]).

What is model and what is Pocket: every row above is host instruction counts plus a cycle model, except the right-hand column (B-591, old-player profile build, unknown track, `dec_pct` includes profile ticks). **No v2 figure exists on a Pocket.** Budget rule from the review: model x1.3 (caches off) or x1.15 (cache model), +/-10 %.

### 2.2 RAM by region (Player 2 DIAG=0, MEASURED by the review with `nm -S` of the linked ELF)

| Region | Bytes | Note |
|---|---|---|
| Helix code (after `--gc-sections`) | 28,844 | the scoping's 33.5 KB counted the 5 KB `PolyphaseStereo` that is not linked |
| Read-only tables | 12,614 | Huffman about 8.5 KB |
| `.data` | 640 | |
| Arena (static in `libc_min.c`, peak 23,824) | 24,576 | `SubbandInfo` 8,708 B is the polyphase history, written on every slot even with the unit |
| Unit glue | 160 | |
| **Helix total** | **about 66.8 KB** | 192 KB link; image end 149,248, heap limit 162,816, gap 13,568 |

Outside the decoder: `mp3d_t` (1.5 KB frame window + 4.6 KB `pcm[]`) in the `dv` union shared with FLAC (6,360 B).

### 2.3 Where v2 should land (budget, [EST] from the review's floor table, with my placements)

| Item | Bytes |
|---|---|
| Spectrum, 2 channels x 576 x int32, done in place | 4,608 |
| IMDCT overlap (half storage, 9 per subband as minimp3 and Helix do) | 2,304 (full storage 4,608 is the fallback) |
| Reservoir 511 + main-data scratch (at most 511 + 1,405 for MPEG-1 non-free-format) | about 1,920-2,048 |
| Side info, scale factors, header state, per-granule band scales | about 600 |
| DCT32 scratch | 256 |
| **State subtotal** | **about 9.7-10.5 KB** (target arena <= 14 KB incl. margin) |
| Tables (Huffman about 4 KB, scale-factor bands 0.8 KB, pow43 0.6 KB, windows/twiddles/antialias/DCT constants about 0.7 KB) | about 6 KB; Helix 12.6 KB |
| Code (Layer III only, no software window, no float, no SIMD; intensity and mixed blocks cold) | 14-20 KB [EST] |
| **Total** | **about 30-40 KB**; frees 27-37 KB against 66.8 KB; the gate is 22 KB |

`pcm[]` (4.6 KB) leaves only with the ring-direct output (section 3.8, optional, later).

---

## 3. Design of the decoder under (d)

### 3.1 Module breakdown and where each part comes from

Source tree `fw2/decode/mp3v2/` (derived, edited, credited; section 7). "From minimp3" means converted from the pinned upstream function; "new" means written from the standard. The function-level map is `docs/features/MP3_V2_MINIMP3_MAP.md`, created in P2 and checked by a script.

| # | Module | minimp3 origin [FETCH] | Action | Phase |
|---|---|---|---|---|
| M1 | Header helpers: valid, compare, bitrate, rate, samples per frame, frame bytes, padding | `hdr_*` | keep (tiny, pure); shared by decoder and frame driver (replaces calls to Helix's `MP3FindSyncWord`/frame-info in the driver) | P2 |
| M2 | Bit reader | `bs_init`, `get_bits` | keep logic; audit for unaligned 16/32-bit loads [MEMORY: believed byte-wise]; refill 32 bits at a time only if profiling asks | P2 |
| M3 | Side info | `L3_read_side_info` | keep, change to our arena structs; MPEG-1 and LSF both | P2 |
| M4 | Bit reservoir | `L3_save_reservoir`, `L3_restore_reservoir` | keep; add the underflow status of 3.5 | P2 |
| M5 | Scale factors and band gains | `L3_read_scalefactors`, `L3_decode_scalefactors`, `L3_ldexp_q2` | convert: integer exponent plus 4-entry 2^(f/4) mantissa; fold the pre-gain here (3.2) | P2 |
| M6 | Huffman + requantisation fused | `L3_huffman`, `L3_pow_43`, tables | convert: output Q-format spectrum (3.2); pow43 table plus interpolation for escapes | P2 |
| M7 | Stereo | `L3_midside_stereo`, `L3_intensity_stereo*`, `L3_stereo_*` | keep MS (hot); intensity in the cold image (3A) | P2 |
| M8 | Reorder (short blocks) | `L3_reorder` | keep; fold into the IMDCT input addressing if cheaper | P2/P3 |
| M9 | Antialias | `L3_antialias` | **rewrite from the standard** (8 butterflies, coefficients from the formula) | P3 |
| M10 | IMDCT36 / IMDCT12 / window switching / overlap / frequency inversion | `L3_imdct*`, `L3_dct3_9`, `L3_idct3`, `L3_change_sign` | **new**, derived from the standard's formula (own fast factorisation, verified against the O(n^2) definition) | P3 |
| M11 | 32-point DCT (matrixing) to the unit's words | `mp3d_DCT_II` | **new**, written to the ABI of WP-02 (section 3.4) | P3 |
| M12 | Unit glue, slot pipelining, self-test, silence on timeout | (none; Helix's `Subband` is not read) | new; reuses `fw2/decode/poly_hw.c` | P3/P4 |
| M13 | Frame decode driver (one frame in a linear buffer, status, counters) | `mp3dec_decode_frame` shape only | new; same call shape as Helix so `mp3_frames_core.h` changes little | P4 |

**What is not taken at all:** Layer I/II, SIMD, float output, the `_ex` layer, `mp3d_synth*` and the 512-tap synthesis, `g_win` (the unit has its own ROM). Section 3A.

### 3.2 Fixed-point strategy

Principles: one rounding rule everywhere (add half, arithmetic shift: round half up, as the unit does); one C source used by firmware and host, so the host "fixed model" **is** the golden (bit-exact on host and rv32, golden hashes in section 4); every saturation counted in a debug counter.

**Spectrum format (proposal, to be fixed by WP-12 from measured headroom).** Spectrum values after requantisation are int32 **Q4.28** relative to the standard's xr, where +/-1.0 corresponds to int16 full scale (+/-8.0 range, saturating). Reasoning [DERIVED]: absolute resolution 2^-28 against the 16-bit step 2^-15, so 13 bits of margin before the arithmetic is visible; accumulated error over about five multiply stages at Q31 coefficients is about 2^-26..2^-24 (the review's figure). A fixed global format avoids per-band block exponents (the review calls the exponent the hardest numerics), **provided** the measured maximum of |xr| over the vectors and the owner's library fits (WP-12). Fallback if not: one block exponent per granule and channel (computed from the Huffman result: maximum magnitude and the largest band gain), applied as a final shift at the IMDCT output. Values below 2^-28 flush to zero, which is below the 16-bit LSB by 2^13.

**Requantisation.** Value = sign x pow43(|q|) x 2^((global_gain - 210 - pregain_steps)/4 - scalefac terms): integer exponent per band plus the fractional quarter from a 4-entry mantissa table (Q30). pow43 for |q| < 129 from a table (129 x 4 B), for larger from the interpolated form (as upstream does; only escape values reach it: |q| <= 8,206, 8,206^(4/3) is about 2^17.3). Product of an up-to-18-bit magnitude and a Q30 mantissa uses `mul`+`mulh` (2 instructions) then one shift with rounding and a saturating clamp to Q4.28. Gate: relative error of the requantised value at most 2^-20 against float64 for every |q| and gain (exhaustive host test: 8,207 x all exponents).

**Pre-synthesis gain (the contract's optional pre-gain) costs about nothing [DERIVED]:** requantisation already multiplies each band by its gain; the Q15 pre-gain (at most 1.0, default unity) multiplies the per-band mantissa once per band (about 22 multiplies per granule and channel), not once per value. It acts before MS and is linear, so it is equivalent to scaling the output. The review's alternative (2.3 k multiplies per frame, under 1 % CPU) is the fallback.

**Antialias and IMDCT.** Q31 coefficients through `mulh` (one lost bit per product), rounding at each multiply stage that feeds another; windows and twiddles generated at build time from the formulas (a script writes the table header, like `tools/gen_mp3_poly_rom.py` does for the unit). Long blocks (and start/stop) use the standard's window (sine) as defined for each block type; short blocks three 12-point transforms; mixed blocks switch at subband 2. The factorisation is derived by us (work package WP-16) and verified against the direct O(n^2) formula in float64, then in fixed point (stage error at most 2^-22 relative).
**Zero-band skipping [EST]:** the Huffman stage knows the highest nonzero line; subbands above it need no IMDCT, only the overlap tail to be emitted. At 128 kb/s the upper half of the spectrum is typically zero, so this is the largest CPU lever inside the back end; it must be proven bit-identical to the unskipped path (a test, not an assumption).

**Frequency inversion and layout.** The IMDCT writes straight into the **[slot][subband]** layout (32 contiguous words per slot) so the DCT32 reads sequentially. Reason [READ review + project docs]: the D$ is 4 KiB direct-mapped, write-through; stores do not allocate, so strided stores cost the same as sequential ones, while strided loads (minimp3 and Helix read columns with a stride of 18 words = 72 B) thrash. WP-05's cache-aware `rvprof` must A/B the two layouts before P3 commits.

**Headroom.** The unit's datapath is `out = clip16( round( sum(w x c) >> 26 ) )` with c = D x 2^18 [READ `mp3_poly_model_core.h`: CSHIFT 12, NFRAC 6, 32-bit truncation of bits 51:20 of the 64-bit sum]. So `out = sum(D x w) / 256` and the **word scale is w = V_std x 2^23** for the standard's full scale 1.0 = 32768 [DERIVED, to be confirmed by WP-02]. Words must stay below about 2^31 and the 64-bit sum below 2^51 so the unit's 32-bit cast never wraps: the DCT output is saturated at +/-(2^31-1) and counted.

**Table placement and sizes.** Hot RAM (192 KB link, BRAM): Huffman `int16` stream (about 4 KB, [EST]; count it in WP-01), scale-factor-band tables 824 B (`g_scf_long` 8x23, `g_scf_short` 8x40, `g_scf_mixed` 8x40) [FETCH], pow43 0.6 KB, windows/twiddles below 1 KB. Cold PSRAM image (about 32 cycles per instruction word [READ scoping]): only code with worst-case per-frame instruction count x 32 below 5 % of the frame budget (intensity stereo bands, mixed-block set-up, free-format search if ever enabled). Mechanism (how a function is placed cold) is unknown to me: U8, read by WP-19 from `fw2/platform/link.ld` and the RAM_SHRINK plan.

### 3.3 Cache layout as a design rule (review Q4.3)

1. Each stage's inner loop, plus the tables it touches in that loop, is under 4 KiB; no two functions called from the same hot loop share an I$ index (address mod 4,096, 32 B lines) (the Layered Wave method, `sim/test_p2_icache.py` exists).
2. Hot loops are store-light (D$ write-through, 38-40 k stores per Helix frame); prefer register accumulation, one store per output.
3. Strided access is store-side only (3.2).
4. Sections: `.text.mp3v2.<stage>` per stage with an explicit order; a host test reads the ELF map and checks rules 1 (index overlap) per stage, and fails the build on a violation.
5. The Huffman tables are ordered so the most used table selections and their first codewords sit together (measured from the library, WP-04/WP-12), since the 4.2 KB stream cannot stay resident in the 4 KiB D$ with the bit buffer and spectrum.
Gate: I$ misses at most 1,500 and D$ misses at most 1,800 per stereo frame in the cache model (Helix: 1,450-1,950 and 1,800-2,000), mono at most 3,000 each (Helix mono 4.6 k and 4.8 k; the mono gain comes from the unit).

### 3.4 Window-unit interface, specified from the standard (a task: WP-02; blocks every back-end stage)

The unit is "Helix PolyphaseStereo in hardware" and its input is Helix-defined [READ unit header, review finding 7]. Required deliverable `docs/features/MP3_POLY_UNIT_ABI_2026-xx.md`, with a test:
1. The standard's matrixing V[i] = sum_k S[k] cos((16+i)(2k+1)pi/64), i = 0..63, and its symmetries give **32 unique words per slot** [DERIVED: V[32-i] = -V[i] (so V[16] = 0), V[96-i] = V[i] for i = 33..63; unique set V[0..15] and V[33..48], consistent with B-1168 "row 16 uses V[48] = -X[0]"; check this].
2. The push order P0..P31 as an index map into the standard's V (today P0 = X[0], P1..16 = X[16..31], P17..31 = X[15..1] in B-1168's X notation). **Decision recommended: keep the order and the ROM** (no RTL change, no fit), and re-specify it as numbers. A different order would change the ROM and need a fit: only if the numerics review finds the existing order costs DCT32 instructions.
3. The scale: w = V_std x 2^23 (3.2), the rounding constant (2^25 in the 64-bit sum, half-LSB at the output), shift 26, clip to int16, the 32-bit truncation, the history depth (16 slots per channel), and `clear`/timeout behaviour (`tau_poly_hw_clear`, 200,000/400,000 poll limits).
4. Mono: push the channel twice, read the L half (the running fix; this plan references it and states the ABI consequence: R equals L).
5. Test: random subband samples, float64 matrixing and textbook synthesis (512-tap window with the standard's D) against the unit model `sim/mp3_poly_model*.c` fed with our DCT words: at most 1 LSB difference (rounding), exact zero where the float64 value is not within 2^-10 LSB of a rounding boundary. This also makes the unit's golden standard-derived; Helix becomes only an oracle there.
6. **Self-check redefinition.** Today the first `TAU_POLY_VERIFY_SLOTS` slots are compared with the software window [READ `poly_hw.c`]; v2 has no software window. Replace by a unit self-test at `clear`: push a fixed impulse slot, read 32 words, compare with a 64 B constant table from the ABI test; mismatch disables the unit (silence plus counter, as today). Host test included.

### 3.5 PCM output contract (WP-10; contract review C1)

```
typedef struct {
  const int16_t *pcm;      /* interleaved pairs after trim (L,R; mono as pairs with L=R only if the consumer needs pairs) */
  uint32_t pairs;          /* count */
  uint8_t  ch;             /* 1 or 2 as decoded */
  uint8_t  fmt;            /* 0 = PCM16; 1 = PCM24 RESERVED (not produced in v1) */
  uint16_t flags;          /* DISCONT (state reset, a seek or header change), CONCEALED, WARMUP, RATE_CHANGED */
  uint32_t rate_hz;        /* source rate tag (8000..48000) */
  uint32_t spf;            /* samples per channel in the source frame: 1152, or 576 for LSF */
  uint32_t src_pos;        /* source pair index of pcm[0] after trim and skip: the single position rule (scoping S4) */
  uint16_t pregain_q15;    /* gain applied inside the decoder, 32768 = unity */
} mp3v2_blk_t;
```
- Rate conversion stays out (D-A01). The decoder only tags; `aout` converts (the 48 kHz drain fix lands first and is referenced).
- **Granules go straight to the ring (S7).** The decoder produces PCM per slot (32 pairs, 128 B) read from the unit; a sink callback (`pairs, src_pos, flags`) lets the driver write them to the ring (or tempo staging) and apply trim and skip by pair counter. **P4 keeps today's frame-sized `pcm[]` behind the same struct** (low risk, same driver shape); the ring-direct sink is WP-23, optional, worth 4.6 KB and the `pcm[]` copy, after the decoder is proven.
- Pre-gain: default unity; the firmware may set min(ReplayGain, 0 dB) (decision D11). Clipping facts: rail samples 0.03-0.45 % on the owner's Cowboy Bebop albums (up to 98,218 on one track), 0-3 on the Test Album [MEASURED review, an upper bound on decoder-created clipping].
- **Gapless trim rule.** The existing driver counts every frame, including one that did not decode (`frame_idx++` when `used`), and the trim arithmetic uses decoded-stream coordinates: the Info/Xing frame occupies spf pairs of silence, real audio starts at `S = spf + delay + 529`, length `P = frames*spf - delay - padding` [READ `mp3_trim.h`; proven on six files against FLAC]. The v2 rule: **one frame in, spf samples of output position out, always**; the Info/Xing/VBRI frame decodes as silence (or is reported as silent with the same position effect); the decoder's own delay must be the standard's 529 with no extra latency. Gates: six-file equality with the FLAC sample counts and lag-0 alignment against Helix on the whole library.
- **Behaviour change to approve (D13).** Helix today returns an error for a frame it cannot decode and the driver skips it (`used`), which drops spf pairs of audio from the ring; minimp3 returns 0 samples on reservoir underflow [FETCH] (the same). The v2 rule below emits silence for a corrupt frame so ring length and P3 accounting survive; warm-up frames at the start or after a seek still output nothing (they are trimmed away today).
- **Status codes** (frame call): OK; WARMUP (reservoir not yet filled: spf of silence produced, flagged, not an error count); CONCEALED (frame replaced, see 3.7); NEED_DATA (fewer bytes than the frame); SYNC_BAD (no frame consumed, driver resyncs).
- LSF frames are 576 samples: `spf` is in the contract; the driver already derives it from the frame info.

### 3.6 Seek pre-roll

After a seek the reservoir and overlap are empty. The driver lands on a frame boundary (Xing TOC for VBR, arithmetic for CBR, the skip list in `skiptab_core.h`) and the decoder decodes **N pre-roll frames and discards them** (`skip_before`, existing). N = max(1, ceil(511 / (frame_bytes - 4 - side_info))) capped at 6 [DERIVED]: 5 frames at 32 kb/s, 2 at 128 kb/s. One frame covers both the 18-slot overlap and the 15-slot unit history. A discontinuity zeroes overlap and reservoir and clears the unit; a gapless join keeps them (rule from the scoping, section 10.3). Test: seek-then-decode equals decode-from-start from the landing sample on, bit-exact, for 20 random landing points per file.
Xing TOC accuracy (1/256 of the file, 0.4 %, 2.4 min on a 10 h book [EST review]) is a library/driver matter, not the decoder's: recorded as U7 with a measurement, no plan item.

### 3.7 Error concealment rule

On any frame that cannot be decoded (bad side info, Huffman overrun past `part2_3_length`, main data beyond what the reservoir holds mid-stream): produce spf samples by running the back end with a **zero spectrum**, so the overlap tail and the unit history decay naturally (no step), set CONCEALED, count it, drop the reservoir (it can no longer be trusted), and let the following frames report WARMUP until `main_data_begin` points inside valid data. Never output zero samples (position and P3 survive). A header that changes rate or channel count: DISCONT plus RATE_CHANGED, state reset. Bounded work: at most one frame of decode per call, at most 6 consecutive WARMUP frames before the driver treats the stream as broken. Host fuzz proves bounded time and no out-of-bounds access (section 4.6).

### 3.8 Container and tag handling (VBRI, Xing/Info, ID3, footers, junk)

Mostly the frame driver's job; v2 gives it the header helpers and tests.
- **ID3v2:** the driver skips by seek using the synchsafe size; **add 10 for the footer flag**, and skip by size (unsynchronisation changes content, not the declared size; minimp3_ex does the same [FETCH: footer by size, no unsync handling, no extended header]). Test with tags that have a footer, padding, and an extended header.
- **Xing/Info/LAME:** keep today's parser (`mp3d_parse_info`): frame count, delay, padding, TOC; Info frame silent.
- **VBRI** (Fraunhofer): not supported by minimp3 or `_ex` [FETCH]; recognised at 36 bytes after the header [MEMORY]; read frame count and byte count for `pairs_total` and tail skip; **no delay trim** (the VBRI delay field is not LAME's; treat as unknown unless a reference file proves otherwise). Detected as a non-audio frame that decodes as one silent frame. A test file is needed: synthetic (WP-08).
- **Trailing junk:** ID3v1 (128 B "TAG", plus the 227 B "TAG+") and APEv2 (footer "APETAGEX") at the end must never become a frame: the sync confirmation needs agreeing following headers (`mp3d_confirm` in the driver); minimp3's rule is 10 consecutive matches with an ID3v1/null tail allowed [FETCH], which needs a 14 KB window and so does not fit the 1.5 KB `mp3d_t` window: the driver's smaller confirmation stays. Tests: tails of each kind on files with and without Xing frame counts.
- **CRC:** step over the 16-bit field when the protection bit is 0 (minimp3 reads and discards 16 bits and never computes the CRC [FETCH]). Decision D2: no verification in release 1; cheap to add later (CRC-16 over header bytes 2-3 plus side info).
- **Free format:** frame length from the distance to the next sync (bounded 2,304 B in minimp3 [FETCH]); **gated off in release 1** (rejected with a status; Helix does not support it either [MEMORY: Helix returns an error]; the owner's 59 files contain none).

### 3.9 Scope of MPEG versions and options (header-scan evidence)

Evidence [MEASURED, review, 59 MP3 files in `test music/`]: all MPEG-1 Layer III at 44.1/48 kHz; joint stereo with MS plus plain stereo and mono; VBR with Xing on most albums, LAME Info on the Test Album; the speech file has no tag; **zero** LSF, intensity, mixed-block, CRC or free-format frames. Constraint: no regression against Helix, which supports MPEG-2/2.5, intensity and mixed blocks [MEMORY: Helix handles MPEG-1/2/2.5 Layer III]. Therefore:

| Feature | Release 1 | Placement | Verification |
|---|---|---|---|
| MPEG-1 L3 32/44.1/48 kHz, mono/stereo/MS, all block types, scfsi, reservoir | support | hot | library, Test Album, vectors |
| MPEG-2/2.5 LSF (576-sample frames, one granule, LSF scale-factor coding) | **support** (no regression vs Helix; spoken-word 22.05/24 kHz files exist in the wild); needs the 48 kHz drain fix to be audible | LSF side-info and scale-factor branches hot (small, per granule); everything else shared | vectors and synthetic streams only (no real file) |
| Intensity stereo (MPEG-1 and LSF) | support | **cold** | synthetic streams, vectors |
| Mixed blocks | support | **cold** set-up, shared IMDCT | synthetic streams, vectors |
| Free format | **gated** (rejected) | not linked | revisit on a stated need |
| CRC | step over, no check | | |
| Layer I and II | not supported (Helix does not either) | not linked | |

The compile-time options are `MP3V2_LSF`, `MP3V2_INTENSITY`, `MP3V2_MIXED` (default 1 in test builds; the shipped default follows D2).

---

## 3A. minimp3 adaptation scope (the owner's addition)

**Principle.** We do not edit minimp3 in place. The firmware tree is a **derived** tree: each needed upstream function is copied once, converted, restructured and credited; everything else is simply not copied. Upstream stays untouched in a separate pristine vendored copy (section 7.1) used as the float oracle. "Remove" below therefore means "not carried into the derived tree".

### 3A.1 CPU: SIMD (SSE, NEON, `HAVE_SIMD`)

**Our CPU has no SIMD.** [READ] `fw2/build.sh` compiles with `-march=rv32im`; `src/fpga/rtl/VexRiscv_Full.v` is a SpinalHDL v1.9.4 generated VexRiscv (header lines 1-3) whose features the scoping lists as hardware multiplier, iterative divider, full barrel shifter, instruction and data caches, CSR, Wishbone data bus [READ scoping 5.5]; a search of the file for `simd` finds nothing, and the repository has no vector (RVV) or packed-SIMD (P-extension) support. The generated file has no configuration record, so the exact plugin set is unverified (scoping U4); nothing indicates a custom instruction plugin.
- **SSE and NEON paths: remove** (not carried over). Verified inventory [FETCH]: guards `HAVE_SSE`/`HAVE_SIMD` (x86 SSE2) and the NEON guard (`__ARM_NEON`/`__aarch64__`/`_M_ARM64`) that sets `HAVE_SIMD 1`; `MINIMP3_NO_SIMD` forces them off; `MINIMP3_ONLY_SIMD` (auto on x86_64/aarch64) drops the scalar tails; runtime dispatch `have_simd()` via `minimp3_cpuid` (CPUID inline asm, MSVC `__cpuid`); `HAVE_ARMV6` `ssat` clipping; functions with a SIMD and a scalar version: `L3_midside_stereo`, `L3_antialias`, `L3_imdct36`, `mp3d_DCT_II`, `mp3d_synth`, `mp3dec_f32_to_s16`. (The name `HAVE_ARMNEON` that appears in common descriptions is **not found** in this commit.) On our build none of it is compiled today, so the effect on ROM, RAM and CPU is zero; the gain is that the derived source has no dead branches, intrinsics or asm to carry.
- **The equivalent for us** is the **hardware polyphase window unit** (`tau_mp3_poly.sv`), which replaces the heaviest SIMD kernel (`mp3d_synth`). Custom instructions are not planned; D-A01 allows them only if the owner names a CPU need, and the scoping (5.2) counted at most 1.5-2.5 % CPU for all candidates together.
- **Does reading the SIMD routines help?** Slightly, as a **map of independent loop iterations**: they process four antialias subband pairs, four DCT-II columns or four output samples at once, which shows which loops have no cross-iteration dependency. On an in-order scalar RV32IM that parallelism buys nothing by itself (no SIMD, no dual issue), so the map is of use only for loop interchange and unrolling decisions in M9-M11 and for the per-stage cache layout. It must not be used as the algorithm source for our back end: M9-M11 are written from the standard (3A.5, 7.3); the reading is optional and recorded in `docs/PROVENANCE.md` as inspiration if done (project rule 7 style).

### 3A.2 Feature-by-feature table

| # | Feature or region (verified) | What it does | Our need now | Action | RAM / ROM / CPU effect | Risk |
|---|---|---|---|---|---|---|
| 1 | SIMD: SSE2, NEON, cpuid, `have_simd`, `MINIMP3_ONLY_SIMD`, ARMv6 `ssat` [FETCH] | 4-wide kernels for MS, antialias, IMDCT36, DCT-II, synth, f32-to-s16 | none (RV32IM) | **remove** | none on our build (never compiled); source shrinks | none |
| 2 | Layer I and II: `L12_*` five functions, tables `g_alloc_L1`, `g_alloc_L2M2`, `g_alloc_L2M1`, `g_alloc_L2M1_lowrate`, `g_bitalloc_code_tab[92]`, `g_deq_L12` (54 floats), `L12_scale_info` on stack about 0.9 KB, about 200 lines; `MINIMP3_ONLY_MP3` removes it [FETCH] | Layer I/II decode | none; Helix does not decode them either [MEMORY] | **remove** (we adopt `ONLY_MP3`) | about 3-4 KB code [EST] and about 0.4 KB tables not carried; stack 0.9 KB not needed | a Layer I/II file is rejected with a status (as Helix) |
| 3 | Float output: `MINIMP3_FLOAT_OUTPUT`, `mp3d_scale_pcm`, `mp3dec_f32_to_s16` [FETCH] | float PCM or conversion to int16 with rounding and clipping | replaced by fixed-point stages and the unit's clip16 | **remove** | no softfloat linked (the Stage 0 cost); no libgcc float routines | none |
| 4a | LSF (MPEG-2/2.5): side info with one granule and 8-bit `main_data_begin`, LSF scale-factor branch in `L3_decode_scalefactors`, rate and bitrate tables `halfrate`, `g_hz` [FETCH] | 16/22.05/24 and 8/11.025/12 kHz | no regression vs Helix; none in the owner's 59 files | **keep, compile flag `MP3V2_LSF`** (hot, small branches) | about 1 KB code, 0.3 KB tables [EST] | untested by real files: synthetic and vector tests (WP-08, WP-15) |
| 4b | Intensity stereo: `L3_intensity_stereo`, `_band`, `L3_stereo_top_band`, `L3_stereo_process`, `g_pan[14]` [FETCH] | intensity joint stereo (MPEG-1 and LSF) | none in library; Helix supports it [MEMORY] | **keep, flag `MP3V2_INTENSITY`, cold placement** | about 1-1.5 KB code moved to PSRAM [EST: Helix's equivalent 1.1+0.9 KB, scoping CSV]; hot CPU 0 | cold code runs about 30x slower: bounded per frame (3.2 rule) |
| 4c | Mixed blocks: `mixed_block_flag`, `g_scf_mixed`, `n_long_bands` in `L3_decode` and `L3_imdct_gr` [FETCH] | long bands below subband 2, short above | none in library | **keep, flag `MP3V2_MIXED`** (set-up cold, shared IMDCT) | small | rare path: vectors |
| 4d | Free format: `HDR_IS_FREE_FORMAT`, `free_format_size`, 2,304 B look-ahead search in `mp3d_find_frame`, `MAX_FREE_FORMAT_FRAME_SIZE` [FETCH] | bitrate index 0 frames | none; Helix lacks it [MEMORY] | **gate: not linked in release 1**, status reject | saves the 2,304 B scratch worst case (main data buffer sized for 1,405 B) | a free-format file is unplayable (as today) |
| 4e | CRC: bit tested, 16 bits read and discarded, no check [FETCH] | steps over the field | step over | **keep as is** | none | none |
| 5a | `hdr_valid`, `hdr_compare` (does not compare bitrate or channel mode [FETCH]), `hdr_bitrate_kbps`, `hdr_sample_rate_hz`, `hdr_frame_samples`, `hdr_frame_bytes`, `hdr_padding` | header decode | needed by driver and decoder | **keep (M1)**, as a shared pure header | about 0.5 KB code | none (host-tested) |
| 5b | `mp3d_match_frame` (up to 10 following headers; ID3v1/null tail ends a match), `mp3d_find_frame` (byte scan, free-format look-ahead), `hdr_is_id3v1`, `hdr_is_null` [FETCH] | sync search in a whole-buffer | our driver has a 1.5 KB window, a stream ring and a 2-header confirmation (`mp3d_confirm`) | **remove** (keep the driver's logic; reuse only `hdr_*`) | 10 matches need about 14 KB look-ahead: does not fit | resync behaviour must equal today's (one byte at a time): driver tests |
| 6a | ID3v1/ID3v2/APEv2 skip, Xing/Info parse with delay/padding, gapless in `minimp3_ex.h` [FETCH: skip by declared size, footer +10, no unsync or extended header, APE footer, no VBRI, TOC skipped, `mp3dec_load_index` builds a frame index by a full scan] | file-level API | our `mp3_frames_core.h`, `mp3_trim.h`, `lame_tag.h`, library, skip list cover it | **remove the whole `_ex` layer**; reuse ideas (footer +10, APE footer size) as driver tests | none | none |
| 6b | stdio, mmap, malloc/realloc, 128 KiB I/O buffer, index array, `MINIMP3_ENABLE_RING` (memfd/shmget) in `_ex` [FETCH] | host file access | impossible and unneeded on the Pocket | **remove** | none | none |
| 7 | Synthesis: `mp3d_DCT_II`, `mp3d_synth_pair`, `mp3d_synth`, `mp3d_synth_granule`, `g_win[240]` (960 B), `qmf_state[960]` floats, `syn[33][64]` floats (8,448 B of stack) [FETCH] | matrixing and 512-tap window | window in the unit; DCT32 ours | **remove** (window and history), **rewrite** DCT (M11) | 960 B table, 3.8 KB history, 8.4 KB scratch not carried | the unit ABI (3.4) |
| 7b | IMDCT: `L3_dct3_9`, `L3_imdct36`, `L3_idct3`, `L3_imdct12`, `L3_imdct_short`, `L3_imdct_gr`, `L3_change_sign`, `L3_antialias`, `g_twid9`, `g_twid3`, `g_mdct_window[2][18]`, `g_aa[2][8]`, `g_sec[24]` | transforms | the back end | **rewrite from the standard** (M9, M10); may read for the map only | tables regenerated from formulas | numerics: WP-16 |
| 8 | State: `mp3dec_t` about 6.7 KB (`mdct_overlap[2][288]` floats, `qmf_state[960]` floats, reservoir 511 B) and `mp3dec_scratch_t` about 16.3 KB **on the stack** (`maindata[2815]`, `grbuf[2][576]` floats, `syn`, 4 `L3_gr_info_t`) [FETCH] | state and scratch | one arena, no big stack | **restructure** into `mp3v2_t` (one struct, `static_assert` size, no stack arrays above 256 B, no recursion, no alloca) | overlap int32 half-size 2.3 KB; grbuf 4.6 KB; maindata about 1.9-2.0 KB; no `syn`; stack bound by `-fstack-usage` | the bound is a gate (section 4) |
| 9 | Tables: `tabs` flat `int16` (count not determined by the fetch; expected about 1.9-2.1 k [review]), `tabindex[32]` int16, `tab32[28]`, `tab33[16]`, `g_linbits[32]`, `g_pow43` float[145], `g_scf_*` 824 B, `g_scf_partitions[3][28]`, `g_scfc_decode[16]`, `g_preamp[10]`, `g_expfrac[4]`, `halfrate`, `g_hz` [FETCH] | Huffman and requantisation data | needed | **keep data, convert floats to fixed** (extractor with structural checks: Kraft sums, counts, as B-1168 did) | about 6 KB total vs Helix 12.6 KB [EST]; the `tabs` count and the rv32 size are measured by WP-01 | extractor errors: Kraft, round trip |
| 10 | `memcpy`, `memset`, `memmove`, `memcmp`; no assert, no math.h, no malloc in `minimp3.h` [FETCH]; `__builtin_unreachable`, `__builtin_constant_p` workarounds | portability glue | `fw2/platform/libc_min.c` provides mem functions [MEMORY: verify the set in WP-19] | **replace** by `fw2` helpers or inline loops; drop the builtins | none | any unaligned 16/32-bit access in the upstream bit and table code must be audited against the CPU (review R3): audit item in WP-13 |
| 11 | Return convention: reservoir underflow returns 0 samples, side-info error calls `mp3dec_init` and returns 0 [FETCH] | frame result | position accounting needs spf per frame | **rewrite** (3.5, 3.7) | none | the behaviour change D13 |
| 12 | Header-change handling: a changed bitrate or channel mode alone does not reset state; other changes zero the state [FETCH] | VBR works, rate switch resets | same | **keep the rule**, report DISCONT and RATE_CHANGED | none | channel-count change mid-stream: vectors (`he_mode`) |
| 13 | `MINIMP3_NONSTANDARD_BUT_LOGICAL` (mono `qmf_state` handling in `mp3d_synth_granule`) [FETCH; correctness not assessed] | synthesis-state quirk | n/a (no synthesis) | **n/a, removed with the synthesis** | none | none |

### 3A.3 Measured size

I did **not** compile minimp3 for rv32 in this task (documents only). Sizes above are [FETCH] counts or [EST]. WP-01 compiles pristine float minimp3 `-march=rv32im -O2` (with `MINIMP3_ONLY_MP3`, `MINIMP3_NO_SIMD`) with the review's tools and reports: code size per function, `tabs` count, `.rodata`, and the stack peak (scratch 16.3 KB on the stack is the known figure), to settle the RAM estimate of section 2.3 before P2 starts.

### 3A.4 Which edits when

| Phase | In scope |
|---|---|
| P2 (front end) | copy needed functions only (M1-M8) into the derived tree; remove SIMD, Layer I/II, float output, `_ex`, find_frame, builtins; float to fixed (3.2); arena struct; tables extractor; status codes; stage tests against the oracle |
| P3 (back end) | M9-M12 new; layout [slot][subband]; zero-band skipping; pre-gain folded into M5 |
| P4 | `MP3V2` flag, API shim, self-test, counters, cold placement of intensity/mixed |
| Later (P5-P6 or after) | ring-direct sink; zero-copy bit reading from the stream ring; granule-step API (worst-frame time halved); Huffman table order tuned from library statistics; optional CRC check; free format if ever needed |

### 3A.5 Rule for the back end

The back end (M9-M11) is written from the standard and the unit ABI. Authors may read minimp3 (CC0) for the map in 3A.1 but **do not read Helix's `imdct.c`, `dct32.c`, `subband.c`, `polyphase.c`** (RPSL, and the owner's ownership goal); Helix is used only as an output oracle. This is a convention, not a guarantee (an AI author cannot prove it never saw Helix, as D-A01 records); the similarity audit of section 7.4 is the evidence.

---

## 4. Verification

### 4.1 The float64 reference model (reconciling the review and the owner's request)

The review advises dropping a separate float model under (d) because float minimp3 is the reference; the owner asked for a float64 ISO-derived one. Plan: **a float64 model of the back end only**, written from the standard (WP-06): antialias, direct O(n^2) IMDCT with the standard's windows and block switching, overlap, frequency inversion, direct matrixing, 512-tap synthesis with the standard's D (from `tools/data/mp3_synthesis_window.txt`), and a switch for the unit's arithmetic (clip16 and rounding as in `mp3_poly_model_core.h`). Its input is a spectrum. **Reference decoder for accuracy = float minimp3's front end (float32, error about 6e-8 relative, far below the 8.8e-6 full-scale bound) feeding the float64 back end**, plus the vectors' own reference output. Cost 3-4 sessions less than a full float64 decoder.
**Common-mode risk:** the front end of the device under test and of the reference are both minimp3-derived, so a table typo or misreading inherited from upstream would not show in that differential. It is caught by three independent checks: ISO vectors with their own reference output (where obtainable), Helix (independent code) end to end, and an encode-decode round trip of known signals through LAME output where available.

### 4.2 What ISO/IEC 11172-4 compliance requires, and at which point (**every statement here must be checked against the standard text; [MEMORY]**)

- Full accuracy (as I recall): RMS of the difference to the reference decoder below 2^-15/sqrt(12) of full scale (8.8e-6, 0.29 LSB at 16 bits) **and** maximum absolute difference at most 2^-14, on a defined test stream. Limited accuracy: RMS below 2^-11/sqrt(12) (1.41e-4, about 4.6 LSB), no maximum. Which stream and whether the reference output is wider than 16 bits: **not known to me**.
- 2^-15/sqrt(12) is the RMS of ideal 16-bit rounding. A decoder that ends in a 16-bit clamp, compared with a wide reference, sits at the bound; two correct 16-bit outputs agree except at rounding boundaries.
- **Comparison points (proposal).** (i) Pre-rounding: the unit model's unclipped, unrounded sum (the host model exposes the 64-bit sum) against the float64 reference: measures decoder arithmetic only; gate = full-accuracy bound with margin (aim 10x under: our arithmetic error is about 2^-24 relative). (ii) End to end 16-bit against the vector's reference PCM and against the float64 reference rounded: gate = limited accuracy everywhere, and at most 1 LSB maximum difference with a count of 1-LSB differences reported (Helix's figures measured in the same harness are the bar). The first claim a release makes is "meets limited accuracy on all vectors and full accuracy before the final 16-bit rounding", not "ISO full-accuracy compliant", until the text confirms it.
- WP-07 checks this clause; it needs the standard (paywalled): the owner supplies the text or a pointer (D14).

### 4.3 Vector sources and licences

| Source | Licence status | In the repo? |
|---|---|---|
| minimp3 repository `vectors/` (names include `compl.bit`, `he_*`, `si*`, `sin1k0db.bit` [review]; a `vectors/performance/` set incl. `MEANDR90.mp3` [READ Stage 0, which calls the set CC0]) | **not covered by CC0** if ISO material (review); Stage 0 says CC0: **contradiction, unresolved**; each file's origin unknown | **No.** Download script with pinned SHA-256 into the ignored `third_party/minimp3/vectors/`; local only |
| Already committed: `sim/common/test_mp3.h`, `test_mp3_128.h` (168 KB, listed in `docs/ATTRIBUTIONS.md` as minimp3 test vectors, CC0, "test data, not shipped") [READ] | same question | flag to the owner (D4): confirm or remove |
| ISO/IEC 11172-4 and 13818-4 streams | copyright ISO, redistribution unknown | local only if the owner supplies them |
| Own **synthetic streams** from a stream builder (WP-08): random valid frames covering every table selection, linbits, count1 A/B, all block types, mixed, MS, intensity, LSF, scfsi, preflag, `subblock_gain`, VBRI/Xing/Info headers, tags, tails | ours (MIT) | **Yes** (small, generated on demand; golden hashes committed) |
| LAME-encoded files from the owner's music | data, not code (LAME is LGPL; output is not copied code); owner note | not committed; skipped when absent |
| Pocket-recorded loopback | existing practice | no |

### 4.4 Differential tests

- **Against float minimp3 (pristine plus an oracle patch for stage dumps, host only):** per granule, spectrum after requant, after stereo, after antialias, after IMDCT, subband samples; fixed vs float within stage tolerances (requant 2^-20 relative, IMDCT chain 2^-22), on vectors, synthetic streams and the library.
- **Against Helix (end to end):** same frames, same status and resync decisions; PCM within the band measured Helix-vs-float64 (not bit-exact; bit-exactness with Helix is not a goal); **lag-0 alignment** (cross-correlation peak at 0) and equal sample counts on the whole library.
- Disagreements are classified (table error, rounding, bug in either) as the Talos rewrite did.

### 4.5 Golden hashes and mutation tests

- Golden SHA-256 of the decoder's PCM (host, unit model) per vector, per synthetic stream and per library file under `skip when absent`, plus per-stage hashes; a re-golden needs an audit entry. Because firmware and host share one C source, host equals rv32 bit-exactly (checked by running the rv32 simulator on a subset).
- **Mutation tests, red then green, per stage** (as `sim/` does): off-by-one in a scale-factor band edge; wrong long-window half for start/stop; short-block window half; skipped antialias butterfly; MS rounding; sign of the odd subband inversion; a dropped reservoir byte; wrong linbits width; missing sign bit; wrong `subblock_gain` shift; preflag table offset; DCT word order (the unit ABI); pre-gain applied twice; zero-band skip off by one subband. Each must fail a named check.

### 4.6 Fuzzing and soak

- **ASan + UBSan fuzz** (libFuzzer or an equivalent loop, host): random bit flips, truncation, junk between frames, `main_data_begin` beyond data and before the stream start, region counts and `big_values` out of range, zero and maximum frame sizes, rate and mode switches (MPEG-1/LSF, mono/stereo) mid-stream, a frame size that changes with the padding bit, Xing/VBRI/Info parsing, ID3v2 with footer and unsynchronisation, trailing ID3v1/APE. Pass: no sanitizer finding, no infinite loop, bounded instructions per frame (asserted), resync within a stated number of bytes (review), the frame-count invariant (samples out equals frames x spf, WARMUP and CONCEALED frames included). Corpus seeds from the stream builder; upstream minimp3's known bug fixes since the pin are added as regression streams (7.5).
- **Soak:** host 10^5 random frames per mode; Pocket 60 minutes (Phase 5).

### 4.7 Pocket checks (existing rows; the owner approves any card write)

FULL Check `SR_T_LOAD` and `SR_T_LOAD2` (`dec_pct`, push %, busy %); Info heap and arena peak (the arena peak row reads 0 on some builds, B-586: check); `UNDER RING`, `HW UNDER EV`, `GOV PANIC`; poly counters `slots`, `mismatch`, `timeout`, `silent` (must stay 0 on the healthy run; the self-test of 3.4 adds one row); tempo 1.25x-1.75x on the mono speech file and stereo; gapless six-file equality (`pairs_total`, P3); seek, skip silence (lead uses seek); the Test Album; a 22.05 kHz file once the drain fix is in; `tools/lab/cymo_loopback.py` for waveform checks. Never claim a result as hardware-verified until it ran on a Pocket; the DEV core is named `TAU DEV NN` and goes through `tools/install_dev_core.py` (dry run first, `--card /Volumes/Pock`).

---

## 5. Phased plan

### 5.1 Phases, gates, effort

| Phase | Work | Entry gate | Exit gate | Sessions |
|---|---|---|---|---|
| R0 (outside this plan, separate sessions) | non-44.1 kHz drain fix in `aout.c`; mono through the unit; Pocket check of both | running | both merged, host tests green | 2-3 (not counted) |
| P0 Groundwork (host only, parallel) | WP-01..WP-08: vendor pristine minimp3 and oracle, unit ABI, library scan, rail count, cache model, float64 back-end model, ISO text check, stream builder | D-A01 recorded | oracle builds and decodes the library; ABI document passes its test; scan and rail numbers in a report; cache-aware `rvprof` reproduces the review's table within 2 % | 2-3 |
| P1 Specs | WP-09..WP-12: tables extractor, output contract and API, driver edge tests, fixed-point format study | P0 WP-01, WP-02, WP-04, WP-06 | reviews C1 (contract), C2a (formats) and C3 (unit ABI) passed; synthetic streams cover all features; D2, D4, D11, D13 decided | 3-4 |
| P2 Front end | WP-13..WP-15 | P1 | each stage passes golden and mutants vs the oracle; ASan fuzz clean on the front end; function map complete | 4-5 |
| P3 Back end (parallel with P2 after the spectrum interface is fixed in WP-12) | WP-16..WP-18 | WP-02, WP-06, WP-12 | accuracy in both domains; cache budget test; instruction gate on the rv32 simulator (stereo at most 200 k, mono at most 150 k) | 4-5 |
| P4 Integration | WP-19..WP-21 | P2 and P3 | `MP3V2=1` builds; `make test-host` and `sim/test_p2_mp3.py` equivalents pass; heap gap at least 35.5 KB; stack bound; model gates; the `MP3V2=0` image is byte-identical to today's (`tau.rom` SHA-256 as B-1168 recorded) | 2-3 |
| P5 Pocket gates | WP-22, WP-23 | P4 and owner approval of the card write | no regression on any row of 4.7; `dec_pct` at most 20 %; owner listening pass; 60-minute soak; six-file gapless; seek; tempo | 3-4 (owner time dominates) |
| P6 Close-out | WP-24, review C4 | P5 | shipped image links v2 only, Helix flag kept, provenance and notices done, audit entries | 1-2 |
| **Total (P0-P6)** | | | | **19-26 (expected 21)** |

Calendar: 6-9 weeks. P0 work packages run in parallel in separate worktrees (about 8 agent runs in about 2-3 working sessions); P2 and P3 run in parallel (saves about 4 sessions of calendar); the owner's Pocket time (P5) is the critical path.

**Parallelism.** Separate worktrees (`git worktree add ../tau-alpha-mp3v2-<wp> <branch>` or the Agent tool's worktree isolation; project rule 1), each work package its own branch, merged by the coordinator in dependency order. Helix-reading packages (measurements) and back-end authors are different sessions.

### 5.2 Phase gates in one place

Entry/exit gates are in the table above. Cross-phase rules: (a) nothing before P4 touches `fw2/` build outputs, RTL, a fit or a card; (b) every package ends with an audit entry (B-NNN with an evidence label and what was not run on a Pocket); (c) commits as the owner, no AI attribution (rule 9), `git branch --show-current` before every commit, no `git reset`/`git stash` on the shared tree (rule 2).

### 5.3 DISPATCH TABLE (ready-to-brief work packages)

Tiers: **haiku** (mechanical), **sonnet** (implementation and measurement), **opus** (numerics, specification, judgment). "Done-when" is the acceptance test the coordinator checks.

| ID | Title | Inputs | Deliverable | Done-when | Depends | Tier |
|---|---|---|---|---|---|---|
| WP-01 | Vendor pristine minimp3 and build the float oracle | pinned commit `48e3a085...`, `third_party/libhelix-mp3` layout (UPSTREAM.md, UPSTREAM.sha256), `tools/helix_tree.py`, `.gitignore` lines 66-68 | `third_party/minimp3/` (minimp3.h, minimp3_ex.h, LICENSE, UPSTREAM.md, UPSTREAM.sha256; vectors dir still ignored); `.gitignore` edited; `third_party/minimp3-tau/oracle.patch` (stage dumps, host only); `tools/mp3v2/oracle_tree.py`; host harness that decodes a file to PCM and to stage dumps; rv32 `-O2` size and stack report of pristine float minimp3 with `ONLY_MP3`+`NO_SIMD`; re-verification of every [FETCH] statement of 3A | pristine file hashes match UPSTREAM.sha256; harness decodes the Test Album and matches Helix within 1 LSB RMS; the report states code bytes per function, `tabs` count and stack; `test-host` green | none | sonnet |
| WP-02 | Window-unit ABI document from the standard | `tau_mp3_poly.sv`, `sim/mp3_poly_model_core.h`, `tools/mp3_window_derive.py`, B-1168 provenance doc, section 3.4 | `docs/features/MP3_POLY_UNIT_ABI_2026-xx.md` and `sim/test_mp3_poly_abi.py` (float64 matrixing and synthesis vs the unit model, with the unit self-test constants) | ABI states the 32 unique words (index map into the standard's V), scale 2^23, rounding, clip, wrap rule, history/clear/timeout; the test is green and has red mutants (wrong order, wrong scale); `mp3_poly_*` tests unchanged | none | opus |
| WP-03 | Library header scan | the owner's `test music/` (skip if absent), `docs/features/MP3_DECODER_V2_EXPERT_REVIEW...` Q8 | `tools/mp3v2/scan_library.py` plus a report: version, rate, mode, mode extension (intensity), CRC, free format, Xing/Info/VBRI, ID3v2 footer/unsync/extended, ID3v1/APE tails, block-type usage if the oracle is present | the report reproduces the review's 59-file findings; any LSF/intensity/CRC file is listed; Xing TOC vs true frame offsets computed (U7) | none | haiku (script) then sonnet (review) |
| WP-04 | Rail and headroom count on the library | oracle (WP-01), Helix host harness (`sim/p2_mp3_harness.c`), the library | report: per track rail samples in Helix output vs float oracle peak above 1.0; decoder-created clipping estimate; max |xr| per track (input to WP-12); recommendation for D11 | numbers for every track; method states what is an upper bound | WP-01 | sonnet |
| WP-05 | Cache-aware `rvprof` committed | `tools/mp3_v2_measure/`, review Q1 (4 KiB direct-mapped I$/D$, 32 B lines, write-through no write-allocate, 10-15 cycle miss) | cache model in `rvprof.c`, `measure.py` with `TAU_POLY_FW/REQUIRED` and a unit stub, the table of 2.1 reproduced; a layout A/B harness (column-strided vs [slot][subband]) on a synthetic loop | reproduces the review's five rows within 2 %; an I$-alias checker reading an ELF map | none | sonnet |
| WP-06 | Float64 back-end reference model | standard formulas (transcribed in the doc), `tools/data/mp3_synthesis_window.txt` | `tools/mp3v2/ref_backend.py` (or C): antialias, direct IMDCT, overlap, inversion, matrixing, synthesis, unit-arithmetic switch; metric tool (RMS, max, count of 1-LSB differences, both comparison points) | model fed with the oracle's spectra reproduces the oracle's float PCM within 1e-5 FS; unit-arith mode equals `mp3_poly_model_core.h` on random data | WP-01 | opus |
| WP-07 | ISO/IEC 11172-4 clause check | owner's copy of the standard or the clause text | `docs/features/MP3_COMPLIANCE_CLAUSE_CHECK.md`: tolerance statements confirmed or corrected, test streams named, comparison domain | every [MEMORY] statement of 4.2 marked confirmed or corrected | owner supplies text (D14) | sonnet |
| WP-08 | Synthetic stream builder | oracle (WP-01), tables extractor (WP-09) | `tools/mp3v2/stream_builder.py` + committed small corpus and generator seeds; coverage report (table selections, block types, features) | every stream decodes with the oracle without error; coverage 100 % of table selections, all block types, MS, intensity, LSF, mixed, scfsi, preflag | WP-01, WP-09 | sonnet |
| WP-09 | Huffman and band tables extractor | minimp3.h tables as data (pinned), standard's table list | `tools/extract_mp3v2_tables.py`, generated `tables.h` in our layout, `sim/test_mp3v2_tables.py` (Kraft sums, counts, max code length, value range, round trip with the builder), provenance header like B-1168 | all selections pass the structural identities; the generated file is reproduced byte for byte; records exact lines read | WP-01 | sonnet |
| WP-10 | Output contract and decoder API | 3.5-3.8, `mp3_frames_core.h`, `mp3_trim.h`, `aout.h` | `docs/features/MP3_V2_CONTRACT.md` (struct, flags, status codes, trim rule, pre-roll, concealment, container rules) and `fw2/decode/mp3v2/mp3v2_api.h` (header only) | contract review C1 passed; the questions of section 9 answered | WP-03 (results), WP-04 | opus |
| WP-11 | Driver edge tests on Helix | `mp3_frames_core.h`, WP-08 corpus | `sim/test_mp3_driver_edge.py`: ID3v2 footer/extended, VBRI, APE/ID3v1 tails, junk, frame-count invariant, six-file trim equality (skip if absent), seek pre-roll equality | tests green on Helix (they define what v2 must preserve); failures listed as driver defects, not fixed here | WP-08 | sonnet |
| WP-12 | Fixed-point format study | WP-04 max |xr|, WP-06 model, oracle | `docs/features/MP3_V2_FIXED_POINT_FORMATS.md` and a host C model of the Q pipeline: spectrum format (Q4.28 or block exponent), requant error exhaustive test, IMDCT chain error, saturation counters | proves requant error <= 2^-20, chain <= 2^-22, no saturation on library and vectors; decision recorded; interface struct between front and back end fixed | WP-04, WP-06, WP-09 | opus |
| WP-13 | Front end A: header, bit reader, side info, reservoir, scale factors | WP-09, WP-10, WP-12, oracle stage dumps, minimp3 functions M1-M5 | `fw2/decode/mp3v2/` files + `sim/test_mp3v2_front_a.py` + mutants; unaligned-access audit | side info, scale factors and band gains equal the oracle on all streams; mutants fail; ASan clean | WP-09, WP-10, WP-12 | sonnet |
| WP-14 | Front end B: Huffman + fused requantisation | WP-13, WP-12 | same, M6 | spectrum within 2^-20 of oracle on all streams and the library; all 15 big-value codebooks + count1 A/B and linbits covered by the corpus; mutants fail | WP-13 | sonnet |
| WP-15 | Front end C: stereo, reorder, LSF, intensity (cold-ready) | WP-14 | M7, M8 | MS/intensity/reorder vs oracle; LSF streams pass; cold placement attribute ready | WP-14 | sonnet |
| WP-16 | Antialias + IMDCT + overlap (own) | WP-06, WP-12, the standard formulas | derivation note `docs/features/MP3_V2_IMDCT_DERIVATION.md`, `fw2/decode/mp3v2/imdct.c`, host tests, zero-band skipping | stage error <= 2^-22 vs float64; skip path bit-identical to full path; 3 block types + mixed; mutants fail; instruction count per frame recorded | WP-06, WP-12 | opus |
| WP-17 | DCT32 + unit glue + mono + pre-gain | WP-02 (ABI), WP-16 output layout | `dct32.c`, `unit_glue.c`, host test through the unit model | PCM equals float64 within the ABI test bound (domain i) and 1 LSB (domain ii) on all streams; mono via the unit; pre-gain unity is bit-identical to no pre-gain | WP-02, WP-16 | opus |
| WP-18 | Granule loop, state reset rules, concealment, golden hashes | WP-13-17 | `mp3v2_decode.c` (granule loop, status codes, WARMUP/CONCEALED), golden hashes, seek pre-roll test | sections 3.5-3.7 behaviours pass; frame-count invariant; golden hashes committed | WP-15, WP-17 | sonnet |
| WP-19 | `fw2` integration behind `MP3V2` | WP-18, `fw2/build.sh`, `mp3_frames_core.h`, `poly_hw.c`, `fw2/platform/link.ld` | build flag, API shim keeping the driver shape, unit self-test, counters, cold placement of intensity/mixed, `fw2/diag_features.json` entries if any diagnostics (rule 5) | `MP3V2=0` gives today's `tau.rom` hashes; `MP3V2=1` builds; `check_heap_gap.py` at least 35.5 KB; `make test-host` green | WP-18 | sonnet |
| WP-20 | rv32 gates: cycles, cache, stack | WP-05, WP-19 | report: instruction and model cycles per file (stereo, mono), cache misses, `-fstack-usage` call-graph bound, I$-alias test | gates of section 1 met or the shortfall quantified | WP-19 | sonnet |
| WP-21 | Fuzz, mutation, soak (host) | WP-08, WP-18 | ASan/UBSan fuzz target and corpus, `test-host` integration, soak run | 10^5 frames per mode clean; invariants hold | WP-18 | sonnet |
| WP-22 | DEV core packaging and Pocket checklist | WP-19-21; `docs/CARD_INSTALL_PROCEDURE.md`; `tools/install_dev_core.py` | a packaged `TAU DEV NN` and a checklist of 4.7 | owner approves the dry run; package built | WP-21 | haiku (procedure) with a sonnet check |
| WP-23 | Pocket gates and optional ring-direct sink | owner's Pocket results | audit entries with evidence labels; ring-direct sink spec and patch if the owner wants the 4.6 KB | all 4.7 rows green; owner listening pass | WP-22 | sonnet |
| WP-24 | Provenance and removal-from-shipped close-out | section 7, review C4 | `docs/PROVENANCE.md`, `NOTICE.md`/`docs/ATTRIBUTIONS.md` entries, similarity report, the function map check, flag default, audit entry | C4 passed; notices merged | WP-23 | sonnet |

**First three to brief:** WP-01, WP-02, WP-03 (no dependencies, host-only, and WP-01 and WP-02 gate everything else).

---

## 6. What lands first, and what must be decided or specified before the back end

**Lands first (separate sessions, not duplicated here):** (1) the **non-44.1 kHz output-rate fix** in `fw2/aout/aout.c` (today `rate_hz == 44100 ? 44100 : 48000` drains 32/24/22.05/16 kHz at the wrong speed [READ `aout.c:38`]); it is the precondition for any LSF or 32 kHz test being meaningful; (2) **mono through the window unit** (push twice, read L): needed so the unit's ABI and the host unit model cover mono before v2 depends on it. Neither branch is merged; this plan assumes both and states the ABI consequence (3.4 item 4). If the mono fix changes the push protocol, WP-02 follows it.

**Must be decided or specified before any back-end stage is written:**
1. The window-unit ABI (WP-02) and its review (C3).
2. The fixed-point formats and the spectrum interface between front and back end (WP-12, C2a).
3. The output contract (WP-10, C1), including D13 (concealment behaviour change).
4. D2 (coverage scope), D11 (where ReplayGain applies), D4 (vector licences).
5. The cache layout A/B result (WP-05) for the [slot][subband] choice.

---

## 7. Provenance

### 7.1 Vendoring approach (decided)

- **Two trees, not one patched tree.** (a) **Pristine** `third_party/minimp3/`: `minimp3.h` and `minimp3_ex.h` byte-identical to upstream at the pinned commit (`48e3a085f0e1a99446b34614e1e7f17f09dc2e82`, the commit B-1168 pinned; `minimp3.h` SHA-256 `eb4b4481...875a` per B-1168), the upstream `LICENSE` (CC0 1.0, SHA-256 `6a1ee543...d01c9`), `UPSTREAM.md` (URL, commit, retrieval date) and `UPSTREAM.sha256`, mirroring `third_party/libhelix-mp3/`. Used for the float oracle and the vector downloads; host only, never linked into firmware. Oracle stage dumps are a small **patch applied at build time** into `build/` (like `tools/helix_tree.py`), never an edit of the pristine files. (b) **Derived** `fw2/decode/mp3v2/`: our code, listed function by function against upstream in `docs/features/MP3_V2_MINIMP3_MAP.md`. The heavy edits (float to fixed, restructure, removal) must not masquerade as a patch of upstream, so the derived tree has its own file names and a header on every derived file: "Derived from lieff/minimp3 (CC0 1.0), commit <pin>, function(s) <names>; converted to fixed point and restructured by Tau Alpha; the original is dedicated to the public domain".
- **`.gitignore` must change.** Lines 66-68 currently say "minimp3, picorv32 -> evaluated and rejected (Stage 0)" and ignore `third_party/minimp3/` [READ]. WP-01 replaces that entry by `third_party/minimp3/vectors/` (still ignored: vectors are not ours to redistribute) and updates the comment. Not changed by this plan (documents only).
- **Upstream bug fixes.** The pin is explicit. `tools/mp3v2/upstream_watch.py` (WP-01 or WP-24) lists upstream commits since the pin that touch functions named in the map (via the repository's compare API), run before every release and quarterly; each relevant fix is ported by hand into the derived tree with an audit entry and a regression stream added to the corpus; bumping the pristine pin is its own commit followed by an oracle re-run of every test. Upstream's issue list is scanned once in P1 for known decoder defects (fuzz-found crashes, off-by-one in tables) that apply to the functions we take; any found become mutation or regression cases.
- **Test vectors:** never vendored (4.3).

### 7.2 CC0 verification (what is done and what is open)

Done [READ B-1168 and the window provenance doc]: the `LICENSE` file is CC0 1.0 legal code, hashed; the file header carries the public-domain dedication and links the CC0 page; **no author name, "based on" notice or other credit is found in `minimp3.h`** [FETCH]. Open (U5, WP-24): CC0 binds the author's rights only. If upstream's code or tables were themselves copied from copyleft decoders (for example the FFmpeg- or mpg123-family code), the CC0 label would not clear them. The README and the issue/credit history must be read once for derivation claims, and WP-24 may run the similarity tool of section 7.4 against minimp3 for the record. **No legal conclusion is drawn here (D-T20 item 5: the assistant draws none).**

### 7.3 What is ours

Back end (M9-M12), the output contract, the unit glue and ABI document, the stream builder, all tests, the table-generator scripts (formulas, so no data copied), the fixed-point design. Derived from minimp3 (credited): M1-M8 as converted functions, and table data extracted by script from the pinned file with its exact line ranges recorded (as B-1168 did). Not taken from Helix at all in the shipped image: after the flag default is 1 the shipped link contains no Helix; the pristine Helix tree stays in the repo for the oracle and the fallback until the owner decides otherwise (D12).

### 7.4 Notices and similarity

- `docs/PROVENANCE.md`: dated entry with pin, hashes, the function map and the "read for ideas" list (including the optional SIMD-map reading of 3A.1).
- `NOTICE.md` and `docs/ATTRIBUTIONS.md`: replace the "rejected" minimp3 line with: "The MP3 decoder's front end is derived from lieff/minimp3 (CC0 1.0 Universal), commit 48e3a085f0e1a99446b34614e1e7f17f09dc2e82, converted to fixed point. Credited as a courtesy; the dedication places no condition on use." The window-ROM sentence from B-1168 stays. Draft text only; the owner folds it in (no change made here).
- **What remains of `MP3_DECODER_V2_CLEANROOM_PROCESS.md` under (d):** kept: section 3 (tables are extracted as data from a recorded source and verified by structural identities; windows and constants from formulas), section 4's method (token winnowing, normalised identifiers, k = 12, window 8) **scoped to the back end only** and with thresholds **calibrated first** by running it on independent public decoders against each other (review: minimp3 vs Helix vs pvmp3) so that a hit means something for DSP butterflies, section 5 process rules (own branches, commits as the owner, stage tests use the standard and the float model, not Helix output), section 6 evidence checklist. Dropped: the contract-author/implementer/verifier separation and the "implementer never sees Helix" worktree control (an AI cannot credibly claim it, D-A01). The three clean-room documents remain valid as history; they now say "superseded by D-A01 where they conflict".

---

## 8. Risks, mitigations, rollback

| Risk | Likelihood | Effect | Mitigation |
|---|---|---|---|
| Requant fixed-point format wrong (range or error) | medium | accuracy or clipping | WP-12 measures max |xr| and exhaustive error before P2 starts; block-exponent fallback |
| Unit ABI mapping error (V order, scale) | medium | wrong audio | WP-02 test against float64 synthesis; C3 review; self-test at runtime; silence plus counter on failure |
| Instruction gate missed because the back end is not as lean as modelled | medium | dec_pct above 20 % | WP-05 cache model early; zero-band skip; layout A/B; the model's +/-20 % band is stated; fallback to Helix flag |
| Common-mode error with the oracle (front end derived from the oracle) | medium | undetected spec misreading | ISO vectors, Helix end to end, encode round trip (4.1) |
| Vector licences (ISO material, unresolved CC0 claim of Stage 0 vs review; 168 KB already committed) | certain | cannot commit vectors | local only; own synthetic corpus; owner decision D4 |
| CC0 label does not clear upstream's own sources | low-medium, unknown | provenance challenge | U5 check, similarity run, no legal conclusion drawn |
| Concealment change (D13) alters behaviour on corrupt files | low | audible silence instead of skipped audio | approve explicitly; Pocket check |
| LSF, intensity, mixed untested by any real file | high | bugs in rare paths | vectors and the stream builder; cold placement bounded; Helix flag fallback |
| Cold placement runs about 30x slower than hot | low | per-frame spike | rule 3.2 (bounded worst case); gate in WP-20 |
| Unaligned access traps or uncached accesses in upstream bit code | low | crash | audit in WP-13; review R3 question |
| I$/D$ thrash | medium | misses above 1,500/1,800 | design rule 3.3, alias checker, A/B |
| Scope creep (output stage seams S1, S2, S5, S8) | high | stall | scope line unchanged (scoping 10.4): separate follow-ups |
| Schedule slip from owner Pocket time | high | calendar | P0-P4 are host-only; P5 batched into two Pocket sessions |
| Two decisions share the id D-A01 in `docs/DECISIONS.md` (`tau-assets.bin` at line 101 and the MP3 decoder at line 191) | certain | ambiguous references | renumber one at merge (not done here) |

**Rollback.** Source flag `MP3V2=0` builds today's Helix image byte-identically (the gate in WP-19); nothing in RTL changes at any phase; the decoder is host-first so a failed phase costs only host work; the owner can stop after any phase with no shipped change. The shipped image links only one decoder (RAM), so a field rollback is a re-flash of the previous release, not a runtime switch. **Two-release fallback (D-A01):** the source flag stays for two releases after the first release that ships v2; then the Helix glue and flag are removed from `fw2`, and the pristine Helix tree stays as host oracle until D12 is decided. Provisional ship rule: release N ships v2 only if P5 is green; otherwise it ships Helix and the flag default stays 0.

---

## 9. Expert review checkpoints (exact questions)

**C1 Contract review (end of P1, after WP-10). Reviewer who has not seen the plan's reasoning.**
1. Can every behaviour in the contract be implemented from the standard plus the contract alone? Which statements depend on an implementation choice?
2. Does one frame in always produce spf samples of position (WARMUP, CONCEALED, Info/Xing/VBRI frame)? Is the trim formula still right when a mid-stream frame is concealed?
3. Is pre-roll N = max(1, ceil(511 / (frame_bytes - 4 - side_info))) sufficient and necessary for bit-exact seek landing, for MPEG-1 and LSF?
4. Is the pre-gain placement (per-band mantissa, before MS) equivalent to output scaling for all stereo modes including intensity?
5. Are the rate tag, `src_pos`, DISCONT and RATE_CHANGED enough for the ring, tempo staging and gapless join?

**C2 Numerics and fixed point (a: after WP-12; b: after P3).**
1. Is Q4.28 with saturation enough for the ISO bound at 320 kb/s peaks and for the owner's hot masters, or is a block exponent required? What is the largest |xr| in the vector set?
2. Are the stage error budgets (requant 2^-20, IMDCT chain 2^-22) consistent with the end goal (arithmetic error at least 10x under 2^-15/sqrt(12) before the final rounding)?
3. Is rounding applied once per multiply stage and in the same direction as the unit's? Any bias accumulation across 36 slots?
4. Does the zero-band skip preserve bit-exactness including the overlap tail and block-type switches?
5. Is saturation placed where the standard allows, and are the saturation counters reachable?

**C3 Window-unit interface (after WP-02).**
1. Is the claim "32 unique words V[0..15], V[33..48]" and the index map correct against the standard's matrixing?
2. Is the word scale 2^23 and the wrap rule (64-bit sum below 2^51) correct, and what is the largest legal word?
3. Does mono-by-pushing-twice leave the history of the second channel harmless? Does the running mono fix change the protocol?
4. Is the self-test (impulse slot, constant table) sufficient to catch a wrong ROM, wrong order and a stuck unit?
5. Does keeping today's push order cost the DCT32 more than a different order would (instruction count), justifying a fit?

**C4 Pre-removal review (P6, before Helix leaves the shipped build).**
1. Do all rows of 4.7 pass on a Pocket with v2, including a 22.05 kHz file and tempo?
2. Does the function map list every derived function, and does the similarity audit (calibrated thresholds, back end only) meet them?
3. Are the notices and the pinned commit consistent across `PROVENANCE.md`, `NOTICE.md`, `ATTRIBUTIONS.md` and the file headers?
4. Is the `MP3V2=0` image still byte-identical, and is the fallback documented for two releases?
5. Is the open CC0 derivation question (U5) closed or explicitly accepted by the owner?

---

## 10. Owner decisions and unknowns

### 10.1 Decisions (scoping D1-D10 plus the review's D0 and D11, plus new)

| # | Decision | Status | Recommendation |
|---|---|---|---|
| D0 | Base: minimp3-derived (d) or clean room (a) | **resolved** (D-A01: (d)) | |
| D1 | Approve replacing Helix on ownership and RAM grounds (plus about 35 % decode CPU from removing the glue) | **resolved** (D-A01) | |
| D2 | Coverage scope | **open** | support MPEG-1, LSF, intensity, mixed (flags; intensity and mixed cold); free format gated; CRC stepped over; verify with vectors and the stream builder; library scan (WP-03) first |
| D3 | Software window mandatory or not | **resolved in substance** by D-A01 (glue to the unit) plus the mono fix; confirm | v2 has no software window; unit absent means silence plus counter (as Player 2 today); Helix flag build keeps its own |
| D4 | Vector sources and licences | **open** | local only, pinned download script, commit only own synthetic streams and hashes; owner to rule on the already committed `sim/common/test_mp3*.h` |
| D5 | Output contract fields | **open** (resolved at C1) | the struct in 3.5 |
| D6 | Helix fallback for how long | **resolved** (D-A01: two releases, source flag) | shipped images link v2 only; sub-question D12 |
| D7 | Scope line for output-stage seams | **resolved** (D-A01 keeps conversion out; drain fix and mono fix first) | S1 low, S2 a separate project |
| D8 | 48 kHz in the decoder | **resolved** (no; D-A01) | |
| D9 | A stated CPU need for DCT32/IMDCT kernels or custom instructions | **open** | none now; revisit with P5 measurements (D-T12 style) |
| D10 | Roles of author and verifier | **resolved** (superseded under (d), D-A01) | author plus verifier; AI caveat recorded |
| D11 | Where negative ReplayGain is applied (decoder pre-gain or after the clamp) | **open** | decide after WP-04's rail count; the mechanism ships either way (default unity) |
| D12 | After the two releases, does the pristine Helix tree leave the repository or stay as the host oracle | **open (new)** | stay as oracle (host only, attributed) until the owner decides; it is not in the shipped image |
| D13 | Concealment: silence for a corrupt frame instead of dropping its samples | **open (new)** | approve (3.5, 3.7) |
| D14 | ISO/IEC 11172-3/-4 (and 13818) text for WP-07 | **open (new)** | owner supplies the clauses or a copy; otherwise the compliance statement stays at "limited accuracy plus pre-rounding full accuracy, unverified against the text" |

### 10.2 Unknowns and the cheap way to resolve each

| # | Unknown | Cheap resolution |
|---|---|---|
| U1 | Compiled rv32 size and stack of minimp3, `tabs` count | WP-01 (an hour of compiling) |
| U2 | Cache miss penalty (10-15 cycles is the review's assumption) | calibrate the model with the first v2 Pocket run; until then +/-10 % band |
| U3 | Decoder-created share of the rail samples | WP-04 (float oracle peak vs Helix rails) |
| U4 | Licence and provenance of each minimp3 vector, and the two committed headers | read the upstream README and file names; ask the owner; WP-01 report |
| U5 | Whether minimp3 itself derives from copyleft code | read README/credits and history once; optional similarity run (7.2); owner decides |
| U6 | The tolerance clause of 11172-4 | WP-07 with D14 |
| U7 | Xing TOC seek accuracy on long files | WP-03 computes the error against true frame offsets |
| U8 | How a function is placed in the cold image | read `fw2/platform/link.ld` and the RAM_SHRINK plan (WP-19) |
| U9 | Ring-direct sink versus tempo staging and the `dv` union shared with FLAC | design note in WP-23; P4 keeps `pcm[]` |
| U10 | Maximum |xr| (spectral headroom) | WP-04/WP-12 |
| U11 | Whether Helix emits silence for the Xing frame in all cases and for VBRI | differential on the library and synthetic headers (WP-11) |
| U12 | The exact VexRiscv plugin set (no config record in the tree) | ask the owner for the SpinalHDL config; no plan item depends on it |
| U13 | Whether the 4 KiB cache sizes are the same on the shipped CPU as in the generated Verilog | review read lines 5721-5725, 6467-6468 of `VexRiscv_Full.v`; WP-05 re-reads them |

---

## 11. Contradictions found in the sources (for the owner and coordinator)

1. **Vector licence.** `STAGE0_RESULTS.md` calls minimp3's `vectors/performance/` set "CC0"; the review says the ISO-derived vectors are not covered by CC0. Unresolved (D4, U4).
2. **Duplicate decision id.** `docs/DECISIONS.md` has two entries named D-A01 (line 101 `tau-assets.bin`, line 191 the MP3 decoder).
3. **Gapless claim in the review.** The review says a decoder that drops underflow frames shifts every trim by 1,152; the current driver already counts such frames (`frame_idx++` when `used`) so Helix's warm-up frame does not shift the trim. What is lost is the audio length from the ring (a corrupt frame's samples), hence D13.
4. **Float model.** The review says drop the separate float model; the owner's brief asks for a float64 reference: reconciled in 4.1 (back end only).
5. **minimp3 return on underflow** is 0 samples [FETCH], which is exactly the behaviour the contract must not copy.
6. **Already-committed minimp3 test data** (`sim/common/test_mp3*.h`, listed as CC0 in `ATTRIBUTIONS.md`) predates and conflicts with the review's "do not commit vectors" advice.
7. **`.gitignore`** says minimp3 is "evaluated and rejected"; D-A01 now adopts it (changed in WP-01).
8. **Unit header comment** still says the ROM comes from Helix (B-1168 left it for owner approval); harmless but inconsistent with the provenance document.
9. `STAGE0_RESULTS.md` is at the repository root, while the task brief and D-A01 point to `docs/features/`: referenced by name only.
