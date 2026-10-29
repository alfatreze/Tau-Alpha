#!/usr/bin/env bash
# Replay with the idle-bus classification monitor. MIT licence.
#   sim/sdram_sched_v/run_idle.sh TAG TRACE [plusargs...]      e.g.  run_idle.sh base allbi +norelease +skip_pictures=1 +limit_pictures=1
# Output: build/idle/TAG_TRACE.log (PIC / RESULT / IDLE lines). Extra iverilog arguments via IV_ARGS.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TAG="$1"; TRACE="$2"; shift 2
OUT="$ROOT/build/idle"; mkdir -p "$OUT"
RTL="$ROOT/src/fpga/core"
DATA="$ROOT/work/tauv_treq"; [ -d "$DATA" ] || DATA="$(git -C "$ROOT" worktree list --porcelain | awk '/^worktree /{print substr($0,10); exit}')/work/tauv_treq"
case "$TRACE" in clip) TREQ="$DATA/tnv3_tears_q30_48pic.treq";; *) TREQ="$DATA/worst/$TRACE.treq";; esac
BIN="$OUT/$TAG.vvp"
if [ ! -f "$BIN" ] || [ -n "$(find "$RTL" "$HERE/idle_mon.sv" "$ROOT/sim/sdram_replay/tb_replay.sv" -newer "$BIN" 2>/dev/null)" ]; then
    iverilog -g2012 -I "$ROOT/sim/sdram_chip_model" ${IV_ARGS:-} -s tb_replay -s idle_mon -o "$BIN" "$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv" \
        "$RTL/tau_sdram_sv_tm.sv" "$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv" "$ROOT/sim/sdram_replay/tb_replay.sv" "$HERE/idle_mon.sv" 2> "$OUT/$TAG.cc.log" || { cat "$OUT/$TAG.cc.log"; exit 2; }
fi
vvp "$BIN" "+treq=$TREQ" "+scan=$DATA/scan_800x720_1frame.req" "$@" > "$OUT/${TAG}_${TRACE}.log" 2>&1
grep "^PIC\|^IDLE\|VERDICT" "$OUT/${TAG}_${TRACE}.log" | cut -c1-200
