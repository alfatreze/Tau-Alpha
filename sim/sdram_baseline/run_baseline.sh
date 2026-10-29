#!/usr/bin/env bash
# Old SDRAM controller through the new chip model. MIT licence. iverilog + vvp, run from anywhere.
#
#   run_baseline.sh run  CTRL SCEN [extra -P overrides]   one run; CTRL = pre (before S0: tRFC 80 ns) | s0 (the tree: tRFC 98 ns)
#   run_baseline.sh matrix                                CTRL {pre,s0} x SCEN {nominal,unp_late,unp_early,pk_late,pk_early}, 8 ms each, in parallel
#   run_baseline.sh raw                                   as-is: no EMRS waiver, registers X (what the model says about the init)
#   run_baseline.sh sweep                                 short runs over the command delay and the chip-clock delay
#   run_baseline.sh long                                  64 ms mixed traffic (both controllers) and 110 ms write-heavy traffic (minutes; run in the background)
#   run_baseline.sh summary                               print the class x run matrices of build/sdram_baseline/*.json
# Scenario parameters (ps) are in the case statement below; see README.md for the reasoning.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_BASELINE_BUILD:-$ROOT/build/sdram_baseline}"
mkdir -p "$OUT"
T=tb_sdram_baseline

# The controller as a build artefact. iverilog does not support the unpacked struct in the controller (the Verilator benches do),
# so a 'packed' keyword is inserted in the build copy; nothing else changes. The "pre" copy is the file before the S0 commit.
prepare() {
    [ "${PREPARED:-0}" = 1 ] && return
    SRC="$ROOT/src/fpga/rtl/mem/tau_sdram_ctrl.sv"
    sed 's/^  typedef struct {/  typedef struct packed {/' "$SRC" > "$OUT/ctrl_s0.sv"
    if [ ! -s "$OUT/ctrl_pre.sv" ]; then
        git -C "$ROOT" show cafd7477^:src/fpga/rtl/mem/tau_sdram_ctrl.sv | sed 's/^  typedef struct {/  typedef struct packed {/' > "$OUT/ctrl_pre.sv"
    fi
}

scen_params() {   # prints -P overrides for a scenario
    local p="-P$T"
    case "$1" in
        # functional baseline: the chip clock 3.5 ns after the controller clock puts the rising-edge capture inside the tAC 8 ns window
        nominal)   echo "$p.CLK_DLY=3500 $p.CMD_DLY=1000 $p.DQ_DLY=1000 $p.OE_MIN=1000 $p.OE_MAX=1000 $p.DMIN=0 $p.DMAX=0" ;;
        # old design, no I/O register packing: fabric register to pin 3.0 .. 8.0 ns (assumption), DDIO clock 1.5 .. 3.5 ns, board 0.2 .. 0.6 ns
        unp_late)  echo "$p.CLK_DLY=1500 $p.CMD_DLY=8000 $p.DQ_DLY=8000 $p.OE_MIN=3000 $p.OE_MAX=8000 $p.DMIN=200 $p.DMAX=600" ;;
        unp_early) echo "$p.CLK_DLY=3500 $p.CMD_DLY=3000 $p.DQ_DLY=3000 $p.OE_MIN=3000 $p.OE_MAX=8000 $p.DMIN=200 $p.DMAX=600" ;;
        # packed outputs, the S0 hardware (registers in the I/O cells): 1.5 .. 3.5 ns, as in the chip-model findings
        pk_late)   echo "$p.CLK_DLY=1500 $p.CMD_DLY=3500 $p.DQ_DLY=3500 $p.OE_MIN=1500 $p.OE_MAX=3500 $p.DMIN=200 $p.DMAX=600" ;;
        pk_early)  echo "$p.CLK_DLY=3500 $p.CMD_DLY=1500 $p.DQ_DLY=1500 $p.OE_MIN=1500 $p.OE_MAX=3500 $p.DMIN=200 $p.DMAX=600" ;;
        *) echo "unknown scenario $1" >&2; return 1 ;;
    esac
}

run_one() {       # run_one LABEL CTRL OVERRIDES...
    local label="$1" ctrl="$2"; shift 2
    prepare
    iverilog -g2012 -DSIM -o "$OUT/$label.vvp" "$@" -I"$ROOT/sim/sdram_chip_model" \
        "$ROOT/sim/sdram_chip_model.sv" "$OUT/ctrl_$ctrl.sv" "$HERE/tb_sdram_baseline.sv" 2>&1 | grep -v "^$" | head -5
    vvp "$OUT/$label.vvp" | python3 "$HERE/aggregate.py" parse "$OUT/$label.json" "$OUT/$label.log"
    echo "== $label"; grep -E "^\[BENCH\] (traffic|refresh)|^\[CHIPMODEL\] (capture|errors)" "$OUT/$label.log"
}

case "${1:-}" in
    run)
        ctrl="$2"; scen="$3"; shift 3
        run_one "${LABEL:-${ctrl}_${scen}}" "$ctrl" $(scen_params "$scen") "$@" ;;
    matrix)
        prepare; export PREPARED=1
        for ctrl in pre s0; do for scen in nominal unp_late unp_early pk_late pk_early; do
            "$0" run "$ctrl" "$scen" > "$OUT/${ctrl}_${scen}.stdout" 2>&1 &
        done; done
        wait; cat "$OUT"/*_*.stdout ;;
    raw)
        prepare; export PREPARED=1
        for ctrl in pre s0; do
            LABEL=${ctrl}_raw "$0" run "$ctrl" nominal -P$T.EMRS_WAIVE=0 -P$T.INIT_REGS=0 -P$T.SHORT=1 > /dev/null 2>&1
        done ;;
    sweep)
        prepare; export PREPARED=1
        for cmd in 1000 2000 3000 4000 5000 6000 6500 7000 7500 8000 9000; do   # command / address / data output delay, chip clock delay fixed at 3.5 ns
            LABEL=sweep_cmd_$cmd "$0" run s0 nominal -P$T.SHORT=1 -P$T.CLK_DLY=3500 -P$T.CMD_DLY=$cmd -P$T.DQ_DLY=$cmd -P$T.OE_MIN=$cmd -P$T.OE_MAX=$cmd > /dev/null 2>&1
        done
        for clkd in 0 1000 2000 2500 3000 3500 4000 4500 5000 6000; do      # chip clock delay (capture window), output delays 1 ns
            LABEL=sweep_clk_$clkd "$0" run s0 nominal -P$T.SHORT=1 -P$T.CLK_DLY=$clkd -P$T.DMIN=200 -P$T.DMAX=600 > /dev/null 2>&1
        done ;;
    long)
        prepare; export PREPARED=1
        LABEL=long_mixed_s0 "$0" run s0 nominal -P$T.RUN_US=64000 > "$OUT/long_mixed.stdout" 2>&1 &
        LABEL=long_mixed_pre "$0" run pre unp_early -P$T.RUN_US=64000 > "$OUT/long_mixed_pre.stdout" 2>&1 &
        LABEL=long_wh_s0 "$0" run s0 nominal -P$T.RUN_US=110000 -P$T.WRITE_HEAVY=1 > "$OUT/long_wh.stdout" 2>&1 &
        wait; cat "$OUT"/long_*.stdout ;;
    summary)
        python3 "$HERE/aggregate.py" table "$OUT"/pre_*.json "$OUT"/s0_*.json ;;
    *) sed -n 2,12p "$0"; exit 2 ;;
esac
