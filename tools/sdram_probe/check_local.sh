#!/usr/bin/env bash
# Local checks of the SDRAM probe that need no Quartus: Verilator lint and an iverilog elaboration of the probe top with
# the probe's macros, for the base variant and with CALIBRATE / BIST / PERF on, with and without the Altera primitives
# (stand-ins in stubs/local_check_stubs.sv). MIT licence.
#   tools/sdram_probe/check_local.sh
set -u
cd "$(dirname "$0")/../.."
R=src/fpga/core
RTL="$R/tau_sdram_phy.sv $R/tau_sdram_phy_core.sv $R/tau_sdram_dq_cap.sv $R/tau_sdram_dq_cap_dual.sv $R/tau_sdram_clk_out.sv \
$R/tau_sdram_sched_v.sv $R/tau_sdram_sv_pq.sv $R/tau_sdram_sv_arb.sv $R/tau_sdram_sv_tm.sv"
P=tools/sdram_probe
rc=0
for D in "" "-DPROBE_CALIBRATE" "-DPROBE_BIST" "-DPROBE_PERF" "-DPROBE_CALIBRATE -DPROBE_BIST -DPROBE_PERF"; do
  for Q in "" "-DTAU_SDRAM_ALTDDIO_OUT -DTAU_SDRAM_ALTDDIO_IN"; do
    # lint: only warnings that are not width/unused noise of the stand-ins (the probe top itself must be clean of errors)
    out=$(verilator --lint-only -Wall -Wno-fatal -Wno-DECLFILENAME -Wno-UNUSEDSIGNAL -Wno-UNDRIVEN $D $Q --top-module tau_sdram_probe_top \
          $P/stubs/local_check_stubs.sv $RTL $P/tau_sdram_probe_top.sv 2>&1)
    errs=$(echo "$out" | grep -c '^%Error')
    top_warn=$(echo "$out" | grep '^%Warning' | grep -c 'tau_sdram_probe_top.sv')
    echo "verilator [$D $Q]: errors=$errs warnings-in-probe-top=$top_warn"
    [ "$errs" -eq 0 ] || { echo "$out" | grep '^%Error' | head; rc=1; }
    [ "$top_warn" -eq 0 ] || { echo "$out" | grep '^%Warning' | grep 'tau_sdram_probe_top.sv' | head -5; rc=1; }
    vvp_out="${TMPDIR:-/tmp}/sdram_probe_check.$$.vvp"
    iv=$(iverilog -g2012 -o "$vvp_out" $D $Q -s tau_sdram_probe_top $P/stubs/local_check_stubs.sv $RTL $P/tau_sdram_probe_top.sv 2>&1)
    ivrc=$?
    rm -f "$vvp_out"
    # "sorry: constant selects in always_*" is an iverilog note about sensitivity lists, not an error
    [ "$ivrc" -eq 0 ] || { echo "iverilog FAILED [$D $Q]"; echo "$iv" | grep -v 'constant selects' | head -5; rc=1; }
  done
done
# the macros must not leak the simulation mutants
grep -q 'MUT_' $P/tau_sdram_probe_top.sv && { echo "MUT_ macro in the probe top"; rc=1; }
echo "check_local: $([ $rc -eq 0 ] && echo OK || echo FAILED)"
exit $rc
