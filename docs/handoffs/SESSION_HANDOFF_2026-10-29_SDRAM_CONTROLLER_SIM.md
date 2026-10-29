# Session handoff 2026-10-28 to 29: the shared SDRAM controller in simulation (contract v0.9)

Audit **B-907..B-912**. `main` is local, ahead of origin (nothing pushed). Read after `SESSION_HANDOFF_2026-10-28_TALOS_SDRAM_CONTROLLER.md`. Card unchanged: stable TAU + TAU DEV 199. **Nothing of this is in Quartus or on a Pocket.**

## What exists (all clean-room, written by separate agents from the contract and the datasheet text, merged into `main`)
* **Contract** `docs/features/SDRAM_CONTROLLER_CONTRACT.md` v0.9 (changes listed at its end). Findings files: `SDRAM_CHIP_MODEL_FINDINGS.md`, `SDRAM_PHY_FINDINGS.md`, `SDRAM_SCHED_V_FINDINGS.md`, `SDRAM_REPLAY_FINDINGS.md`. Reply to Tau V: `SDRAM_REPLY_TO_TAU_V_2026-10-28.md` (a log; the clean version was pasted in chat).
* **Chip model** `sim/sdram_chip_model.sv` (+ `sim/sdram_chip_model/`, `run_tests.sh`): 65 tests, iverilog (4-state needed). **PHY** `src/fpga/core/tau_sdram_phy*.sv` (+ `sim/sdram_phy/`, `run_tests.sh`, `lint.sh`): 120. **Scheduler** `tau_sdram_sched_v.sv` + `tau_sdram_sv_*.sv` (+ `sim/sdram_sched_v/`): 55. **Replay** `sim/sdram_replay/` (README; `run_replay.sh`, `run_full.sh`, `run_mutants.sh`): inputs in `work/tauv_treq/` (git-ignored, SHA-256 checked against Tau V's list).
* **Probe prepared, not run:** `tools/sdram_probe.py launch NAME --variant base|cal|bist|perf|all` (the VM was busy with Tau V's `rcprobe` compile; the tool refuses to start while any Quartus process runs; do not use `--force`). Estimate 15-30 min once it starts (unmeasured). The first real run may need fixes (PLL pin name, `oe_q` instance paths, SDC syntax, report parsers are unchecked).

## Decisions (owner)
D-T10 ownership: Tau Alpha builds everything; **Tau V owns `sched_v` from its tag; Tau Alpha owns `sched_a` and the common elements.** D-T11 parked: a common-modules repository, first test at the `sdram-phy-v1` tag. Tau V agreed the 90 % busy rule is measured flat out (timed mode keeps deadlines).

## Numbers (simulation)
Flat-out busy, p50 / max: clip 20.0 / 22.8 %, allbi 65.3 / 66.4 %, static 42.1 / 42.6 %, scenechange 48.3 / 48.8 %, pan 48.7 / 49.1 %, random 46.7 / 47.7 %; 0 mismatches, 0 PHY faults, row hit about 60 %, urgent wait 40 (bound 155). Tau V's old-controller memtest (100 MHz CL2 and 150 MHz CL3, 62 MB, 2,658 / 3,846 soak passes, 0 errors) is the Pocket baseline, not a margin proof.

## Open, in order
1. Run the probe when the VM is free (ask Tau V; I may not poll-and-launch unattended: the permission classifier denied that). 2. The OLD controller through the chip model (the tRFC and EMRS findings are expected). 3. `sched_a` (Tau Alpha's own scheduler) and its replay of the player's traffic. 4. Tags `sdram-phy-v1` / `sdram-sched-v-v1` (after the probe; the tag notes must say simulation only), the MSA datasheet diff, the interface document. 5. Pocket runs: a phase/capture sweep with `CALIBRATE`, Tau V's memtest against the new PHY, the soak. 6. Known gaps: S15 (only read-behind-write ordering across ports), the bandwidth cap is a fixed window, `idle_ok` arbitration is the author's design, CALIBRATE loses the fanout-1 input packing when on, replay timed mode only on subsets, CL3 and 133/150 MHz not run. Then the owner's full review of pending work and v0.7.

## Traps
iverilog is required for the model and the PHY (Verilator is 2-state); the full replay is about 5k cycles/s (use shards: `run_full.sh --jobs 7 --shard 6`); agents hit a session rate limit once (resume with SendMessage); `brew install poppler` was installed for the datasheet.
