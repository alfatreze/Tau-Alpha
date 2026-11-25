# MP3 decoder v2: scoping report for an own, clean-room decoder aligned with the Pocket and the RV32 CPU (2026-11-25)

**Evidence label: HOST-MEASURED (instruction counts) + MODEL (cycles) + READ (RTL, firmware, docs) + ESTIMATE where stated.** Nothing was fitted, run on a Pocket, installed or pushed. Branch `worktree-agent-ad0ca94d7ab18980d`. New measurement tooling lives in `tools/mp3_v2_measure/` (`rvprof.c` an RV32IM simulator with a per-PC profile written from the RISC-V spec, `harness.c`, `measure.py`, `patterns.py`); it links the CURRENT decoder only to measure it and contains no decoder.
**Audience: the owner and the coordinator. This report names Helix functions and describes its internals. It must NOT be given to the clean-room author** (see `MP3_DECODER_V2_CLEANROOM_PROCESS.md`; the author receives only the contract that is extracted from sections 4 and 7 and from the standards). Same rule for `MP3_DECODER_V2_REQUIREMENTS.csv`, whose "current cost" column is Helix-derived.

---

## 0. Summary and recommendation

* **Where the time goes today.** With the software window the decoder costs about 600 k instructions and 763 k model cycles per 44.1 kHz stereo frame, and **the polyphase window alone is 67 %** of that; that is why the hardware window unit exists. With the unit, what the CPU still runs is about 255 k model cycles per frame, of which **IMDCT/antialias/overlap 33 %, software DCT32 24 %, requantisation 18 %, Huffman and bit reader 14 %, mid/side 6 %**. On a Pocket the decode loop is **about 30 % of the CPU at 1.00x** (B-591, stages h 4 / d 4 / i 7 / s 12 percent). The model is 1.1-2.2x optimistic against those Pocket figures (about 1.6x overall, section 1.4), so budgets use the Pocket numbers and the model only ranks and compares.
* **Where the RAM goes today.** The decoder is **about 71.5 KB of the 192 KB hot RAM (37 %)**: 33.5 KB code, 12.75 KB read-only tables (Huffman 8.5 KB), 0.64 KB data and a **24.6 KB arena** (measured peak 23,824 B on hardware; its structures add to 23.9 KB by `sizeof`, of which the polyphase history buffer is 8.7 KB that the hardware window unit makes unnecessary). The shipped heap gap is **13,568 B** (DIAG=0, rebuilt today).
* **The prize is RAM, not CPU.** A new decoder that is allowed to drop the software polyphase window (the fw2 build already declares the unit required), keeps one granule of state and uses compact tables should land at **about 35-45 KB** (estimate): **a freed 27-37 KB, about 2-3x today's whole heap gap**. CPU parity is the requirement; headroom beyond it (tempo on stereo, FLAC unification) is a bonus, not a need, and is only worth chasing with the DCT32/IMDCT kernels (section 5), not with custom instructions.
* **Architecture: C, "host-first new decoder behind one swap, Helix kept as oracle and as a build-flag fallback for two releases"** (section 4). Stage-by-stage replacement inside the shipped Helix pipeline is rejected: stage boundaries inside Helix are Helix's own data structures, which the clean-room author must never see.
* **Custom VexRiscv instructions are NOT justified by the model** (section 5): fused multiply-high-accumulate, the only candidate with real hits, is fusable in 6.2 k of the 212 k instructions that remain with the hardware window (2.9 % of instructions, 0.7-1.6 % of the CPU after calibration). Nothing else reaches 1 %. Do not touch the shared `VexRiscv_Full.v`.
* **Resampling: keep it out of the decoder.** The decoder emits PCM at the source rate with a rate tag; the output stage owns conversion, gain and EQ (section 9). The largest real audio-quality seam found is NOT in the decoder: the built resampler serves only 44.1 kHz at 1.00x; every other rate and varispeed still goes through a zero-order hold (27.8 dB SINAD against 58.2 dB), and three unlocked "48 kHz" tick sources exist (section 10).
* **Effort (honest):** 26-36 working sessions of implementation and verification plus 2-3 Pocket gates, calendar 8-11 weeks; no fit unless the optional DCT32/IMDCT kernel phase is taken (one fit, about 1h40).
* **Biggest risks:** clicks and desync on edge streams (the Test Album covers only 44.1/48 kHz MPEG-1), a schedule slip from the author/verifier separation, and the temptation to bundle the output-stage fixes (section 10) into the decoder rewrite. Mitigation: differential oracle plus ISO-derived float reference, fuzzing, and a hard scope line (section 10.4).

---

## 1. MEASUREMENT of the current decoder

### 1.1 What was measured and how (and what was not)

| Item | Method | Label |
|---|---|---|
| Instructions per frame per function and stage | the real Helix sources of `third_party/libhelix-mp3` built with the vendored riscv-none-elf-gcc 15.2 (`-march=rv32im -O2`, Player 2's flags, **software window**, no `TAU_POLY_FW`) and run for 150 frames in `tools/mp3_v2_measure/rvprof.c`; per-PC counts mapped to symbols with `nm` | **HOST-MEASURED, exact for the compiled code** |
| Cycles per frame | the same run with the cycle model at the head of `rvprof.c` (ALU/store 1, load 1 +1 load-use, MUL 1 +2/+1 dependent, taken branch +2, JAL +1, JALR +2, DIV 34; no cache or wait-state effects) | **MODEL** (calibration below) |
| Pocket per-stage CPU | audit B-586..B-593 (DEV 87-91, FULL Check, `SR_T_LOAD`, `SR_T_LOAD2`) | **Pocket-measured** (one unknown track, 1.00x) |
| Static RAM, code and tables | rebuilt `fw2/build.sh DIAG=0` today into the scratchpad; `size`, `nm -S` | **HOST-MEASURED (build output)** |
| Arena | `sizeof` of Helix's eight state structures (host x86-64, so pointer members are slightly larger than on RV32) against the hardware peak in `fw2/platform/libc_min.c` (23,824 B) | measured, agrees to 0.1 % |
| Not measured | MPEG-2/2.5 LSF streams (no test file), intensity stereo and mixed blocks (no test file), free format, CRC, resync cost, a Pocket run of any new code, heap-peak on a Pocket (the `peak_bytes` record reads 0 on some builds, B-586) | said so |

Test material (the owner's, not in the repo): Test Album `02 MacCunn MP3 128 44k` (stereo 44.1 kHz 128 kb/s), `05 MacCunn MP3 320 48k` (stereo 48 kHz 320 kb/s), `11 Speech ... 128 44k` (mono). All MPEG-1 Layer III, joint stereo where stereo.

### 1.2 Per-stage cost per frame, software window (model cycles; instructions in brackets)

| Stage (Helix function) | 44.1 st 128k | 48 st 320k | 44.1 mono 128k |
|---|---|---|---|
| Polyphase window (`PolyphaseStereo`/`Mono`, MADD64) | 508,212 (391,104) **66.6 %** | 508,212 **64.7 %** | 242,424 **61.4 %** |
| Antialias + IMDCT + window/overlap (`IMDCT`, `WinPrevious`) | 84,304 (70,570) 11.0 % | 87,757 11.2 % | 48,616 (+3.4 k) 12.3 % |
| Polyphase DCT32 (`FDCT32`) | 61,740 (53,424) 8.1 % | 61,740 7.9 % | 30,870 7.8 % |
| Requantisation (`DequantBlock`, `DequantChannel`, `Dequantize`, `FreqInvertRescale`) | 39,944 + 4,885 + 2,200 = 47.0 k 6.2 % | 48,870 + 4,552 + 2,284 = 55.7 k | 27,927 + 3,314 + 1,520 = 32.8 k |
| Huffman + bit reader (`DecodeHuffman`, `GetBits`) | 34,669 (28,197) 4.5 % | 55,567 7.1 % | 27,420 6.9 % |
| Stereo (`MidSideProc`; intensity not exercised) | 15,175 (13,660) 2.0 % | 0 (no MS in this file) | n/a |
| Scale factors + side info + header + frame glue | 1,628 + 654 + 272 + 421 = 3.0 k | 3.0 k | 1.9 k |
| memcpy / memmove (bit reservoir shuffling) | 6.0 k | 9.4 k | 5.3 k |
| **Total** | **762,972 (602,894 instr)** | **785,182 (621,040)** | **394,852 (321,971)** |

Antialias and reorder are inlined into the IMDCT and requantiser functions and cannot be separated by function boundary; the table reports them where the compiler put them. The 48 kHz file has no mid/side frames, so its stereo row is zero; the 128 k stereo file shows the MS cost.

At 1.00x the frame rate is 38.28 (44.1 kHz) or 41.67 (48 kHz) frames/s. Software window: 762,972 x 38.28 = 29.2 M cycles/s = **43.8 % of 66.67 MHz (model); times the calibration factor (1.6-1.75) = 70-77 %**, which agrees with the design document written before the unit existed ("MP3 decodes at about 70-77 % of real time on the CPU", `MP3_FILTERBANK_KERNEL_DESIGN.md`). That agreement is the one independent check that the model is not far off.

### 1.3 With the hardware window (the shipped case)

Window removed, the remainder is **254,760 model cycles/frame** (44.1 st 128k) = 9.75 M cycles/s = 14.6 % (model) / about 23-26 % (x1.6-1.75) against the Pocket's decode 27 % (stage sum) to 30 % (`MP3Decode` including profile ticks). The unit's own handoff (130 MMIO accesses per slot) is the Pocket's `sub_hw_pct` 3 % (B-588). The remainder split (model): IMDCT/AA/overlap 84.3 k (33 %), DCT32 61.7 k (24 %), requant 47.0 k (18 %), Huffman 34.7 k (14 %), MS 15.2 k (6 %), the rest (copies, glue) 11.9 k (5 %).

### 1.4 Calibration of the model against the Pocket

Per-frame Pocket cycles from B-591 (stage % of 66.67 MHz, divided by 38.28 frames/s; 1 % is 17.4 k cycles per frame; the track is unknown, so a 48 kHz track would read 8 % lower per frame):

| Stage | Pocket (cycles/frame) | Model (this report) | Pocket / model |
|---|---|---|---|
| Huffman + side info + scale factors (h 4 %) | about 70 k | 37 k | 1.9 |
| Requant + MS (d 4 %) | about 70 k | 62 k | 1.1 |
| IMDCT etc. (i 7 %) | about 122 k | 84 k | 1.45 |
| Software DCT32 (`sub_fdct_pct` 8 %) | about 139 k | 62 k | 2.2 |

So the model under-counts memory-bound loops (DCT32, Huffman) by up to 2.2x and compute-light ones barely at all. **Consequence for use:** Pocket numbers set budgets; model numbers rank stages and predict the *relative* effect of an instruction or algorithm change, with a stated 1.1-2.2x spread. Anything claimed "saves X %" below carries that spread.

### 1.5 RAM (Player 2 build, DIAG=0, rebuilt 2026-11-25)

Image: `.text` 90,220 B, `.rodata` 16,484, `.data` 1,136, `.bss` 41,404; image end 149,248; heap limit 162,816; **gap 13,568 B**; stack 5 KB, compressed-data ring 24 KB and tag zone 4 KB sit above (`fw2/platform/link.ld`). Cold image 140,440 B data + 73,956 B code in PSRAM (not Helix).

| Helix item | Bytes | Where it lives today | Could live | Note |
|---|---|---|---|---|
| Code (13 files + `mp3dec.c`) | about 33,500 | hot RAM | hot RAM (decode loop); rarely used parts (MPEG-2 intensity, mixed blocks, free-format, resync) in the cold PSRAM image at about 32 cycles/instruction word (RAM_SHRINK plan) | `PolyphaseStereo`+`Mono`: 7,500 B in `polyphase.o` kept as the fallback even though `fw2/build.sh` sets `TAU_POLY_REQUIRED=1` |
| Huffman tables (`xmp3_huffTable` 8,484 + lookups/offsets 384) | 8,868 | hot RAM (`.rodata`) | PSRAM would cost up to 1,865 loads x 30 cycles = about 56 k cycles/frame = 3 % of CPU (upper bound: not every load is a table load); a compact encoding (about 2-3 KB, estimate) costs decode instructions instead | the single biggest table |
| `trigtabs` (antialias, IMDCT window 576, polyCoef 1,056, DCT) | 2,156 | hot | generated at build; polyCoef only for the software window | `polyCoef` is dead once the window is hardware-only |
| `mp3tabs` (bitrate, slot, sfb 666, ISF tables) | 1,278 | hot | hot or cold (header work is per frame, not per sample) | |
| Data (`dqchan` 568, `imdct` 72) | 640 | hot | | pow(4/3) and similar working tables |
| **Arena** (`MP3DecInfo` 2,064 incl. 1,940 main-data bytes; `FrameHeader` 64; `SideInfo` 328; `ScaleFactorInfo` 284; `HuffmanInfo` 4,624; `DequantInfo` 840; `IMDCTInfo` 6,944; `SubbandInfo` 8,708) | 23,856 (measured peak 23,824) | hot bss | | `SubbandInfo` 8,708 B is the 1,024-word x 2 polyphase history, redundant with the unit's on-chip V history when the unit is mandatory |
| `dv` union (`mp3d_t` frame window 1,536 etc.) | 6,360 | hot bss, shared with FLAC | | outside the decoder proper |
| **Helix total in hot RAM** | **about 71,500** | | | 37 % of the 192 KB link |

### 1.6 Where each table could live (for the clean-room design)

| Table | Size | Access pattern | Best home |
|---|---|---|---|
| Huffman | about 9 KB as stored | per decoded symbol, data-dependent | hot RAM compact form first; measure PSRAM only if RAM is the binding goal |
| Scale-factor-band, bitrate, slot, ISF | about 1.2 KB | per frame | hot, or cold |
| Antialias coefficients (8 pairs), IMDCT window (36+12 points), DCT constants | under 1 KB | per granule | hot; **or computed at build time by a generator from the closed-form formulas in the standard** (no data copied from anywhere) |
| Synthesis window D[512] | 1,056 B in Helix | hardware only | already in the unit's ROM (derived from the standard, B-1168); not needed in software |
| pow(4/3) and 2^(x/4) requantisation | 192-256 B each | per coefficient | hot |

---

## 2. TARGETS (budgets the new decoder must beat or match)

| Metric | Must match (gate) | Aim (needs a stated reason) | Real prize? |
|---|---|---|---|
| CPU, decode loop at 1.00x with the hardware window, Pocket `SR_T_LOAD2.dec_pct` on the same track | no worse than today's (30 %, DEV 91) | 24 % (about 20 % off today) by a leaner Huffman/requant and one-pass granule structure | Only as headroom for stereo tempo (stereo MP3 128k tops out at about 1.25x today, B-545/B-553) and FLAC unification |
| Worst-frame time (the longest atomic step; today about 12 ms) | no worse | halve by splitting a frame into granule steps | Yes for UI/governor smoothness; not a rate need |
| Hot RAM total (code + tables + arena), 192 KB link | no more than today's 71.5 KB | **35-45 KB**, i.e. free 27-37 KB | **YES: the owner's stated goal (gapless ring bookkeeping, tempo, FLAC unification)** |
| Heap gap after the swap (DIAG=0 / DIAG=1) | at least 13,568 B / 10,400 B | at least 40 KB / 37 KB | the number the owner can spend |
| Code size | no more than 33.5 KB | 15-20 KB | part of the RAM prize |
| Worst stack | no more than today's measured peak (1,672 B, B-230) | | rule: do not eat the 4x stack margin |
| Audio | within ISO limits and not audibly worse on the Test Album, tone vectors and speech | | |

**Argument (the D-T12 test).** CPU is not a binding need today: 30 % decode plus the rest leaves the Pocket at 44 % busy at 1.00x (B-593), no underruns, tempo works to 1.75x for mono speech. RAM IS binding: the 192 KB link, a rule that features are dropped never the floor (project rule 5), heap gaps of 13.5 / 10.4 KB, and gapless/tempo/FLAC already measured against it. So the justification for the rewrite is ownership (no RPSL) first, RAM second, CPU headroom third; no CPU speed-up is claimed as a goal.

---

## 3. REQUIREMENT inventory (full table: `MP3_DECODER_V2_REQUIREMENTS.csv`)

**Needed now (must not regress):** MPEG-1 Layer III 32/44.1/48 kHz, all bit rates 32-320, CBR and VBR (frame-by-frame bit rate), mono, stereo, joint stereo MS, long/short/start/stop blocks, scale factor reuse (scfsi), bit reservoir across frames, padding, the Xing/Info/LAME header (frame count, delay, padding for gapless: `fw2/cores/mp3_trim.h`, `lame_tag.h`), ID3v2 skip by seek, resync after junk (one byte at a time), seeking by byte position with the skip list (`skiptab_core.h`), the P3 sample accounting (`pairs_total`), ReplayGain (applied in the gain stage), skip silence (a lead/tail from `tau-skip.bin`, applied on pairs), one frame per call, no blocking.
**Needed but untested by any shipped file (decide with the owner, section 11):** MPEG-2 and MPEG-2.5 LSF (16/22.05/24 and 8/11.025/12 kHz; spoken-word MP3s at 22.05/24 kHz exist and are the reason the Cymo doc names 22.05/24), intensity stereo, mixed blocks, free format, CRC checking (today: CRC bits skipped), 8-bit-Huffman linbits edge values, lone granule streams.
**Interfaces the rest of the firmware relies on** (read from `fw2/cores/mp3_frames_core.h`, `aout.h`, `mp3play.c`): a frame driver that owns framing and calls the decoder with one frame in a linear buffer; output as interleaved int16 pairs trimmed to the pair, `pcm_blk_t { pcm, pairs, ch, src_pos }` with `src_pos` the source pair index (heard-position and P3 arithmetic); a stream ring of 24 KB; `MP3GetLastFrameInfo` (rate, channels, bit rate, samples per frame) for the rate tag; Report/Info counters (poly slots/mismatch/timeout/silent, `SR_T_LOAD`, `SR_T_LOAD2`, `UNDER RING`, `HW UNDER EV`, arena peak); the hardware window handshake `tau_poly_hw_start/finish/clear` with its self-check window and silence-on-timeout rule (B-587).

---

## 4. ARCHITECTURE options and recommendation

| Option | Description | For | Against |
|---|---|---|---|
| A. Full rewrite, one swap | New decoder written from the contract, verified on the host against an ISO-derived float reference and Helix as oracle, swapped in behind a build flag | cleanest provenance; free design (granule-streaming, one buffer set); simple author separation | no intermediate on-device proof; needs the host proof to be strong |
| B. Stage-by-stage replacement inside Helix | replace Huffman, then requant, ... each behind Helix's structures | on-device increments | **Impossible without the author reading Helix's structures (`HuffmanInfo`, `IMDCTInfo`, ...); mixes RPSL layout into the new code; defeats the provenance goal** |
| C. Host-first stage models, then the swap | A, but each stage first exists as a Python/C float and fixed-point model with its own golden vectors from the standard's formulas, then composed | stage-level evidence without Helix structures; unit tests per stage | slightly more upfront work |

**Recommendation: C then A.** Stages are specified by contract at behavioural boundaries (side info -> scale factors -> Huffman spectrum -> requantised spectrum -> stereo -> reorder/antialias -> IMDCT/overlap -> 32 subband samples -> window -> PCM) each with an ISO-derived float reference written by the contract author in Python; the implementer builds the fixed-point C stage and proves it against that stage's golden data; composition is checked end to end against Helix (oracle only) and against the ISO conformance streams. On the Pocket the whole decoder swaps in behind one build flag with Helix kept as a flag-selectable fallback for two releases (RAM paid only while both are linked in the test build; the shipped build links one).

---

## 5. HARDWARE / ISA opportunities (instruction-count model, no RTL touched)

### 5.1 What stays in software after the hardware window (per 44.1 kHz stereo frame, measured)
212 k instructions: IMDCT 70.6 k, DCT32 53.4 k, requantisation 35 k, Huffman+bit reader 28 k, MS 13.7 k, copies/glue about 11 k. Class mix of the two largest (DCT32, IMDCT): 20-17 % loads, 18-12 % stores, mulh 11 % / 10 %; the rest plain ALU and address arithmetic.

### 5.2 Custom-instruction candidates, counted on the real compiled hot loops (`patterns.py`, dynamic count per frame)

| Candidate | Pattern | Count per frame | Of the 212 k remaining instructions | Estimated saving | Verdict |
|---|---|---|---|---|---|
| Multiply-high-accumulate (`mulh` then `add`/`sub` within 3 instructions) | IMDCT 5,439; DCT32 576; DequantChannel 83 | 6,098 | 2.9 % | 1 instruction each plus a 0-2 cycle mul-use stall: 6.1 k-18.6 k cycles = 2.4-7.3 % of the 255 k remaining model cycles = 0.4-1.0 % CPU (x1.6: 0.7-1.6 %) | **Not justified** |
| 64-bit multiply-accumulate (MADD64 for the window) | 72,576 `mul`/`mulh` per frame in the software window | n/a with the unit | only matters if the unit is absent; the fw2 build requires it | **No** |
| Dual 16-bit MAC for the IMDCT | needs 16-bit operands | 0 provable: Helix's IMDCT is 32-bit data | would change precision; no evidence | **No** |
| Bit extract / shift-and-mask | `slli/srli/srai` followed by add/or/and of the result | IMDCT 2,700; DequantBlock 1,745; Huffman 837; DCT32 288; others small: 5.6 k | 2.6 % | well under 1 % CPU | **No** |
| Rounding shift / saturate | right shift followed by a compare/branch (clamp) | DequantBlock 1,761; Huffman 197: 2.0 k | 0.9 % | under 0.5 % CPU | **No** |
| Count leading zeros | software clz | not in the hot list (below the profile's noise: `__clzsi2` not in the top 14) | | | **No** |
| All combined | | at most 14 k instructions | 6.6 % | **1.5-2.5 % CPU** | Do not regenerate the CPU |

**Conclusion.** After the hardware window, the cost is generic loads/stores/ALU in two transform loops (DCT32, IMDCT) and table-driven decode, not multiply-accumulate. A custom instruction cannot pay for the risk of regenerating `src/fpga/rtl/VexRiscv_Full.v` (the task text names `src/fpga/core/cpu/`; the file is actually in `src/fpga/rtl/`), which is shared by copy with Tau V, Reader and Gallery, needs the SpinalHDL toolchain (not in this tree), a new fit and a re-verification of the CPU in all four consumers.

### 5.3 The better lever, if CPU headroom is ever wanted: DCT32 and IMDCT as hardware kernels
`MP3_FILTERBANK_KERNEL_DESIGN.md` already costs them (FDCT32 about 4-5 % CPU, IMDCT 6-9 %, 0-3 DSP each) and parks them. This model supports it: DCT32 + IMDCT are 57 % of the remaining model cycles (146 k of 255 k), and the Pocket shows the DCT32 alone at 8 % of the CPU. A kernel cannot be justified until the owner has a CPU need (stereo tempo, FLAC unification) that the RAM work does not satisfy. Resources are not the obstacle: 43 of 66 DSPs free, M10K 274-281 of 308 (about 27-34 free, the binding resource with ALM LAB packing at 93 %), ALMs about 84 %. If taken, it needs a fit (about 1h40 per seed, two seeds), the firmware probe-then-adopt pattern already used for the window unit, and the same host-first discipline.

### 5.4 How to prove a benefit before touching RTL
Add the hypothetical instruction to `rvprof.c` (decode and cost it as one op), rebuild the new decoder with an intrinsic, and compare the instruction count and the cycle model with and without it on the same files; accept only if the saving exceeds the sum of the calibration spread (a factor 1.1-2.2) and a stated CPU need. For this scoping that experiment is already answered by `patterns.py` (5.2).

### 5.5 SpinalHDL regeneration flow and risks (for the record)
Regenerate in a toolchain checkout from the same VexRiscv config (the generated file has no config record in this tree: **unverified which plugin set produced it**; features seen in the Verilog: hardware multiplier, iterative divider, full barrel shifter, instruction and data caches, CSR, a Wishbone data bus), add a `CustomSimdAddPlugin`-style instruction, regenerate, replace by copy in four projects, re-run every CPU-level bench (`test-rtl-psram-fw`, the real-CPU benches), fit twice, hold and setup margins. Risks: the shared file drifts from Tau V, Reader and Gallery; any CPU change invalidates every shipped fit's timing numbers; the compiler needs `.insn` or an intrinsic. All for 1.5-2.5 % CPU. Not recommended.

---

## 6. VERIFICATION

* **Conformance vectors (legitimate sources, to be confirmed by the owner).** (1) ISO/IEC 11172-4 and 13818-4 define conformance test bitstreams with reference decoded output; the vectors are distributed with the standard parts and circulate on MPEG-related sites. **I did not obtain or verify any, and their redistribution licence is unknown;** do not commit them without an owner decision. (2) The `lieff/minimp3` repository (CC0 1.0, licence text read for B-1168) ships a test-vector set (streams plus reference PCM) that I recall as derived from conformance material; **not in this tree and not verified here** (the B-1168 note says `third_party/minimp3/` carries test vectors, but that directory is absent on this branch). Verify the licence and the provenance of each vector before use. (3) Streams the owner generates with an encoder (LAME is LGPL; its OUTPUT files are data, not copied code, but confirm with the owner under rule 7) for every mode: all MPEG-1/2/2.5 rates, mono/stereo/joint/intensity, mixed blocks, short blocks, free format, VBR, CRC on, Xing/Info/LAME headers. (4) The project's own music (`test music/`, not in the repo: skip when absent, as `sim/test_p2_mp3.py` does) and generated tone vectors (`tone_1k_44100_60s.flac` exists as a source).
* **Tolerance rules.** The ISO full-accuracy rule as I recall it (**verify against the standard before it enters a test**): RMS of the difference to the reference decoder below 2^-15/sqrt(12) of full scale and a maximum difference within a small multiple of the 16-bit step; a "limited accuracy" class exists with a looser RMS bound. Procedure: write the float64 reference decoder from the standard in Python (the contract author), decode every vector with it, and measure the fixed-point decoder's RMS and max against it and against the vector's own reference PCM. Record both numbers per vector. Helix's own numbers vs the same reference are recorded as the bar to match or beat.
* **Differential against Helix (oracle only).** Frame by frame on the host: identical frame boundaries, errors and resync decisions; PCM within the measured Helix-vs-reference band (not bit-exact: **bit-exactness with Helix is not a goal and would defeat provenance**). Disagreements are classified (as the Talos rewrite did): table error, rounding, bug in either.
* **Golden hashes** for the project's vectors once the decoder is accepted (per file, per mode, per rate), stored like the Cymo and meter goldens; any later change must be a deliberate re-golden with an audit entry.
* **Mutation tests, project style** (`sim/` red-then-green): per stage, mutants such as an off-by-one in a scale-factor band edge, a wrong window half for short blocks, a skipped antialias butterfly, MS rounding, a dropped reservoir byte, a wrong linbits width, a missing sign; each must fail a named check.
* **Fuzzing** (host): random bit flips, truncation, junk between frames, bit reservoir pointing before file start, `main_data_begin` larger than available, region counts out of range, zero-length and maximum-length frames, rate changes mid-stream; pass condition: no out-of-bounds access (AddressSanitizer build of the new C), no infinite loop, bounded time per frame, a defined error code, resync within N bytes; plus a watchdog on the Pocket.
* **On-Pocket checks** with what exists: the FULL Check `SR_T_LOAD`/`SR_T_LOAD2` (decode %, push %, busy %), Info heap and arena peak, `UNDER RING`/`HW UNDER EV`/`GOV PANIC`, poly `mismatch`/`timeout`/`silent` (must stay 0), the audio window and loopback tone analysis (`tools/lab/cymo_loopback.py`), tempo at 1.25x-1.75x on the mono speech file, gapless on the tone album (P3 pairs equal), seek and skip-silence. Listening pass by the owner on the Test Album plus the speech file.

---

## 7. CLEAN-ROOM (summary; the process is in `MP3_DECODER_V2_CLEANROOM_PROCESS.md`)

Roles: **contract author** (may have seen Helix: writes the behavioural contract and the ISO-derived float reference model, never code for the decoder), **implementer** (never saw Helix or this report; receives only the contract, the standards' formulas and the permitted data), **verifier** (runs Helix and the similarity audit). Data tables are extracted as DATA from permitted sources with a licence check and verified by structural identities (Kraft sums, counts, symmetry), exactly as the synthesis window was in B-1168. Similarity audit: identifier-normalised token winnowing against Helix, minimp3 and any other read source; thresholds in the process document.

---

## 8. PHASED PLAN (entry/exit gates, effort, parallelism, fits, rollback)

| Phase | Work | Entry gate | Exit gate | Effort (sessions) |
|---|---|---|---|---|
| P0 | Owner decisions (section 11), vectors sourced, licence of each logged, similarity tool built | this report approved | decisions recorded in `DECISIONS.md`; vectors committed or located | 2 |
| P1 | Contract + float reference model (Python) + stage golden data, written by the contract author | P0 | expert reviews C1 and C2 (section 10) passed; float model passes the conformance streams to the ISO bound | 5-6 |
| P2 | Harness and oracle: host decoder harness, Helix oracle wired (existing `sim/p2_mp3_harness.c` pattern), fuzz and mutation scaffolding | P0 | harness decodes with Helix and with the float model, reports RMS/max, runs under ASan | 2-3 |
| P3 | Implementer builds stages in order: bit reader + side info + scale factors; Huffman + requant; stereo + reorder + antialias; IMDCT/overlap; FDCT32 + glue to the unit; all fixed-point C in the style of the `fw2/cores/*_core.h` host-testable cores | P1 | each stage passes its golden data and mutants; end-to-end within the ISO bound on all vectors; similarity audit clean | 10-14 |
| P4 | Integration behind a build flag in `fw2` (frame driver, counters, arena sizing, hardware window handshake) and the rv32sim cycle/RAM measurement | P3 | `make test-host` and `sim/test_p2_mp3.py` equivalent pass on the new decoder; RAM and model CPU at or below the Section 2 gates | 3-4 |
| P5 | Pocket gate 1: DEV core with the new decoder, FULL Check, listening, tempo, gapless, Report counters; Pocket gate 2: soak 60 min and an edge-case playlist | P4 | no regression on any row of section 6; owner listens | 3-4 (owner time dominates) |
| P6 | Provenance close-out: pre-deletion review, remove Helix from the shipped build (kept as oracle in `sim/`, and as fallback flag for two releases), update NOTICE/ATTRIBUTIONS | P5 | review R5 passed | 1-2 |
| P7 (optional, only on a stated CPU need) | DCT32/IMDCT hardware kernel | P6 and owner need | one two-seed fit, Pocket check | 8-12 plus 1 fit |

**Parallel work in separate worktrees:** P1 (contract + float model) and P2 (harness) run in parallel; P3 stages can be built by one implementer or in parallel by stage once their contracts exist (Huffman/requant in one worktree, IMDCT/overlap in another); the output-stage follow-ups (section 10) are separate worktrees and must not block P3. **Fits:** none in P0-P6 (software only, the window unit's ROM and handshake are unchanged); one in P7. **Rollback:** the build flag selects Helix; the old decoder stays linkable until P6 is signed; nothing in RTL changes before P7. **Total (P0-P6): 26-36 sessions; calendar 8-11 weeks with Pocket gates.**

---

## 9. The 48 kHz question: could the decoder make 48 kHz output native?

The coordinator relayed the owner's question: the Pocket's audio bus is exactly 48 kHz (I2S 16-bit stereo, MCLK 12.288 MHz; `analogue-pocket-dev` KB `hardware-video-audio-input.md`, cited by `CYMO_AUDIO_ENGINE.md` section 13: sample-rate adjustment is not allowed), most music is 44.1 or 32 kHz. Could the new decoder fold sample-rate conversion into synthesis?

**What is built, not designed (READ, `src/fpga/core`, `fw2/aout/aout.c`).** `pcm_ring` (PSRAM, CPU writes {R,L} int16 pairs) -> `pcm_fifo` (2,048 pairs, drains at the source rate by a fractional accumulator, `rate_inc`) -> `tau_gain_stage` -> either (a) the hold path: `sound_i2s` samples whatever is presented at LRCK (a zero-order hold) or (b) `tau_cymo_feed` + `tau_cymo_resamp` (**a 32-tap, 160-phase Kaiser FIR, exactly 160:147, 44.1 kHz only**) -> `tau_halcyon` (17-stage biquad cascade, 24-bit coefficients, its own 48 kHz tick, input EQW = 18 from the resampler) -> 16-bit audio -> `sound_i2s`. `fw2/aout/aout.c` enables the resampler only for `rate_hz == 44100` at 1.00x (`want = 4`, otherwise 0). The header comment in `tau_cymo_resamp.sv` ("NOT YET WIRED") is stale: `tau_soc.v` instantiates it and the audit has hardware numbers.
**Measured quality (Pocket, EVO 4 loopback, B-713):** resampler ON 58.2 dB SINAD, THD -75.8 dB; hold path 27.8 dB SINAD, spurs at -36 dBc (matches the model, 27.7 dB). Older loopback: 54.5 dB ON vs 10.8 dB OFF on a differential chain later voided (B-529/B-531/B-666).

1. **Needed regardless of the decoder.** Source rates 8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48 kHz (MP3) and up to 96 kHz (FLAC); varispeed (the output tick moves, pitch follows, speeds up to 2.00x for MP3); a tempo input at the source rate (WSOLA runs on decoded pairs at the source rate, firmware, cold code); an EQ whose coefficients are computed for 48 kHz. The decoder cannot own any of these: the same decoder output feeds tempo, FLAC shares the output stage, and speed is a user control.
2. **Is decoder-integrated conversion sound?** Not for the cases that matter. The MP3 synthesis filterbank produces 32 samples per slot at the *source* rate; changing the rate by an integer factor is possible only by dropping or adding subbands (a 2:1 decimation drops the upper 16 bands), and 44.1 -> 48 kHz (160/147) is not an integer ratio. A rational conversion in the window stage needs a different filter per output phase: the hardware window unit has one fixed 512-tap coefficient set and 32-in/32-out slots, so it cannot change rate; doing it in software costs 48 kHz x 32 taps x 2 channels = 3.1 M multiply-accumulates per second (more than the whole remaining decode MAC load) for something the hardware resampler does for one DSP. It would also (a) put gain and EQ at the wrong place (EQ coefficients would be per source rate, breaking the single Halcyon data format and presets), (b) not serve FLAC or tempo or varispeed, so a second converter would still be needed, and (c) tie correctness of audio to decoder bugs. **Not sound; do not do it.**
3. **Clean split of responsibilities.** The decoder emits stereo/mono PCM at the source rate, trimmed, with a **rate tag per frame** (rate, channels, source pair index). The output stage owns conversion to 48 kHz, gain/ReplayGain/ramps, EQ and final rounding. This is what `CYMO_AUDIO_ENGINE.md` section 6 designed and what is largely built.
4. **48 kHz sources.** No conversion and zero algorithmic error: `aout` already selects the hold at the exact 48,000 Hz drain rate and the resampler stays off. A decoder-side fast path (skip anything) has nothing to skip: the decoder already outputs 48 kHz samples. The only residue is clock-domain drift between the `clk_sys` accumulators and the I2S LRCK (section 10, seam S1), common to all rates. **Recommendation: keep conversion in the output stage; the rewrite changes nothing here except to supply the rate tag. Do not invent a need.**

---

## 10. WHOLE-ENGINE ALIGNMENT

Read for this section: `CYMO_AUDIO_ENGINE.md`, `_REVIEW`, `CYMO_AUDIO_STACK_REVIEW.md`, `CYMO_OUTPUT_STAGE_SPEC.md`, `GAPLESS_SPEC.md`, `SKIP_SILENCE_SPEC.md`, `CYMO_TEMPO_INTEGRATION.md`, the RTL headers of `tau_pcm_ring.sv`, `pcm_fifo.v`, `tau_gain_stage.sv`, `tau_cymo_feed.sv`, `tau_cymo_resamp.sv`, `tau_halcyon.sv`, `sound_i2s.v`, the audio wiring in `tau_soc.v` lines 1140-1420, and `fw2/aout/aout.c`. Not read in full (said so): `CYMO_DSP_REVIEW.md`, `CYMO_GAIN_STAGE_DESIGN.md`, `CYMO_HALCYON_SPEC.md`, `CYMO_HEADPHONE_PLAN.md` (only through the cross-references in the stack review and the output-stage spec); Halcyon coefficient generation firmware; the crossfeed path. No Pocket run, no RTL change.

### 10.1 Stage table (as built)

| # | Stage | Owner | Format | Rate / clock | Buffer | Lives in |
|---|---|---|---|---|---|---|
| 1 | Stream read + framing, ID3 skip, resync, Xing/LAME trim | firmware `mp3_frames_core.h` | bytes; trimmed int16 pairs out | file-driven | 24 KB compressed ring + 1.5 KB frame window | firmware |
| 2 | **MP3 decode** (this project) incl. polyphase | firmware + window unit | int16 pairs; window output rounded/clamped to 16 bit in the unit | decode loop, bursty (about 12 ms per frame) | arena 24 KB + `dv` 6.4 KB; unit holds V history on chip | firmware + RTL `tau_mp3_poly` |
| 3 | Skip silence (lead/tail), gapless trim | firmware | pair counts | file-driven | none | firmware |
| 4 | Tempo / WSOLA (optional) | firmware, cold code | int16 pairs, mono search | source rate, runs N x faster than output | `tempo_st` 4.4 KB hot + 512 B + PSRAM staging at +0x600000 | firmware (cold) |
| 5 | PCM ring | CPU writes, `tau_pcm_ring` reads | {R,L} int16 in one 32-bit word | source rate; CPU-write / HW-read | PSRAM ring up to seconds (2 s planned, 345 KiB) | RTL + firmware |
| 6 | `pcm_fifo` | RTL | 32-bit {R,L} | **drains at the source rate** by a fractional accumulator from `clk_sys` | 2,048 pairs = 46 ms at 44.1 kHz | RTL |
| 7 | Gain stage: volume (dB, Q15, 5 ms ramp), ReplayGain folded into the target (boost cap 65535 = +6.02 dB, peak-limited by firmware), fade after flush | RTL `tau_gain_stage`, target computed in firmware | in 16, out 16 (rounded, no dither), Q15 gain | at the FIFO tick | none | RTL + firmware (Q15 math, `pcm_vol_tab` 202 B) |
| 8 | Elastic feed | RTL `tau_cymo_feed` | 16-bit pairs | track-rate tick into a 4-entry queue, pulled by the resampler | 4 entries, STALE/DROP counters | RTL |
| 9 | **Resampler** (44.1 kHz only, 160:147, 32 taps) | RTL `tau_cymo_resamp` | 16 in, EQW = 18 out | its own `cymo_tick` fractional accumulator from `clk_sys` | 32-deep history x2 | RTL |
| 10 | Halcyon EQ (17 biquads, Q2.22 coefficients, preamp multiply, round + clamp to 16) | RTL `tau_halcyon`; presets in firmware (`halcyon.c`) | 18-bit in, 16 out, 24-bit coefficients | **its own 48 kHz fractional tick from `clk_sys`** | shadow/live coefficient banks, state RAM | RTL + firmware |
| 11 | I2S out | RTL `sound_i2s` | 16-bit slot (15-bit option removed from default) | **LRCK from the MCLK PLL domain, asynchronous to `clk_sys`** | none (latches at LRCK) | RTL |
| M | Meters (spectrum bank, wave) | RTL | tap the FIFO output, pre-gain | FIFO tick | | RTL |
| F | FLAC | firmware `flac2` + hardware LPC kernel | int32 decode, 24-to-16 rounding in firmware | file-driven | `dv` shared with MP3 | firmware + RTL |
| C | Report / Info counters | firmware | | | | firmware |

**Order of stages today:** tempo (source rate, firmware) -> ring -> FIFO -> gain -> [resample] -> EQ -> round/clamp -> I2S. This order is right: tempo needs unscaled audio; gain before EQ gives the EQ the headroom it needs (the decision "gain before the EQ (B)" in the stack review); the soft clipper and dither were designed (output stage spec) and are **deferred by owner decisions D-G02/D-G03** (the analogue floor already dithers, Halcyon gives back its own boost, positive ReplayGain is peak-limited).

### 10.2 Seams and proposed fixes, ranked by value

| Rank | Seam | Evidence | Proposed fix | Cost / risk | Measure first | Scope |
|---|---|---|---|---|---|---|
| S1 | **Several independent "48 kHz" tick sources:** `pcm_fifo`'s drain accumulator, `cymo_tick`, Halcyon's tick (all fractional accumulators from `clk_sys`, fixed by B-649/B-527) versus the I2S LRCK from the MCLK PLL (separate clock domain; the skill says treat as asynchronous). Any ppm difference means a dropped or repeated sample every few seconds | READ: `CYMO_AUDIO_ENGINE_REVIEW.md` C1; `tau_soc.v` 1321-1325, `tau_halcyon.sv` 82-95, `sound_i2s.v`; the Halcyon tick fix (B-649) removed a 30.7 Hz error but a ppm offset to the real LRCK remains by construction. No audit entry measures a slip rate on a Pocket | One LRCK frame strobe (one-bit synchroniser, KB-009) into `clk_sys`, used as the output tick by resampler AND Halcyon; FIFO drain pulled by the resampler | RTL change in `tau_soc.v`, `tau_cymo_resamp`, `tau_halcyon`, one fit; medium regression risk (the whole audio path) | a long loopback capture of a 1 kHz tone, count slips (phase jumps) per minute, with Cymo ON and OFF | **Separate follow-up** (output stage) |
| S2 | **The resampler is fixed at 160:147 and 44.1 kHz at 1.00x:** 32, 22.05, 24, 16 kHz and varispeed fall back to the hold at 27.8 dB SINAD; 48 kHz needs none | READ `aout.c` 38-44; MEASURED B-713 | A pull-driven resampler with a firmware-set rational ratio (the design of `CYMO_AUDIO_ENGINE.md` 6.1 and the review): also deletes `tau_cymo_feed` (the elastic queue exists only because the FIFO pushes at the source rate), and the `CLK60/CLK66` drain interlock | RTL + coefficient banks per ratio (M10K is the tight resource); high value for 22.05/24/32 kHz spoken-word files | list which rates the owner's library actually has (cheap: scan the card) | **Separate follow-up** |
| S3 | **No rate or format tag in the PCM path:** `rate_hz` is a global set at track start; the ring holds untagged pairs, so a gapless join between 44.1 and 48 kHz tracks (the Test Album has both) is a flush, not a join | READ `GAPLESS_SPEC.md` (same-rate join), `tau_pcm_ring.sv` | Define the decoder's output contract now as (pairs, channels, rate, source pair index, discontinuity flag); later let the ring carry a small segment table (start index, rate) so hardware applies a rate change at the boundary | contract: zero cost, in scope; hardware segment table: RTL | check how often adjacent tracks differ in rate in the owner's library | **Contract IN SCOPE; hardware follow-up separate** |
| S4 | **Position accounting defined in several places:** `src_pos` (frame driver), P3 `pairs_total`, ring `cnt` (heard position), skip-silence lead/tail, gapless trim, `ui_sec` from decoded frames, tempo staging offset | READ `mp3_frames_core.h`, GAPLESS 5.1, TEMPO 3 | The decoder contract states one rule: every output block carries the source pair index after trim and skip; everything else derives from it | zero RTL; tests exist (`test_p2_mp3.py`, P3) | | **IN SCOPE** |
| S5 | **Up to three 16-bit roundings in a row** with no dither: window unit (round + clamp to 16), gain stage (`+16384 >> 15`), Halcyon output (round + clamp); the resampler output is 18-bit between them | READ `tau_gain_stage.sv`, `tau_halcyon.sv`, `tau_poly` | None now. Candidate: a wider PCM contract (the contract field reserves "PCM24") so the unit/gain/resampler/Halcyon can carry 18-24 bits | RTL in four blocks, FIFO width, ring entry 2 words; **no measured need** (D-G02: analogue floor already dithers; the loopback floor is analogue, B-689..B-717) | whether the noise is measurable in a quiet-passage loopback at low volume | **Out of scope; keep the contract extensible** |
| S6 | **Decoder clipping before gain:** the window unit clamps to 16 bit before the gain stage, so a boosted-ReplayGain or an inter-sample-hot MP3 (decoded peak above the tag's peak) clips hard where the gain stage cannot fix it; the firmware peak-limit uses the tag's peak | READ D-G04 (positive RG peak-limited), `tau_gain_stage` BOOST | none until measured | needs the unit to emit wider words (RTL) | count decoder-side overshoot on the Test Album and the owner's library with the float model (cheap, host) | **Measure inside the rewrite's host phase; fix is a follow-up** |
| S7 | **Two copies on the way to the ring:** decode -> `pcm[]` (4.6 KB) -> (tempo staging in PSRAM when on) -> ring through the uncached PSRAM window (about 26 cycles per word write, KB-040) -> hardware read | READ GAPLESS/PCM_RING docs, `mp3play.c` | The new decoder writes pairs straight into the ring in granule-sized blocks (576 pairs) and skips `pcm[]` | saves 4.6 KB RAM; CPU about the same (the write is the cost); in scope as an interface choice | | **IN SCOPE (interface), small** |
| S8 | **Tempo cost lands on the decoder budget** (stereo MP3 128k tops out near 1.25x; FLAC none) | `CURRENT_STATUS.md` B-545/B-553 | none beyond a faster decoder; the DCT32/IMDCT kernels are the lever (section 5.3) | | the stereo tempo need from the owner | **Follow-up, on need** |
| S9 | **EQ and gain constants at 48 kHz:** Halcyon coefficients exist for 48 kHz only. Holds only if the EQ stays after the resampler (it does) | READ `tau_soc.v`, `tau_halcyon.sv` | **Do not move conversion into the decoder** (section 9) | | | **Rejected** |

### 10.3 Latency and glitch behaviour at track change (READ, not re-measured)

Manual change: `pcm_flush` (empties the FIFO, resets the resampler feed and arms the gain stage's fade-in of 2,048 samples), 263-292 ms load, 1.48 s press-to-sound measured (B-717); Halcyon is not cleared. Natural end, gapless (B-741, same rate only): the ring is not flushed, the resampler and Halcyon keep running, the gain stage does not fade, the trim comes from the Xing/LAME header. Hardware window unit: `tau_poly_hw_clear` at decoder init, 8-slot self-check window, one slot of silence on a timeout. The new decoder must reproduce exactly these hand-offs; it must also reset its overlap and bit reservoir on a discontinuity and keep them across a gapless join (a reservoir pointing before the new file's first frame is the existing "frame 1" error, B-586 note).

### 10.4 Scope line

**In scope for the decoder rewrite:** the output contract with the rate tag and source pair index (S3, S4), writing straight to the ring (S7), RAM and CPU targets (section 2), the measurement of decoder-side clipping on the host (S6), the interface to the window unit (unchanged), counters. **Separate follow-ups, each its own scope with measurement first:** S1 LRCK-locked single tick, S2 general pull-driven resampler, hardware segment table for rate changes at gapless boundaries, wider PCM path (S5), tempo kernels (S8), the deferred soft clipper and dither. **Rejected:** decoder-integrated rate conversion (S9, section 9).

---

## 11. RISKS and mitigations

| Risk | Likelihood | Effect | Mitigation |
|---|---|---|---|
| Audio correctness on edge streams (no test file for LSF, intensity, mixed blocks, free format) | high without vectors | clicks, desync, silence | conformance vectors; encoder-generated edge streams; fuzzing; decide scope with the owner (below) |
| Clicks at gapless and seek boundaries from reservoir/overlap state | medium | audible | contract states reset/keep rules; P3 pair-count tests; Pocket gapless test |
| Cycle budget miss because memory-bound loops cost 2x the model | medium | decode above 30 % | budgets use Pocket numbers; measure with rv32sim early; the DCT32/IMDCT kernel option |
| Clean-room evidence is honour-based | medium | provenance challenge | roles, two repositories or access control, audit log, similarity tooling (process document) |
| Schedule slip from role separation | high | calendar | contract author starts P1 immediately; implementer is a fresh agent per stage |
| Shared CPU file (only if custom instructions are pursued) | avoided | 4 projects | do not pursue (section 5) |
| Scope creep from the output-stage seams | high | rewrite stalls | the scope line in 10.4 |
| Helix kept as fallback keeps RPSL code in the tree | certain until P6 | provenance incomplete | flag-selectable for two releases, then removed from the build; oracle stays in `sim/` only |

## 12. EXPERT REVIEW checkpoints

| # | Reviewer | When | Exact questions |
|---|---|---|---|
| R1 | Contract reviewer who never saw Helix | end of P1 | Can an engineer implement every stage from the contract plus the standard alone? Which statements depend on an implementation choice rather than the standard? Are error and resync behaviours specified? |
| R2 | DSP/numerics | after the float model and again after P3 | Are the fixed-point formats per stage (spectrum, IMDCT, overlap, window input) sufficient for the ISO bound at 320 kb/s peaks? Is saturation placed where the standard allows? |
| R3 | RV/ISA | end of P4 | Is the generated code free of unaligned or uncached traps (the arena, PSRAM windows)? Does any loop need the I-cache layout care of Layered Wave? Stack depth? |
| R4 | Timing/fit (only if P7) | before the fit | DSP/M10K/ALM, LAB packing, hold/setup margins vs the +0.655 ns / +0.078 ns baselines |
| R5 | Pre-deletion | P6 | Does the similarity audit meet the thresholds? Is every table's source and licence recorded? Are all rows of section 6 green on a Pocket? |

## 13. OWNER DECISIONS and unknowns (each with a cheap way to resolve)

| # | Decision / unknown | Cheap resolution |
|---|---|---|
| D1 | Approve the rewrite on ownership + RAM grounds (no CPU claim) | read sections 0, 2 |
| D2 | **Scope of coverage:** MPEG-2/2.5 LSF (22.05/24 kHz speech), intensity stereo, mixed blocks, free format, CRC: needed or not? | scan the owner's library for sample rates and modes with a header scan script (minutes) |
| D3 | Is the hardware window unit mandatory (drop the software window, 7.5 KB code + 1 KB table + 8.7 KB history)? fw2 already sets `TAU_POLY_REQUIRED=1`, but the software path is still linked | owner decision; can be done on Helix today as a RAM quick win (not in this scope) |
| D4 | Vector sources and licences: ISO conformance streams, minimp3's vectors, own encoder output (LAME is LGPL; its output is data) | owner confirms; I recorded what I could not verify (section 6) |
| D5 | Output contract fields (rate tag, source pair index, discontinuity, reserved PCM24) | agree in P1 |
| D6 | Keep Helix as a flag-selectable fallback for how many releases (proposed 2) | decision |
| D7 | Hard scope line: output-stage seams S1-S3, S5, S8 are separate projects | agree 10.4 |
| D8 | **48 kHz question (owner, relayed):** keep conversion in the output stage, decoder emits source rate with a tag (recommended) | answered in section 9 with evidence |
| D9 | Stereo tempo or FLAC unification as a stated CPU need that would justify the DCT32/IMDCT kernels | owner states the need, or it stays parked (D-T12 style) |
| D10 | Same author for contract and float model is acceptable if they have seen Helix; the implementer is separate | accept the roles |
| U1 | Whether `SubbandInfo`'s history is touched when the unit is healthy (it is allocated either way) | grep/trace the shipped path; the number matters for the RAM estimate |
| U2 | The Pocket decode % for 48 kHz and 320 kb/s files (the audit's track is unknown) | one FULL Check on `05 MacCunn 320 48k` |
| U3 | A real slip rate at the LRCK (S1) | loopback capture, 5 minutes |
| U4 | The CPU configuration of `VexRiscv_Full.v` (no config record in the tree) | ask the owner for the SpinalHDL config |

## 14. What was NOT done or verified

No Pocket measurement, no fit, no RTL change, no new decoder code. Not obtained: ISO conformance streams, minimp3's vectors, the printed standards (Huffman tables and the tolerance rule are quoted from memory and flagged). Not measured: MPEG-2/2.5, intensity, mixed blocks, free format, CRC, resync cost, the Pocket cost on any file but the audit's single unknown track. The cycle model is optimistic by 1.1-2.2x against the Pocket and the SDK sysroot needed a workaround on this machine (`MP3_SDKROOT`).
