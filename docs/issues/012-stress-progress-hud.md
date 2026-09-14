# 012 — Improve SDRAM stress progress visibility

**Status:** Pocket UI/functionality verified; raw timer wrap defect found; corrected ROM pending staging
**Date:** 2026-09-14
**Evidence level:** **Pocket | design**

## User observation

Pass notifications appear only occasionally below track time, making it hard to
follow a multi-minute run. User estimates a pass takes roughly 2–3 minutes;
that is an estimate, not a timed measurement. The current test UI does not make
pass progress and recent results continuously legible.

## Implemented behavior

Add a compact, persistent status strip to the stress-only build, not the normal
TAU release UI. Show:

- active visualizer and cumulative completed-pass number;
- current pass progress as a small bar and percentage, computed from verified
  words within the current pass;
- elapsed time for the current pass (and optionally an estimated remaining
  time only after a complete prior pass provides a basis);
- the previous pass result and elapsed duration, e.g. `Last: PASS 04 · 02:31`;
- a latched failure state with failure count if a mismatch or timeout occurs.

The strip updates at most once per second and only exists in the stress build,
limiting framebuffer/CPU overhead that could perturb the contention measurement.
Select + Start forces a safe refresh and is consumed by the stress build, so it
does not stop playback. Normal player bindings and the normal TAU ROM are not
modified.

## Pros and cons

**Pros:** The tester sees that the pump is alive, knows how far through a pass
it is, can spot a latched failure, and can compare recent pass durations
without timing manually or opening a full-screen view.

**Cons/risks:** HUD rendering itself adds framebuffer writes and CPU work; a
frequent redraw could perturb the very arbitration being measured. ETA can be
misleading before timing stabilizes or when playback/visualizer work changes.
An always-visible strip consumes screen area and must not obscure track time,
artwork, or player controls.

## Timing evidence plan

Record actual pass start/completion timestamps for several consecutive passes
under the same MP3 and visualizer, and note when seek, pause/resume, or asset
changes occur. Compare quiet/stress-off baseline and stress-on durations. The
user's current 2–3 minute estimate is notably longer than the existing rough
documentation estimate (~1 minute), so replace estimates with measured values
before treating throughput as a regression or acceptance threshold.

## Pocket result and timer-wrap defect (2026-09-14)

The user completed a named visualizer pass matrix and the HUD was legible while
the player continued to run. It exposed a defect in the HUD's original
duration implementation: `R_CYCLES` is a 32-bit 60 MHz counter and wraps every
71.582788 seconds. The HUD stored a raw `end - start` difference for each
pass, so every multi-minute duration silently lost a full counter period.

The source now replaces that raw duration with a software accumulator that
consumes short unsigned `R_CYCLES` deltas on each main-loop iteration and
stores whole seconds plus a fractional remainder. This is correct provided the
main loop samples at least once per 71.58 seconds, a condition far looser than
normal player operation. It remains a stress-only diagnostic wall-clock; it
is not the player audio-frame clock and should not be compared directly to a
track's elapsed time.

The completed named-mode matrix and the invalid stopped-playback Eye run are
recorded in [SDRAM_CONTENTION_DIAGNOSTIC.md](../SDRAM_CONTENTION_DIAGNOSTIC.md).
Rebuild, stage only the stress ROM, and Pocket-smoke-test one pass that exceeds
72 seconds before accepting any duration or throughput value.

## Artifact and next gate

The HUD implementation exceeded the existing `-O2` stress image's token-heap
safety margin, and the linker correctly rejected that image. Do not weaken the
linker guard. The planned HUD artifact therefore uses `-Os` only for the
separate stress firmware target; normal TAU stays on its existing optimization
profile and is not rebuilt or modified. This means HUD timings are diagnostic
evidence for bounded SDRAM contention, not a substitute for release-player CPU
performance evidence.

`make firmware-sdram-stress` produced a 127,764-byte ROM with SHA-256
`61d39f955bde907fb63f59cca1c560ad31ea965c9d588627252db585b2bc3b9a`.
The prior Pocket ROM was archived in
`work/diagnostics/sdram-stress/pre-hud-rom-2026-09-14/tau.rom` before the new
ROM replaced only `/Assets/tau_sdram_strs/common/tau.rom` on the card.

Verify the HUD on Pocket and compare pass timing/audio/display behavior with
the pre-HUD run. This HUD is session telemetry, not a persistent SD log:
capture a photo of the status strip at each completed pass for the present
audit, or add a separately scoped persistent-log feature later. Do not use this
proposal to change SDRAM RTL or Quartus build.
