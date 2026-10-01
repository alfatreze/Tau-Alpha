# Session handoff — Cymo resampler: built, fit, two real hardware bugs found and fixed

**2026-10-01. Read this first for anything Cymo-related.** Supersedes
`docs/handoffs/SESSION_HANDOFF_2026-09-30_METER_PREVIEW_PERSIST_AND_MCLK.md` as the entry point (that
session's own content — meter preview, persistence, OP_BAR widen — is all still correct and hardware-
confirmed, just no longer the newest thing). Full blow-by-blow: `docs/AUDIT_TRAIL.md` B-471 through B-491.

## Headline

Built the Cymo 44.1→48 kHz polyphase FIR resampler end to end this session: host model → golden model →
RTL + sim + mutation tests → synthesis-only check → real two-seed fit → live-audio-path wiring → firmware
probe/toggle → card install → **real hardware listening tests, which found TWO genuine bugs, both
root-caused and fixed, both re-fit and re-installed.** The second fix's own hardware re-test (does the
1 kHz tone now give the same pitch on every toggle? is the "tiny constant noise" during music gone?) is
the very next thing to do — **not yet confirmed as this handoff is written.**

## What's built and proven

- **The resampler itself** (`src/fpga/core/tau_cymo_resamp.sv`, 160-bank/32-tap Kaiser polyphase FIR,
  `tools/gen_cymo_resamp_rom.py` generates the ROM): sim-verified bit-exact against its own golden model
  (`sim/cymo_resamp_model.c`, independently cross-checked in Python), all 5 mutation hooks caught.
  Fit-proven multiple times, always closing clean with real margin.
- **Live audio-path wiring** (`src/fpga/core/mp3_soc.v`, `eq_biquad.v` untouched after B-484's revert,
  `pcm_fifo.v` completely untouched throughout): the resampler sits as a second, parallel consumer of
  `pcm_fifo`'s existing `fifo_l`/`fifo_r`/`pcm_sample_tick`, feeding the EQ's input in place of the raw
  hold when `cymo_live_en` (sticky, `R_CYMO_CTRL` bit 2) is set.
- **Firmware**: `CYMO_RESAMP_READY()` boot probe (plain status-bit read, no aliasing risk), a
  Diagnostics-only "CYMO RESAMPLER" toggle (off at boot, never persisted), an Info page row ("CYMO
  RESAMP"). A global Helios utility (`helios_wrap_index()`) was also added this session — unrelated to
  Cymo, a mid-session ask to fix clamped (not wrapping) scroll on the Info/Stress-Status pages.
- **Card**: `alfatreze.TAU_DEV_59` carries the latest of everything below. `TAU`/`TAU_DIAGNOSTIC`
  (release) are untouched throughout.

## The two real hardware bugs (both found by the owner's own listening, both fixed)

1. **B-484 — borrowed, imprecise tick → audible "vibrato."** The resampler's `start` tick first reused
   `eq_biquad.v`'s own internal 48 kHz divider. That divider was never built to be *accurate* (fine for
   an EQ's IIR coefficients); under `TAU_CLK66` it truncates to a real 48,030.74 Hz, not 48,000. Through
   the resampler's fixed 147:160 ratio that implied an effective 44,128 Hz input-consumption rate against
   `pcm_fifo`'s correctly-calibrated 44,100.00 Hz supply — a ~28 Hz beat, matching the reported "tiny
   vibrato" on every 44.1 kHz track tried. **Fix**: gave the resampler its own dedicated fractional-
   accumulator tick (same technique `pcm_fifo.v` already uses correctly, reusing the exact `pcm_rate`
   reset-default constants). `eq_biquad.v`'s now-dead `tick_out` port was fully reverted. Fit, installed,
   re-tested — **owner confirmed music sounded better, lower noise floor, bigger soundstage** (consistent
   with the SINAD improvement the hold-vs-FIR measurements always predicted).

2. **B-488 — missing reset-on-enable → non-deterministic transient, mistaken for a second bug.** The
   re-test above also surfaced a 1 kHz-tone test showing a DIFFERENT pitch every time the toggle was
   switched on (OFF always constant). Reasoned from first principles before touching RTL: a fixed-ratio
   resampler's steady-state output frequency cannot depend on starting phase, so a per-toggle difference
   had to be a transient, not a ratio error. Found the real gap: `cymo_live_en`'s own `clear` wiring only
   covered an explicit MMIO clear or a real track change, never the toggle itself — every engage resumed
   against stale 32-tap history. **Fix**: a one-cycle rising-edge detector on `cymo_live_en`, OR'd into
   `clear`. Fit, installed (`cymo-b488` seed 1, all corners positive) — **NOT YET RE-TESTED on hardware.**

Both lessons are written up as new `analogue-pocket-dev` skill entries (local, hardware-validated):
**KB-083** (an approximate tick divider reused for a sample-rate converter creates a real beat-frequency
bug even when harmless for its original purpose) and **KB-084** (a toggled stateful unit must reset on
every enable transition, not only on an unrelated domain event).

## What's still open

- **The actual re-test of B-488's fix** — does the 1 kHz tone now give the same pitch every toggle? Is
  the "tiny constant noise" reported during ordinary music playback gone too, or still there (possibly a
  third, separate issue — the architecture's own inherent, normally-benign push/pop timing jitter between
  two independent accumulators is one candidate, not yet investigated)?
- `pcm_fifo.v` itself has never been touched and does not need to be, per the design (K2,
  `docs/features/CYMO_AUDIO_ENGINE.md` section 14) — confirmed still correct through both bug fixes.
- Scope remains 44.1 kHz input ONLY (`Q_STEP=147` fixed). 48 kHz and 22.05 kHz material still falls back
  to the hold; if `cymo_live_en` is engaged on non-44.1 kHz material it WILL mis-resample (not a bug,
  just outside this increment's scope — flagged to the owner mid-session).
- No decision yet on whether/when this becomes a real release feature (currently pure
  Diagnostic-Build-only, off by default, firmware probe-gated).
- `docs/ROADMAP.md` has no Cymo row yet — not added this session (out of scope for a docs-save pass to
  reorder the owner's own ordered list); worth adding once the current bug-fix cycle settles.

## Session process note (new standing rule)

Owner asked for a new project rule, now in `CLAUDE.md` section 3: **whenever a Quartus fit is launched,
state an estimated duration AND an estimated local finish clock time in the same message, and re-state
it whenever asked to check status.** This session's own fits (B-473/477/480/486/490) all landed within
a tight 1h40–1h45m band for this exact macro bundle — use that as the baseline estimate for the same
bundle going forward, the general 50min–1h45m range otherwise.

## Files touched this session (all committed)

RTL: `src/fpga/core/tau_cymo_resamp.sv`, `tau_cymo_resamp_rom.svh`, `mp3_soc.v`, `eq_biquad.v`,
`core_game.vh`, `ap_core.qsf`. Tools: `tools/gen_cymo_resamp_rom.py`,
`tools/blit_g3_poly_blend_ram192_clk66_dbuf_lpc_cymo_qsf_append.txt`,
`tools/ui_snapshot_renderer.py`. Sim: `sim/cymo_resamp_model.c`, `sim/cymo_resamp_rom.h`,
`sim/test_cymo_resamp_model.py`, `sim/tb_tau_cymo_resamp.v`. Firmware: `fw/player.c`,
`fw/settingsui.inc`, `fw/helios.inc`. Docs: this file, `docs/features/CYMO_AUDIO_ENGINE.md`,
`docs/AUDIT_TRAIL.md` (B-471..B-491), `docs/CURRENT_STATUS.md`, `CLAUDE.md`. Makefile: 
`test-rtl-cymo-resamp[-mutation]` wired into `test-rtl`/`rtl-lint`.
