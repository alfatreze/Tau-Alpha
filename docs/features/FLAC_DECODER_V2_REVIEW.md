# FLAC decoder for Player 2: audit of spec 8.2, revised spec, and reuse feasibility

Date 2026-10-16. Audit of `docs/features/PLAYER_V2_SPEC.md` section 8.2 (and the places that lean on it: 7.3, 9, 11 governor text, 15, 16 D3). Evidence labels: [HW] measured on a Pocket, [READ] read in source today, [HOST] host-proven, [EST] estimate, [EXT] external fact fetched today, [MEM] from memory, to verify. Nothing here is built or installed.

## 1. Verdict in five lines

1. **Do the new decoder, but not for CPU.** The spec's CPU gate is stale: it was written against the pre-C1 decoder (hard file idle 8%). The inherited decoder with the Rice fast path already measures **45% idle** on the hard 48 kHz file [HW, B-562]. The gate "idle at least 20%" is met by the status quo. The real reasons to replace it are: bounded steps, survivable errors, exact seek, sample accounting, and provenance.
2. **Four defects in the spec text** (section 3): partition-sized yielding does not work for partition order 0; the data-availability contract is per frame and breaks above about 19.5 KB; a frame error ends the track; "MD5 parity" is wrong for 24-bit.
3. **Eight things the spec does not say** (section 4): frame CRC-16, sync validation, variable block size, unknown total samples, ID3v2 before `fLaC`, mono, error concealment policy, 24 to 16 requantisation.
4. **Reuse is not worth it as product code.** One candidate is LGPL (out), one is licence-clean but is a whole-frame, malloc, 64-bit design with no hooks for the LPC unit or step budgets. Use them as **conformance and speed oracles** on the host, not as the decoder (section 7).
5. **Do a measurement stage first (S8-0)**, because nobody knows today where the cycles of the C1 decoder go. Several design choices (burst LPC, bit-reader work) depend on the answer.

## 2. What the numbers actually say today

| Fact | Value | Source |
|---|---|---|
| Hard file, 48 kHz stereo, 1.00x, old decoder | 0% idle, 12 FIFO stalls / 30 s | [HW] B-562 (DEV 81) |
| Same file with the Rice fast path (C1) | **45% idle, 0 stalls** | [HW] B-562 (DEV 80) |
| Decode cost split before C1 | channel 1 about 2x channel 0 (63 to 66% vs 33 to 36%); LPC reconstruction 89 to 96% of channel 0; bit reader 4 to 11% | [HW] B-360/B-363 |
| Hardware LPC | about 2x lower worst-case spike | [HW] B-386 |
| Spec governor model: 48.2 pairs/ms of CPU against 44.1 needed, UI gets 3.5% | pre-C1 numbers | spec 11 |
| Card read | about 736 KB/s | [HW] |

Consequence: with C1, decode makes about 48 / 0.55 = **87 pairs per ms of CPU** on that file (upper bound: idle includes UI and I/O), nearly 2x real time, not 1.09x. The governor simulation in the spec must be rerun with C1 numbers before it is quoted as a reason; the "UI receives 3.5% on hard FLAC" conclusion belongs to a decoder that no longer ships. The unknown that matters: **after C1 and the LPC unit, what is the split between Rice/bit reader, the per-sample MMIO handshake to the LPC unit, decorrelation plus `to16`, and the sink copy?** Nobody has measured it (B-363 predates C1). S8-0 measures it.

## 3. Defects in the current spec text

**D1. "Yields after each residual partition group (about 512 samples)" is wrong as a mechanism.** The Rice partition order is chosen by the encoder; order 0 means one partition of the whole block (4,096 samples). Yield on a **sample count** (every N residuals, N chosen so a step is at most the cycle budget), with the Rice parameter and the remaining count in the saved state. Partition boundaries are irrelevant to the decoder state because the only per-partition state is `k` (and the escape width).

**D2. The data-availability contract is per frame, and it is not safe.** Today: `FLAC_NEED = 19,500` bytes must be buffered before a frame starts [READ, `mp3play.c`]; the comment says a worst-case 4608 x 2 x 16-bit frame is 18.5 KB. But a 24-bit stereo frame of 4,096 samples can be **24.6 KB** (verbatim or noise-like), larger than the 24 KB ring and the gate. Then `stream_avail < FLAC_NEED` never becomes false, or the decode runs and `flac_pull` goes into the **blocking** `io_service()` loop (up to 6 s) [READ]. The spec's own rule "decode never waits on the card" is violated for exactly the hi-res files the 48 kHz cap still admits. Fix: each `step` declares `need_bytes` for the unit of work it is about to do (a conservative bound computed from the frame header: for a sample-count slice, `slice x max_bits_per_residual / 8 + slack`), the transport guarantees them or answers `STEP_NEED_DATA` without side effects. A frame may then be larger than the ring.

**D3. One bad frame ends the track.** [READ, `mp3play.c`: any `fe != FLAC_OK` calls `track_ended()` and counts one error.] An SD read glitch or a single flipped bit therefore silences the rest of the album. Define the policy in the spec (section 5.6).

**D4. "libFLAC's MD5 for each" is only valid for 16-bit sources.** For 24-bit files the player outputs `to16` (rounded), whose MD5 differs from libFLAC's. The oracle must be: libFLAC (or dr_flac, section 7) decoded to 32-bit, then the same `to16` on the host, compared sample for sample; the STREAMINFO MD5 additionally proves the full-precision decode before `to16`.

**D5. "Bit reader aligned to the LPC unit's feed" is not a requirement.** It cannot be tested. Replace with the concrete design in 5.4 (software-pipelined issue/collect) and a measured gate.

**D6. The ch0-in-PSRAM alternative is moot.** The 20 KB channel-0 buffer lives in the Helix arena, which is statically sized for MP3 anyway [READ]. Moving it to PSRAM (26 to 32 cycles a word, about 3 to 4 ms of a 85 ms hard frame) frees RAM nobody can reuse. Keep it on chip. (It would matter only if the arena were ever shrunk below Helix's need.)

**D7. The step granularity target (8 ms) has no derivation.** The longest atomic step already allowed elsewhere is the MP3 frame (about 12 ms). Pick the FLAC budget from the system, not from the number in the MP3 row: the budget is what the governor's lead minus the UI slice tolerates; it is a parameter (`budget_cycles`) tested at 0.5, 2, 8 and 12 ms, with output independent of the budget (section 6).

## 4. What the spec does not cover and must

1. **Frame CRC-16.** The old decoder checks the header CRC-8 only [READ: `flac_crc8`]. A body CRC-16 catches SD corruption that the old decoder plays as noise. Cost [EST]: table-driven, about 10 to 14 cycles a byte; at 900 kbps that is about 110 KB/s, about 1.5 to 2% CPU. Make it a setting of the decoder core (`FLAC_CRC16 = 0/1`), decide by the S8-0 measurement; use it unconditionally in **seek probing** where false syncs are the main hazard.
2. **Sync validity.** A `0xFFF8` pattern occurs inside audio. Accept a frame only if: header CRC-8 passes, sample rate/channels/bps agree with STREAMINFO or are "from STREAMINFO", block size is within `[min,max]`, and (for seek probing and error resync) the **next** header sits where this frame ends or the CRC-16 passes. This is the same lesson as MP3 seeking [handoff section 4].
3. **Variable block size.** `blocking strategy = 1` carries a sample number, not a frame number. The source position (`src_pos`, seek, time) uses the sample number directly; with fixed block size it is `frame_number x blocksize`. Both must be in the host corpus (the Xiph and IETF suites contain them).
4. **Unknown total samples.** `total_samples == 0` is legal (streamed encodes). P3's expectation is then unknown: report `put` against "unknown" as a flag, not a defect; the timeline falls back to a size estimate and is corrected at end of file.
5. **Container prefixes.** An ID3v2 tag before `fLaC` exists in the wild. The open must skip a well-formed ID3v2 (size from the syncsafe header) and then require `fLaC`. [READ: `fw/flac.c` references ID3 only in comments; verify by test.]
6. **Mono.** Duplicate to both channels in the sink (or let the ring take mono with a flag); the old player refuses `channels > 2` only. Specify and test.
7. **Mid-stream parameter change** (rate, bps or channel assignment differs from STREAMINFO): treat as end of segment with an error flag; never reconfigure silently.
8. **24 to 16 requantisation.** `to16` rounds (B-619) but does not dither (D-G03). This is the only point where a full-precision signal is requantised; the later hardware gain stage works on 16-bit input. Keep the decoder default as is, but give `to16` one hook (an optional TPDF dither source in the decode core, off by default, about 8 cycles a sample [EST]) so the recording gate D-G01 can decide later without touching the decoder again.

## 5. Revised specification

### 5.1 Shape: a resumable state machine in a host-proven core

* `fw2/cores/flac2_core.h` (header-only, no I/O, no malloc, no globals): the whole decoder as an explicit state machine, the same discipline as `wsola_core.h` / `tempo_core.h` / `governor_core.h`. `fw2/decode/flac2.c` is only glue (stream feed, LPC unit calls, `pcm_blk_t` output).
* **Independent twin:** `tools/flac_ref.py` (exists, bit-exact reference) extended to produce the same intermediate values; the golden test compares per frame: header fields, residuals (hash), reconstructed channel 0, final pairs. Written from RFC 9639 by section, not from `fw/flac.c`.
* **State** (all in one struct, size stated and asserted): input cursor and bit reservoir, header fields, current subframe (type, order, wasted, shift, coefficients up to 32 x int16, warm-up), Rice state (`k`, remaining count, escape width), output cursor, channel phase (ch0 buffer, ch1 stream), LPC unit ownership flag, error code.

### 5.2 Unit of work and budget

`step(budget_cycles)` runs until one of: budget spent, a block of output pairs is ready (at most 64 or 128, no larger than the sink buffer), `NEED_DATA`, end of frame, error. Granularity inside a frame:

| Phase | Yield points |
|---|---|
| frame header, subframe header, coefficients | atomic (at most about 40 us) |
| channel 0 residuals | every N residuals (N from the budget, not from partitions) |
| channel 1 streaming (decode + decorrelate + `to16` + emit) | after every emitted block of pairs |
| LPC unit | state lives in the unit and survives yields; the core records "unit owned by this subframe" so a stale unit state is never reused |

**Property that must hold and is tested:** output is byte-identical for any sequence of budgets from 1 cycle to infinity (random budgets, including 1-cycle steps that always yield at the first point). This is the key test for a resumable decoder; mutants: forget to save `k`, forget the escape width, reuse a stale LPC history after a yield, double-emit a block.

### 5.3 Data contract (fixes D2)

`need_bytes(step)` is part of the core's API (pure function of the state): the worst-case bytes the next unit can consume. Transport answers it from `stream_avail`; a short answer is `NEED_DATA` with no state change. Frames bigger than the ring are supported. `flac_pull`-style blocking reads do not exist in the decode path; the open (metadata walk) remains the only place that waits, as a state machine step in its own right (so the UI can show LOADING).

### 5.4 Hardware use, concretely

* **LPC unit** per sample today is: write residual, spin on status (the unit needs about `order + 4` cycles), read sample [READ, `lpc_hw.c`]. The CPU spins for the unit's latency. **Software pipelining, no RTL:** Rice residuals do not depend on reconstruction, so issue residual *i*, decode residual *i+1* (the Rice step), then collect sample *i*. The unit's latency overlaps useful work. Gate: adopt only if S8-0 shows the spin is at least 8% of the decode time [EST: typical order 8 to 12 gives 12 to 16 cycles a sample].
* **Burst LPC (RTL, only if S8-0 says MMIO handshakes dominate):** a small residual FIFO (write N residuals, read N samples) so a channel-0 subframe is one burst. That is a new fit (about 2 h) and a new MMIO range; not part of S8, an option behind a measured gate.
* No hardware Rice decoder, no hardware CRC: the earlier scoping showed the bit reader at 4 to 11% before C1, and C1 cut it further.
* Failure policy unchanged and kept: any anomaly disables the unit for the session, the software loop continues from the exact sample (the existing fault-injection test is carried over as a golden case).

### 5.5 Memory

Arena use stated per track in `arena_bytes(track)`: channel-0 buffer `blocksize x 4` (20,480 B at 5,120), state struct (target under 600 B), no other buffers; sink buffer 128 pairs (512 B) on the stack or in state. 5 KB stack is respected: the core's deepest call chain is measured on the host with a stack-depth tool and asserted under 1 KB.

### 5.6 Errors, resync, accounting (fixes D3)

* On header error, CRC error or desync inside a frame: if the header was good, **emit `blocksize` silent pairs** (so `put` stays equal to the timeline and gapless accounting remains meaningful), count `frame_errors`, and resync by scanning for the next valid frame (5.7). If the header was bad, scan from one byte after the failed sync; the silent fill uses the previous block size and only up to the next valid frame's sample number (variable block size gives it exactly; fixed block size gives it by frame number).
* Give up (end the segment with an error) only after N consecutive failures (suggest 8, a constant) or end of file.
* All counters appear in the START report: `frame_errors`, `resyncs`, `silent_pairs`, `crc_checked`.
* P3 expectation: `pairs_total` from STREAMINFO when non-zero; the segment closes with `put == total` or a flagged reason.

### 5.7 Seek

* **SEEKTABLE** when present: pick the last point at or before the target; but table entries can be wrong or placeholder (`0xFFFFFFFFFFFFFFFF`); validate by decoding the header at the offset (CRC-8, parameters).
* **Without a table:** interpolate the byte offset from the sample target using the average bytes per sample, then a bounded **bisection** on frame headers, each probe validated per 4.2; the old file's own comment notes that interpolating blindly "made the transport unusable" [READ]. Finish by decoding and discarding to the exact pair.
* Seek is itself a state machine (`SEEK_TABLE`, `SEEK_PROBE`, `SEEK_DISCARD`) that returns `NEED_DATA` or asks the stream layer for a read at a new offset; it never blocks. A seek is cancellable by a newer seek (generation counter, as in the stream layer).
* Cost bound: discarding up to one frame is up to about 85 ms of CPU on the hard file; do it in budgeted steps with the output gated off.

### 5.8 Refusals and kept behaviours (unchanged)

48 kHz cap, more than 2 channels, blocks over 5,120 (reported with the "NO PLAY:" wording), 8 to 24 bit, ReplayGain from Vorbis comments, `skip` over PICTURE and other large blocks, `to16` rounding. Add: the open must not read a PICTURE block at all, only skip it.

## 6. Verification plan (replaces spec section 15 for FLAC)

1. **Conformance corpus:** the IETF CELLAR `flac-test-files` suite (the RFC 9639 test files: subset, uncommon features, and **faulty** files) [MEM: public repository on GitHub under ietf-wg-cellar; verify licence before copying anything into the repo; keep it outside the tree and fetch in CI/host scripts]. Plus the 8 real files, the synthetic vectors (`tools/flac_make_test.py`), STREAMINFO MD5 and libFLAC. Expectation per file: exact or a defined refusal.
2. **Budget independence** (5.2) with random and adversarial budget sequences.
3. **Chunked input independence:** same output when the stream hands over 1 byte at a time, 3 bytes, random sizes (the `NEED_DATA` paths).
4. **Fuzz and fault injection, host:** bit flips, truncation, every field at its extreme, block size over the buffer, order over block size, partition count that makes a partition shorter than the predictor order, Rice escape 31 with width 0..32, shift out of range, 24-bit with wasted bits, `wasted` equal to `bps`. Requirement: no out-of-bounds access (build with the address sanitiser on the host core), no hang (step budget counter), and a defined error. Property: after any corruption the decoder resynchronises on the next intact frame.
5. **Differential against `fw/flac.c`** (kept as reference until acceptance): identical pairs on every intact file; documented differences only on faulty files.
6. **Cycle model:** `rv32sim` instruction counts per output second on the 8 files against the C1 baseline (the numbers S8-0 produces).
7. **Mutation set:** at least one mutant per fixed bug (stale LPC history, lost `k`, wrong wasted-bit shift in streaming, M/S odd-bit rule, `to16` rounding, resync accepting a false sync).
8. **Hardware:** START report rows (frame_errors 0, resyncs 0, `WAITS` flat, `MP DEC AVG`), HEADROOM, plus a deliberately corrupted file on the card (one flipped bit, one truncated frame) that must play on with one silent block; a FLAC gapless pair on the EVO 4 as in the spec.

## 7. Reuse feasibility

### 7.1 Candidates

| Candidate | Licence | Shape | Facts |
|---|---|---|---|
| **dr_flac** (single header, `dr_libs`) | Public domain / MIT-0 [EXT, end of `dr_flac.h`] | Pull API with read/seek/tell callbacks, `drflac_read_pcm_frames_*`; 12,760 lines [EXT] | Has `DR_FLAC_NO_CRC`, `NO_SIMD`, `NO_STDIO`, `NO_OGG`; allocation callbacks; seek table support; whole-frame granularity; allocates its frame buffer(s) for all channels; no hooks for an external LPC unit [MEM for allocation details] |
| **libfoxenflac** | **LGPL-2.1** [EXT, fetched today] | Push-style state machine, no malloc, tiny; about 40 KiB RAM for the Subset (stereo, up to 48 kHz) [EXT]; author calls it unoptimised, "significantly worse" on ARM6 [EXT]; no seeking, no metadata beyond STREAMINFO [EXT] | The shape is right (resumable) but the licence rules it out (Tau is MIT, D-P01; a statically linked LGPL blob in a firmware image brings relinking duties); and no seek |
| **libFLAC** | BSD-3 | Reference decoder; malloc, 64-bit paths, large | Use only as the host oracle |
| Nayuki "simple FLAC decoder" | MIT [MEM] | Teaching code, whole frames, slow | Good as an independent second reading of the RFC, not a product |
| Rockbox / ffmpeg FLAC | GPL / LGPL | | Out (D-P01) |

### 7.2 Benefit test (what reuse would have to buy)

| Needed by Player 2 | dr_flac | foxen | Writing it (5.x) |
|---|---|---|---|
| Licence compatible with MIT product | yes | **no** | yes |
| Resumable steps bounded by a cycle budget | no (whole frame; could be patched in, which makes it a fork) | partial (by input availability, not CPU) | yes by design |
| Frame larger than the ring, no blocking | no (pull callback blocks) | yes | yes |
| Hardware LPC unit with fallback | patch needed | patch needed | yes |
| Streaming channel 1 over the channel-0 buffer (RAM) | no: two full channel buffers (about 40 KB at 5,120) [MEM] | no (about 40 KiB) [EXT] | yes (20 KB) |
| Exact seek with validated sync, non-blocking | has seek, blocking, unvalidated for this stream layer | none | designed (5.7) |
| Error concealment with accounting | no | no | designed (5.6) |
| Tuned for rv32im (no 64-bit multiply, no barrel assumptions) | generic; fast paths for 32-bit predictors exist [MEM] | unoptimised | C1 Rice path and LPC unit already exist |
| Conformance already proven by a large user base | **yes** | some | must be earned (corpus + fuzz) |
| Provenance (no HarpMudd application source) | yes (third party, attributed) | yes | yes |

Reading: reuse wins on two lines (field-proven conformance, provenance for free) and loses on seven, five of them structural (no budgeted steps, no ring-independent data contract, RAM, seek, hardware). Patching those in turns a third-party file into a fork that Tau must maintain and that no longer carries the "field-proven" argument. For provenance, the spec already accepts that attribution notes name HarpMudd's remaining work; a new decoder satisfies the intent as well.

### 7.3 Recommendation and an inexpensive spike that could change it

**Do not adopt as product code. Use dr_flac as a host oracle and speed yardstick.** Two reasons it earns its place without shipping: (a) a **third independent decoder** (libFLAC, dr_flac, ours) makes disagreements on faulty files visible; (b) it is the cheapest honest answer to "can we be much faster?".

Spike, about half a day, host only, to run inside S8-0:
1. Build dr_flac with `NO_STDIO NO_SIMD NO_OGG NO_CRC` (and a CRC variant) for rv32im with the repo's toolchain; wrapper reading from memory.
2. Run it and `fw/flac.c` (C1, `TAU_LPC_FW=0`) under `rv32sim` over the 8 files: instructions per output second, code size, peak heap, stack.
3. **Decision gate for reconsidering reuse:** dr_flac must be both at least **20% fewer instructions** than C1 on the hard file **and** at most +6 KB code and at most +20 KB RAM against Player 2's budget. Failing either, reuse is closed for good and the result goes into `docs/DECISIONS.md` (rejected options, so it is not re-litigated). If it passes the speed gate only, take **the idea** (what it does in its 32-bit fast predictor, where it is faster) into `flac2_core.h` and cite it in `docs/PROVENANCE.md`; do not copy.
Expectation [EST, low confidence]: dr_flac will not beat a Rice path tuned for this CPU with the hardware LPC unit; it will beat the old pre-C1 path.

## 8. Build order (replaces S8's single row)

| Stage | Content | Gate | Size |
|---|---|---|---|
| S8-0 | Measure the C1 decoder's cycle split (Rice / LPC handshake / decorrelate+`to16` / sink) on the host (`rv32sim`) and on a Pocket (DEV 149 FLAC rows); rerun the governor model with C1 numbers; the dr_flac spike (7.3) | numbers written into this file; decide CRC-16 default, pipelined LPC, burst-LPC | S |
| S8-1 | `flac2_core.h` + twin + corpus + budget/chunk independence + fuzz (host only) | all tests green, mutants killed, ASan clean | M |
| S8-2 | Glue: `flac2.c`, `need_bytes`, errors/resync, START rows; coexists with the old decoder behind a switch | host differential equal; DEV: hard file idle not worse than C1 minus 3 points | M |
| S8-3 | Seek (table + bisection) | seek corpus incl. variable block size, exact landing | M |
| S8-4 | Gapless FLAC hardware acceptance (tone pair on the EVO 4), corrupted-file test, remove `fw/flac.c` | spec gates | S |
| S8-opt | Pipelined LPC issue/collect (firmware); burst LPC (RTL fit) only if S8-0 demands | measured gain at least 8% of decode | S / L |

## 9. Decisions requested from the owner

1. Accept that S8's gate is **no regression against C1** (45% idle on the hard file) rather than "20% idle".
2. CRC-16 on by default (about 1.5 to 2% CPU [EST]) or only when probing: recommend on, unless S8-0 shows it costs the hard file more than 3 points.
3. Error policy: silent block and resync, give up after 8 (recommended), versus end the track.
4. Reuse spike: yes, as an oracle and yardstick, with the gate in 7.3 (recommended).
5. TPDF dither hook in `to16`: build the hook now, off by default, decide on by-ear/recording data later.

## 9b. Owner decisions (2026-10-16)
All five accepted as recommended: (1) gate = no regression against C1; (2) CRC-16 on by default (revisit if S8-0 shows more than 3 points on the hard file); (3) silent block + resync, give up after 8; (4) dr_flac spike as oracle/yardstick with the 7.3 gate; (5) off-by-default dither hook in `to16`.

## 10. Honest limits of this review

I did not run anything on a Pocket and did not profile the C1 decoder; every [EST] number is a pre-measurement guess and S8-0 exists to replace them. Facts about dr_flac internals marked [MEM] come from memory and must be confirmed in the spike. The IETF test suite licence and file list must be checked before it is used. I did not read all 1,030 lines of `fw/flac.c`; findings D2, D3 and the CRC-8-only observation come from the specific functions quoted above.
