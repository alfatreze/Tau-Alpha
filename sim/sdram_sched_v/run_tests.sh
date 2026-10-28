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
basic | |
lengths | | lengths_cl2
lengths | CLSEL=3 LMAX_T=256 | lengths_cl3
lengths | W_LEAD=1 LMAX_T=200 | lengths_wlead1
lengths | W_LEAD=3 LMAX_T=200 | lengths_wlead3
splits | |
be | |
order | |
all_ports | | all_ports_cl2
all_ports | CLSEL=3 | all_ports_cl3
all_ports | BANK_HASH=0 | all_ports_hash0
all_ports | BANK_HASH=1 | all_ports_hash1
all_ports | BANK_HASH=3 | all_ports_hash3
all_ports | QDEPTH=2 WF_DEPTH=6 | all_ports_small
bad_req | |
refresh_load | |
refresh_urgent | |
refresh_urgent | SOFTD=8 | refresh_urgent_forced
urgent_bound | |
urgent_bound | REFIPS=1500000 | urgent_bound_refresh
aged_row | REFIPS=600000000 |
preempt | SOFTD=9 HARDD=9 |
batching | |
wrr | |
twod | |
reset_mid | |
chain | |
lookahead | BANK_HASH=0 |
raw_order | |
skew_regions | BANK_HASH=3 |
urgent_early | UECAP=32 |
bw_cap | BWCAP4=256 BWWIN=2000 |
EOF
)
MUTANTS=$(cat <<'EOF'
MUT_NO_PAGE_SPLIT | splits | |
MUT_BADREQ | bad_req | |
MUT_HASH_DROP | order | |
MUT_NOURG | urgent_bound | |
MUT_FLATW | wrr | |
MUT_NOCHAIN | chain | |
MUT_NOBST | basic | |
MUT_PRE_UNPROT | lookahead | BANK_HASH=0 |
MUT_NOAGE | aged_row | REFIPS=600000000 |
MUT_NOHARD | refresh_urgent | |
MUT_DONE_EARLY | order | | mut_done_early
MUT_RLAST | basic | | mut_rlast
MUT_BCLOSED | preempt | SOFTD=9 HARDD=9 |
MUT_BATCHUNL | batching | |
MUT_WLEAD_OFF | basic | | mut_wlead_off
MUT_2D_STRIDE | twod | |
MUT_NO_RAW | raw_order | |
MUT_SKEW_IGN | skew_regions | BANK_HASH=3 |
MUT_NOEARLY | urgent_early | UECAP=32 |
MUT_NOCAP | bw_cap | BWCAP4=256 BWWIN=2000 |
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
if [ "$WANT" = all ] || [ "$WANT" = tests ] || [ "$WANT" = lint ]; then
    if verilator --lint-only -Wall --top-module tau_sdram_sched_v "$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv" > "$OUT/lint.log" 2>&1; then
        echo "PASS lint (verilator -Wall)"; pass=$((pass+1)); else echo "FAIL lint"; head -20 "$OUT/lint.log"; fail=$((fail+1)); fi
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
        build_run "$label" "$name" "$params TMO_CYC=120000" "$def"; rc=$?
        if [ $rc -eq 1 ]; then echo "KILLED $label (test $name fails with -D$def)"; pass=$((pass+1));
        elif [ $rc -eq 0 ]; then echo "SURVIVED $label (test $name still passes with -D$def)"; fail=$((fail+1));
        else fail=$((fail+1)); fi
    fi
done <<< "$MUTANTS"
echo "sdram_sched_v: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
