#!/usr/bin/env bash
# Whole-path test and replay measurement of tau_sdram_sched_a with the real tau_gfx (tb_a_gfx). MIT licence.
#   sim/sdram_sched_a/run_gfx.sh [NCMD] [extra -P overrides of tb_a_gfx]
# Runs the reference stack (0), the NEW path (2; must match the reference) and the OLD controller path (1; OLD=0 skips it).
# Prints one RESULT line per stack. Minutes at NCMD 150.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${SDRAM_SCHED_A_BUILD:-$ROOT/build/sdram_sched_a}"
mkdir -p "$OUT"
RTL="$ROOT/src/fpga/core"
NCMD="${1:-150}"; shift || true
# the old controller as a build artefact (iverilog needs the packed typedef; nothing else changes), as sim/sdram_baseline does
sed 's/^  typedef struct {/  typedef struct packed {/' "$ROOT/src/fpga/rtl/mem/tau_sdram_ctrl.sv" > "$OUT/ctrl_old_build.sv"
SRC=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv"
     "$RTL/tau_sdram_sv_tm.sv" "$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv"
     "$RTL/tau_sdram_a_gfx_if.sv" "$RTL/tau_sdram_a_cpu.sv" "$RTL/tau_sdram_sched_a.sv"
     "$RTL/tau_sdram_cpu_bridge.sv" "$RTL/tau_sdram_arbiter.sv" "$OUT/ctrl_old_build.sv"
     "$RTL/tau_gfx.sv" "$RTL/tau_talos.sv" "$RTL/tau_scanout.sv" "$RTL/tau_cdc_gray_ctr.sv" "$RTL/tau_cdc_gray_bus.sv" "$RTL/font_rom.v" "$RTL/font_ram.v"
     "$HERE/tb_a_gfx.sv")
PLUS=""
run() {   # stack
    local s="$1"; shift
    iverilog -g2012 -DSIM -I "$ROOT/sim/sdram_chip_model" -I "$RTL" -Ptb_a_gfx.STACK=$s -Ptb_a_gfx.NCMD=$NCMD "$@" -s tb_a_gfx -o "$OUT/gfx_$s.vvp" "${SRC[@]}" 2> "$OUT/gfx_$s.cc.log" \
        || { echo "COMPILE FAIL stack $s"; head -20 "$OUT/gfx_$s.cc.log"; return 2; }
    vvp "$OUT/gfx_$s.vvp" $PLUS ${PLUSEXTRA:-} > "$OUT/gfx_$s.log" 2>&1
    grep -E "^(RESULT|IDLE|TEST|  FAIL|  note)" "$OUT/gfx_$s.log"
}
r0=$(run 0 "$@"); echo "$r0"
crc=$(echo "$r0" | sed -n 's/.*crc=\([0-9a-f]*\).*/\1/p' | head -1)
fen=$(echo "$r0" | sed -n 's/.*fences=\([0-9]*\).*/\1/p' | head -1)
PLUS="+REFCRC=$crc +REFFENCE=$fen"
if [ "${NEW:-1}" = 1 ]; then run 2 "$@"; fi
if [ "${OLD:-1}" = 1 ]; then run 1 "$@"; fi
