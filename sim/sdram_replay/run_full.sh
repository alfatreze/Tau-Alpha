#!/usr/bin/env bash
# Run a whole .treq file as parallel shards and summarise it (MIT licence).
#   sim/sdram_replay/run_full.sh [--stub] [--jobs N] [--shard K] [--first P] [--count C] TARGET [plusargs...]
# TARGET: clip | allbi | static | scenechange | pan | random | path to a .treq. Plusargs go to every shard (e.g. +norelease).
# The file is cut into shards of K pictures (default 6); shard s runs +skip_pictures=s +limit_pictures=K. The pictures before a
# shard are fast-forwarded: their writes go straight into the memory and the shadow (no DUT time), so reads of earlier pictures are
# still checked. --first/--count run only pictures P .. P+C-1 (e.g. a timed run of a few pictures). The DUT starts cold in every
# shard (open rows, refresh debt, FIFO levels); a picture's busy time is changed by a cold start by well under 1 %.
# Output: build/sdram_replay/full_<label>_<mode>[_<tag>]/shard_<n>.log and the merged summary (also summary.txt there).
# Set REPLAY_TAG to keep several runs of one file apart.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RUN="$HERE/run_replay.sh"
JOBS=6; K=6; FIRST=0; COUNT=0; STUB=()
while [ $# -gt 0 ]; do
    case "$1" in
        --stub) STUB=(--stub); shift;;
        --jobs) JOBS="$2"; shift 2;;
        --shard) K="$2"; shift 2;;
        --first) FIRST="$2"; shift 2;;
        --count) COUNT="$2"; shift 2;;
        *) break;;
    esac
done
[ $# -ge 1 ] || { sed -n 2,13p "$0"; exit 2; }
TARGET="$1"; shift
MODE="timed"
for a in "$@"; do [ "$a" = "+norelease" ] && MODE="norelease"; done
data_dir() {
    if [ -n "${REPLAY_DATA:-}" ]; then echo "$REPLAY_DATA"; return; fi
    if [ -d "$ROOT/work/tauv_treq" ]; then echo "$ROOT/work/tauv_treq"; return; fi
    local main; main="$(git -C "$ROOT" worktree list --porcelain | awk '/^worktree /{print substr($0,10); exit}')"
    echo "$main/work/tauv_treq"
}
DATA="$(data_dir)"
case "$TARGET" in
    clip) F="$DATA/tnv3_tears_q30_48pic.treq";;
    allbi|static|scenechange|pan|random) F="$DATA/worst/$TARGET.treq";;
    *) F="$TARGET";;
esac
NPIC="$(python3 -I "$HERE/treq_count.py" "$F")"
[ "$COUNT" -gt 0 ] || COUNT=$((NPIC - FIRST))
LABEL="$(basename "$F" .treq)"
OUT="${REPLAY_BUILD:-$ROOT/build/sdram_replay}/full_${LABEL}_${MODE}${REPLAY_TAG:+_$REPLAY_TAG}"
rm -rf "$OUT"; mkdir -p "$OUT"
echo "$LABEL: $NPIC pictures, running $FIRST..$((FIRST + COUNT - 1)) in shards of $K, $JOBS jobs, mode $MODE, logs in $OUT"
"$RUN" ${STUB[@]+"${STUB[@]}"} --build-only "$TARGET" || exit 2
: > "$OUT/jobs.txt"
for ((s = FIRST; s < FIRST + COUNT; s += K)); do
    n=$K
    [ $((s + K)) -gt $((FIRST + COUNT)) ] && n=$((FIRST + COUNT - s))
    echo "$s $n" >> "$OUT/jobs.txt"
done
export RUN F OUT
export STUBFLAG="${STUB[*]+${STUB[*]}}"
export EXTRA="$*"
xargs -P "$JOBS" -L 1 "$HERE/run_shard.sh" < "$OUT/jobs.txt"
python3 -I "$HERE/replay_summary.py" --merge "$OUT"/shard_*.log | tee "$OUT/summary.txt"
