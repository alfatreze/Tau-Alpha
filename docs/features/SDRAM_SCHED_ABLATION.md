# sched_v ablation: which features earn their area (replay, flat out; simulation only)

Method: `sim/sdram_sched_v/run_ablation.sh` (sources: `sim/sdram_replay`, `+norelease`, i.e. memory time per picture with the generated scan-out, CPU and DMA traffic, real PHY, chip model, every word checked). Trace `clip`: pictures 1..3 of `tnv3_tears_q30_48pic` (24 fps, period 4,166,666 cycles); trace `allbi`: picture 1 of the worst-case all-bi file. Busy is the replay's busy time as a percentage of the period (the pass rule is at most 90 %). Summary: `python3 sim/sdram_sched_v/ablation_summary.py`; logs in `build/abl/` (not committed). Three clip pictures and one allbi picture are a sample, not the whole files; a cold start per run moves a picture by well under 1 %. **No area was measured here (Quartus is not available to this agent); the area column is reasoning from the structures, to be confirmed by the VM probe.**

| config (what is switched off) | clip busy p50 % | clip busy max % | allbi busy max % | commands / request | row hit | urgent wait max (cycles) | all runs |
|---|---|---|---|---|---|---|---|
| base (v1 defaults) | 21.04 | 21.22 | 44.17 | 4.41 | 0.609 | 0 | PASS |
| QDEPTH 2 | 21.20 | 21.60 | 45.00 | 4.44 | 0.593 | 0 | PASS |
| QDEPTH 1 | 21.38 | 22.19 | 45.93 | 4.43 | 0.603 | 0 | PASS |
| look-ahead off (no ACT/PRE while a burst streams) | **24.69** | **25.64** | **51.98** | 4.60 | 0.603 | 3 | PASS |
| batching off | 21.15 | 21.39 | 44.40 | 4.47 | 0.609 | 0 | PASS |
| RAW check off | 21.04 | 21.22 | 44.17 | 4.41 | 0.609 | 0 | PASS |
| same-row continuation off | 21.00 | 21.17 | 44.04 | 4.40 | 0.601 | 0 | PASS |
| 2D requests off (`+nofuse`, `EN2D=0`) | 21.04 | 21.22 | 44.17 | 2.25 (twice the requests) | 0.609 | 0 | PASS |
| minimal: QDEPTH 1, no look-ahead, no batching, no RAW check, no continuation, no 2D | 25.38 | 26.37 | 53.76 | 2.43 | 0.593 | 1 | PASS |

## Reading
* **Every configuration passes** with a large margin (at most 53.8 % against 90 %). Caps, `urgent_early` and the region table are off in the replay (the replay does not use them); they cost area only.
* **Only look-ahead pays**: switching it off costs +16 % busy (clip) and +18 % (allbi). Everything else is within 2 % of the base, QDEPTH 1 included (+4.6 % on allbi), batching +0.5 %, continuation and the RAW check 0 %. The replay's ports read disjoint regions, so the RAW check cannot show a benefit here; it is a contract rule (no read ahead of an older overlapping write) and stays, but in a cheap form.
* **2D fusing changes the request count (2x) but not the busy time**: the decoder's requests are short and the scheduler's per-request overhead is small, so 2D earns no throughput here; it is a parameter, off in a minimal build (and it is what brings the multipliers and the wide queue entries).
* The minimal configuration runs 26 % busy on the clip and 54 % on the worst case: **a design with a single-entry queue and no look-ahead has headroom** against the 90 % rule, with the scan-out urgent waits unchanged (0..3 cycles).

## Consequences for the synthesis redesign
Keep: look-ahead (one ACT/PRE target prepared while a burst streams), weighted round robin with urgent, the open-row table, refresh and preemption, the write FIFO pull, same-row continuation (costs almost nothing, keep as a parameter). Make cheap or optional: queue depth 2 (parameter, default 2), batching (parameter, default on, small), RAW check (coarse row ranges, no stamps, no multipliers), 2D (parameter `ENABLE_2D`, default off in the small build), caps and `urgent_early` (parameters, default off, no logic when off), region table (only with BANK_HASH 3). Budget for the pipelined redesign: replay busy not worse than base +20 % (clip) and +30 % (allbi) at the default build.

## Redesign for synthesis (commits c3efcdb8, 83f46fbb): what changed and the numbers
Motivation: probe B-914 (10,937 ALMs, 5,153 registers, 27 DSP blocks, Fmax 19.7 MHz, 33 logic levels). **Nothing below was synthesised; areas and depths are design targets and reasoning, to be confirmed by the VM probe.**

| change | effect (reasoned) |
|---|---|
| RAW check: 13-bit row ranges stored at accept (2D bound by a shift: `addr + (h << 11)`), no stamps, no 27-bit subtractors, no multipliers | removes the 27 DSP blocks (the 2D span multiplies) and about 24 27-bit comparators; a newer overlapping write also holds a read back (cannot deadlock) |
| queue depth 2 default, 2D fields only on 2D ports, `CNT_EN` (statistics counters optional), read descriptor FIFO 8 | far fewer flops (about 2,400 queue flops at depth 4 with 2D everywhere; counters 6 x 160 flops) |
| registered head piece in the queue, decision pipeline A (flags) - B (candidates) - C (arbitration) - D (target regs), stale stages blanked after an issue | the 33-level path is cut into register-to-register stages |
| fast open-row cache (last ACT) and ACT-after-PRE shortcut | a row open does not wait for the 4-stage refresh of the flags |
| write pull independent of this cycle's pop | `w_next` no longer depends on the command decision |

Designed stage depths (6-input LUT levels, upper bounds by inspection, not measured): queue head piece (min of three lengths, page room) 5; A: row-hit flag (4:1 mux of the open row + 13-bit equality) 5, RAW block (13-bit range compares + OR tree) 5, batch fit (10-bit add and compare) 4, others-want 5; B: candidate/urgent/early/continuation 5; C: arbitration (urgent > early > continuation > WRR rotation over 6 ports) 7, the deepest stage; D: 6:1 field mux 3. In the offer cycle (registers and PHY permits to the PHY inputs): command 6, `wvalid` 5, `wdata` 3 (8:1 FIFO mux). Counters and the read return (a 4-level port decode) are the other places to watch. Target 100 MHz at 6-8 levels per stage.

Verification (iverilog, simulation only): the 28 system tests, unit tests, 20 mutants (all killed), Verilator -Wall lint clean; `sched_min` 6 tests, 5 mutants. The urgent-bound test still passes against the unchanged 155-cycle formula (the first measurement under load was 181 cycles before the test's region overlap was removed; the pipeline adds latency, the margin to the formula is smaller: re-measure after any change).

Replay (same pictures as the table, flat out): clip busy max **28.5 %** (v1 21.2 %, +34 %), allbi **58.6 %** (v1 44.2 %, +33 %); urgent wait max 0 (clip) and 38 cycles (allbi); `phy_fault` 0, chip errors 0, all verdicts PASS. The stated budget (+20 % clip, +30 % allbi) is missed by a few points: the price of the blanked pipeline for short pieces (a piece shorter than about 6 words cannot chain). Both numbers are far from the 90 % rule and about equal to the ablation's minimal configuration (26.4 % / 53.8 %). A first cut without the fast open-row cache measured 74.6 % on allbi.

## sched_min (`tau_sdram_sched_min.sv`)
Single port, no queue, one open row, no FIFO (write data goes straight to the PHY, `W_LEAD` 2), no look-ahead, no 2D, refresh between pieces, aged-row close. About 250 flops (about 80 of them the three counters) and a few hundred LUTs: **estimate 250-400 ALMs, 0 DSP, 0 M10K** (not synthesised). Tests: basic, every length 1..512, bad requests, refresh load, aged row; 5 mutants killed; lint clean.
