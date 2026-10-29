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
