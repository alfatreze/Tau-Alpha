#!/usr/bin/env bash
# Run the SDRAM PHY tests (iverilog + vvp) against the chip model. MIT licence.
#   sim/sdram_phy/run_tests.sh            all tests (fast set)
#   sim/sdram_phy/run_tests.sh mutants    the mutation tests only
#   sim/sdram_phy/run_tests.sh <label>    one test
# Each line of TESTS is:  name | iverilog -P overrides (parameters of tb_sdram_phy) | label
# Each line of MUTANTS is: mutant define | test name | -P overrides | label ; the test must FAIL with the mutant compiled in.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_PHY_BUILD:-$ROOT/build/sdram_phy}"
IVERILOG="${IVERILOG:-iverilog}"
VVP="${VVP:-vvp}"
mkdir -p "$OUT"
T=tb_sdram_phy
RTL="$ROOT/src/fpga/core"
SRC=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv" "$HERE/$T.sv")

TESTS=$(cat <<'EOF'
init | INITCYC=10000 | init_cl2_real_wait
init | CLSEL=3 INITCYC=10000 | init_cl3_real_wait
init | | init_short
init | TOP=1 | init_top
EOF
)
MUTANTS=$(cat <<'EOF'
EOF
)

WANT="${1:-all}"
pass=0; fail=0
build_run() {   # $1 label  $2 test  $3 params  $4 defines(space separated -D list)
    local label="$1" name="$2" params="$3" defs="$4" args=() dargs=()
    local vvpf="$OUT/$label.vvp"
    for p in $params; do args+=("-P$T.$p"); done
    for d in $defs; do dargs+=("-D$d"); done
    if ! "$IVERILOG" -g2012 -I "$ROOT/sim/sdram_chip_model" ${dargs[@]+"${dargs[@]}"} ${args[@]+"${args[@]}"} -s "$T" -o "$vvpf" "${SRC[@]}" 2> "$OUT/$label.cc.log"; then
        echo "COMPILE FAIL $label"; cat "$OUT/$label.cc.log"; return 2
    fi
    "$VVP" "$vvpf" +TEST="$name" > "$OUT/$label.log" 2>&1
    if grep -q "^TEST $name PASS" "$OUT/$label.log"; then return 0; else return 1; fi
}
while IFS='|' read -r name params label; do
    name="$(echo "$name" | xargs)"; params="$(echo "$params" | xargs)"; label="$(echo "$label" | xargs)"; [ -z "$label" ] && label="$name"
    [ -z "$name" ] && continue
    if [ "$WANT" = all ] || [ "$WANT" = "$label" ]; then
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
echo "sdram_phy: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
