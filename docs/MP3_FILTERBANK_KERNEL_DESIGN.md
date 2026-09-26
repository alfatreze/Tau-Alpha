# MP3 synthesis filterbank kernel: design, resource fit, and lighter variants

Status: **DESIGN + resource estimate (2026-09-26). Nothing built.** Answers: does it fit with at least one M10K free, and can a lighter
part-implementation do most of the good? Measured inputs are from `docs/ARCHITECTURE_ROADMAP.md` section 2 (B-087/B-098: for MP3 the "Subband" stage
is 41-59% of decode time, Huffman 2-5%, IMDCT 9-13%; MP3 decodes at about 70-77% of real time on the CPU). Estimates are marked [EST]; the V symmetry
numbers were checked numerically for this note.

## 1. What "Subband" is (Helix `Subband()`, `third_party/libhelix-mp3/real/subband.c`)
Per granule, per 32-sample slot (18 per granule) and per channel: `FDCT32` (a fast 32-point DCT of the 32 subband samples, writes the V vector into `vbuf`) then
`PolyphaseStereo/Mono` (the 512-tap window: 32 PCM samples, each a 16-tap sum of 64-bit multiply-accumulates over V and the 264-entry `polyCoef` table).
Per slot-channel: FDCT32 is about 80 multiplies and 200 adds [EST ~1.2 k instructions]; the window is **512 MADD64** [EST ~5 k instructions on RV32IM, each
MADD64 is a mul, a mulh and a carry]. So **the window is about 80% of Subband, FDCT32 about 15-20%** [EST]. Per MP3 frame (2 granules x 2 channels x 18 slots =
72 slot-channels): window about 23% of a 26 ms frame, FDCT about 5.5% [EST], matching the measured Subband share (29-45% of real time).

## 2. What a hardware window unit needs
* **A MAC:** 32-bit x 32-bit signed into a 64-bit accumulator, then the same rounding/shift/clip Helix does (`SAR64(sum, 32-CSHIFT)`, `ClipToShort`).
  Throughput needed is tiny: 36,864 MACs per 26 ms frame; one MAC every 4 clocks is 2.5 ms of hardware time per frame (10%), so it is **1 to 2 DSP blocks**
  (of 55 free), sequenced, no wide combinational chain (the project's timing rule).
* **V history on chip.** The window reads V at 16 slots of history. Helix's FDCT32 emits only **32 unique words** per slot (host-checked, section 5), and the window reaches back 16 slots. So each channel needs **512 x 32 bit = 16 kbit**.
  Helix's own `vbuf` is 2 x 1088 words (double-sized so it needs no modulo indexing) and stays in CPU RAM as the software path.
* **The window coefficients:** `polyCoef`, 264 x 32 bit = 8.4 kbit, a ROM (MLAB or logic, or one M10K).
* **Interface (MMIO, the new 0x100+ space from B-287):** the CPU keeps running `FDCT32` unchanged except that its 32 outputs also go to a `V_PUSH` register (32 writes
  per slot); the unit updates its history and produces 32 PCM samples, readable as 16 words (two 16-bit samples each) or pushed straight into the PCM path.
  MMIO cost per slot-channel [EST]: 32 writes + 16 reads, about 400 cycles, against about 5,000 instructions saved.

## 3. Does it fit? (M10K is the scarce resource)
Now: 299 of 308 M10K used; the wave block (B-283, in fit) makes it 300, so **8 free**. DSP 11 of 66. ALMs 6.5 k of 18.5 k.

| Variant | M10K | DSP | ALM [EST] | Free M10K after (wave block in) | Verdict |
|---|---|---|---|---|---|
| **V-full**: the whole 1024-word V per channel, stereo | 8 (+1 ROM) | 1-2 | 0.5 k | 0 to -1 | does not fit with a block to spare |
| **V-unique** (recommended): 512 words per channel, stereo, ROM in MLAB/logic | **4** | 1-2 | 0.6-1.0 k | **4** | **fits, 4 free** |
| V-unique, ROM in an M10K | 5 | 1-2 | 0.4 k | 3 | fits, 3 free |
| V-unique, V in MLAB (LUT RAM) | 0 | 1-2 | 2-3 k | 8 | fits, no M10K, but a 512:1 read mux per bit and about 100 MLABs: timing risk |
| V in CPU RAM through a DMA read port | 0 | 1-2 | 0.6 k | 8 | second read port on the 4-lane main RAM; inference and arbitration risk (the B-223..B-228 history); not recommended |

M10K arithmetic: 512 x 32 per channel maps to 512x20 blocks, 2 per channel, so 4 for stereo. 512 x 32 for two channels sharing one 1024 x 32 memory is also 4.
So **yes, it fits with at least one block free; the recommended variant leaves 3 to 4.** Total after it: about 304 of 308.

## 4. Lighter variants: what each buys
CPU savings are of a whole 26 ms frame [EST from the op counts above; verify with a profile after any build].

| Variant | Hardware | Bit-exact vs Helix | CPU saved | M10K | Note |
|---|---|---|---|---|---|
| **Window MAC only** (the design above) | V history + MAC + coef ROM | yes, if the integer FDCT outputs keep the exact +/- relations (to be proven on the host first) | about 17-19% (Subband 29% to about 11%) | 4 | best value per block |
| FDCT32 only | 32-word working set, a few multiplies | yes (it is Helix's own fast algorithm) | about 4-5% | 0-1 | small win, more design (a butterfly network) |
| Half the window (only some output samples) | same | yes | proportional, but needs both V halves anyway | 4 | no memory saving, so pointless |
| Reduced-precision V (24-bit) | 3 blocks instead of 4 | **no** | same | 3 | breaks bit-exactness; not recommended |
| IMDCT (9-13% of decode) | its own overlap buffers | yes | about 6-9% | about 2-3 | a later, separate kernel |
| Huffman/bit reader for MP3 | barrel shifter and tables | yes | 1-3% | 1-2 | not worth it (smallest stage) |
| Full FDCT + window + IMDCT | all of the above | yes | about 25-30% | about 7-8 | the "hardware MP3" end state; needs the 192 KB RAM shrink first |

## 5. Host symmetry check: DONE (B-291), result better than assumed
`sim/mp3_poly_probe.c` + `sim/test_mp3_poly_probe.py` (in `make test-host`) run Helix's REAL `FDCT32` and `PolyphaseStereo` (only the platform macros are replaced by a
bit-identical portable shim) for 200 slots of random stereo data, find every `vbuf` position the window actually reads (perturb each position, see whether the PCM
changes), and match each to the FDCT32 word that wrote it. Results:
* **No sign or rounding assumption is needed.** Helix's `FDCT32` already emits only **32 unique words per call** (it writes each twice, at `d[0]` and `d[8]`, for its
  8-phase indexing). The unit stores those words exactly as Helix computes them, so it is bit-exact by construction. The earlier worry (exact +/- relations on mirrored V
  positions) does not apply to this design.
* **0 violations:** every position the window reads holds a word from one of the last 16 slots of the same channel; the oldest age read is 15.
* **Per call** the window reads only **263 distinct words per channel** (about 16-17 per age), but **over its life every one of a slot's 32 words is read at some age**,
  so the store must hold all 32: **16 slots x 32 words = 512 words per channel**, as planned. (263 is the per-call working set; it would matter only for a per-call cache.)
* Storage stays **4 M10K for stereo** (512 x 32 bit per channel = two 512x20 blocks each), or 2 if a channel could use the 256x40 mode, which 512 words cannot.

## 5b. Remaining risks
* **Timing:** a 32x32 MAC into 64 bits maps to about 2-4 DSP partial products; use a multi-cycle sequenced MAC with registered stages (B-111/B-114/B-231 rule), never a one-cycle 64-bit accumulate chain.
* **Audio safety:** a stall or a missed slot must never glitch playback: the software path stays selectable (`hw_poly` probe, same fail-safe style as `BLIT_READY()`), and a timeout falls back per slot.
* **Interface cost:** the 32 FDCT words per slot-channel must reach the unit (MMIO writes, or the FDCT32 output stage redirected); measured against the estimate only after a build.
* **FLAC** is unaffected (it does not use this path).

## 6. Build order and status
1. ~~Host symmetry check~~ **done (section 5): the gate passed.**
2. ~~Golden model~~ **done (B-292):** `sim/mp3_poly_model.c` equals Helix's real `PolyphaseStereo` on 170 slots (quiet, normal, loud, full scale, impulse, silence, alternating extremes; thousands of clipped samples). ROM tables are generated from Helix (`tools/gen_mp3_poly_rom.py`), with a drift check.
3. ~~RTL~~ **done, not fitted (B-292):** `src/fpga/core/tau_mp3_poly.sv` (+ generated `tau_mp3_poly_rom.svh`), `sim/tb_tau_mp3_poly.v` replays the model's vectors bit-exactly (170 slots, 5,440 PCM words), four mutants killed (no rounding, +c2 for -c2, age off by one, no clip). Wired into `mp3_soc.v` behind `TAU_POLY` (registers 0x100-0x110, `docs/MMIO_ALLOCATION.md`). **4,691 clocks per stereo slot** (78 us; 36 slots per MP3 frame = 2.8 ms, about 11 percent of hardware time).
4. Firmware (next): `hw_poly` probe, redirect FDCT32's 32 unique output writes (skip the duplicate sample-16 write) to `POLY_PUSH`, read the 32 PCM words back, per-slot software fallback, a Check test comparing hardware and software PCM. Behind a macro, default off. **Not started.**
5. Fit (two seeds; bundle `tools/blit_g3_poly_qsf_append.txt` = wave block + wider MMIO + `TAU_POLY=1`), then a hardware soak and the per-stage profile to measure the real CPU saving. **Not started.** Expected: about 4 M10K for the `ring` array (to be confirmed by synthesis: a plain 1024 x 32 array with one write and one read port), 1-2 DSP blocks for the 32x32 multiply, coefficient and tap ROMs in logic.
Stereo only: a mono stream stays on the software path.
