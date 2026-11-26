# MP3 v2 WP-05: the cache-aware profiler, committed and checked against the expert review (2026-11-25)

**Status: report (work package WP-05 of `docs/features/MP3_DECODER_V2_PLAN_2026-11-25.md`).** Host-only: no firmware, RTL, fit, card or VM was touched; nothing ran on a Pocket. **Evidence labels:** **[MEASURED-MODEL]** run in this session on the profiler's model (instruction counts are exact for the compiled code; cycles and misses are the model's); **[READ]** read in a source file; **[INFERRED]** a conclusion drawn from numbers, not observed; **[QUOTED]** a figure taken from the review and not re-measured. The Pocket-side figures below are the review's reading of B-591 (an old-player profile build on a guessed track), not new measurements.

## 1. Result in one paragraph

`tools/mp3_v2_measure/` now builds **the decoder as Player 2 builds it** (`TAU_POLY_FW=1 TAU_POLY_REQUIRED=1`, the patched Helix tree of `tools/helix_tree.py`, `-O2 -march=rv32im`, a RAM stub with the window unit's loops in place of `poly_hw.c`) and runs it under an RV32IM simulator with **the CPU's cache model: 4 KiB direct-mapped I$ and D$, 32-byte lines, write-through, no write-allocate, a miss penalty of 12 cycles (the review's 10 to 15)**. **The review's five-row table is reproduced: instructions per frame within 0.04 %, cacheless model cycles within 0.56 %, stores within 0.00 %, on all five rows**, and the Battle row's per-function split (IMDCT, FDCT32, Huffman, DequantBlock, Subband glue) to within 0.1 %. Two things the review could not say and this tool can: **the review's stub was heavier than `poly_hw.c`** (the faithful stub gives 2.8 % fewer instructions per stereo frame, section 4), and **the code layout alone moves the I$ misses by 1.7x** (1,450 to 2,411 per frame over 13 link orders of the same code, 2.9 % of the cycles; the review's 1,449 is the luckiest of them, section 6). **Correction factors (Pocket over model, from the review's B-591 stage figures): 1.34 to 1.38x cacheless, 1.15 to 1.19x with the cache model** (section 5), the second being the budget rule. **Layout A/B: [slot][subband] misses exactly as often as column-strided when nothing aliases and 2.4x less often when 24 lines of scratch alias the output array** (section 7).

## 2. What was wrong and what changed

The first version (the scoping package) built a configuration nothing ships: Helix from the pristine tree (no `TAU_POLY_*`, so the software window ran: 600,663 instructions per frame on the 02 MacCunn file against Player 2's 267 k), no unit stub, no cache model ("hot code and data are on-chip RAM": on-chip yes, cached in 4 KiB no), 150 frames. Changed, all under `tools/mp3_v2_measure/` (nothing under `tools/mp3_v2/` was touched; the WP-04 tool calls into it only):

| File | Change |
|---|---|
| `rvprof.c` | `--cache` (I$/D$ model as above, cold start, MMIO never cached), `--imiss N`, `--dmiss N`, `--icache-bytes`, `--dcache-bytes`; per-PC I$ and D$ miss counts as two new profile columns; the summary line gains misses, loads and stores. Instruction counts do not depend on the cache option (tested). |
| `measure.py` | rebuilt: `build(out, frames, mode, stub, order_seed)` with `p2` (today's Player 2), `p2-premono` (what the review measured for the mono row, before the mono change), `software` (the old configuration); `run()`, `calibration()`, `reproduce()`; `--review` prints the review's table beside the measured one; `--no-cache`, `--imiss`, `--dmiss`, `--stub shipped|review`. |
| `poly_stub.c` (new) | the unit's API as RAM loops of `poly_hw.c`'s shape (64 pushes, a poll, 32 x index store + result read + 2 halfword stores), `-DPOLY_STUB_REVIEW=1` for the review's shape. |
| `layout_ab.c`, `layout_ab.py` (new) | the synthetic D$ layout A/B and the I$ alias pair. |
| `alias_check.py` (new) | the I$ alias checker (section 6). |
| `patterns.py` | reads the first five profile columns (it failed on the two new ones). |
| `sim/test_mp3_v2_profiler.py` (new) | 39 checks in `make test-host` (section 8). |

Unit and model parameters re-read [READ]: `src/fpga/rtl/VexRiscv_Full.v` (the file `src/fpga/ap_core.qsf` includes) declares for the instruction cache `banks_0 [0:1023]` of 32 bits (4 KiB) and `ways_0_tags [0:127]` (128 lines, one way, so 32-byte lines and direct-mapped), and for the data cache four byte arrays of 1,024 (4 KiB) and 128 tags: **plan unknown U13 is confirmed (the generated Verilog has the sizes the review used)**. That the D$ is write-through with no write-allocate is the review's and LAYERED_WAVE_PERF_PLAN's statement; it was not re-derived from the Verilog here.

## 3. The review's table, reproduced [MEASURED-MODEL]

`python3 tools/mp3_v2_measure/measure.py <scratch> 0 --review`. The review says "1,000 to 3,000 frames per file"; 1,000 reproduces four rows to the instruction and 3,000 the Battle row. The review's "model cyc/frame" is the **cacheless** model (the plan labels it "caches off"); here it is `cycles - 12 x misses`, exact because a miss only adds its penalty. Review-shaped stub (section 4), mono row on the build the review had (mono ran the software window before the mono change B-1172).

| Row (frames) | Instr/frame (review) | Ratio | Cacheless cyc/frame (review) | Ratio | Stores (review) | I$ miss (review) | D$ miss (review) |
|---|---|---|---|---|---|---|---|
| 02 MacCunn 128 k 44.1 st (1000) | 274,493 (274,389) | 1.0004 | 330,596 (328,767) | 1.0056 | 38,676 (38,676) | 2,480 (1,449) | 2,042 (1,821) |
| 05 MacCunn 320 k 48 st (1000) | 294,057 (293,953) | 1.0004 | 354,348 (352,518) | 1.0052 | 38,715 (38,716) | 2,488 (1,432) | 2,260 (2,015) |
| Tank! 320 k-class VBR 44.1 st (1000) | 299,504 (299,400) | 1.0003 | 359,966 (358,136) | 1.0051 | 39,989 (39,989) | 2,998 (1,953) | 2,233 (1,996) |
| 06 Battle (3000) | 300,293 (300,189) | 1.0003 | 360,920 (359,090) | 1.0051 | 39,646 (39,646) | 2,690 (1,641) | 2,291 (1,965) |
| 11 Speech 128 k 44.1 mono (1000) | 318,139 (318,139) | 1.0000 | 389,774 (389,774) | 1.0000 | 40,765 (40,766) | 4,548 (4,572) | 4,312 (4,837) |

**The five rows the plan asks for are within 2 %, and the instruction, cycle and store columns are within 0.6 %.** The mono row, which needs no stub, reproduces exactly and its I$ misses match to 0.5 % (the model and the link order are the review's). **The misses of the four stereo rows are not reproduced** (I$ 1.5 to 1.7x, D$ 1.12x the review's): the review's scratch layout and stub placement are not recorded and the I$ count depends on them (section 6); the D$ column is within 17 % (1.12x on three rows, 1.17x on Battle) and the mono D$ 11 % low.

Battle, per function, cacheless cycles per frame (review in brackets): IMDCT 89.9 k (89.9), FDCT32 77.2 k (77.2), Huffman 53.3 k (53.3), DequantBlock 47.2 k (47.2), Subband glue 28.2 k (28.2), push 20.1 k (20.9), read 15.7 k (13.0). The first five are the same to the cycle; the stub's push and read split differs (its sum, 35.8 k against 33.9 k, is 5 % high).

## 4. The review's stub was heavier than the shipped one [MEASURED-MODEL, INFERRED]

Against the faithful stub (the loops of `fw2/decode/poly_hw.c` over RAM registers), the review's rows carry a **constant 7,636 instructions, 7,566 cacheless cycles and 1,152 fewer stores per frame on every stereo row** (36 slots per frame: +212 instructions, +210 cycles, -32 stores per slot). The stores point to a result array indexed in RAM with no index store per word; the extra instructions are copying of unknown shape. `poly_stub.c -DPOLY_STUB_REVIEW=1` reproduces those deltas with a padding loop (it is documented in the file as not decoder work) so that the table above can be reproduced; **any new measurement uses the faithful stub.** Faithful-stub rows, the same files and frames:

| Row | Instr/frame | Cacheless cyc/frame | Stores | I$ miss | D$ miss | Ratio to the review: instr, cycles |
|---|---|---|---|---|---|---|
| 02 MacCunn 128 k | 266,753 | 321,200 | 39,828 | 2,132 | 2,006 | 0.972, 0.977 |
| 05 MacCunn 320 k 48 k | 286,317 | 344,952 | 39,867 | 2,140 | 2,220 | 0.974, 0.979 |
| Tank! | 291,764 | 350,570 | 41,141 | 2,650 | 2,187 | 0.974, 0.979 |
| 06 Battle (3000) | 292,553 | 351,524 | 40,798 | 2,342 | 2,265 | 0.975, 0.979 |
| 11 Speech mono (premono build) | 318,139 | 389,774 | 40,765 | 4,548 | 4,214 | 1.000, 1.000 |

Not modelled by either stub: the **latency of a real MMIO access** and the unit's compute time. The review measured `sub_hw` at about 3 % of a frame on the Pocket (52 k cycles); the faithful stub's push and read are 14.2 k and 12.2 k cycles on Battle (26 k, with misses 29 k). That gap, 23 k to 26 k cycles per frame, is the subband stage's excess in section 5 [INFERRED, the MMIO share not measured].

## 5. Pocket over model [QUOTED Pocket figures, MEASURED-MODEL model]

B-591's stage figures as the review quotes them (1 % of the CPU = 17.4 k cycles per frame, so about 70, 70, 122, 209 k for Battle, an old-player profile build on a guessed track) against this tool's model of the Battle row, groups as the review's (h Huffman, side info, scale factors, sync; d requantise and stereo; i IMDCT incl. frequency inversion; s FDCT32, glue, unit push and read). Thousands of cycles per frame.

| Stage | Pocket [QUOTED] | Review's model | This model, faithful stub: cacheless / with cache | Pocket over model: cacheless / with cache | Review-shaped stub: with cache, ratio |
|---|---|---|---|---|---|
| h | 70 | 58 | 58.9 / 66.3 | 1.19 / 1.06 | 66.3, 1.06 |
| d | 70 | 62 | 54.3 / 59.9 | 1.29 / 1.17 | 59.7, 1.17 |
| i | 122 | 97 | 97.5 / 112.8 | 1.25 / 1.08 | 113.4, 1.08 |
| s | 209 | 139 | 131.8 / 157.3 | 1.59 / 1.33 | 170.8, 1.22 |
| h+d+i+s | 471 | 356 | 342.5 / 396.3 | **1.38 / 1.19** | 410.2, **1.15** |

The review's `d` of 62 k is not recovered by grouping (this model's `DequantBlock` is 47.2 k, equal to the review's own function figure; the group is 54.3 k). **Budget rule [INFERRED]: model cycles x 1.35 cacheless, x 1.15 to 1.2 with the cache model, plus or minus 10 %**, the larger value being the faithful one and the difference in `s` being the unmodelled MMIO latency. With the cache model on, the per-stage ratios are within 1.06 to 1.17 except `s`; the cacheless ratios of 1.19 to 1.59 are not a constant, which is why the cache model matters.

## 6. Layout sensitivity and the alias checker [MEASURED-MODEL]

**Same code, 13 link orders** (the Helix source files shuffled with seeds 1 to 12, plus the default order; Battle, 1,000 frames, faithful stub): instructions per frame 282,769 in every order (so the layout changes nothing but the cache), **I$ misses per frame 1,450 to 2,411 (median 2,131), D$ misses 2,113 to 2,475, cycles per frame 384.2 k to 396.1 k (a 3 % band)**. The review's 1,449 is the minimum of this range (seed 1: 1,450) [INFERRED: its layout was a lucky one]. The shipped firmware's layout is a fourteenth value this tool cannot measure (the firmware does not run in the simulator); `alias_check.py --prof-elf` projects the harness profile onto the shipped `fw2.elf` (built to scratch with `TAU_BUILD_OUT`): 623 lines of decoder code executed at least 1,000 times against 128 sets, 128 conflict sets, **alias exposure 31.4 % of fetches in the shipped layout against 26.7 % in the harness layout** (an upper bound: the share of fetches on a line that shares its set with a hotter one; the decoder's hot code is 5 times the I$, so most of this is capacity, not alias). Largest pairs in both layouts: FDCT32 with IMDCT, Huffman with IMDCT, Huffman with FDCT32. **Read it as: the shipped layout is probably no better than the middle of the 13; the I$ is a 3 % lever on the frame, and the lever for reducing it is the code's size, not its placement.**

**`alias_check.py <elf> [--prof prof.txt [--prof-elf other.elf]]`** reads the ELF symbol table (the link map) and a per-PC profile: it lists conflict sets (two or more hot lines on one set), the function pairs involved, and the exposure. Static mode (symbols only) lists functions that share sets. On the synthetic pair of section 7 it flags the aliased layout (exposure above 30 %, pair f1/f2, 32 shared sets) and passes the disjoint one (below 5 %, only the driver's own lines).

## 7. The layout A/B and the I$ alias pair [MEASURED-MODEL]

`layout_ab.c`, run by `layout_ab.py`: the IMDCT to DCT32 handoff of one channel (18 slots x 32 subbands, two channels in turn, 200 granules), D$ misses per granule-channel, miss penalty 12. Stores never allocate, so only the DCT32 loads differ between layouts. **Scratch** is 24 lines read once per slot (the decoder's per-slot stack arrays and window words), placed on other sets than the output array ("disjoint") or on sets that hold 24 of its lines ("aliasing").

| Layout | Scratch | Instr | Cycles | D$ misses | Loads | Stores |
|---|---|---|---|---|---|---|
| column-strided `[sb][slot]` (stride 72 B) | none | 10,201 | 15,955 | 190.1 | 2,304 | 1,170 |
| column-strided | disjoint | 11,959 | 18,712 | 204.1 | 2,736 | 1,173 |
| column-strided | **aliasing** | 11,959 | **23,068** | **567.1** | 2,736 | 1,173 |
| `[slot][subband]` | none | 10,285 | 16,039 | 190.1 | 2,304 | 1,170 |
| `[slot][subband]` | disjoint | 12,043 | 18,796 | 204.1 | 2,736 | 1,173 |
| `[slot][subband]` | **aliasing** | 12,043 | **19,180** | **236.1** | 2,736 | 1,173 |

**Result for the plan's open item 5 (the layout choice before P3 commits): `[slot][subband]` is neutral when nothing aliases (identical misses, +0.8 % instructions) and robust when something does (236 against 567 misses, 19.2 k against 23.1 k cycles per granule-channel).** Mechanism [INFERRED from the table]: the column read reuses each output line about 8 times over 8 slots, so every conflict on such a line is paid again on each reuse; the sequential read touches a line once. Scaled to a frame (four granule-channels, aliasing against disjoint scratch): +17.4 k cycles for the column layout against +1.5 k for the contiguous one in this synthetic worst case, 5.3 % against 0.5 % of a 330 k-cycle frame. This is a synthetic loop with a chosen aliasing scratch, not a decoder measurement; it supports the plan's choice and does not size the effect in the real decoder.

I$: two 1,024-byte straight-line functions called alternately 4,000 times: on disjoint sets 16,071 misses and 2.31 M cycles, **on the same sets 268,009 misses and 5.33 M cycles (16.7x the misses, 2.3x the cycles)**, with identical instruction counts.

## 8. Tests (`sim/test_mp3_v2_profiler.py`, in `make test-host`, about 21 s with the library)

39 checks. (1) The cache model on hand-built RV32 programs with known miss counts: straight-line code, a loop that fits, sequential loads twice, a store to a cold line (no write-allocate), a store to a present line, two addresses 4 KiB apart (20 of 20 miss) and 32 B apart (2 of 20), additive penalties (`cycles(on) - cycles(off) = 7 x I$ + 11 x D$`), instruction counts independent of the cache option, an 8 KiB D$ option. **Red:** four mutants of `rvprof.c` (write-allocate stores, 64-byte I$ lines, no D$ penalty, a two-set D$ index) are each caught by at least one check; the real model fails none. (2) The A/B and I$ alias numbers as assertions. (3) The alias checker on the aliased and disjoint pair, dynamic and static. (4) Player 2's configuration: the unit API linked, the software window not in the image, the stub's registers present; the profile has seven columns; `patterns.py` reads it; calibration groups sum to the cacheless total; link order changes I$ misses and not instructions. (5) The five rows of the review within 2 % on instructions, cacheless cycles and stores, and the faithful-versus-review stub delta (skipped when the owner's library is absent; sections 1 to 4 and 6 to 7 run on generated streams and synthetic loops without it).

## 9. Not done, limits

- The review's stub source is not preserved: the review-shaped stub is a calibrated reconstruction (constant deltas), documented as such; its miss counts differ from the review's.
- The MMIO latency of the unit's registers and the unit's compute time are not modelled (the stub registers are RAM).
- Pocket figures are the review's quotation of B-591, not new. Nothing here ran on a Pocket.
- The layout of the shipped firmware's I$ misses is projected as an exposure, not measured (the firmware does not run in the simulator).
- Cache parameters: sizes confirmed in the Verilog (U13); the write policy and the 10 to 15 cycle miss are the review's and the project's documents'.
- The `--prof-elf` projection assumes the same sources and flags in both ELFs.
- One file per row at 1,000 to 3,000 frames; the review's own check on the old build showed 150 frames stable to 0.03 % for 02; content-dependent stages (Huffman, IMDCT) vary by file.

## 10. Reproduce

```
python3 tools/mp3_v2_measure/measure.py <scratch> 1000 "<file.mp3>"                  # Player 2 build, cache model on, faithful stub
python3 tools/mp3_v2_measure/measure.py <scratch> 0 --review                         # the review's five rows (owner's library)
python3 tools/mp3_v2_measure/measure.py <scratch> 1000 --software-window "<file>"    # the old configuration
python3 tools/mp3_v2_measure/layout_ab.py <scratch>                                  # layout A/B and I$ alias tables
python3 tools/mp3_v2_measure/alias_check.py <elf> --prof <scratch>/prof.txt          # alias report
```

The host `cc` needs `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk` (measure.py uses it when present).
