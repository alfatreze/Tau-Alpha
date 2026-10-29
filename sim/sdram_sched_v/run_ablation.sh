#!/usr/bin/env bash
# Feature ablation of sched_v with the replay harness (flat out, +norelease). MIT licence.
#   sim/sdram_sched_v/run_ablation.sh            build every configuration, then run all in parallel (JOBS, default 12)
# Output: build/abl/<config>_<trace>.log; summary with sim/sdram_sched_v/ablation_summary.py
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
export REPLAY_BUILD="$ROOT/build/abl"
mkdir -p "$REPLAY_BUILD"
R="$ROOT/sim/sdram_replay/run_replay.sh"
JOBS="${JOBS:-12}"
# name | -P overrides for tb_replay | extra plusargs
CONFIGS=$(cat <<'EOF'
base | |
q2 | QDEPTH=2 |
q1 | QDEPTH=1 |
nolook | LOOKAHEAD_P=0 |
nobatch | BATCH_P=0 BATCH_REQS=1 |
noraw | RAW_P=0 |
nocont | CONT_P=0 |
no2d | EN2D=0 | +nofuse
min | QDEPTH=1 LOOKAHEAD_P=0 BATCH_P=0 BATCH_REQS=1 RAW_P=0 CONT_P=0 EN2D=0 | +nofuse
EOF
)
JOBLIST="$REPLAY_BUILD/jobs.txt"; : > "$JOBLIST"
while IFS='|' read -r name params extra; do
    name="$(echo "$name" | xargs)"; params="$(echo "$params" | xargs)"; extra="$(echo "$extra" | xargs)"
    [ -z "$name" ] && continue
    pargs=(); for p in $params; do pargs+=(-P "$p"); done
    "$R" --build-only ${pargs[@]+"${pargs[@]}"} clip > "$REPLAY_BUILD/${name}_build.log" 2>&1 || { echo "build failed: $name"; continue; }
    echo "$name clip ${params:-none} ;; $extra +norelease +skip_pictures=1 +limit_pictures=3" >> "$JOBLIST"
    echo "$name allbi ${params:-none} ;; $extra +norelease +skip_pictures=1 +limit_pictures=1" >> "$JOBLIST"
done <<< "$CONFIGS"
run_job() {
    local line="$1" name trace rest params extra pargs=() p
    name="${line%% *}"; rest="${line#* }"; trace="${rest%% *}"; rest="${rest#* }"
    params="${rest%% ;; *}"; extra="${rest#* ;; }"
    [ "$params" = none ] && params=""
    for p in $params; do pargs+=(-P "$p"); done
    REPLAY_LOG="$REPLAY_BUILD/${name}_${trace}.log" "$R" ${pargs[@]+"${pargs[@]}"} "$trace" $extra > /dev/null 2>&1
}
export -f run_job; export R REPLAY_BUILD
xargs -P "$JOBS" -I{} bash -c 'run_job "$@"' _ {} < "$JOBLIST"
echo "ablation runs done"
