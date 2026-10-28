#!/usr/bin/env bash
# Build and run the SDRAM replay harness (iverilog + vvp). MIT licence.
#   sim/sdram_replay/run_replay.sh [--stub] [-P NAME=VAL ...] [-D DEFINE ...] TARGET [plusargs...]
# TARGET is clip | allbi | static | scenechange | pan | random | a path to a .treq file.
# --stub        replace PHY + sched_v + chip model with replay_stub.sv (about 50x faster; harness development only)
# -P / -D       iverilog parameter override of tb_replay / define (sched_v parameters, see tb_replay.sv)
# plusargs      passed to vvp unchanged (+limit_pictures=N +limit_mbs=N +nofuse +norelease +period_cycles=N ...), see README.md
# Environment:  REPLAY_DATA (directory of the .treq/.req files; default: work/tauv_treq of this checkout or of the main checkout)
#               REPLAY_BUILD (output directory; default build/sdram_replay), REPLAY_LOG (log file; default <build>/<label>.log)
# The log is also printed; the exit status is 0 only when the log has a "VERDICT PASS" line.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${REPLAY_BUILD:-$ROOT/build/sdram_replay}"
IVERILOG="${IVERILOG:-iverilog}"
VVP="${VVP:-vvp}"
RTL="$ROOT/src/fpga/core"
mkdir -p "$OUT"

data_dir() {
    if [ -n "${REPLAY_DATA:-}" ]; then echo "$REPLAY_DATA"; return; fi
    if [ -d "$ROOT/work/tauv_treq" ]; then echo "$ROOT/work/tauv_treq"; return; fi
    local main; main="$(git -C "$ROOT" worktree list --porcelain | awk '/^worktree /{print substr($0,10); exit}')"
    echo "$main/work/tauv_treq"
}
DATA="$(data_dir)"

STUB=0; PARAMS=(); DEFS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --stub) STUB=1; shift;;
        -P) PARAMS+=("-Ptb_replay.$2"); shift 2;;
        -D) DEFS+=("-D$2"); shift 2;;
        *) break;;
    esac
done
[ $# -ge 1 ] || { sed -n 2,12p "$0"; exit 2; }
TARGET="$1"; shift
case "$TARGET" in
    clip) TREQ="$DATA/tnv3_tears_q30_48pic.treq";;
    allbi|static|scenechange|pan|random) TREQ="$DATA/worst/$TARGET.treq";;
    *) TREQ="$TARGET";;
esac
LABEL="$(basename "$TREQ" .treq)"
SCAN="$DATA/scan_800x720_1frame.req"

if [ "$STUB" = 1 ]; then
    BIN="$OUT/replay_stub.vvp"
    SRC=("$HERE/replay_stub.sv" "$HERE/tb_replay.sv"); DEFS+=("-DSTUB")
else
    BIN="$OUT/replay.vvp"
    SRC=("$ROOT/sim/sdram_chip_model.sv" "$RTL/tau_sdram_phy_core.sv" "$RTL/tau_sdram_dq_cap.sv" "$RTL/tau_sdram_clk_out.sv" "$RTL/tau_sdram_phy.sv"
         "$RTL/tau_sdram_sv_pq.sv" "$RTL/tau_sdram_sv_arb.sv" "$RTL/tau_sdram_sched_v.sv" "$HERE/tb_replay.sv")
fi
# rebuild when any source or option differs (the binary name carries a hash of the options)
KEY="$(echo "${PARAMS[*]:-} ${DEFS[*]:-}" | cksum | cut -d' ' -f1)"
BIN="${BIN%.vvp}_$KEY.vvp"
if [ ! -f "$BIN" ] || [ -n "$(find "${SRC[@]}" -newer "$BIN" 2>/dev/null)" ]; then
    "$IVERILOG" -g2012 -I "$ROOT/sim/sdram_chip_model" ${DEFS[@]+"${DEFS[@]}"} ${PARAMS[@]+"${PARAMS[@]}"} -s tb_replay -o "$BIN" "${SRC[@]}" || exit 2
fi
LOG="${REPLAY_LOG:-$OUT/$LABEL.log}"
"$VVP" "$BIN" "+treq=$TREQ" "+scan=$SCAN" "$@" 2>&1 | tee "$LOG"
grep -q "^VERDICT PASS" "$LOG"
