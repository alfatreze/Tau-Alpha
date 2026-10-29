# SDRAM replay harness: findings, ambiguities and results (2026-10-28)

Harness: `sim/sdram_replay/` (README there). Simulation only: iverilog 4-state, `tau_sdram_phy` + `tau_sdram_sched_v` (main at `b8f9d319`, v0.8) +
`sim/sdram_chip_model.sv`. **Nothing here is a statement about a Pocket or about Quartus.** Written clean-room from the contract (v0.8, sections 4, 5, 7.3, 7.4) and
Tau V's `docs/TREQ_TAU_V_SPEC.md` (draft 1, SHA-256 `ab387add...ceb` as quoted by the contract); no legacy controller source or history was read.

Input files (git-ignored, SHA-256 checked before use, referenced and never copied):

| File | SHA-256 |
|---|---|
| `work/tauv_treq/tnv3_tears_q30_48pic.treq` | `d8dbb3f619f4d2c4898c877fd63686a535552fe4684f4ec13faaaa49d21990c1` |
| `work/tauv_treq/worst/allbi.treq` | `143efbc2cb062471a02d5be1068cfae4944d3c8b19fe76b0b70442a0f2988ebd` |
| `work/tauv_treq/worst/static.treq` | `756b91a16c16358baa761821d4edab0d108c0d7f9d70865ead76417e275058dd` |
| `work/tauv_treq/worst/scenechange.treq` | `7b00be9fe44b9f3b307d6dc8f8fb811ec17217fcb3ff0ed2d86bad2ed348e501` |
| `work/tauv_treq/worst/pan.treq` | `35ad636ec08e26a68c93031659057106f6526503a8f7933ef1d366efab5869b3` |
| `work/tauv_treq/worst/random.treq` | `4606ab4fb644bb48d58694eecf7193ab55c373f8b2a6425103bfa1852d3717ee` |
| `work/tauv_treq/tnv3_tears_q30_48pic.req` | `af00952b59aeb29cdb6e8692c4d64e6c65b102cd78d5d5cfa6c403bb2c9897ec` |
| `work/tauv_treq/scan_800x720_1frame.req` | `b74303f1571e0af5be4f4e2b839029d633af715c49c3465c8e870ae6e39ac964` |

Tau V's validator `work/tauv_treq/test_treq_tauv.py` over the six `.treq` files: all `ok` (the clip equals its `.req`), its 7 mutants refused, `PASSED`.

## Findings and ambiguities

**F1. The 90 % busy rule cannot pass in the timed mode on the five worst-case files, whatever the memory does.** The files release macroblock j of a picture at `pic_release + j / 67,500 s`,
and a Level 3 worst picture has 2,250 macroblocks at 30 fps: the last macroblock is released 33.319 ms after the picture's release, **99.96 % of the 33.333 ms period**. `PIC_END` is a full barrier,
so `reached(n) - max(release(n), reached(n-1))` is at least 99.96 % in the timed mode (checked on the file: span 33.318518 ms in every picture of `allbi`). The clip (24 fps, 1,050 macroblocks) has a parser span of 37 %.
The rule as written (spec section 3, contract 7.4) therefore measures the parser rate on the worst files. The harness reports it exactly as specified (timed mode) and in addition measures
**(a) flat out (`+norelease`)**: releases ignored, busy = the memory time the picture needs with scan-out, CPU and DMA running; this is the quantity the 10 % slack is meant for, and
**(b) lag** in the timed mode: `reached(n)` minus the last macroblock release, the delay the memory adds beyond the parser. Proposal for the owner / Tau V: apply the 90 % rule to (a), keep the deadline check in the timed mode, report (b).

**F2. Port numbering differs between the `.treq` spec and `sched_v`'s defaults.** The spec numbers 0 scan-out, 1 reference read, 2 reconstruction write, 3 CPU, 4 DMA. `sched_v`'s default
`WEIGHT` / `MAXG_*` vectors follow the contract text (decoder 4, engine 2, CPU 1, DMA 1) with the engine on port 3, i.e. CPU = 4 and DMA = 5. The harness follows the task statement
(CPU on 3, DMA on 4, `CPU_P` / `DMA_P` parameters), so the CPU gets weight 2 instead of 1 and the DMA weight 1; port 5 is idle. The effect on the numbers is small (single-word CPU reads, 64-word DMA) but it is a guess about the final port map.

**F3. Some `.treq` requests cross a 1,024-word page** (the spec says never): 4,932 in the clip, 2,188 in `allbi` (counted from the files). The scheduler splits them as the contract requires; no request is rejected.

**F4. 2D fusing.** Implemented as the contract words it: consecutive port-1 records of one macroblock, same word count, constant positive step of 1..2,047 words; greedy, at most 63 rows and 512 words; a run of one stays 1D.
A step is taken from the first pair, so a window's last row followed by the next window's first row can extend a run by accident (Tau V notes the same: "spurious pairs make the fusing slightly optimistic"). The caller's wording "a switch to run un-fused" was read as `+nofuse`. Result: see the tables.

**F5. Address placement of the generated ports is a harness choice** (regions 5, 6, 7 of the 25-bit word address: DMA ring, CPU area, scan-out frame; the decoder pictures use regions 0..3). The scheduler's bank hash (default `a[11:10] ^ a[21:20] ^ a[23:22]`) mixes the region bits,
so another placement changes which rows collide with the pictures. The scan-out pattern file starts at word 0 (the same words as picture slot 0 in `allbi`), so it is relocated; reads of the scan-out frame are not racing the decoder's writes this way.
The store is exact only when `a[21:19] = 0` (the pictures stay under 2^19 words per slot; the harness stops with `bad_address` otherwise). **`sim/sdram_chip_model.sv` got one new option, `MEM_COMPACT = 2` (with `MEM_LOG2 = 22`): an exact 22-bit store for rows with `row[9:7] = 0`; default behaviour unchanged.**

**F6. The scan-out model is an approximation:** a 1,200-word FIFO drained smoothly at 600 words per line (22.676 us), 720 active lines and 15 blank lines per frame, urgent below 600 words, requests issued when the free space covers the outstanding words plus the request.
A real line buffer drains in bursts; the model is smoother. Because requests are issued as soon as the FIFO has room, the FIFO usually stays above one line and `urgent` rarely rises, so the urgent bound is exercised only when the scheduler falls behind;
`+scan_urgent=W` raises the threshold for a stress run.

**F7. Unchecked reads.** The first pictures read reference pictures that were never written (the streams start mid-sequence or with synthetic references): such words are counted as `unchecked_uninit_words` and not compared (the chip returns X). Everything that was written by the stream is compared, and
shards fast-forward the earlier pictures' writes through the back door so that reads of them are checked too.

**F8. Timed mode costs simulation time even when idle:** the clip is 200 M cycles of simulated time (48 pictures of 4.17 M cycles) against about 30 M cycles of actual memory work; only the real DUT's roughly 5,000 cycles per second makes this expensive. The task's estimate of "about 25 M cycles" is the flat-out figure.

**F9. Reading of the record fields.** `PIC_START`'s release is a gate (the earliest start); `PIC_END` is reached when `done` of the last write and `r_last` of the last read have come; `FENCE_RD` waits for `r_last` of earlier port-1 reads (writes' `done` are not required). Busy of picture n uses the file's `PIC_START` release for `release(n)`.

**F10. Metrics definitions chosen where the contract is silent:** row hit rate = `1 - ACT / (RD + WR)` over command-bus column commands; commands per request = non-NOP commands over all requests (every port); burst-tail waste = chip-driven read words minus words delivered (0: the scheduler ends read bursts exactly);
urgent wait measured to the first `r_valid` word from `max(accept, rise of urgent, end of the port's previous request)`, compared with `URGENT_BOUND_CLK`.

## Results (real PHY + `sched_v` v0.8 defaults, chip model, all generated ports running)

All runs: 0 read mismatches, 0 chip/shadow differences at write `done`, `phy_fault` 0, chip-model errors 0, DQ conflicts 0, no rejected request (`bad_req` 0), scan-out underflow 0, offer-rule monitors 0.
Percentages are of the picture period (clip 24 fps = 4,166,666 cycles; worst files 30 fps = 3,333,333 cycles). Logs: `build/sdram_replay/full_*` (not committed).

**Flat out (`+norelease`), memory time per picture, every picture of every file:**

| File | Pictures | busy p50 | busy p99 | busy max | 90 % rule |
|---|---|---|---|---|---|
| clip (Tears, 24 fps) | 48 | 20.03 % (834,720 cycles) | 22.84 % | 22.84 % (951,599) | PASS |
| `allbi` | 23 | 65.26 % (2,175,457) | 66.41 % | 66.41 % (2,213,705) | PASS |
| `static` | 24 | 42.13 % | 42.56 % | 42.56 % | PASS |
| `scenechange` | 24 | 48.34 % | 48.84 % | 48.84 % | PASS |
| `pan` | 24 | 48.68 % | 49.07 % | 49.07 % | PASS |
| `random` | 23 | 46.66 % | 47.75 % | 47.75 % | PASS |

For comparison Tau V's idealised model (no command bus, no scan-out, no CPU or DMA): clip worst picture 15 %, `allbi` 44 %, `static` 27 %, `scenechange` 32 %, `pan` 32 %, `random` 31 %. The measured values are about 1.5 times higher (1.51 to 1.58), below the review's 88-98 % estimate for a B picture. The generated traffic is included (scan-out about 0.26 words per cycle).
(`allbi` is the worst by a wide margin; a B picture there has 200,000 file records and 1.4 M words.)

**Timed (release_ns honoured), subsets only (an idle gap costs as much simulation time as a busy cycle):**

| File | Pictures | busy p50 / max | parser span | lag p50 / max | past deadline | 90 % rule as written |
|---|---|---|---|---|---|---|
| clip | 0..7 | 37.31 % / 37.31 % | 37.30 % | 579 / 674 cycles | 0 | PASS |
| `allbi` | 1..4 | 99.96 % / 99.98 % | 99.96 % | 233 / 815 cycles | 0 | **FAIL by construction (F1)** |

In the timed mode the memory adds under 1,000 cycles (about 8 us) to the parser; every deadline is met with a whole picture of margin. The `allbi` failure is the parser rate, not the controller (finding F1: the flat-out 66 % is the number about the memory).

**Other measured values** (flat out, whole files; ranges over the six files):

* Requests per macroblock after 2D fusing: clip 36.19 (Tau V's own count: 36.2), `allbi` 36.46, `static` 34.88, `scenechange` 35.14, `pan` 35.31, `random` 34.93; reads are about 3.4 per macroblock, writes 32.
* Commands per request (all ports): 4.35 clip, 4.40 `allbi`, 3.88 to 4.03 the others. Row hit rate 60.6 % on every file (`1 - ACT / column commands`).
* Burst-tail waste: 2 words in 24.9 M (clip), 9 in 28.8 M (`allbi`), 0 in the timed subsets: read bursts are cut exactly.
* Scan-out: no underflow; the FIFO fell as low as 589 of 1,200 words; urgent was asserted rarely in the normal runs (8 to 28 measured waits per file, worst 40 cycles). In a stress run with `urgent` asserted whenever the FIFO is not full (`+scan_urgent=1199`, clip pictures 1-2) there were 1,846 urgent waits, worst **69 cycles against the bound 155**, none over; picture 1's busy time was 18.06 % against 18.03 % without the stress.
* Latencies (accept to done, cycles, whole clip): decoder read average 352, max 1,568; decoder write average 41, max 1,251; scan-out average 328, max 1,746; **CPU single-word read average 134, max 4,440 (44 us)**; DMA write average 191, max 2,557. The CPU maximum is the largest latency seen on any port and is not explained here (it is one outlier in 194,000 reads; not investigated).
* Refresh: 49,885 REF in the 38.9 M cycles of the clip run, one per 780 cycles (the 7.8 us interval), no `phy_fault`.

## Harness validation

* `sim/sdram_replay/run_mutants.sh` (12 runs: the unmutated flat-out and timed base runs pass, ten defects are killed with the matching reason): lost request (hang), corrupted write data (chip/shadow compare), corrupted read data (mismatch),
  `done` without `r_last` (protocol), scan-out starved (underflow), feeder without fence, feeder without barrier, offering before `release_ns`, a slow scheduler over 90 % (calibrated against its own measured busy time), a slow scheduler past the deadline.
  Result: **12 ok, 0 bad on the stub DUT and 12 ok, 0 bad on the real PHY + `sched_v` + chip model.** The mutants are runtime options plus a shim between the harness and the DUT, so they test the harness's detection, not `sched_v`.
* Tau V's validator over the six files (above). The clip's request stream equals the documented untimed `.req` (the validator checks it), so the flat-out clip run is the "untimed replay" of contract 7.4 with the generated traffic added.
* Cross-checks: the requests per macroblock of the clip equal Tau V's count (36.2); the harness recomputes every verdict in `replay_summary.py` from the `PIC` lines (harness and summariser agree in all runs).

## Not done / limits

* The timed mode was run only for clip pictures 0..7 and `allbi` pictures 1..4 (about 25 minutes per shard); the other four worst files were run flat out only. `static`, `scenechange`, `pan`, `random` timed would show the same structure (parser span 99.96 %).
* Shards start with a cold DUT (open rows, refresh debt); the flat-out figures are per picture and unaffected by more than the first few hundred cycles. The merged `cycles` of timed shards counts stream time of the shard (a reporting quirk fixed in the harness after those runs: it now prints simulated cycles).
* Scan-out, CPU and DMA are generated by the models of F5, F6 and the contract's guesses (DMA direction and size are Tau V's guesses); 133/150 MHz and CL3 were not run (`CLSEL=3` is a parameter but was not exercised by a replay).
* `sched_a` is not covered; the harness drives `sched_v` only. No Quartus, no Pocket.
* No entry in `docs/AUDIT_TRAIL.md` was written (left to the integrating session).
