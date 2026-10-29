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
rw_data | | rw_data_cl2
rw_data | CLSEL=3 | rw_data_cl3
rw_data_rise | CAPMODE=0 TAC2=4000 | rw_data_rise_cl2_tac4
n_cap_rise_worst | CAPMODE=0
cap_delay_ok | DMAX=1500
rw_data_rise_real | CAPMODE=0 CLKMIN=3200 CLKMAX=3600 DQMIN=2300 DQMAX=2700 | rw_data_rise_real_cl2
rw_data_rise_real | CAPMODE=0 CLSEL=3 CLKMIN=3200 CLKMAX=3600 DQMIN=2300 DQMAX=2700 | rw_data_rise_real_cl3
n_cap_fall_real | CLKMIN=3200 CLKMAX=3600 DQMIN=2300 DQMAX=2700 | n_cap_fall_real_cl2
n_cap_fall_real | CLSEL=3 CLKMIN=3200 CLKMAX=3600 DQMIN=2300 DQMAX=2700 | n_cap_fall_real_cl3
n_cap_rise_real_late | CAPMODE=0 DMIN=3000 DMAX=3000 CLKMIN=3200 CLKMAX=3600 DQMIN=2300 DQMAX=2700 | n_cap_rise_real_late
n_cap_delay_late | DMAX=2500
n_cap_skew_hold | CAPSKEW=2000
wr_rd_turn | | wr_rd_turn_cl2
wr_rd_turn | CLSEL=3 | wr_rd_turn_cl3
turn_rw | | turn_rw_cl2
turn_rw | CLSEL=3 | turn_rw_cl3
turn_rw_tight | | turn_rw_tight_cl2
turn_rw_tight | CLSEL=3 | turn_rw_tight_cl3
n_turn_rw_early | | n_turn_rw_early_cl2
n_turn_rw_early | CLSEL=3 | n_turn_rw_early_cl3
n_turn_cl3_wrterm | CLSEL=3
n_oe_during_read |
multibank | | multibank_cl2
multibank | CLSEL=3 | multibank_cl3
legal_ref_burst |
n_wdata_x |
n_tccd | TCCD=2
n_tck | P_PS=8000
EOF
)
LONGTESTS=$(cat <<'EOF'
legal_tref_full |
n_tref_full |
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
run_list() {   # $1 = list text is read from stdin; $2 = fast|long
    while IFS='|' read -r name params label; do
        name="$(echo "$name" | xargs)"; params="$(echo "$params" | xargs)"; label="$(echo "$label" | xargs)"; [ -z "$label" ] && label="$name"
        [ -z "$name" ] && continue
        if [ "$WANT" = "$label" ] || [ "$WANT" = "$2" ] || { [ "$WANT" = all ] && [ "$2" = fast ]; }; then run_one "$name" "$params" "$label"; fi
    done
}
run_list fast fast <<< "$TESTS"
run_list long long <<< "$LONGTESTS"
echo "sdram_chip_model: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
