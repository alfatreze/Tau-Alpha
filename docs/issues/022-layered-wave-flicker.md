# Layered Wave meter flickers on hardware

**Status:** Reported on hardware (owner, first boot of `alfatreze.TAU_DEV_59`, 2026-10-02). **Tagged for later by the owner; not investigated.**
This file only records what is and is not known, so the next person does not start from zero or from a guess.

## Observed

"Wave has flickering issues." No further detail yet: not recorded which view (HISTORY or SPECTRUM), which preset or layer count, whether it is
whole-box flicker, tearing, or shimmer in the drawn shape, nor whether it correlates with the music, with settings changes, or with the menu.

## Why the cause cannot be attributed yet

This was the **first hardware run of the merged build**, which contains two things that had never run on a card: the Layered Wave meter itself (as merged from
`meter-layered-wave`) and its history ring in PSRAM (B-509, `docs/features/meters/METER_MODULE_SPEC.md` section 27). A flicker could come from either, from
neither (a pre-existing property of how the meter draws), or from contention. There is no earlier hardware observation of this meter to compare against in
this repository.

## Hypotheses (not findings)

1. **Clear-then-redraw every frame.** `lw_history()` paints the whole box background and then every layer on each redraw (`lw_rect(X, in->y, W, in->h, cl->bg)` first). Without
   double buffering or beam-aware drawing, a full clear followed by a long run of draw commands can show as flicker. Helios H2 double buffering and beam gating (`helios_rows_safe`) exist
   for exactly this class of problem; whether this meter takes advantage of them is not established.
2. **The self-scaling stride oscillating.** `lw_stride` widens when a frame needs more than `LW_BUDGET` (300) commands and narrows when it needs fewer than half. If a setting sits near the
   threshold the drawing resolution can alternate between two values frame to frame, which reads as shimmer.
3. **The PSRAM ring.** An unwrap is about 19 k cycles at worst (arithmetic, not measured), plus contention with cold-code instruction fetch. A frame that occasionally runs long would show as an
   irregular update rate rather than a steady picture.

## Cheap discriminators (when it is picked up)

- Reinstall the pre-ring Layered Wave build (`alfatreze.TAU_0_6_0_A_45` on the `meter-layered-wave` branch history) and look: flicker there means it predates the ring.
- Read the per-meter cost from **Diagnostics > Meter Sweep** (commands per frame, yield time) for Layered Wave against Bars, and note the stride it settles on.
- In the Diagnostic Build's experimental group, switch the **cost guard** to OFF and to RELAXED: if the flicker changes, hypothesis 2 is live.
- Note whether it flickers with the menu closed and the screen otherwise static (points at the meter) or only when other UI is repainting (points at beam/double-buffer interaction).

## Where it is recorded

`docs/ROADMAP.md` section 3 ("Later and parked"), `docs/AUDIT_TRAIL.md` B-511. Owned by the Layered Wave session / worktree (`../tau-alpha-meter-layered-wave`).

## Update (B-512)
Hypothesis 1 confirmed from source as the only live meter without beam protection; a beam gate (`LW_BEAM_GATE`, 28-row margin) and an Info > LAYERED WAVE row (draws, skips, last/worst draw time, stride) are built on branch `lw-flicker`, host-verified, not yet on hardware. Next: install a build with it, read the draw time, and judge the flicker. Double buffering was rejected (whole-frame flip, ~100 ms flip wait in the decode loop).
