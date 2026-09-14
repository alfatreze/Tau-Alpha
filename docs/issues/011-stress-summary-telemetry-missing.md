# 011 — SDRAM stress summary differs between Pocket runtime and reviewed source

**Status:** Mitigated in new reproducible HUD build; old ROM provenance remains unknown
**Date:** 2026-09-14
**Evidence level:** **code-review | Pocket**

## Symptom

The stress core displays occasional `SDRAM PASS n` messages below the track
time. The user reports that Select + Start also displays diagnostic summary
information on Pocket, although this stops playback; pressing A resumes it.
Exact displayed fields/values have not yet been captured in the audit.

## Cause

`docs/SDRAM_CONTENTION_DIAGNOSTIC.md` and Pocket behavior include a Select +
Start summary, but current `fw/player.c` review found no stress-summary handler
or renderer. The source's normal Start binding can stop playback. The running
Pocket artifact therefore does not match the behavior predicted from the
reviewed source, or the source review has missed a compile-time/build-path
difference. `stress_pump()` does increment pass/word counters and emits a pass
toast; `stress_toggle()` snapshots audio underrun and framebuffer stall
counters.

## Impact and interim interpretation

Each completed pass is a deterministic 1 MiB write/read check. User reports
two clean passes on the default visualizer, then pass 3 clean while on the
second visualizer. Select + Start stops playback on the Pocket build; A resumes
it. Record this interruption when interpreting the concurrent playback test.

The user's Select + Start attempt stopped playback. This is consistent with the
ordinary Start binding reaching the stop path; the stress build has no stress
summary handler to consume the combo. Start therefore must not be used as a
stress-status shortcut during the current test.

One full pass per visualizer is the minimum current gate. During the stress-on
run, exercise seek, pause/resume, artwork, and track/playlist changes. Five
clean passes on one visualizer exceed the pass-count minimum for that mode but
do not replace testing the other visualizers or the interaction matrix.

## Resolution / next gate

The prior installed ROM (SHA-256 `2e896ae1…f295b3de`) was archived before the
HUD update, but its exact source/flags cannot be reconstructed from the
available records. The new explicit `player-stress` build target compiles with
`TAU_SDRAM_STRESS=1` and `TAU_STRESS_HUD=1`; its Select + Start handler refreshes
the live HUD and consumes the combo, so it no longer stops playback.

Confirm this behavior on Pocket and use the HUD rather than relying on the old
summary view. The older ROM's provenance remains a historical audit gap, but
does not block the new reproducible test path. Do not change SDRAM RTL or
rebuild Quartus as part of this issue.
