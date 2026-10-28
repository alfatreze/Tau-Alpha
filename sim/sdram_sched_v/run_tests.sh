#!/usr/bin/env bash
# Run the Tau V SDRAM scheduler tests (iverilog + vvp). MIT licence.
#   sim/sdram_sched_v/run_tests.sh            unit tests and all system tests (fast set), then the mutation tests
#   sim/sdram_sched_v/run_tests.sh tests      unit and system tests only
#   sim/sdram_sched_v/run_tests.sh mutants    the mutation tests only
#   sim/sdram_sched_v/run_tests.sh <label>    one test
# Each line of TESTS is:   test name | iverilog -P overrides (parameters of tb_sched) | label
# Each line of MUTANTS is: define | test name | -P overrides | label ; the test must FAIL with the mutant compiled in.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_SCHED_BUILD:-$ROOT/build/sdram_sched_v}"
IVERILOG="${IVERILOG:-iverilog}"
VVP="${VVP:-vvp}"
mkdir -p "$OUT"
RTL="$ROOT/src/fpga/core"
T=tb_sched
SRC=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv"
     "$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv" "$HERE/$T.sv")
WANT="${1:-all}"
pass=0; fail=0

TESTS=$(cat <<'EOF'
basic | REFIPS=600000000 |
lengths | REFIPS=600000000 | lengths_cl2
splits | REFIPS=600000000 |
be | REFIPS=600000000 |
order | REFIPS=600000000 |
all_ports | REFIPS=600000000 | all_ports_cl2
EOF
)
MUTANTS=$(cat <<'EOF'
EOF
)

run_unit() {   # label top sources...
    local label="$1" top="$2"; shift 2
    "$IVERILOG" -g2012 -s "$top" -o "$OUT/$label.vvp" "$@" 2> "$OUT/$label.cc.log" || { echo "COMPILE FAIL $label"; cat "$OUT/$label.cc.log"; fail=$((fail+1)); return; }
    "$VVP" "$OUT/$label.vvp" > "$OUT/$label.log" 2>&1
    if grep -q "^TEST .* PASS" "$OUT/$label.log"; then echo "PASS $label"; pass=$((pass+1)); else echo "FAIL $label"; tail -15 "$OUT/$label.log"; fail=$((fail+1)); fi
}
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

if [ "$WANT" = all ] || [ "$WANT" = tests ]; then
    run_unit unit_arb tb_arb "$HERE/tb_arb.sv" "$RTL/tau_sdram_sv_arb.sv"
    run_unit unit_queue tb_queue "$HERE/tb_queue.sv" "$RTL/tau_sdram_sv_pq.sv"
fi
while IFS='|' read -r name params label; do
    name="$(echo "$name" | xargs)"; params="$(echo "$params" | xargs)"; label="$(echo "$label" | xargs)"; [ -z "$label" ] && label="$name"
    [ -z "$name" ] && continue
    if [ "$WANT" = all ] || [ "$WANT" = tests ] || [ "$WANT" = "$label" ]; then
        build_run "$label" "$name" "$params" ""; rc=$?
        if [ $rc -eq 0 ]; then echo "PASS $label"; pass=$((pass+1)); else echo "FAIL $label"; tail -15 "$OUT/$label.log"; fail=$((fail+1)); fi
    fi
done <<< "$TESTS"
while IFS='|' read -r def name params label; do
    def="$(echo "$def" | xargs)"; name="$(echo "$name" | xargs)"; params="$(echo "$params" | xargs)"; label="$(echo "$label" | xargs)"
    [ -z "$def" ] && continue
    [ -z "$label" ] && label="mut_$def"
    if [ "$WANT" = all ] || [ "$WANT" = mutants ] || [ "$WANT" = "$label" ]; then
        build_run "$label" "$name" "$params" "$def"; rc=$?
        if [ $rc -eq 1 ]; then echo "KILLED $label (test $name fails with -D$def)"; pass=$((pass+1));
        elif [ $rc -eq 0 ]; then echo "SURVIVED $label (test $name still passes with -D$def)"; fail=$((fail+1));
        else fail=$((fail+1)); fi
    fi
done <<< "$MUTANTS"
echo "sdram_sched_v: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
