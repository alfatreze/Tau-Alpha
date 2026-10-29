# SDRAM PHY + scheduler synthesis/fit probe (contract 7.5)

Measures the area and the Fmax at 100 MHz of `tau_sdram_phy` + `tau_sdram_sched_v` (6 ports) with the real dram_* pins,
I/O standards, 4 mA drive and the real SDRAM SDC, at slow 85C and fast 0C. **Prepared, not run: nothing here has been
through Quartus.** Contract: `docs/features/SDRAM_CONTROLLER_CONTRACT.md` sections 1 and 7.5.

## Run it
```
python3 tools/sdram_probe.py launch NAME [--variant base|cal|bist|perf|all|cal,bist] [--seed 1] [--dry-run]
python3 tools/sdram_probe.py status NAME      # running / done; area, Fmax, slack per corner (pulls the reports when done)
python3 tools/sdram_probe.py collect NAME     # copy the reports to work/diagnostics/sdram_probe-NAME/
```
Needs the owner's approval for the VM (rule 3): say how long it takes. The tool prints the local finish time; the
first estimate is **15-30 minutes** (a guess: no earlier probe exists; the full core takes 1h40). It refuses to start
while a Quartus process is running on the VM. `--dry-run` prints every ssh command and the file list and touches nothing.

Variants: `base` (default; CALIBRATE, BIST, PERF all off), `cal`, `bist`, `perf`, `all`, or a list (`cal,bist`). They
set the project macros `PROBE_CALIBRATE`, `PROBE_BIST`, `PROBE_PERF`, which the top turns into the PHY parameters of
that name. Run `base` first (the contract's budget is for the default build) and `all` for the worst case. CALIBRATE adds
`tau_sdram_dq_cap_dual` (DQ fanout 2): the "one register in the I/O cell" packing of that variant is not the base one.

## What is in the stage (flat, on the VM `~/tau-local/sdram_probe-NAME`)
`tau_sdram_probe_top.sv`, `sdram_probe.qsf/.qpf/.sdc`, `probe_report.tcl` at the root; the RTL (`tau_sdram_phy*.sv`,
`tau_sdram_dq_cap*.sv`, `tau_sdram_clk_out.sv`, `tau_sdram_sched_v.sv`, `tau_sdram_sv_pq.sv`, `tau_sdram_sv_arb.sv`) and
the core's PLL (`mf_pllbase*`) copied from the **current working tree** into `rtl/`. The clean-room files
(`tau_sdram_ctrl.sv`, `tau_sdram_arbiter.sv`, `tau_sdram_cpu_bridge.sv`) are not involved.

## The probe
* **Clock:** the core's own PLL `mf_pllbase`, 74.25 MHz from the `clk_74a` pin (PIN_V15, 3.3-V LVCMOS), `outclk_3` =
  100 MHz (the SDRAM clock of the core). The generated clock on `dram_clk` is taken from that PLL output counter
  (`u_pll|mf_pllbase_inst|altera_pll_i|general[3].gpll~PLL_OUTPUT_COUNTER|divclk`, the core's name without the
  `ic|mp1|` prefix).
* **dram_clk** through `ALTDDIO_OUT` (`TAU_SDRAM_ALTDDIO_OUT`), **FALL read capture** through `ALTDDIO_IN`
  (`TAU_SDRAM_ALTDDIO_IN`); both macros are in the QSF.
* **Pins:** the 38 dram_* pins with locations, `1.8 V`, `CURRENT_STRENGTH_NEW 4MA` and the 50 ohm series termination on
  `dram_clk`, copied from `src/fpga/ap_core.qsf`; `FAST_OUTPUT_REGISTER`, `FAST_OUTPUT_ENABLE_REGISTER`,
  `FAST_INPUT_REGISTER` from `src/fpga/core/tau_sdram_phy_assignments.txt` (instance path of the `oe_q` register fixed to
  `u_phy|u_core`).
* **Client side:** `NPORTS` 6, `ENABLE_2D` on port 1, `BANK_HASH` 3 (the region table in use), the other parameters
  at their defaults. Every client signal is one register away from a `VIRTUAL_PIN` (`v_*`, false-pathed): the request,
  write-data, read-data and completion ports are registered both ways. The slow statistics/diagnostic outputs
  (counters, perf snapshot, fault, calibration, BIST) go to virtual pins **unregistered** so that they cost no area;
  their registers are placed freely.
* **SDC:** output delays address/BA 1.5/1.0 ns, command, DQM and CKE 1.5/1.5 ns, data 1.5/1.0 ns, each plus 0.3 ns
  board margin (max = setup + margin, min = -(hold + margin)); read input delays tAC max 8 ns and tOH min 1.8 ns
  (no board margin on the input side, variable `in_board`); `derive_pll_clocks`, `derive_clock_uncertainty`. No
  multicycle is needed for the FALL capture: the falling edge of `clk` is the next rising edge of the inverted chip
  clock, which is Quartus's default relationship (setup 10 ns, hold 0).

## What it reports
`status` prints: the fitter summary (ALMs, registers, RAM blocks = M10K) from `tools/quartus_fit_summary.py --revision
sdram_probe --pin-prefix dram` (which also lists whether the dram_* output/input/OE registers sit in the I/O cells);
the rows `u_phy`, `u_sched` and the top of the fitter's "Resource Utilization by Entity" (**read the area from these
two rows**: the top row also holds the wrapper's roughly 750 client-side registers, about 400 ALMs of register
packing); the Fmax summary of each corner; the worst setup/hold slack per corner and clock, the slow 85C and fast 0C
ones starred; and `probe_report.tcl` (run after the compile): per corner, clk to clk (CORE), clk to `sdram_clk_pin`
(OUT, every output pin), `sdram_clk_pin` to clk (IN, the read capture), the worst slack per output pin group, and an
Fmax estimate `1000 / (10 - setup slack)` of the core paths. Compare with the contract: under 2,000 ALMs and 4 M10K,
positive slack at both corners. Everything is copied to `work/diagnostics/sdram_probe-NAME/`.

## Local checks (no Quartus)
`tools/sdram_probe/check_local.sh`: Verilator lint and an iverilog elaboration of the probe top for the base variant and
with CALIBRATE/BIST/PERF on, with and without the Altera primitives (stand-ins in `stubs/local_check_stubs.sv`, never
staged). `python3 tools/sdram_probe.py launch x --dry-run` shows the commands.

## Not verified (needs Quartus)
That the project reads and fits; that the pin name of the PLL output counter in the SDC and the instance paths of the
assignments exist (a mismatch shows as a warning: `Ignored assignment` / `No ... found`, then check the result); that
the `FAST_*_REGISTER` packing succeeds; that the virtual pins and the false paths behave; the parsers of `status`
(written against the usual report layout, tested only on a hand-made sample); the finish-time estimate.
