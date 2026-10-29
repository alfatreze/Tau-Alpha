#!/usr/bin/env bash
# Run the SDRAM PHY tests (iverilog + vvp) against the chip model. MIT licence.
#   sim/sdram_phy/run_tests.sh            all tests (fast set)
#   sim/sdram_phy/run_tests.sh mutants    the mutation tests only
#   sim/sdram_phy/run_tests.sh long       the 66 ms no-scheduler refresh run (minutes)
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
SRC=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_dq_cap_dual.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv" "$HERE/$T.sv")

TESTS=$(cat <<'EOF'
init | INITCYC=10000 | init_cl2_real_wait
init | CLSEL=3 INITCYC=10000 | init_cl3_real_wait
init | | init_short
init | TOP=1 | init_top
legal_min | | legal_min_cl2
legal_min | CLSEL=3 | legal_min_cl3
silent_bst | |
n_act_open | |
n_rd_closed | |
n_wr_closed | |
n_a10_rd | |
n_a10_wr | |
n_trcd | |
n_tras | |
n_trp | |
n_trc | TRC_X=80000 |
n_trrd | |
n_ref_open | |
n_trfc_act | |
n_trfc_ref | |
n_wr_in_read | |
legal_rd_ref | |
n_turn | | n_turn_cl2
n_turn | CLSEL=3 | n_turn_cl3
n_tdpl | |
n_prea_young | |
n_pre_closed | |
n_init_offer | |
n_wrap_rd | |
n_wrap_wr | |
fault_sticky | |
rd_data | | rd_data_cl2_fall
rd_data | CLSEL=3 | rd_data_cl3_fall
rd_data | RDEX=1 | rd_data_cl2_fall_x1
rd_data | RDEX=2 | rd_data_cl2_fall_x2
rd_data | CLSEL=3 RDEX=2 | rd_data_cl3_fall_x2
rd_data | CAPMODE=0 TAC2=4000 | rd_data_cl2_rise
rd_data | CAPMODE=0 TAC2=4000 RDEX=1 | rd_data_cl2_rise_x1
rd_data | CAPMODE=0 TAC2=4000 RDEX=2 | rd_data_cl2_rise_x2
rd_data | CLSEL=3 CAPMODE=0 TAC3=4000 | rd_data_cl3_rise
rd_data | DMAX=1500 | rd_data_cl2_fall_board
rd_data | TOP=1 | rd_data_top
n_rise_worst | CAPMODE=0 |
rd_chain | | rd_chain_cl2
rd_chain | CLSEL=3 | rd_chain_cl3
wr_gap | |
refresh_debt | |
preempt_timeout | DEBTMAX=3 REFIPS=200000 |
preempt_ack | DEBTMAX=3 REFIPS=200000 |
preempt_wburst | DEBTMAX=3 REFIPS=200000 |
preempt_young_row | DEBTMAX=3 REFIPS=200000 |
aged_row | REFIPS=1000000000 |
aged_row_sched | REFIPS=1000000000 |
reset_rburst | | reset_rburst_cl2
reset_rburst | CLSEL=3 | reset_rburst_cl3
reset_wburst | |
turn_rw | | turn_rw_cl2
turn_rw | CLSEL=3 | turn_rw_cl3
wr_rd_turn | | wr_rd_turn_cl2
wr_rd_turn | CLSEL=3 | wr_rd_turn_cl3
n_wr_in_read | CLSEL=3 | n_wr_in_read_cl3
cal | CALIB=1 EXPW=7 EXPB=1 | cal_some_cl2
cal | CALIB=1 CLSEL=3 TAC3=8000 EXPW=7 EXPB=1 | cal_some_cl3
cal | CALIB=1 TAC2=4000 EXPW=63 EXPB=1 | cal_all
cal | CALIB=1 DMIN=2500 DMAX=2500 EXPW=0 EXPB=0 | cal_none
cal | CALIB=1 TOP=1 EXPW=7 EXPB=1 | cal_top
cal_reset | CALIB=1 EXPW=7 EXPB=1 |
rand | CALIB=1 | rand_cal
perf_hist | PERFON=1 |
perf_turn | PERFON=1 |
perf_rand | PERFON=1 NRAND=6000 | perf_rand_cl2
perf_rand | PERFON=1 NRAND=6000 CLSEL=3 | perf_rand_cl3
bist_clean | BISTON=1 BLO=0 BHI=1 MEMC=1 |
bist_clean | BISTON=1 BLO=0 BHI=1 MEMC=1 CLSEL=3 | bist_clean_cl3
bist_stuck0 | BISTON=1 BLO=0 BHI=1 MEMC=1 |
bist_stuck1 | BISTON=1 BLO=0 BHI=1 MEMC=1 |
bist_stuck_p1 | BISTON=1 BLO=0 BHI=1 MEMC=1 |
bist_wait | BISTON=1 BLO=0 BHI=1 MEMC=1 |
bist_waitquiet | BISTON=1 BLO=0 BHI=1 MEMC=1 |
rand | | rand_cl2
rand | CLSEL=3 | rand_cl3
rand | RDEX=1 CLSEL=3 | rand_cl3_x1
rand | CAPMODE=0 TAC2=4000 | rand_cl2_rise
rand | CLSEL=3 NRAND=40000 | rand_long_cl3
rand | NRAND=40000 CAPMODE=0 TAC2=4000 RDEX=2 | rand_long_cl2_rise_x2
EOF
)
LONGTESTS=$(cat <<'EOF'
tref_idle | TREFPS=64000000000 | tref_idle
EOF
)
MUTANTS=$(cat <<'EOF'
MUT_A10 | n_a10_rd | |
MUT_TRCD | n_trcd | |
MUT_TRAS | n_tras | |
MUT_TRP | n_trp | |
MUT_TRC | n_trc | TRC_X=80000 |
MUT_TRRD | n_trrd | |
MUT_TRFC | n_trfc_act | |
MUT_ACT_OPEN | n_act_open | |
MUT_COL_CLOSED | n_rd_closed | |
MUT_REF_OPEN | n_ref_open | |
MUT_TDPL | n_tdpl | |
MUT_TURN | n_turn | |
MUT_WR_IN_READ | n_wr_in_read | |
MUT_NOWRAP | n_wrap_rd | |
MUT_DQM | wr_gap | |
MUT_OE_EARLY | rd_data | |
MUT_DEBT | refresh_debt | |
MUT_NOPREEMPT | preempt_timeout | DEBTMAX=3 REFIPS=200000 |
MUT_NOPULSE | preempt_timeout | DEBTMAX=3 REFIPS=200000 |
MUT_PREA_EARLY | preempt_young_row | DEBTMAX=3 REFIPS=200000 |
MUT_NOAGE | aged_row | REFIPS=1000000000 |
MUT_PERF_ACT | perf_hist | PERFON=1 |
MUT_PERF_BIN | perf_hist | PERFON=1 |
MUT_PERF_HITSWAP | perf_turn | PERFON=1 |
MUT_PERF_TURN | perf_turn | PERFON=1 |
MUT_PERF_IDLE | perf_rand | PERFON=1 NRAND=6000 |
MUT_BIST_NOINV | bist_stuck0 | BISTON=1 BLO=0 BHI=1 MEMC=1 |
MUT_BIST_NOCHK | bist_stuck0 | BISTON=1 BLO=0 BHI=1 MEMC=1 |
MUT_BIST_RANGE | bist_stuck_p1 | BISTON=1 BLO=0 BHI=1 MEMC=1 |
MUT_BIST_NOWAIT | bist_wait | BISTON=1 BLO=0 BHI=1 MEMC=1 |
MUT_BIST_NOQUIET | bist_waitquiet | BISTON=1 BLO=0 BHI=1 MEMC=1 |
MUT_CAL_NOCMP | cal | CALIB=1 EXPW=7 EXPB=1 |
MUT_CAL_BEST | cal | CALIB=1 EXPW=7 EXPB=1 |
MUT_CAL_NOSEL | cal | CALIB=1 EXPW=7 EXPB=1 |
MUT_CAL_NOFAULT | cal | CALIB=1 DMIN=2500 DMAX=2500 EXPW=0 EXPB=0 |
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
while IFS='|' read -r name params label; do
    name="$(echo "$name" | xargs)"; params="$(echo "$params" | xargs)"; label="$(echo "$label" | xargs)"; [ -z "$label" ] && label="$name"
    [ -z "$name" ] && continue
    if [ "$WANT" = long ] || [ "$WANT" = "$label" ]; then
        build_run "$label" "$name" "$params" ""; rc=$?
        if [ $rc -eq 0 ]; then echo "PASS $label"; pass=$((pass+1)); else echo "FAIL $label"; tail -15 "$OUT/$label.log"; fail=$((fail+1)); fi
    fi
done <<< "$LONGTESTS"
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
