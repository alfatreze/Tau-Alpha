# Session handoff — 2026-09-29: FLAC LPC hardware-confirmed, Helios items 1-3, Talos is next

**Read this first.** Supersedes `docs/handoffs/SESSION_HANDOFF_2026-09-28_FLAC_LPC_AND_CLEANUP.md` for
what it covers (that doc's "check lpc-b372" instruction is now resolved, see below); still correct for
its own repo-cleanup summary.

## 1. FLAC LPC: fixed, fitted, hardware-confirmed, quantified (B-376..B-386)

The `lpc-b372` fit this session inherited (both seeds) **failed**: needed >=2,061 LABs, device has 1,848
— not a timing miss, a real over-capacity failure. Full arc, in order:

- **B-376**: real ALM-breakdown pulled from the failed fit's synthesis report. `tau_flac_lpc` alone:
  1,922 ALUTs / 1,714 registers / **0 block-memory bits**, despite holding real per-subframe state —
  more than `tau_spec_bank`'s entire filter bank. Every other kernel in this design (`tau_mp3_poly`,
  `tau_spec_bank`, `eq_biquad`) has an explicit, deliberate `ramstyle` choice on every small array;
  `tau_flac_lpc` never got that decision made.
- **B-377 (real negative result, kept honest)**: first fix attempt converted `hist_mem`'s 31-way
  parallel-shift write into a ring buffer, assuming the write side was the expensive part. Verified
  correct (20,000 vectors + mutations unchanged) but measured **essentially zero ALUT/register
  savings** (1,922->1,914). Wrong diagnosis: the write side was always cheap (fixed source per slot, no
  mux); the real cost was the READ side (`mem[runtime_index]`, a LUT mux), untouched by the ring-buffer
  conversion.
- **B-378 (the real fix)**: `ramstyle="MLAB, no_rw_check"` on both arrays (matching `mp3_fb.sv`'s own
  `glyphbuf` idiom) routes the read through dedicated memory hardware instead of a LUT mux — but that
  needs a REGISTERED (1-cycle) read, so the MAC state split into two (present address, then accumulate
  once the read lands; 2 cycles/tap instead of 1). Real Fitter run: **Successful, 18,064/18,480 ALMs
  (98%, 416 ALM margin), all four corners closed clean, zero negative slack anywhere.** RBF:
  `work/diagnostics/lpc-b378/ap_core_s1.rbf`, sha256 `301645...aa7869`.
- **B-379**: installed as `alfatreze.TAU_0_6_0_A_16` (replaced the prior 0.6.0-alpha.15 test core).
- **B-380**: first real hardware result — 3 Checks + an R3 stress pass, 0 timeouts throughout, no audio
  issues. Not yet sample-exact verified (no hw-vs-sw Check comparison exists), but confirms the unit
  runs correctly under real silicon timing.
- **B-381**: owner reported **real, specific microstuttering** heard on earlier builds, now gone. Before
  crediting LPC specifically (the bitstream bundles 5 other RTL features, clk66 among them — a real
  confound), built an A/B firmware pair on the SAME bitstream: `alfatreze.TAU_DEV_52` (`LPC_FW=0`,
  forces software FLAC) vs `TAU_0_6_0_A_16` (`LPC_FW=1`). **Stutter returned on `TAU_DEV_52`** —
  confirms LPC specifically, not something else in the bundle.
- **B-382/B-384 (a real bug caught before being trusted)**: built new `flac_lpc_max_cyc`
  instrumentation (worst single real-LPC call in a Check window — the existing `t_pct`/`c1_pct` are
  window AVERAGES and stayed flat regardless, exactly why they couldn't see a rare data-dependent
  spike). First version used raw cycles capped at a u16 (65535) — **pegged at the cap on every real
  FLAC block, both cores**, which looked like a real "hardware doesn't help" result until traced: one
  timed call covers a WHOLE block's remaining samples (thousands), already >100,000 cycles for a normal
  block, so the cap was meaningless from the start. Fixed to milliseconds (`lpc_max_ms`).
- **B-385/B-386**: real, corrected numbers. **Worst single-call latency: 11 ms (software) vs 5-6 ms
  (hardware)** — roughly halved, smaller than the raw MAC-cost ratio alone would suggest (2 cycles/tap
  hardware vs ~10-30 cycles/tap for software's 64-bit accumulate on RV32IM), consistent with real
  per-sample firmware overhead (MMIO writes/polls) that doesn't shrink just because the arithmetic got
  faster. Only one fully clean paired reading (the other software run's Check window caught an MP3
  track); both hardware readings landed consistently below it.

**`analogue-pocket-dev` skill KB-069** (local, git-ignored) has the full story, promoted to
**hardware-validated** — including the honest negative result from the ring-buffer attempt, and a
general, reusable lesson: a runtime-indexed array read costs a LUT mux regardless of write pattern; only
a real registered block-memory read removes it, and requires pipelining the surrounding state machine.

**Card state**: `alfatreze.TAU_0_6_0_A_16` (hardware, `LPC_FW=1`) and `alfatreze.TAU_DEV_52` (software
A/B control, `LPC_FW=0`) both carry the corrected `lpc_max_ms` instrumentation. `TAU`/`TAU_DIAGNOSTIC`
untouched throughout.

**Not done**: a real hardware-vs-software sample-exact Check comparison (the design doc's own remaining
item, `docs/research/FLAC_LPC_KERNEL_DESIGN.md` section 7 item 4) — the golden-vector testbench proves
the arithmetic in simulation; nothing yet proves bit-exact output on real silicon.

## 2. Helios review: items 1-3 done, item 4 next in that track (B-387, D-H01)

`docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md` section 5's near-term tier:

1. **DONE** (commit `9606bc5`): meter draw contract (`fw/meter.h`'s `mtr_in_t`) for the 4 live meters
   (Winamp Bars/Scope, Chladni, VU Master). Found and fixed a real prerequisite bug first: MASTER VU was
   dead code since `8d529d1` (never `#include`d, never dispatched) — commit `80a71ca`.
2. **DONE** (commit `31953c0`, B-387): Settings' three hand-maintained dispatch chains (draw/input/open
   for the 6 rich pages — Check, Decode Sweep, Blit Test, Meter Sweep, Meter Trace, Winamp Configurator)
   collapsed into one `set_spg[]` function-pointer table. `release`'s ROM changed for real (Configurator
   ships there). Not yet hardware-tested or installed.
3. **PARKED** (`D-H01` in `docs/DECISIONS.md`): checked for a natural second `helios_excl[]` caller
   (VU Master's overlay, the stress HUD row, Chladni's corner) — none currently broken in the shape the
   mechanism exists to fix. Left as a one-caller mechanism; revisit when a real need appears, not
   manufactured.

Items 4-8 (the `helios_view_t` registry, retiring 7 legacy meters, a real partial-invalidation
vocabulary, wiring up H2 double buffering, revisiting 720p) are **not started**.

## 3. The other session's Talos finding — likely the highest-leverage next step

**A concurrent session** (commit `1ace737`, co-authored Claude Opus 5.5, landed mid-session while this
one was on the LPC work) did a full read-from-source review of `mp3_fb.sv` (Talos, the blit engine):
`docs/research/TALOS_REVIEW_2026-09-28.md` + `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md`
(**status: planned, not started, explicitly sequenced "after the FLAC LPC work... lands" — which just
happened**).

**The headline finding directly explains this session's own ALM crisis**: with `TAU_BLIT_BLEND` on
(which it is, in the current all6-combined+LPC bitstream — present in the RTL, not yet used by any
firmware caller), `glyphbuf` (Talos's 128-word row buffer) gets a **second read and a second write
site** from the blend pipeline, a shape no memory primitive supports, so Quartus falls back to
registers: **~6,500 ALMs** instead of the ~80 ALMs two real MLAB copies would cost. `mp3_fb` is 7,800
ALMs with blend on vs 1,313 without — **this is the single largest saving available on the entire chip,
needs no new feature work, and the fix is already designed** (`TALOS_REVIEW_2026-09-28.md` section 1a):
give `glyphbuf` exactly one registered write port (merge every writer into one `{we, waddr, wdata}`
selected by state — they never fire in the same cycle functionally) and two MLAB read copies. Estimated
result: `mp3_fb` back to ~1,400 ALMs, whole design back to ~60% (from today's 98%), with real headroom
for everything else on the books (Talos 2's own further opcodes, the FLAC bit-reader/IMDCT kernels, the
Chladni row-burst hardware idea, anything else this project has parked waiting for ALM budget).

Same root-cause CLASS as this session's own `KB-069` finding (a memory-inference failure from an
access pattern no primitive supports), independently discovered by the other session, at roughly 5x the
ALM cost of this session's LPC fix.

**Also found, not yet fixed** (same review): two live correctness bugs (`OP_BAR`'s 7-bit lit-row count
wraps above 127, not yet seen on hardware since nothing currently draws that tall; `fb_wait()` used as
"engine finished" at 3 call sites when it only means "FIFO not full" — real races, not closed) and one
latent hazard (a burst crossing a 1,024-word SDRAM page hangs on read / wraps on write — unreachable
with today's firmware, reachable the moment anyone uses a non-512 stride).

**Recommended immediate next step, per the other session's own review** (`T2-00` in
`TALOS_REVIEW_2026-09-28.md` section 7): fix `glyphbuf`'s single-writer/dual-MLAB-read shape first — low
risk, same behaviour, same existing tests (blend + mutation + reference-renderer diff) — before
considering the larger Talos 2 phased rewrite (`TALOS2_REIMPLEMENTATION_PLAN.md`, P0-P4, a bigger,
higher-risk undertaking with its own acceptance-test plan).

## 4. Card state

`alfatreze.TAU`, `alfatreze.TAU_DIAGNOSTIC` (release, v0.5.0, unchanged all session), `alfatreze.TAU_0_6_0_A_16`
(all6-combined + FLAC LPC hardware, `LPC_FW=1`, hardware-confirmed), `alfatreze.TAU_DEV_52` (same
bitstream, `LPC_FW=0`, the A/B control — keep for any future re-verification, or remove once the Talos
fix needs a fresh install slot).

## 5. Immediate next steps, in order of leverage

1. **T2-00** (Talos `glyphbuf` single-writer fix) — highest leverage, lowest risk, already designed by
   the other session, recovers ~6,500 ALMs. Do this before anything else that needs ALM budget.
2. Once T2-00 lands and is fit-confirmed: re-evaluate whether the fuller Talos 2 rewrite
   (`TALOS2_REIMPLEMENTATION_PLAN.md`) is still warranted given the ALM pressure it was originally
   solving may already be resolved by T2-00 alone.
3. Helios item 4 (`helios_view_t` registry) — the seam (`helios_view_changed()`) already exists from an
   earlier session.
4. A real hardware-vs-software FLAC LPC sample-exact Check comparison, if bit-exactness on real silicon
   still needs proving beyond the golden-vector testbench.
5. `TAU_0_6_0_A_16`/Settings dispatch collapse (B-387) hardware test/install — functionally low-risk
   (verified by `make test-host` + byte-level reasoning) but never actually booted.
