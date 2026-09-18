# 015 — Phase 2 conditional left a duplicate SoC instance header

**Status:** Fixed; corrected macro-enabled Quartus retry passed
**Date:** 2026-09-14
**Evidence level:** **Quartus | code-review**

## Observation

The first Quartus Analysis & Synthesis attempt for the `TAU_PHASE2_WINDOW`
configuration failed after 1m28s with a syntax error at `core_game.vh:147`,
near `u_soc`, expecting `)`. The compile log is in the isolated VM build
directory:

`/home/taualpha/tau-local/phase2-window-a054-20260914/src/fpga/output_files/ap_core.map.log`

## Cause and fix

The new preprocessor conditional selected either
`mp3_soc #(.PHASE2_WINDOW_ENABLE(1)) u_soc (` or the default `mp3_soc u_soc (`
header, but the original unconditional `mp3_soc u_soc (` line remained directly
below `endif`. Removed the stale original line, leaving exactly one selected
instance header followed by the shared port list.

## Impact and workaround

Quartus stopped before fitting; no resource/timing report or `.sof`/`.rbf`
artifact was created. The failed build did not touch the normal project QSF or
Pocket card. No workaround is needed beyond a fresh compile of the corrected
source snapshot.

## Next gate

The corrected source was restaged at
`/home/taualpha/tau-local/phase2-window-a056-20260914`; `make check-fpga` and
the full `TAU_PHASE2_WINDOW` Quartus flow passed. The successful flow completed
in 1h18m22s, with 0.120 ns minimum reported hold slack and zero TNS. Run a
separate default/macro-off build next. See audit entries A-055 and A-056.
