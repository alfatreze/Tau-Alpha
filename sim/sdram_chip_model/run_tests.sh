#!/usr/bin/env bash
# Run the SDRAM chip model tests (iverilog + vvp). MIT licence.
#   sim/sdram_chip_model/run_tests.sh            all tests (fast set)
#   sim/sdram_chip_model/run_tests.sh long       also the full 64 ms refresh run
#   sim/sdram_chip_model/run_tests.sh <name>     one test
# Each line of TESTS is:  name | iverilog -P overrides (top-level parameters of tb_sdram_chip_model) | optional label
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_CHIP_BUILD:-$ROOT/build/sdram_chip_model}"
IVERILOG="${IVERILOG:-iverilog}"
VVP="${VVP:-vvp}"
mkdir -p "$OUT"
T=tb_sdram_chip_model

TESTS=$(cat <<'EOF'
init_cl2 |
init_cl3 | CLSEL=3
legal_timing |
n_init_short_wait |
n_init_one_ref |
n_init_no_emrs |
n_init_mrs_value |
n_init_first_not_prea |
n_tmrd |
n_mrs_late |
n_cke_low |
n_trcd |
n_trp |
n_tras |
n_trc |
n_trrd |
n_trfc_act |
n_trfc_ref |
n_act_ref |
n_ref_open |
n_ref_trp |
n_col_closed |
n_act_open |
n_a10_col |
n_bst_noburst |
n_tras_max |
n_tref | TREFPS=100000000
n_setup | TCO_MAX=4500
n_hold | M_TCMH=7000
wr_burst |
wr_mask |
wr_interrupt |
wr_pre_legal |
n_tdpl |
n_tdal |
n_wrap_wr |
n_wrap_rd |
rd_counts | | rd_counts_cl2
rd_counts | CLSEL=3 | rd_counts_cl3
rd_dqm_mask | | rd_dqm_mask_cl2
rd_dqm_mask | CLSEL=3 | rd_dqm_mask_cl3
n_bst_after_bst |
EOF
)
LONGTESTS=$(cat <<'EOF'
EOF
)

WANT="${1:-all}"
pass=0; fail=0
run_one() {
    local name="$1" params="$2" label="${3:-$1}" args=()
    local vvpf="$OUT/$label.vvp"
    for p in $params; do args+=("-P$T.$p"); done
    if ! "$IVERILOG" -g2012 -I "$HERE" ${args[@]+"${args[@]}"} -s "$T" -o "$vvpf" "$ROOT/sim/sdram_chip_model.sv" "$HERE/$T.sv" 2> "$OUT/$label.cc.log"; then
        echo "COMPILE FAIL $label"; cat "$OUT/$label.cc.log"; fail=$((fail+1)); return
    fi
    "$VVP" "$vvpf" +TEST="$name" > "$OUT/$label.log" 2>&1
    if grep -q "^TEST $name PASS" "$OUT/$label.log"; then
        echo "PASS $label"; pass=$((pass+1))
    else
        echo "FAIL $label"; tail -15 "$OUT/$label.log"; fail=$((fail+1))
    fi
}
run_list() {
    while IFS='|' read -r name params label; do
        name="$(echo "$name" | xargs)"; params="$(echo "$params" | xargs)"; label="$(echo "$label" | xargs)"; [ -z "$label" ] && label="$name"
        [ -z "$name" ] && continue
        if [ "$WANT" = all ] || [ "$WANT" = long ] || [ "$WANT" = "$label" ]; then run_one "$name" "$params" "$label"; fi
    done
}
run_list <<< "$TESTS"
if [ "$WANT" = long ]; then run_list <<< "$LONGTESTS"; fi
echo "sdram_chip_model: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
