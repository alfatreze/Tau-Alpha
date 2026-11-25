# MP3 decoder v2: independent expert review of the scoping package (2026-11-25)

**Reviewed:** `MP3_DECODER_V2_SCOPING_2026-11-25.md`, `MP3_DECODER_V2_REQUIREMENTS.csv`, `MP3_DECODER_V2_CLEANROOM_PROCESS.md`, `tools/mp3_v2_measure/`, audit B-1169 (commit `d0d64791`, fast-forwarded into this branch).
**Evidence labels:** **[RE-RUN]** I re-ran the package's own tool; **[MEASURED]** host measurement I made for this review (scratch tools, not committed); **[READ]** source or document read; **[MEMORY]** my recollection of a standard or library, not checked against the text; **[EST]** estimate. Nothing was fitted, installed, run on a Pocket or pushed. No decoder code was written.
**Audience:** like the scoping report, this review names Helix functions and internals. It must not go to a clean-room implementer.

---

## Verdict: GO WITH CHANGES

The decision to replace Helix is sound: it serves ownership (no RPSL), and it frees RAM. The package does not yet earn its main recommendation, though. It frames the choice as "clean-room rewrite or keep Helix" and never compares a real third option: **a fixed-point decoder built on the structure of the public-domain (CC0) minimp3**. That option needs no clean-room machinery, comes with a ready host oracle that needs no licence clearance, and costs roughly 40 % fewer sessions.

Several of the package's load-bearing numbers come from the wrong build configuration:
- **The CPU remainder is ~330-360 k model cycles per frame, not 255 k.** The profiler ran Helix without the hardware-window glue that Player 2 actually ships.
- **The "1.1-2.2x model optimism" is mostly that configuration error,** not memory behaviour.
- **The hot-RAM baseline is 66.8 KB, not 71.5 KB.**
- **The software window cannot be dropped for mono.** Mono MP3 still runs the software window today and is the most CPU-expensive case on the Pocket.

The review also found a live functional defect in the output stage. **Player 2 drains every source rate other than 44.1 kHz at 48 kHz**, so 32 / 24 / 22.05 kHz files would play at the wrong speed and pitch. The scoping describes this as "the hold path".

Go, on these conditions: correct the baselines; put the minimp3-derived fixed-point option (d) on the table as the recommended path; fix the 48 kHz drain bug and route mono through the window unit as separate small items; and widen the whole-engine contract with the seams listed in section 6.

---

## The ten most important findings (ranked by impact)

1. **A missing option, and the best one: a fixed-point minimp3-derived decoder (CC0)** [READ licence + source structure; MODEL of the cycle cost in Q2]. The licence is verified as CC0 1.0 (LICENSE file and file header). minimp3 covers MPEG-1/2/2.5, intensity, mixed blocks and free format, and ships the ISO-derived test bitstreams. Stage 0 rejected it only as *stock float code on a softfloat PicoRV32*. A float-free derivative on today's setup is modelled at about 165-200 k instructions per stereo frame against Helix's 274-300 k as shipped. Effort is 18-25 sessions against 35-50 for an honest clean-room rewrite. No clean-room process is needed, and stock float minimp3 stays available as a licence-clean host oracle.
2. **The profiled build is not the shipped build** [MEASURED]. `measure.py` builds Helix without `TAU_POLY_FW/TAU_POLY_REQUIRED` and then subtracts the window. The shipped path adds FDCT32 write logging (+16 k instructions per frame), the w0/w1 copy loops in `Subband` (22.9 k) and the push/read loops (27.1 k).
   - Measured remainder with the unit (stubbed): **274 k instructions / 329 k model cycles** (MacCunn 128 k), **300 k / 359 k** (Battle 320 k VBR), **294 k / 353 k** (48 kHz 320 k), against the package's 212 k / 255 k.
   - About 66 k instructions per frame (about 24 % of decode) is pure Helix-to-unit glue. This is the largest CPU lever, and any new decoder removes it.
3. **Model calibration: the 1.1-2.2x spread largely disappears** [MEASURED]. Against B-591 (h 4 / d 4 / i 7 / s 12 % of the CPU), the correct build models at **1.1-1.4x per stage, about 1.3x overall** (1.45x against the 30 % that includes profile ticks).
   - Adding a 4 KiB direct-mapped I$/D$ model (the real `VexRiscv_Full.v` configuration) gives 1,450-1,950 I$ and 1,800-2,000 D$ misses per stereo frame. At 10-15 cycles each that is about 35-55 k cycles, which closes most of the remaining gap.
   - The package's claim that "DCT32 and Huffman are memory-bound loops the model under-counts by up to 2.2x" is not supported: it compared Pocket FDCT32+copy+log against model FDCT32 without them.
4. **Mono is not covered by the hardware unit, so the software window stays linked and costs the most CPU of any case** [READ + MEASURED]. `subband.c`'s mono branch calls `PolyphaseMono`, the unit is stereo-only (`tau_mp3_poly.sv` header), and the ELF shows `xmp3_PolyphaseStereo` garbage-collected but `xmp3_PolyphaseMono` (2,496 B) and `polyCoef` (1,056 B) linked.
   - Mono speech: **390 k model cycles per frame, 242 k of it the software window, with 3.8 k D$ misses per frame**. Mono costs about 18 % more CPU than stereo.
   - Mono is exactly the audiobook/tempo use case. Pushing the mono channel twice into the unit (no RTL change) saves about 200 k model cycles per frame, **about 12 % of the CPU (model), about 15 % on a Pocket [EST]**.
   - The package's RAM target and D3 ("drop the software window, 7.5 KB code + 1 KB + 8.7 KB history") are wrong for mono unless this is done.
5. **Latent functional defect: Player 2 plays every rate except 44.1 kHz at 48 kHz** [READ]. `fw2/aout/aout.c:38`: `inc = (rate_hz == 44100u ? 44100u : 48000u) << 32 / CLK_HZ`. The old player used the real rate (`fw/player.c:1013`); the regression came with Player 2 S3 (`9d34e35a`/`c67ac0c8`).
   - Consequence: 32 kHz plays 1.5x fast, 24 kHz 2x, 22.05 kHz 2.18x, 16 kHz 3x, for MP3 and for FLAC ≤ 48 kHz. Not yet observed because the owner's test music is all 44.1/48 kHz (my scan of 59 MP3 files).
   - The package's section 9/10 and S2 say these rates "go through the zero-order hold at 27.8 dB SINAD". That is wrong: they go through the hold at the wrong rate.
   - Fix first, firmware-only (one line plus a host test). It is a precondition for D2 (LSF scope) to mean anything.
6. **The RAM numbers are overstated by about 4.7 KB** [MEASURED, `fw2/build.sh DIAG=0` rebuilt, `nm -S` of the linked ELF].
   - Linked Helix is code 28,844 B, read-only tables 12,614 B, data 640 B, arena 24,576 B and unit glue 160 B: **66.8 KB, not 71.5 KB**. The 33.5 KB code figure counts object sizes before `--gc-sections`, including the 5 KB `PolyphaseStereo` that is not linked.
   - So a 35-45 KB decoder frees **22-32 KB, not 27-37 KB**. The heap gap 13,568 B is confirmed exactly.
   - About 12 KB of that is reachable with a small patch to today's decoder (mono via the unit plus FDCT32 writing only the push log instead of the 8 KB `vbuf`), so the rewrite's *incremental* RAM prize over a patched Helix is about 10-20 KB.
7. **The window unit's interface is Helix-defined, and the scoping calls it "unchanged"** [READ]. The unit is "Helix PolyphaseStereo in hardware", bit-exact with Helix by construction. Its push order (P0 = V0, P1..16 = V16..31, P17..31 = V15..1), the input scaling (Helix's FDCT32 output Q-format after its guard-bit `es` fix-up), the rounding constant, shift and clip are all Helix's.
   - B-1168 (not on main) re-derived the ROM from the standard but itself records "the push order stays a Helix-defined ABI".
   - The clean-room contract has to state this interface as numbers (an ordering of the standard's V vector and a Q-format), written by the contract author **before** P3. Otherwise the implementer cannot target the unit without reading Helix.
   - This is cheap (about 1 session) but it is missing from the package and from R1's checklist.
8. **The ISO tolerance plan is mis-stated in a way that matters for a 16-bit-output unit** [MEMORY, flag for verification].
   - My recollection of ISO/IEC 11172-4: full accuracy requires RMS(difference) < 2^-15/√12 *and* max |difference| ≤ 2^-14 (relative to full scale), measured on a specific -20 dB sine-sweep test stream. Limited accuracy requires RMS < 2^-11/√12, with no maximum-error requirement.
   - 2^-15/√12 is exactly the RMS of ideal rounding to 16 bits. Measured against a higher-precision reference, a decoder whose final output is the unit's 16-bit clamp sits *at* the full-accuracy bound, not under it.
   - The package applies "the ISO bound" to every vector and never says in which domain (16-bit vs wide reference). The gate has to define the domain, or it will either be unpassable or meaningless.
9. **Hot masters clip inside the decoder, before ReplayGain can attenuate** [MEASURED]. Helix's 16-bit output puts **0.03-0.45 % of samples at the rails** on the owner's Cowboy Bebop album: up to 98,218 rail samples and 63,298 separate rail runs on one track. On the classical Test Album the figure is 0-3 samples. These counts are an upper bound on decoder-created clipping, because some rails exist in the master.
   - The window unit clamps to 16 bits and the gain stage applies negative ReplayGain *after* it, so the attenuation cannot recover the over.
   - The package parks this as S6 "measure later". It belongs in the new decoder's output contract now. A cheap option is a pre-synthesis gain multiply on the 576 subband samples per granule per channel: about 2.3 k multiplies per frame, under 1 % CPU, linear, and no RTL.
10. **The cost estimate understates option (a) and overstates its safety** [EST].
    - The "float reference model written from the standard" in P1 is a second complete decoder (LSF, intensity, mixed blocks included). 5-6 sessions is not enough for it.
    - The implementer and the contract author are the same model family, trained on public decoders. The "never saw Helix" guarantee is therefore weaker for an AI implementer than for a human, so the similarity audit is the only real evidence and its thresholds are untested.
    - Realistic effort for (a) is **35-50 sessions, 12-16 weeks**.

---

## Q1. Measurement

**Re-run [RE-RUN].** `measure.py` with 150 frames reproduces the package exactly: 602,894 instructions / 762,972 model cycles (02 MacCunn 128 k). With 3,000 frames (78 s) it gives 603,094 / 763,194, so 150 frames is stable for this file. The window share is 66.6 % (02) and 67.8 % (07 Clementi). Mono speech is 315,022 / 386,644 at 3,000 frames.

**Soundness of the method.**

| Property | Verdict |
|---|---|
| Instruction-accurate? | Yes for the compiled code. RV32IM decode is correct for every opcode the decoder uses, and the counts reproduce. |
| Per-PC attribution | Correct (`nm -S` with suffix stripping). Inlining limits it: antialias sits inside `xmp3_IMDCT`, which the package says. |
| Configuration | **Wrong.** It is not the Player 2 decoder. Player 2 compiles with `-DTAU_POLY_FW=1 -DTAU_POLY_REQUIRED=1` (`fw2/build.sh:26`), which changes FDCT32 (logging), `Subband` (copy loops, push/finish) and links `poly_hw.c`. "Software window minus the window" is not the shipped remainder. |
| Cycle model | No cache model, yet the CPU has a 4 KiB direct-mapped I$ and a 4 KiB direct-mapped write-through D$ in front of the 192 KB BRAM (`VexRiscv_Full.v` lines 5721-5725 and 6467-6468; LAYERED_WAVE_PERF_PLAN). The code (28.8 KB) and Helix's working set (24.6 KB arena) are each about 6x the caches. The header comment "hot code and data are on-chip RAM" is the error: on-chip yes, cached in 4 KiB no. |
| Frame sampling | 150 frames is the first 3.9 s of each file. It mis-states content: "05 MacCunn 320 48k has no MS frames" is false (my header scan finds 283 MS frames; the 03 320 k file has 229). |
| Calibration source | B-591 is an **old-player** (`fw/`) profile build on an unknown track, and its `dec_pct` includes profile ticks. That is acceptable for ranking, but it should be labelled as old-player data. |

**What I measured instead [MEASURED].** I used a scratch copy of `rvprof.c` plus an I$/D$ model (4 KiB, direct-mapped, 32 B lines, D$ write-through and no write-allocate), Helix compiled exactly as Player 2 with `TAU_POLY_FW/REQUIRED`, and the unit stubbed with RAM loops of the same shape. 1,000-3,000 frames per file:

| File | Instr/frame | Model cyc/frame | I$ miss | D$ miss | Stores |
|---|---|---|---|---|---|
| 02 MacCunn 128 k 44.1 st | 274,389 | 328,767 | 1,449 | 1,821 | 38,676 |
| 05 MacCunn 320 k 48 st | 293,953 | 352,518 | 1,432 | 2,015 | 38,716 |
| Tank! 320 k-class VBR 44.1 st | 299,400 | 358,136 | 1,953 | 1,996 | 39,989 |
| 06 Battle (B-591's guessed track) | 300,189 | 359,090 | 1,641 | 1,965 | 39,646 |
| 11 Speech 128 k 44.1 **mono** | 318,139 | 389,774 | 4,572 | 4,837 | 40,766 |

Per-function split, Battle (model cycles): IMDCT 89.9 k, FDCT32 (with log) 77.2 k, Huffman 53.3 k, DequantBlock 47.2 k, Subband glue 28.2 k, push 20.9 k, read 13.0 k.

**Calibration against B-591** (1 % of the CPU = 17.4 k cycles per frame):

| Stage | Pocket | Model (correct build) | Ratio |
|---|---|---|---|
| h: Huffman, side info, scale factors | ≈70 k | ≈58 k | 1.2 |
| d: requant + MS | ≈70 k | ≈62 k | 1.1 |
| i: IMDCT | ≈122 k | ≈97 k | 1.25 |
| s: subband (`sub_fdct` + `sub_hw` + rest) | ≈209 k | ≈139 k | 1.5 |

Inside s, `sub_hw` is about 3 % on the Pocket against 34 k in the model. The gap is MMIO latency, which the stub does not model.

With misses at 10-15 cycles, the residual is about 1.15-1.2x. **Budget rule I would use: model x1.3 with caches off, x1.15 with the cache model, ±10 %.** That is not the package's "1.1-2.2x".

**The package's specific figures:**
- 602,894 and 67 %: correct, but only for the software-window build, which nothing ships.
- "255 k remainder" and its split (IMDCT 33 %, DCT32 24 %, requant 18 %, Huffman 14 %, MS 6 %): **wrong**. The correct split for 02 is IMDCT 25 %, FDCT32 + glue + handoff 43 %, requant 13 %, Huffman 12 %, MS 3 %.
- "6.1 k of 212 k": the denominator is wrong; it is about 274 k.

**RAM.**
- Heap gap 13,568 B: confirmed. Image end 149,248, heap limit 162,816.
- Arena 24,576 B static in `libc_min.c` (peak 23,824): confirmed.
- Code: **28,844 B linked, not about 33,500**.
- Tables: 12,614 B (the package says 12.75 KB): agrees.
- `SubbandInfo` 8,708 B: still written on every slot even with the unit, because FDCT32 writes `vbuf` and the push log is a side copy. That answers the package's U1: yes, the history is touched.

**Is 35-45 KB realistic?** Floor estimate for a lean fixed-point decoder with the unit doing stereo *and* mono:

| Item | Size |
|---|---|
| Spectrum, one granule, two channels, int32 (requant/stereo/antialias/IMDCT done in place) | 4,608 B |
| IMDCT overlap: 18 values per subband, 576 per channel | 2,304-4,608 B (half-size storage as Helix and minimp3 do, otherwise full) |
| Main-data/reservoir | 2,048-2,815 B (511 B reservoir + the largest frame payload: 1,441 B MPEG-1 at 320 k/32 kHz, 2,304 B with free format). About 0 if Huffman reads the 24 KB stream ring in place (wrap handling). |
| DCT32 scratch | 256 B |
| Header/side info/scale factors | about 0.6 KB |
| PCM out | 0 if written straight to the ring (S7), otherwise 4.6 KB |
| **State subtotal** | **10-15 KB** |
| Huffman tables (compact, minimp3-style `int16` codebook stream, about 1.9-2.1 k entries) | about 4 KB; Helix's are 8.5 KB |
| Scale-factor bands for 9 rates, pow4/3 (minimp3: 145 entries) and 2^(x/4) | about 1.5 KB |
| Code (L3 only, no software window, no Layer I/II) | 14-20 KB [EST] |

Total is **30-41 KB**, so the package's 35-45 KB is realistic and not optimistic.

What would make it optimistic:
- keeping a separate IMDCT output buffer (+4.6 KB, as Helix does);
- keeping a `pcm[]` stage buffer;
- unrolled transforms (Helix's FDCT32 is 4 KB, IMDCT 4.8 KB);
- adding LSF and intensity code to the hot image (put it in the cold PSRAM image as the package suggests: correct, and worth stating as a requirement).

## Q2. The missing alternative: a CC0 base (minimp3) versus a clean-room rewrite

**Facts checked [READ].**
- lieff/minimp3 LICENSE is "CC0 1.0 Universal", and the header carries the same public-domain dedication. CC0 has no notice duty (a courtesy line in `docs/PROVENANCE.md` and `ATTRIBUTIONS.md` is good practice). CC0 explicitly excludes patent licences, which does not matter here: the MP3 patents have expired.
- Features: MPEG-1/2/2.5 Layer III, intensity and MS stereo, mixed blocks, free format; Layer I/II removable (`MINIMP3_ONLY_MP3`).
- Numeric format: **float only.** `MINIMP3_FLOAT_OUTPUT` changes only the output type. There is no fixed-point fork from the same author. The "minimp3" that is fixed-point is keyj's older one, which is FFmpeg-derived: LGPL, **excluded by rule 7**.
- State: `mp3dec_t` holds `float mdct_overlap[2][288]` + `float qmf_state[960]` + `reserv_buf[511]`, about 6.7 KB. Scratch holds `maindata[2815]` + `float grbuf[2][576]` + `float syn[33][64]` + side info, about 16 KB (on the stack). With the unit, `qmf_state` and `syn` (12.3 KB) disappear.
- Test vectors: the repo's `vectors/` carries `compl.bit`, `he_*`, `si*`, `sin1k0db.bit` (the ISO 11172-4 Layer III compliance set by name). CC0 cannot cover them: they are ISO material. Use them locally, and do not commit them without an owner decision.
- Its README claims "PSNR > 96 dB" on all vectors. PSNR is not the ISO criterion.

**Stage 0 evidence (coordinator, `STAGE0_RESULTS.md` section 4) and how it differs from today.** Stage 0 measured *stock float minimp3* against Helix on PicoRV32 with a **serial multiplier**, no window unit and softfloat for every float operation, on the worst vector (MEANDR90, 320 k/48 kHz). Helix took 8.43 M cycles per frame; minimp3 did not finish in 24.9 M.

Today's setup is different:
- VexRiscv with a 1-cycle pipelined multiplier;
- the hardware window, which removes minimp3's heaviest float stage, the 512-tap synthesis;
- 66.7 MHz;
- Helix itself models at about 0.36 M cycles per frame, more than 20x faster than its Stage 0 figure.

Stage 0 answers "stock float minimp3 on a softfloat core: no", and that still holds. Even with the unit, minimp3's remaining float work is about 59 k floating-point operations per stereo frame: IMDCT36 (≈212 ops per subband x 128 = 27.1 k), antialias (5.9 k), DCT-II (≈296 per column x 72 = 21.3 k), plus requant and MS. At 40-80 cycles per softfloat op that is 2.4-4.7 M cycles per frame, **1.4-2.7x the whole CPU**. Stage 0 never evaluated a *fixed-point derivative*, and nothing in its result argues against one.

**Modelled cost of a float-free minimp3 derivative on today's setup** [MODEL: minimp3 op counts read from its source x the instruction cost of the same operations in Helix's measured fixed-point code, about 2 instructions per Q31 multiply (`mulh` plus shift) and 1 per add, plus measured load/store density; ±20 %]:

| Stage (stereo frame) | Helix as shipped (measured instr) | Derivative (model instr) | Note |
|---|---|---|---|
| Antialias + IMDCT + overlap | 70.2 k (+ FreqInvert/WinPrevious 3-5 k) | about 62 k | same algorithm family (9-point DCT based); parity ±15 % |
| DCT32 + handoff to unit | 69.5 k + 22.9 k + 27.1 k = 119.5 k | about 35-45 k | 80 multiplies per column in both; the saving is writing V straight to the unit's push, without `vbuf` scatter, log or copy. **Any** new decoder gets this, (a) included |
| Huffman + requant | 28.3 k + 31.5 k + 3.2 k = 63 k | about 50-55 k | minimp3 fuses requant into Huffman (one table lookup and one scale per value); fixed point needs a per-band exponent and a saturating shift |
| MS stereo | 10.0 k | about 8 k | 2 add/sub per pair |
| Bit reservoir, glue | about 8 k | about 6 k | |
| **Total** | **274-300 k** | **about 165-185 k (-35 to -40 %)** | about 13-15 % of CPU (model) against about 21 % |

**Would a float-free minimp3 derivative beat Helix on the current setup? Yes, by about 35 % of decode CPU (model).** But about 75 % of that saving is the Helix-to-unit glue, which a clean-room rewrite removes equally. On pure arithmetic the two are at parity within ±20 %. The case for (d) over (a) is therefore not CPU. It is effort, provenance risk and test depth.

**Fixed-point conversion: cost, Q-formats, accuracy, risk.**
- **Requant.** |x|^(4/3) for x ≤ 8,206 reaches about 2^17.3; scale 2^(-(global_gain-210)/4 - …) spans about 2^±40. That needs a per-band (or per-granule) block exponent with a 32-bit mantissa and saturation to a common Q-format before stereo processing (for example Q(31-g) with g ≥ 6 guard bits). This is the hardest numerics and the main accuracy risk. It is also standard practice (Helix and pvmp3 both do it).
- **IMDCT/antialias/DCT.** Q31 coefficients through `mulh`, one lost bit per multiply, about 5 multiply stages deep: an accumulated error of about 2^-26 to 2^-24 relative, far below the 16-bit output step. Limited accuracy is safe by a wide margin. Full accuracy is limited by the 16-bit unit output and the measurement domain (Q7), not by the arithmetic.
- **Effort and risk per stage.** The front end (header, side info, scale factors incl. LSF/intensity tables, Huffman, reservoir) can be adapted almost unchanged except the fused requant: low risk, and it carries minimp3's fuzzing and Coverity history. The numerics (requant, IMDCT, DCT to the unit's ABI) are new code in either option.

**Other candidates.**
- **pvmp3** (PacketVideo/Android): Apache-2.0, fixed point [READ: licence via AOSP mirrors; fixed-point design MEMORY, verify]. Fixed-point and permissive, but it carries a NOTICE duty, is ARM-tuned and large, and is no more "own".
- **pdmp3**: public domain, float, slow, a reference only.
- **dr_mp3**: public domain / MIT-0, a minimp3 derivative, float.
- **libmad, mpg123, FFmpeg, keyj minimp3**: copyleft (GPL/LGPL), excluded.
- **kjmp2**: Layer II only.

**Comparison** (effort in sessions of the package's size, including Pocket gates):

| Option | Provenance | Owner's "own decoder"? | Effort | Hot RAM | CPU vs today | Edge coverage | Main risk |
|---|---|---|---|---|---|---|---|
| (a) clean-room from the standard | MIT, own; evidence rests on an AI clean room and an untested similarity audit | yes, fully | **35-50 (12-16 weeks)**; the package says 26-36 | 30-41 KB | -30 to -40 % (glue removal) | written from scratch; LSF/intensity/mixed untested by any real file | float model plus implementer common-mode errors; schedule |
| (b) minimp3 whole, converted to fixed point | CC0 into MIT, no notice duty | legally fully owned; about 40 % of lines minimp3-derived | 16-24 | 30-38 KB | -35 % | inherited (fuzzed, ISO vectors) | Q-format conversion of requant |
| (c) pristine Helix + `tau.patch` | RPSL stays | **no** | 3-6 for: mono via unit, FDCT32 without `vbuf`, push/copy glue cut | about 54 KB (frees about 12 KB) | -25 % mono, -10 to -15 % stereo [EST] | Helix's | fails the ownership goal |
| **(d) hybrid: minimp3-derived front end (CC0) + own fixed-point back end written to the unit's documented ABI** | CC0 + MIT | yes (the numerics, the unit interface and integration are own) | **18-25 (6-8 weeks)** | 30-38 KB | -35 % | front end inherited; back end differential-tested against float minimp3 and Helix | requant Q-formats; the V-order mapping to the unit |

**Recommendation: (d).**
- If the owner reads "own" as "nothing derived from anyone": (a), with the corrected 35-50-session estimate.
- In either case, take (c)'s two cheap items now as a bridge (mono via the unit, the 48 kHz drain fix): they are worth ~15 % CPU on mono and they de-risk the unit ABI work.
- Two decisions to record in `DECISIONS.md` so they are not re-litigated (the owner's park-rejected-options rule): (b)/(d) *as stock float* stays rejected (Stage 0 plus the Q2 arithmetic); keyj minimp3 is rejected (LGPL).

## Q3. Architecture

- **Host-first, one flag, Helix as fallback: right.**
- **Two releases of fallback: too long if Helix stays linked in the shipped build.** RAM is the prize, and a fallback that is a build flag (not a runtime switch) costs nothing in the shipped image. Make it "flag-selectable in source for two releases; shipped builds link only the new decoder from release 1".
- **Rejecting stage-by-stage replacement inside Helix: correct for (a)**, for the stated reason. For (d) the question does not arise.
- **The real architectural gap is the unit.** Required before P3: a standard-derived *unit ABI document* covering:
  - the input: the 32 unique V values per channel per slot in the unit's order, with their Q-format and scale relative to the standard's V;
  - the rounding, shift and clip semantics as numbers;
  - the history/clear/timeout behaviour.
  B-1168's derivation of the ROM plus `sim/mp3_poly_model.c` are the tools. Then the unit's testbench golden comes from a standard-derived model, and Helix becomes only an oracle there too.
- **Bit-exactness.** The unit stays bit-exact with *its own model*. The new decoder only needs ISO accuracy *into* it, and the self-check window (first 8 slots compared to the software window) must be redefined, because the new decoder has no software window. Replace it with the clear/timeout/silence rule plus a host test.
- **Mono through the unit (push the channel twice, take L) belongs in the architecture**, not as an option.
- **A DCT32-in-the-unit option** (32x32 MAC or the 80-multiply fast form in the sequencer, the subband samples pushed instead of V) removes about 77 k model cycles per frame. It costs a cosine ROM (1-2 M10K, the binding resource) and a fit. It is the right shape for "aligned with the Pocket", and it should be weighed against the custom-instruction idea in Q4.

## Q4. Custom instructions

"Not justified" is **correct, and more so than stated.** On the correct build the remainder is about 274-300 k instructions, so the fusable `mulh`+add count (about 6 k) is about 2 %, at most about 1-1.5 % of the CPU. The real ISA/SoC misalignment is elsewhere:

1. **About 66 k instructions per frame of handoff glue.** Fix it in the decoder's design: V straight to the unit.
2. **MMIO handoff.** 130 accesses per slot (Pocket `sub_hw` 3 %). An auto-increment push/read port, or letting the unit read a 64-word RAM buffer itself, halves this. It is RTL and costs a fit.
3. **Cache behaviour.** 1.4-2.0 k I$ plus 1.8-2.0 k D$ misses per stereo frame (mono 4.6 k + 4.8 k). Design rules for the new decoder: each stage's hot loop plus its tables under 4 KiB, no two hot stages on the same cache index (the Layered Wave method, `sim/test_p2_icache.py`), and store-light loops (D$ is write-through: 38-40 k stores per frame).

**What I would check that the package did not:** a cache-aware model (done here in scratch), the MMIO cost, and the store count.

**The DCT32/IMDCT hardware kernel lever.** It is worth stating, but sized correctly: DCT32 in the unit is about 77 k cycles per frame (about 4.6 % CPU model, about 6 % on a Pocket). The resource figures (ALM about 84 %, M10K 274-281/308, DSP 23/66) I did not re-check against a fit report. M10K is binding, so a DCT ROM must use the 8-fold cosine symmetry (under 1 M10K). Only on a stated need (D9).

## Q5. The 48 kHz question

- **Is the bus fixed at 48 kHz?** Yes [READ skill `hardware-video-audio-input.md`: "exactly 48 kHz … Sample-rate adjustment is not allowed"]. The core generates MCLK 12.288 MHz from its own fractional-N PLL (`mf_pllbase_mclk.v`). A native 44.1 kHz I2S is not available.
- **Conversion outside the decoder: agree.** The arithmetic is right: 48,000 x 32 x 2 = 3.07 M MAC/s, against about 0.5 M multiplies/s for the whole remaining decode. 160:147 is not an integer ratio, and the window unit has one fixed 512-tap set. An integer-ratio subband-dropping decoder (2:1 by using 16 bands) is possible in principle but serves nothing here: the low LSF rates need *up*-conversion.
- **The package's description of the built path is wrong** (finding 5). The drain is 48 kHz for every non-44.1 rate, so wrong speed, not a hold.
- **"The ring carries no rate tag":** confirmed [READ `mp3play.c:369,449`: the rate is set when a segment opens, "only legal on an empty ring"].
- **The S1 "three independent ticks with no measured slip rate" is computable, not a loopback campaign** [MEASURED arithmetic]. All clocks derive from the same 74.25 MHz reference: clk_sys from the main PLL (600 MHz VCO / 9), MCLK from its own fractional-N PLL. The Halcyon/feed/FIFO accumulators therefore have a fixed rational ratio to LRCK, not a drift.
  - With `TICK_INC = round(48000·2^32/66,666,667) = 3,092,376`, the tick runs at 47,999.993 Hz: -0.147 ppm, **one sample slip every 142 s** (CLK60: +0.047 ppm, one every 439 s). This assumes the fractional-N MCLK is exact; the fit report's achieved frequency settles it.
  - One repeated sample every 2.4 minutes is inaudible on music. **S1 should drop from rank 1 to "low; verify the achieved MCLK in the fit report".**
- **Better split, proposed:**
  1. Fix the drain bug now (the hold at the true rate: correct speed, 27.8 dB-class quality).
  2. Then one pull-driven RTL resampler with a firmware-set ratio from a family of coefficient sets: 44.1→48 (existing 160:147), 32→48 (3:2), 22.05/11.025→48 (via 160:147 after 2x/4x zero-stuff, or a direct 320:147 with a 11 kHz cutoff), 24/12/16/8→48 (2:1, 4:1, 3:1, 6:1). Varispeed uses linear interpolation between adjacent phases.
  This is S2, correctly a separate project. Decoder-side native 48 kHz: no.

## Q6. Whole-engine alignment

I read in full `CYMO_HALCYON_SPEC`, `CYMO_DSP_REVIEW`, `CYMO_GAIN_STAGE_DESIGN`, `CYMO_OUTPUT_STAGE_SPEC`, `CYMO_HEADPHONE_PLAN`, `GAPLESS_SPEC` and `SKIP_SILENCE_SPEC`, plus `aout.c`, `poly_hw.c`, the `subband.c`/`dct32.c` hooks, the `tau_mp3_poly.sv` header, `tau_halcyon.sv` (tick) and `sound_i2s.v`.

**What is wrong in section 10:**
1. Stage 9's "every other rate … hold" (finding 5).
2. S1's rank (Q5).
3. Stage 2 "window output rounded/clamped to 16 bit" is true, but its consequence (S6) is mis-ranked (finding 9).
4. The stage table omits that mono bypasses the unit.
5. The order "gain before EQ" is right, but section 10.1 says positive ReplayGain is peak-limited "so nothing needs a clipper". The headphone plan's A3/A5 show the resampler already hard-clamps at 16 bits before the EQ, and positive gain needs the wide path. That is fine for the decoder, but the table should state it.

**Seams missing from the package (each with what the decoder contract must say):**

| Seam | Evidence | Contract / action | In scope? |
|---|---|---|---|
| **Q-format and headroom at the decoder output** | the unit clamps to 16 bits; measured rail rates 0.03-0.45 % on hot masters | decoder output = int16 after an optional **pre-synthesis gain** (Q15, ≤ 1.0, default unity); firmware may set it to min(RG, 0 dB) and the gain stage then applies the rest; or reserve PCM24 | **IN** (pre-gain is about 1 % CPU, no RTL); measure the effect with the host decoder on the owner's library |
| ReplayGain application point | today only after the 16-bit clamp (`tau_gain_stage`) | as above; at a gapless marker the decoder-side gain changes at the exact boundary sample, which is better than the marker ramp | IN (interface), decision D11 below |
| **Gapless trim semantics** | `mp3_trim`/`lame_tag`: skip delay+529, drop padding-529; six files equal the FLAC counts exactly | the new decoder must emit **every frame's 1,152 samples, including frames whose `main_data_begin` points before the stream start (output silence, never skip)**, and the 529-sample filterbank delay must be the standard's. A decoder that drops underflow frames shifts every trim by 1,152 | **IN**, with a host test of the six-file equality |
| Seek and bit-reservoir restart | after a seek the reservoir is empty; 511 B back is up to 5 frames at 32 kbps; skip-silence lead uses seek; long VBR audiobooks seek at Xing TOC resolution (0.4 %, 2.4 min on a 10 h book) | pre-roll N frames (decode and discard) sized from `main_data_begin`; overlap zeroed at a discontinuity, kept across a gapless join (the package states this rule; add the pre-roll) | IN |
| Error concealment | not specified | on a bad frame: output 1,152 (or 576 for LSF) samples of muted or repeated-and-faded audio, never zero samples, so P3 accounting and trim survive; count it | IN |
| Rate change between or inside tracks | the ring has no tag; rate is set at segment open | discontinuity flag + rate tag per block (the package has this) | IN |
| LSF frames are 576 samples | `mp3_frames_core.h` uses 72 vs 144 slots, fine; P3/`pairs_total` must use samples-per-frame from the header | contract field | IN |
| Tempo/WSOLA input | firmware WSOLA on int16 pairs at the source rate; rate-dependent parameters | unchanged; 22.05/24 kHz must be tested with tempo once the drain is fixed | test only |
| Halcyon coefficient updates / EQ state | the shadow bank commits at a sample boundary; the recall dip clears state | independent of the decoder | OUT |
| Dither and clipping placement | deferred (D-G02/D-G03); analogue floor -76 dBFS | decoder must not dither; one final quantiser later | OUT |
| FLAC symmetry | FLAC pushes int16 after 24→16 rounding; the same ring | the same output contract (rate tag, discontinuity, source index, optional pre-gain) for FLAC, so the gain/RG seam is one rule | contract IN, FLAC change follow-up |
| Hot-RAM layout | 4 KiB I$/D$ | each hot stage under 4 KiB, its own section, a cache-conflict budget test | IN (design rule) |

**In-scope vs follow-up split.**
- Mostly right: S1, S2, S5 and S8 stay separate.
- Move **the 48 kHz drain bug** out of "seams" into an immediate fix (firmware, 1 session).
- Move **mono via the unit** into the decoder scope (or do it first on Helix).
- Move **decoder-output headroom** into scope as a contract decision with a host measurement.

**Improved target architecture (one paragraph).** decoder (granule-streaming, one 4.6 KB spectrum buffer in place, overlap half-size, reservoir read from the stream ring, V straight to the unit for stereo and mono, optional pre-synthesis gain) → blocks of (pairs, ch, rate, source index, discontinuity) written straight into the PSRAM PCM ring (S7, removes `pcm[]`) → RTL ring reader → FIFO at the true source rate → gain stage (volume, remaining RG, fades) → one pull-driven multi-ratio resampler (S2) → Halcyon → (later) one final quantiser → I2S. FLAC writes the same blocks.

| Change | Regression risk |
|---|---|
| The decoder (no RTL) | low if the host gates are met |
| Drain fix | very low |
| Mono via unit | low (host test, one Pocket check) |
| Multi-ratio resampler | medium (RTL, fit, M10K) |

## Q7. Verification and clean room

- **Tolerance.** See finding 8; the statements I give are [MEMORY] and must be checked against the text of 11172-4 before they enter a test:
  - full accuracy: RMS < 2^-15/√12 and max ≤ 2^-14 on the specified sine-sweep stream;
  - limited accuracy: RMS < 2^-11/√12.
  - Define the comparison domain: (i) the decoder's pre-unit value at full precision, through a host model of the unit, against a float64 reference; and (ii) end-to-end 16-bit against the reference rounded to 16 bits. Gate on limited accuracy everywhere and full accuracy in domain (i).
  - Helix's own figures, measured in the same harness, are the bar.
- **Vectors.**
  - ISO 11172-4 Layer III set (MPEG-1 only, includes free format `he_free` and mode changes `he_mode`): copyright ISO, redistribution status unknown.
  - 13818-4 (LSF): the same question.
  - minimp3 repo copies: same status, not covered by CC0.
  - Encoder-generated streams: LAME output is data, so its LGPL does not reach it; fine with an owner note.
  - minimp3 float output on the host is an **independent, licence-clean second oracle**: use it for every stage in (d), and end to end in (a).
- **Data tables.**
  - "32 big-value tables with linbits, 2 count1 tables" is imprecise [MEMORY]: there are 32 table *selections*; 0 is empty, 4 and 14 are unused, 16-23 and 24-31 share two codebooks with different linbits. That makes 15 distinct big-value codebooks plus count1 A and B.
  - Reading minimp3's `tabs[]` as data is legitimate (CC0).
  - Cosine/window/antialias/pow tables from formulas: correct.
  - Kraft-sum and completeness checks: good.
- **Fuzzing/mutation plan.** Adequate in shape. Add:
  - a resync bound in bytes;
  - LSF/MPEG-1 switches mid-stream;
  - a frame size that changes with the padding bit;
  - Xing/VBRI/Info parsing fuzz;
  - ID3v2 with footer and unsynchronisation;
  - the frame-count invariant (samples out = frames x spf, including underflow frames).
- **"Author never sees Helix".** For AI agents trained on public code (Helix and its forks are public) the guarantee is not verifiable. Say so, and make the similarity audit the gate. Calibrate its thresholds first by running it on two independent public decoders against each other (minimp3 vs Helix vs pvmp3): if the 5 % file overlap or 12-token runs fire between independent decoders, the thresholds are meaningless for DSP butterflies. Under (d) the audit only needs to cover the back end.
- **Statements from memory in the package that are wrong or unverifiable:**
  - the tolerance rule (partly right; domain missing);
  - "32 big-value tables" (imprecise);
  - "the B-1168 note says `third_party/minimp3/` carries test vectors" (that directory is ignored and absent; `.gitignore:68`);
  - "05 MacCunn 320 48k has no MS frames" (wrong, 283 MS frames);
  - "PolyphaseStereo + Mono 7,500 B kept as the fallback" (wrong: Stereo is gc'd, Mono is linked because mono needs it);
  - "every other rate … zero-order hold" (wrong, finding 5).

## Q8. Risks and estimate

**Missing from the risk list:**
- the drain bug blocks any LSF/32 kHz testing;
- mono not covered by the unit;
- the unit ABI is Helix-defined;
- decoder-output clipping on hot masters;
- trim breakage if underflow frames are dropped;
- seek pre-roll;
- error concealment and frame accounting;
- Xing TOC accuracy on long files;
- VBRI (Fraunhofer) headers (no frame count, so no tail skip);
- ID3v2 footer/unsynchronisation and APE/ID3v1 tails (junk at the end must not become a "frame");
- CRC: skipping is fine, but the 16-bit CRC field must still be stepped over;
- bitrate switching and free format (frame length from the next sync);
- I$/D$ thrash from code layout;
- the AI clean-room evidence weakness.

**Owner library [MEASURED, my header scan of 59 MP3s in `test music/`]:**
- all MPEG-1 at 44.1/48 kHz;
- joint stereo with MS, plus plain stereo and mono;
- VBR with Xing on most albums, LAME Info on the Test Album;
- the speech file has **no tag at all** (no trim, no tail skip);
- zero LSF, intensity, mixed-block, CRC or free-format frames.

This supports D2: build LSF/intensity/mixed blocks, but put them in the cold image and test them with vectors, not as a hot-path priority.

**Estimate.**
- (a): 26-36 sessions is optimistic. P1 (float model of a full decoder plus contract plus unit ABI) is 8-10, P3 is 14-20, and the rest is about as stated, for **35-50 sessions, 12-16 weeks**.
- (d): **18-25 sessions, 6-8 weeks.**

**Cut or reorder:**
- do the drain fix and mono-via-unit first;
- write the unit ABI document before any decoder stage;
- drop the separate Python float model under (d) (float minimp3 is the reference);
- keep P7 parked.

**First week, to de-risk** (about 4 sessions):
1. Fix the drain (host test with 32/22.05 kHz) and package a DEV core with a 22.05 kHz speech file.
2. Mono through the unit on Helix (host test against the software window; Pocket FULL Check on the speech file: `dec_pct` should fall by about 12-15 points).
3. Write the unit ABI document from the standard plus `mp3_poly_model.c`.
4. Build float minimp3 for the host as an oracle, and compile it `-march=rv32im` to *measure* its code size and stack (it is a single header). This settles the RAM estimate of (d).
5. Run the clip/rail count over the owner's whole card library.
6. Read 11172-4's compliance clause and fix the tolerance text.

## Q9. Decisions D1-D10

| # | Framing OK? | Recommendation |
|---|---|---|
| D1 approve on ownership + RAM | yes; add "and ~35 % decode CPU from removing the unit glue, available to any new decoder" | approve, choosing (d) or (a) explicitly (new D0: base = minimp3-derived (d) or clean room (a)) |
| D2 coverage scope | yes; answered for the test music (none of it LSF/intensity/mixed/free/CRC) | include all of it (it is cheap in (d)), cold image, vector-tested; scan the card library too |
| D3 software window mandatory | **mis-framed**: mono needs the software window today | make the unit mandatory for mono too (push twice), then drop `PolyphaseMono`, `polyCoef` and `vbuf` |
| D4 vector licences | yes | use ISO/minimp3 vectors locally, uncommitted; commit only own encoder-generated streams |
| D5 output contract fields | incomplete | add: optional pre-synthesis gain, the underflow-frame rule, concealment rule, samples-per-frame field |
| D6 Helix fallback releases | partly | source-flag fallback for 2 releases; shipped images link the new decoder only |
| D7 hard scope line | yes, with changes | move the drain fix (now) and mono-via-unit (in) out of "seams"; S1 to low |
| D8 48 kHz question | conclusion right, evidence wrong | keep conversion out of the decoder; fix the drain; S2 resampler as its own project |
| D9 CPU need for kernels | yes | first take the free 35 % from the decoder design; kernels only after that and on need |
| D10 roles | only relevant to (a) | under (d) the roles reduce to author + verifier; under (a) accept, but state the AI caveat and gate on the calibrated similarity audit |
| D11 (new) | | where negative ReplayGain is applied: before synthesis (decoder) or after the clamp (gain stage); decide after the library rail count |

## What the scoping got right

- Ownership and RAM as the drivers, and the D-T12 discipline of not claiming a CPU need.
- Host-first with a single flag and a differential oracle; the rejection of stage-by-stage replacement inside Helix (for a clean room).
- Custom instructions are not worth it, and the shared CPU file should not be touched.
- Keeping rate conversion out of the decoder, the rate tag and source-index contract, and writing straight to the ring.
- The `rvprof` tool itself: accurate, small, reusable. With the cache model and the correct build flags it is a good budget tool.
- An honest "not verified" list; the heap-gap and arena figures are exact.

## What I could not verify

- The ISO 11172-4 text (the tolerance statements above are from memory) and the licence status of the ISO vectors.
- minimp3's compiled RV32 code size and stack (not downloaded; first-week task 4).
- pvmp3's fixed-point details.
- The fit-report resource figures (ALM/M10K/DSP), the achieved MCLK frequency.
- Any Pocket figure: the mono-via-unit saving and the cache miss penalty are model or estimate.
- Whether the 48 kHz drain bug is reachable in shipped builds beyond reading the code (no 32 kHz file was played).
- How many of the rail samples on hot masters are decoder-created rather than mastered.
- The B-1168 branch contents beyond its audit text.
- The CPU's SpinalHDL configuration beyond the cache sizes in the generated Verilog.

## Recommended revised plan (option d)

| Phase | Work | Sessions |
|---|---|---|
| R0 (now, independent) | drain-rate fix + host test; mono through the unit on Helix (`tau.patch`) + host test; Pocket check | 2-3 |
| P0 | decisions D0-D11; vectors located; library scan (rates, modes, rails); 11172-4 text checked | 1-2 |
| P1 | unit ABI document (standard-derived); output contract (rate, discontinuity, source index, pre-gain, underflow, concealment); host harness with three oracles (float minimp3, Helix, the unit model); cache-model `rvprof` committed; float minimp3 rv32 size measured | 3-4 |
| P2 | front end adapted from minimp3 (L3 only; LSF, intensity, mixed blocks in cold sections) with fixed-point fused requant; stage tests against float minimp3; fuzz under ASan | 4-6 |
| P3 | own back end: stereo, antialias, IMDCT/overlap, DCT32 to the unit's ABI, mono via unit, pre-gain; accuracy in both domains; cache budget test | 4-6 |
| P4 | `fw2` integration behind the flag; heap gap and rv32 cycle gates (≤ 200 k instructions per stereo frame, ≥ 22 KB freed) | 2-3 |
| P5 | Pocket gates: FULL Check (`dec_pct` target ≤ 20 % stereo, ≤ 20 % mono), Test Album, speech plus tempo, gapless six-file equality, seek, skip silence; 60-minute soak | 3-4 |
| P6 | Helix out of shipped builds; provenance records (CC0 credit, PROVENANCE, ATTRIBUTIONS) | 1 |
| **Total** | | **20-29 incl. R0 (18-25 without); 6-9 weeks** |

Under option (a) instead, replace P1-P3 with the package's P1-P3, corrected to 8-10 + 2-3 + 14-20: **35-50 sessions in all.**
