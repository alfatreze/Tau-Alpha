# sched_v throughput pass (after B-922: registered offers cost about 30 % busy time)

Simulation only (iverilog, replay flat out, one picture per trace: `allbi` picture 1, `clip` picture 2, `static` and `pan` picture 1; tool `sim/sdram_sched_v/run_idle.sh`, which adds the idle-bus classifier `idle_mon.sv`). Nothing was synthesised: the depths below are designed. The allbi picture used here is lighter than the one behind B-922 (59.7 % here against 85.2 % there), so allbi is also given scaled by the measured cycle ratio. The static and pan pictures reproduce the B-922 figures (61.5 and 65.5 %).

## Where the cycles went (baseline, registered-offer sched_v)
Idle bus cycles per picture (share of all cycles): clip 1,195,875 cycles: data 73.5 %, refresh 1.1 %, **write waits 8.3 %**, row open waits 6.6 %, **pipeline blank after an issue 4.5 %**, issue cycle of non-chained pieces 6.0 %. allbi: data 73.4 %, write waits 10.7 %, blank 4.0 %, row 4.9 %, issue 5.9 %, refresh 1.1 %. Half of the pieces are under 6 words (51 %), only 23 % of the pieces were chained; the decision pipeline (registered head, stages A-D, blank window of 5 cycles after an issue) cannot nominate the next piece of a short burst in time, and a write piece needed lock, pull and an empty pull slot before it could start.

## Changes, measured one after the other (cycles of the sampled picture; busy % of the period)
| change | allbi cycles (busy) | clip cycles (busy) | tests |
|---|---|---|---|
| baseline (7a500733) | 1,988,577 (59.7 %) | 1,195,875 (28.7 %) | 72 pass |
| F1 fast continuation: two cycles after an issue the issued port's next piece in the same bank and row (same request, or a write) becomes the target straight from the registered head, no urgent/starved/cap pending | 1,785,104 (53.6 %), -10.2 % | 1,080,329 (25.9 %), -9.7 % | pass, mutants killed |
| F2 mask only the issued port in the candidate stage (instead of blanking everything) | not run | 1,078,491: gain 0.2 %, **one read mismatch and protocol errors in the replay: rejected and removed** | |
| F3 pull cursor (two write slots: the issued piece's remaining words are pulled by a cursor while the next piece is already committed) and fast lock (a continuing write piece of the batch is committed in the cycle of its fast target) | 1,744,794 (52.3 %), -2.3 % more | 1,060,249 (25.4 %), -1.9 % more | pass |
| F4 fast-lock limit check as a registered flag (piece <= 32 words, 32 words of batch budget left) instead of a 10-bit compare: same result, shallower | 1,744,794 | 1,060,249 | pass |

**Total:** allbi -12.3 %, clip -11.3 %, static 2,049,487 -> 1,771,571 (-13.6 %, 61.5 -> 53.1 %), pan 2,182,212 -> 1,934,357 (-11.4 %, 65.5 -> 58.0 %). Scaling the allbi cycle ratio (0.877) to B-922's figure: **85.2 % -> about 74.7 %** (the picture behind B-922 was not replayed here). Chained pieces 35,497 -> 77,813 of 150k (allbi). Replay checks on every run: 0 data mismatches, 0 phy_fault, 0 chip errors, urgent waits within the bound.

## What was not done
Row-open waits (5 %) and the 5-cycle blank for pieces that need another port (4 %) remain: they need a second selection pipeline or a deeper look-ahead, which would add area and depth. Write coalescing across requests and a third write slot were not tried.

## Paths touched and their designed depth (6-input LUT levels, not measured; v1's deepest stage is the arbitration, 7)
| path | change | depth |
|---|---|---|
| queue head registers (`tau_sdram_sv_pq`) | `r_fast`: bank/row equality of the new head against the previous head, and the `<= 32` flag | 6 (the bank hash plus a 13-bit equality), ends at registers |
| stage D (target registers) | one more source (the fast target): a mux in front of the `td_*` registers | 4 (was 3) |
| `fast_c` | AND of about ten registered or muxed terms | 4 |
| fast lock (`fl`) | `fast_c` plus three flags and the `cur_left <= 4` compare; feeds the lock enable | 6 |
| pull cursor | `pull_pc` select of the pull port: one mux more in `w_next` (a client port, not a PHY port) | 5 |
No adder or comparator wider than 10 bits was added to a decision path, no multiplier, no DSP. The offer outputs are untouched (flops straight to the PHY ports, 0 levels).

## Mutants added
`MUT_FAST_ROW` (fast continuation without the same-row check; killed by `splits`), `MUT_FAST_NOURG` (fast continuation while an urgent request waits; killed by `urgent_bound`), `MUT_FL_NOLIM` (fast lock without the batch limits; killed by the new test `batching6`: 6-word writes).
