#!/usr/bin/env bash
# One shard of run_full.sh: run_shard.sh SKIP COUNT (environment: RUN, F, OUT, STUBFLAG, EXTRA). MIT licence.
set -u
S="$1"; N="$2"
# shellcheck disable=SC2086
REPLAY_LOG="$OUT/shard_$(printf %03d "$S").log" "$RUN" $STUBFLAG "$F" +skip_pictures="$S" +limit_pictures="$N" $EXTRA > /dev/null 2>&1
exit 0
