#!/usr/bin/env bash
# Equivalence of the flat (B-916) PHY core with the reference core of b1e3ddc5 under random stimulus. MIT licence.
#   sim/sdram_phy/run_equiv.sh [define ...]    (a MUT_* define is passed to the NEW core only: the run must FAIL then)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_PHY_BUILD:-$ROOT/build/sdram_phy}"
mkdir -p "$OUT"
RTL="$ROOT/src/fpga/core"
pass=0; fail=0
CASES=(
  "SEED=1"
  "SEED=2 CLSEL=3"
  "SEED=3 CAPMODE=1 RDEX=0"
  "SEED=4 CALIB=1"
  "SEED=5 BISTON=1 DEBT=8"
  "SEED=6 PERFON=1"
  "SEED=7 CALIB=1 BISTON=1 PERFON=1 CLSEL=3 DEBT=6"
  "SEED=8 DEBT=15 REFIPS=2000000"
  "SEED=9 REFIPS=7800000 DEBT=8"
)
for c in "${CASES[@]}"; do
    args=(); for p in $c; do args+=("-Ptb_equiv.$p"); done
    dargs=(); for d in "$@"; do dargs+=("-D$d"); done
    vvp_f="$OUT/equiv.vvp"
    iverilog -g2012 ${dargs[@]+"${dargs[@]}"} "${args[@]}" -s tb_equiv -o "$vvp_f" "$HERE/tb_equiv.sv" "$HERE/ref/tau_sdram_phy_core_ref.sv" \
        "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_dq_cap_dual.sv" 2> "$OUT/equiv.cc.log" || { echo "COMPILE FAIL $c"; fail=$((fail+1)); continue; }
    res=$(vvp "$vvp_f" | grep -E "EQUIV (PASS|FAIL)")
    echo "[$c] $res"
    case "$res" in *PASS*) pass=$((pass+1));; *) fail=$((fail+1));; esac
done
echo "equiv: $pass pass, $fail fail"
[ "$fail" -eq 0 ]
