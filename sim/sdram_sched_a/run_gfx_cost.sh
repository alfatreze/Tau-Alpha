#!/usr/bin/env bash
# ORACLE-SIDE (audit B-1181): per-command cycles of the shipped engine on the realistic memory path (tb_a_gfx_cost).
#   sim/sdram_sched_a/run_gfx_cost.sh [CPU_HAMMER 0|1] [REPS]
# Never hand this or its output to the clean-room author. MIT licence.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_SCHED_A_BUILD:-$ROOT/build/sdram_sched_a}"
mkdir -p "$OUT"
RTL="$ROOT/src/fpga/core"
HAM="${1:-0}"; REPS="${2:-9}"
SRC=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv"
     "$RTL/tau_sdram_sv_tm.sv" "$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv"
     "$RTL/tau_sdram_a_gfx_if.sv" "$RTL/tau_sdram_a_cpu.sv" "$RTL/tau_sdram_sched_a.sv"
     "$RTL/tau_gfx.sv" "$RTL/tau_talos.sv" "$RTL/tau_scanout.sv" "$RTL/tau_cdc_gray_ctr.sv" "$RTL/tau_cdc_gray_bus.sv" "$RTL/font_rom.v" "$RTL/font_ram.v"
     "$HERE/tb_a_gfx_cost.sv")
iverilog -g2012 -DSIM -I "$ROOT/sim/sdram_chip_model" -I "$RTL" -Ptb_a_gfx_cost.CPU_HAMMER=$HAM -Ptb_a_gfx_cost.REPS=$REPS -s tb_a_gfx_cost \
    -o "$OUT/gfx_cost_$HAM.vvp" "${SRC[@]}" 2> "$OUT/gfx_cost_$HAM.cc.log" || { echo "COMPILE FAIL"; head -20 "$OUT/gfx_cost_$HAM.cc.log"; exit 2; }
vvp -n "$OUT/gfx_cost_$HAM.vvp" | grep -E "^(COST|DONE|TIMEOUT)"
