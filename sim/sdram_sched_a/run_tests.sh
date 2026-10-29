#!/usr/bin/env bash
# Run the tau_sdram_sched_a tests (iverilog + vvp). MIT licence.
#   sim/sdram_sched_a/run_tests.sh            unit/system tests (tb_a_unit) and the mutation tests
#   sim/sdram_sched_a/run_tests.sh tests      the tests only
#   sim/sdram_sched_a/run_tests.sh mutants    the mutation tests only
#   sim/sdram_sched_a/run_tests.sh <label>    one test
# Each line of TESTS is:   test name | -P overrides of tb_a_unit | label
# Each line of MUTANTS is: define | test name | -P overrides | label ; the test must FAIL with the mutant compiled in.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_SCHED_A_BUILD:-$ROOT/build/sdram_sched_a}"
IVERILOG="${IVERILOG:-iverilog}"
VVP="${VVP:-vvp}"
mkdir -p "$OUT"
RTL="$ROOT/src/fpga/core"
T=tb_a_unit
SRC=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv"
     "$RTL/tau_sdram_sv_tm.sv" "$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv"
     "$RTL/tau_sdram_wb_adapter.sv" "$RTL/tau_sdram_bridge_mux.sv" "$RTL/tau_sdram_a_gfx_if.sv" "$RTL/tau_sdram_a_cpu.sv" "$RTL/tau_sdram_sched_a.sv" "$HERE/$T.sv")
WANT="${1:-all}"
pass=0; fail=0

TESTS=$(cat <<'EOF'
cpu | | cpu
engine | | engine
engine | QD=2 | engine_qd2
readsizes | | readsizes
mixed | | mixed
mixed | REFIPS=3900000 | mixed_refresh_half
urgent | | urgent
urgent | REFIPS=3900000 | urgent_refresh_half
refresh | REFIPS=1950000 | refresh_quarter
idle | | idle
wb | | wishbone
latency | | latency
EOF
)
MUTANTS=$(cat <<'EOF'
MUT_A_WLEAD | engine | | mut_wlead
MUT_A_NOFILLPORT | urgent | | mut_nofillport
MUT_A_CPUBE | cpu | | mut_cpube
MUT_A_RDLEN | readsizes | | mut_rdlen
MUT_A_AVAIL | idle | | mut_avail
EOF
)

build_run() {   # label test params defines
    local label="$1" name="$2" params="$3" defs="$4" args=() dargs=()
    for p in $params; do args+=("-P$T.$p"); done
    for d in $defs; do dargs+=("-D$d"); done
    if ! "$IVERILOG" -g2012 -I "$ROOT/sim/sdram_chip_model" ${dargs[@]+"${dargs[@]}"} ${args[@]+"${args[@]}"} -s "$T" -o "$OUT/$label.vvp" "${SRC[@]}" 2> "$OUT/$label.cc.log"; then
        echo "COMPILE FAIL $label"; cat "$OUT/$label.cc.log"; return 2
    fi
    "$VVP" "$OUT/$label.vvp" +TEST="$name" > "$OUT/$label.log" 2>&1
    if grep -q "^TEST $name PASS" "$OUT/$label.log"; then return 0; else return 1; fi
}

if [ "$WANT" = all ] || [ "$WANT" = tests ] || echo "$TESTS" | grep -q "| $WANT\$"; then
    while IFS='|' read -r name params label; do
        name="$(echo "$name" | xargs)"; label="$(echo "$label" | xargs)"; [ -z "$name" ] && continue
        if [ "$WANT" != all ] && [ "$WANT" != tests ] && [ "$WANT" != "$label" ]; then continue; fi
        build_run "$label" "$name" "$params" ""; rc=$?
        if [ $rc -eq 0 ]; then echo "PASS $label"; pass=$((pass+1)); grep -E "^  (CPU|fills|engine|refreshes|counters)" "$OUT/$label.log" | head -6
        else echo "FAIL $label"; tail -12 "$OUT/$label.log"; fail=$((fail+1)); fi
    done <<< "$TESTS"
fi
if [ "$WANT" = all ] || [ "$WANT" = mutants ]; then
    while IFS='|' read -r def name params label; do
        def="$(echo "$def" | xargs)"; name="$(echo "$name" | xargs)"; label="$(echo "$label" | xargs)"; [ -z "$def" ] && continue
        build_run "$label" "$name" "$params" "$def"; rc=$?
        if [ $rc -eq 1 ]; then echo "KILLED $def"; pass=$((pass+1)); else echo "SURVIVED $def (rc $rc)"; fail=$((fail+1)); fi
    done <<< "$MUTANTS"
fi
echo "sched_a tests: $pass ok, $fail failed"
[ $fail -eq 0 ]
