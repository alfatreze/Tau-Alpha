# SDRAM replay (throughput) harness

Drives the shared SDRAM controller (`tau_sdram_phy` + `tau_sdram_sched_v` + `sim/sdram_chip_model.sv`) with Tau V's timed request
streams (`.treq`, `docs/TREQ_TAU_V_SPEC.md` of the Tau V repo, contract section 7.4) plus generated scan-out, CPU and DMA traffic, checks
every word against a shadow memory and measures whether every picture is decoded in time. iverilog, 4-state (the chip model needs it).
Simulation only: nothing here says anything about a Pocket or a Quartus fit.

| File | What |
|---|---|
| `tb_replay.sv` | the test bench (the only top level, `tb_replay`) |
| `replay_stub.sv` | a stub scheduler (`-DSTUB`) with fixed simple timing, to develop the harness and to check it quickly; its numbers mean nothing |
| `run_replay.sh` | build and run one simulation |
| `run_full.sh`, `run_shard.sh`, `treq_count.py` | run a whole file as parallel shards and merge |
| `replay_summary.py` | summariser: percentiles, the pass rule recomputed, merging of shards |
| `run_mutants.sh` | 12 negative tests of the harness itself |

Input files are git-ignored and referenced by SHA-256 (see `docs/features/SDRAM_REPLAY_FINDINGS.md`); the scripts look in
`work/tauv_treq/` of this checkout or of the main checkout (`REPLAY_DATA=dir` overrides).

## Quick start

```
sim/sdram_replay/run_replay.sh clip +norelease +limit_pictures=2 +limit_mbs=40     # about 15 s: real PHY + scheduler + chip model
sim/sdram_replay/run_replay.sh --stub clip +norelease +limit_pictures=2             # harness only, a few seconds
python3 sim/sdram_replay/replay_summary.py build/sdram_replay/tnv3_tears_q30_48pic.log
sim/sdram_replay/run_mutants.sh                                                   # the harness' own tests (stub, about 5 min)
```

`run_replay.sh TARGET` takes `clip | allbi | static | scenechange | pan | random` or a `.treq` path. The exit status is 0 only for
`VERDICT PASS`. Speed (measured, an M-series Mac, one core): the real DUT about 5,000 cycles per second, the stub about 30,000, plus
about 3 s of start-up (4 M-word stores). The clip is 200 M cycles of simulated time in the timed mode and about 30 M flat out.

## Options (plusargs of `vvp`; parameters of `tb_replay` with `-P NAME=VALUE` to `run_replay.sh`)

| Option | Meaning |
|---|---|
| `+treq=FILE`, `+scan=FILE` | the streams (the scripts set both) |
| `+norelease` | ignore `release_ns`: offer as fast as the DUT accepts. Generated ports still run in real time. This is the **memory-time** measurement (see "How to read the result") |
| `+limit_pictures=N`, `+limit_mbs=N` | quick mode: stop after N pictures; keep only the first N macroblocks of each picture (the rest is dropped, so the picture is not a real one) |
| `+skip_pictures=S` | fast-forward the first S pictures: their writes go straight into the memory and the shadow, no DUT time (shards) |
| `+nofuse` | do not fuse port-1 reads into 2D requests (every record is one 1D request) |
| `+period_cycles=N` | override the picture period (default from the STREAM record: 1e12 / fps / 10,000 ps) and with it the deadlines `(n + A + 1) x N`; with `+norelease` it makes a flat-out run check a budget |
| `+ahead=A` | decode-ahead depth (default: the STREAM record's, 1) |
| `+no_scan`, `+no_cpu`, `+no_dma` | switch a generated port off; `+scan_fifo=W` (1200), `+scan_urgent=W` (600) the scan-out FIFO model |
| `+nochipchk`, `+nopreload` | no back-door compare at write done (faster); do not preload the scan-out and CPU areas |
| `+tmo=C` | hang watchdog (200,000 cycles without progress on ports 1/2 while a request is pending) |
| `+maxcyc=C` | stop after C cycles |
| `+mut_*` | harness mutants, see `run_mutants.sh` |
| `-P BANK_HASH=n QDEPTH=n WR_HI_REQ=n WR_DEFER_NS=n REF_SOFT=n REF_HARD=n BATCH_REQS=n BATCH_WORDS=n W_LEAD=n CLSEL=n EN2D=h CPU_P=n DMA_P=n WEIGHT_P=.. MAXG_RD_P=.. MAXG_WR_P=..` | `sched_v` / PHY parameters (the defaults are the scheduler's) |

## What the harness does

**Ports 1 and 2 (the file).** Records are offered strictly in file order, one at a time (the next only after the previous was accepted),
never before `T0 + release_ns` (`T0` = the cycle after PHY init; `ns x 100` cycles at 100 MHz). A `FENCE_RD` record waits until every
earlier port-1 read delivered `r_last`. `PIC_START` waits for its release. `PIC_END` is a full barrier: it is reached when every earlier request
is done (reads delivered, writes `done`), no later record is offered before, and the cycle it is reached is compared with its deadline
(its `release_ns`, or `(n + A + 1) x period` with `+period_cycles`).
**2D fusing (contract 7.4):** within one macroblock, consecutive port-1 reads of the same word count `w` whose addresses step by a constant 1..2,047
words are one 2D request (`req_w = w`, `req_h` = the run length, `req_stride` = the step; at most 63 rows and `w x h` at most 512, a longer run is cut).
A run of one is a plain 1D request; `+nofuse` switches the fusing off. Writes stay 1D.
Three monitors (fence, barrier, release) re-check the offer rules independently of the code that enforces them.

**Generated ports** (their addresses live in regions 5, 6, 7 of the address space, away from the pictures; see the findings).
Port 0, scan-out: the 1,846 requests of `scan_800x720_1frame.req` repeat; a 1,200-word FIFO is drained smoothly at the 800x720 display rate
(600 words per 22.676 us line, 720 active lines, 15 blank), a request is issued when the FIFO has room for the outstanding words plus its length
(the read-credit rule), `urgent` is high while the FIFO holds less than 600 words (one line); an underflow is a word due with an empty FIFO.
Port 3, CPU: one random single-word read every 2 us (200 cycles) from a preloaded 64 K-word area. Port 4, DMA: 64-word sequential writes every 64 us
(2 MB/s) into a 128 K-word ring. Refresh is the PHY's and the scheduler's.

**Data integrity.** A write's data is a hash of (word address, write version); the version is a global counter incremented per accepted write request.
The shadow memory stores the last version per word; a read is checked word by word against it. Words never written (the references of the first
pictures) are counted as unchecked, not as errors; a read whose word was rewritten after the read was accepted is counted as a race and skipped.
At every write `done` the chip model's array is compared with the shadow (the write path end to end). The scan-out and CPU areas are preloaded by back door.

## How to read the result

The run prints `PIC`, `PORT`, `RESULT` lines and `VERDICT PASS|FAIL` with one `reason:` line per failed condition; `replay_summary.py` turns them into a table.

* **busy** of picture n = `reached(n) - max(release(n), reached(n-1))`, as a percentage of the period. The pass rule: every `PIC_END` reached by its
  deadline AND busy at most 90 % of the period for every picture AND no scan-out underflow, no mismatch, `phy_fault` 0, chip-model errors 0, no rejected request.
* **Two modes, both must be read.** *Flat out* (`+norelease`): releases are ignored, so busy is the time the memory system needs for the picture
  with the generated traffic running: the quantity the 90 % rule is about. *Timed* (no option): releases honoured, as Tau V specified; the macroblocks arrive at
  67,500 a second, so busy can never be smaller than the parser span (last macroblock release minus picture release), which is **99.96 % of the period on the
  five worst-case files** (2,250 macroblocks at 67,500 a second at 30 fps) and 37 % on the clip (24 fps): the literal 90 % rule cannot pass in the timed mode on those files,
  whatever the memory does (finding F1). The summary therefore also gives **lag** = `reached(n)` minus the last macroblock release of the picture, the delay the memory system adds
  beyond the parser; the deadline check and the lag are the timed-mode quantities that depend on the DUT.
* `requests per macroblock` is decoder requests after fusing divided by macroblocks; `commands per request` is non-NOP PHY commands over all requests of all ports;
  `row hit rate` is `1 - ACT / (RD + WR)` over all command-bus column commands (chained column commands to an open row count as hits);
  `burst-tail waste` is words the chip drove on reads minus words delivered to clients (0 when the scheduler cuts bursts exactly);
  `urgent wait` is, for each scan-out request that is the oldest and pending while `urgent` is high, the cycles from the later of its acceptance, the rise of `urgent` and the end of the
  previous request to its first word, against the scheduler's own documented bound (`URGENT_BOUND_CLK`, 155 at the defaults); it is measured only when the FIFO falls below one line,
  which a healthy run rarely does.
* Percentiles are nearest-rank over the pictures (for fewer than 100 pictures p99 is the maximum).

## Full runs

```
sim/sdram_replay/run_full.sh --jobs 6 --shard 6 clip +norelease                 # all 48 pictures, flat out, 8 shards
sim/sdram_replay/run_full.sh --jobs 6 --shard 3 allbi +norelease
sim/sdram_replay/run_full.sh --jobs 4 --shard 2 --first 0 --count 8 clip        # timed, pictures 0..7
```

Shards run in parallel (`xargs -P`), each fast-forwards the earlier pictures and then simulates its own; `replay_summary.py --merge` merges them. A shard starts with a cold DUT
(open rows, refresh debt), which moves a picture's busy time by well under 1 %. One picture of the clip costs about 150 s flat out, one picture of `allbi` about 5-6 min
(the sum over all 23 is about 2.5 CPU hours); the timed mode adds the idle gaps (4.2 M cycles = 14 min per clip picture). `build/sdram_replay/full_<file>_<mode>/summary.txt` keeps the result.

## Validating the harness: `run_mutants.sh`

Twelve runs: the unmutated base (must pass) and eleven defects the harness must report with the right reason: a lost request (hang), corrupted write data (chip/shadow compare), corrupted read data (mismatch),
a `done` without `r_last` (protocol), a scan-out starved for 9,000 cycles (underflow), a feeder without fence, without barrier, offering before `release_ns`, a slow scheduler giving busy time over 90 %
(calibrated against its own measured busy time) and past the deadline. Mutants are runtime plusargs plus a shim between the harness and the DUT, so one compiled binary serves all.
Tau V's own validator `work/tauv_treq/test_treq_tauv.py` (file layout, markers, deadlines, fences, 7 mutants) is run over the files separately (results in the findings).
