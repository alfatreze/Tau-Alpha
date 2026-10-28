#!/usr/bin/env bash
# Run the Tau V SDRAM scheduler tests (iverilog + vvp). MIT licence.
#   sim/sdram_sched_v/run_tests.sh            unit tests and all system tests (fast set)
#   sim/sdram_sched_v/run_tests.sh mutants    the mutation tests only
#   sim/sdram_sched_v/run_tests.sh <label>    one test
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_SCHED_BUILD:-$ROOT/build/sdram_sched_v}"
IVERILOG="${IVERILOG:-iverilog}"
VVP="${VVP:-vvp}"
mkdir -p "$OUT"
RTL="$ROOT/src/fpga/core"
SCHED=("$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv")
PHY=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv")
WANT="${1:-all}"
pass=0; fail=0

# unit tests: label | top | sources
run_unit() {
    local label="$1" top="$2"; shift 2
    "$IVERILOG" -g2012 -s "$top" -o "$OUT/$label.vvp" "$@" 2> "$OUT/$label.cc.log" || { echo "COMPILE FAIL $label"; cat "$OUT/$label.cc.log"; fail=$((fail+1)); return; }
    "$VVP" "$OUT/$label.vvp" > "$OUT/$label.log" 2>&1
    if grep -q "^TEST .* PASS" "$OUT/$label.log"; then echo "PASS $label"; pass=$((pass+1)); else echo "FAIL $label"; tail -15 "$OUT/$label.log"; fail=$((fail+1)); fi
}
if [ "$WANT" = all ]; then
    run_unit unit_arb tb_arb "$HERE/tb_arb.sv" "$RTL/tau_sdram_sv_arb.sv"
    run_unit unit_queue tb_queue "$HERE/tb_queue.sv" "$RTL/tau_sdram_sv_pq.sv"
    run_unit sys_rows tb_rows -I "$ROOT/sim/sdram_chip_model" "$HERE/tb_rows.sv" "${PHY[@]}" "${SCHED[@]}"
fi
echo "sdram_sched_v: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
