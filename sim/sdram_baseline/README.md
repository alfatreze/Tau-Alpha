# sim/sdram_baseline: the OLD SDRAM controller through the new chip model

Contract 7.1: the old controller (`src/fpga/rtl/mem/tau_sdram_ctrl.sv`, the RTL the player ships, 100 MHz, CL2, full-page bursts) runs through `sim/sdram_chip_model.sv` first, as a baseline. Results: `docs/features/SDRAM_BASELINE_FINDINGS.md`.

Needs iverilog (4-state; the model cannot run under Verilator). From the repository root:

```
sim/sdram_baseline/run_baseline.sh matrix     # 2 controllers x 5 delay scenarios, 8 ms of traffic each, about 30 s
sim/sdram_baseline/run_baseline.sh raw        # as-is init: no EMRS waiver, registers X
sim/sdram_baseline/run_baseline.sh sweep      # command-delay and chip-clock-delay sweeps (short runs)
sim/sdram_baseline/run_baseline.sh long       # 64 ms mixed (both controllers) and 110 ms write-heavy; several minutes, run in the background
sim/sdram_baseline/run_baseline.sh summary    # class x run matrix of everything in build/sdram_baseline/
sim/sdram_baseline/run_baseline.sh run CTRL SCEN [-Ptb_sdram_baseline.X=..]   # one run
```

Outputs go to `build/sdram_baseline/<label>.{json,log}` (json: per error class count, first time, first three occurrences, count during the init; log: the bench and model summary lines).

## What is in it
* `tb_sdram_baseline.sv`: instantiation glue and a client-side driver. No controller code is copied. The traffic is the player's p0 port protocol: framebuffer clear chunks (two 512-word constant bursts per row), CPU single-halfword writes (full and byte-masked) and reads (read request, end-burst on the next cycle), scanout fill (256-word read bursts, re-requested when a refresh or the page end cuts them short; every 50th starts at column 900 to reach the page end), engine copy (64-word read, then a 64-word streamed write from a BRAM-like source, then a read-back check), idle gaps (one of 300 us), all checked word by word against a scoreboard. `WRITE_HEAVY=1` replaces the reads by writes only (worst case for the refresh spacing).
* `run_baseline.sh`: builds and runs, pipes the output through `aggregate.py` (streaming, because a failing corner prints hundreds of thousands of lines).
* `aggregate.py`: counts the model's `[CHIPERR]` lines per class.

## Toolchain shim (not a change of the controller)
iverilog 13 rejects the controller's unpacked `typedef struct`. The script makes a build copy in `build/sdram_baseline/` with the single keyword `packed` inserted (one `sed`); logic and timing are identical. The "pre-S0" controller is `git show cafd7477^:src/fpga/rtl/mem/tau_sdram_ctrl.sv` (tRFC setting 80 ns); the tree's file has the S0 value 98 ns. S0 changed nothing else in this file (the I/O registers and the SDC are QSF/SDC, not RTL), so "pre" vs "s0" differ in tRFC only; the I/O packing is modelled by the delay scenarios.

## Glue the model needs (all in the bench, none in the model)
* The chip clock is `~clk` (the controller's `SIM` stub), delayed by `CLK_DLY`, and gated off until the controller has seen reset (so the model does not count edges of undriven pins).
* Commands, address, bank, DQM and CKE are delayed by `CMD_DLY` (transport), write data by `DQ_DLY`. `fpga_oe` is the controller's DQ driver enable with the envelope "on after OE_MIN, off after OE_MAX". Board delay chip to FPGA is the model's `DMIN/DMAX`. The controller's read data is the pin (`p0_q = SDRAM_DQ`), captured by the consumer on the controller's rising edge, so the model runs with `CAP_MODE = 0` (rising edge).
* `EMRS_WAIVE = 1` (default): the old controller never sends an EMRS, so the model would stay in its init state and flag every later command and stop checking tREF. After the MRS the bench writes `init_stage = 5; init_done = 1` and restarts the row ageing, through hierarchical names. `raw` runs without it.
* `INIT_REGS = 1` (default): `SDRAM_A`, `SDRAM_BA` and `SDRAM_DQM` of the controller have no reset value; they are X in a 4-state simulation. Cyclone V registers power up as 0, so the bench starts them at 0. `raw` leaves them X.
* `nCS` is not connected (the model has no chip select; `core_game.vh` leaves `SDRAM_nCS` open, the board ties it).

## The delay scenarios (all ps; ASSUMPTIONS, there is no Quartus number for the old design's unpacked outputs)
The old design had no I/O register packing and no SDRAM SDC, so no source gives its pin delays. One corner per run (the model checks one delay value per signal), so each scenario has a late and an early corner:

| scenario | chip clock (DDIO) | cmd/addr/DQM/CKE/data | DQ output enable on/off | board |
|---|---|---|---|---|
| `nominal` | 3.5 ns | 1.0 ns | 1.0 / 1.0 | 0 |
| `unp_late` | 1.5 | 8.0 | 3.0 / 8.0 | 0.2 .. 0.6 |
| `unp_early` | 3.5 | 3.0 | 3.0 / 8.0 | 0.2 .. 0.6 |
| `pk_late` (S0 packed) | 1.5 | 3.5 | 1.5 / 3.5 | 0.2 .. 0.6 |
| `pk_early` (S0 packed) | 3.5 | 1.5 | 1.5 / 3.5 | 0.2 .. 0.6 |

`nominal` is the functional baseline: the chip clock 3.5 ns after the controller clock is what puts the controller's rising-edge capture inside the tAC = 8 ns window (see the findings). The unpacked range of 3 to 8 ns is a deliberately wide guess (fabric register, routing, output buffer); the sweeps show where each finding switches on, so the conclusion does not depend on the guess.

Chip parameters are the model's defaults (AS4C32M16MS -6 numbers: tAC 8 ns and tOH 1.8 ns at CL2, tCMS/tCMH 1.5 ns). tAC max and tOH min are stacked in the capture check, which is pessimistic.
