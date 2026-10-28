#!/usr/bin/env bash
# Negative tests of the replay harness itself (MIT licence): each mutant breaks one thing, the harness must report FAIL with the
# matching reason; the unmutated run must PASS.
#   sim/sdram_replay/run_mutants.sh            on the stub DUT (about 5 minutes)
#   sim/sdram_replay/run_mutants.sh --real     on PHY + sched_v + chip model (about 20 minutes)
# The mutants are runtime plusargs of tb_replay (a shim between the harness and the DUT, or a broken feeder rule):
#   +mut_drop=N        the DUT never sees the N-th request accepted on port 1 (a scheduler that loses a request)
#   +mut_wcorrupt=N    one bit of the write data of port 2 flips for 4 words from the N-th word (data corruption on the write path)
#   +mut_rcorrupt=N    one bit of the N-th read word of port 1 flips (corruption on the read path)
#   +mut_nolast=N      the N-th r_last of port 1 is suppressed (a done without r_last)
#   +mut_scanstall=K   port 0 is not served for K cycles (scan-out underflow)
#   +mut_stall=K       ports 1 and 2 accept a request only every K cycles (a slow scheduler: busy time over the budget)
#   +mut_nofence       the feeder ignores FENCE_RD            +mut_nobarrier   the feeder treats PIC_END as no barrier
#   +mut_noreleasegate the feeder offers records before their release_ns (timed mode)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RUN="$HERE/run_replay.sh"
MODE=(--stub); LABEL=stub
if [ "${1:-}" = "--real" ]; then MODE=(); LABEL=real; fi
OUT="${REPLAY_BUILD:-$HERE/../../build/sdram_replay}/mutants_$LABEL"
mkdir -p "$OUT"
BASE=(clip +norelease +limit_pictures=3 +limit_mbs=60 +tmo=20000)
pass=0; fail=0

run() {   # name, expect (PASS or a reason substring), extra args...
    local name="$1" expect="$2"; shift 2
    REPLAY_LOG="$OUT/$name.log" "$RUN" ${MODE[@]+"${MODE[@]}"} "$@" > /dev/null 2>&1
    local log="$OUT/$name.log"
    if [ "$expect" = PASS ]; then
        if grep -q "^VERDICT PASS" "$log"; then echo "ok      $name (passes unmutated)"; pass=$((pass+1)); else echo "BAD     $name should pass"; grep -E "reason|FAIL" "$log" | head -5; fail=$((fail+1)); fi
    else
        if grep -q "^VERDICT FAIL" "$log" && grep -q "reason:.*$expect" "$log"; then echo "KILLED  $name (reported: $(grep -m1 "reason:.*$expect" "$log" | sed 's/^ *reason: //'))"; pass=$((pass+1))
        else echo "MISSED  $name (expected a reason containing '$expect')"; grep -E "reason|VERDICT" "$log" | head -5; fail=$((fail+1)); fi
    fi
}

run base               PASS                    "${BASE[@]}"
run drop_request       "hang"                  "${BASE[@]}" +mut_drop=300
run corrupt_write      "chip/shadow"           "${BASE[@]}" +mut_wcorrupt=2000
run corrupt_read       "read mismatches"       "${BASE[@]}" +mut_rcorrupt=500
run missing_r_last     "protocol errors"       "${BASE[@]}" +mut_nolast=100
run scan_underflow     "scan-out underflow"    "${BASE[@]}" +mut_scanstall=9000
run missing_fence      "offer-rule"            "${BASE[@]}" +mut_nofence
run missing_barrier    "offer-rule"            "${BASE[@]}" +mut_nobarrier
run timed_base         PASS                    clip +limit_pictures=1 +limit_mbs=3
run offer_before_release "offer-rule"          clip +limit_pictures=2 +limit_mbs=3 +mut_noreleasegate   # picture 1 is offered at once instead of 4.2 M cycles later
# a slow scheduler: first measure its busy time with an unlimited budget, then give it a budget it exceeds slightly (over 90 %,
# in time) and a budget it misses (past the deadline)
REPLAY_LOG="$OUT/stall_cal.log" "$RUN" ${MODE[@]+"${MODE[@]}"} "${BASE[@]}" +mut_stall=24 +period_cycles=400000000 > /dev/null 2>&1
B="$(grep '^PIC' "$OUT/stall_cal.log" | sed 's/.* busy=\([0-9]*\) .*/\1/' | sort -n | tail -1)"
if [ -n "$B" ]; then
    run over_90_percent  "above 90 %"            "${BASE[@]}" +mut_stall=24 +period_cycles=$((B * 100 / 95))
    run past_deadline    "past their deadline"   "${BASE[@]}" +mut_stall=24 +period_cycles=$((B / 3))
    # the over-90 case must not also be late (the 90 % rule is what catches it)
    if grep -q "past their deadline" "$OUT/over_90_percent.log"; then echo "NOTE    over_90_percent was also late (busy $B)"; fi
else echo "BAD     stall calibration produced no PIC line"; fail=$((fail+1)); fi
echo "mutants ($LABEL): $pass ok, $fail bad"
[ "$fail" -eq 0 ]
