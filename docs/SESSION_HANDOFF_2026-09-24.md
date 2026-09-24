# Session handoff — 2026-09-24

**Read this first**, then `docs/CURRENT_STATUS.md`, then `docs/PHASE_F_SPEC.md` sections 5/12/14 for the blit
engine's full design/verification history. `docs/AUDIT_TRAIL.md` entries **B-129 through B-154** are this
session's detailed log — this file is the summary, that file is the evidence.

Git: **31 commits ahead of `origin/main`, not pushed.** Working tree clean. Last commit `91dce30`.

## TL;DR

1. **The blit engine passed its first real hardware load test** (B-146) — proven correct in simulation, timing-
   closed on real hardware, and load-tested under real audio, all three legs of proof this phase needed.
2. **Getting there took four separate card/packaging bugs**, none of them blit-engine bugs — a stale library
   index, two `core.json` field-format issues (one real, one a red herring), and a stale Pocket catalog cache.
   All fixed; the fixes are now written into `docs/CARD_INSTALL_PROCEDURE.md` so they don't recur.
3. **A new feature (B8, CLUT blit) was designed, built, and simulation-verified**, but **its Quartus fit does
   not close timing** — a small, real, now well-diagnosed violation. This is the one open technical thread.
4. **A small firmware UX fix** (Start closes menus/library outright) is built and committed but **not yet
   installed on the card** — it needs a rebuild+repackage+install cycle, no VM/Quartus work required.

## What's proven and working

- **Blit engine (Tier 1, B1/B2/B4/B5/B6 minus blend)**: RTL complete, verified against an independent Python
  reference renderer (`tools/host/blit_reference.py`) via `sim/tb_blit_scene.v`/`sim/test_blit_reference.py`,
  with mutation-test coverage for every opcode. Timing-closed on the real product configuration (full G3 macros
  + blit engine, B-134) — the first time this combination was ever built; two prior sessions' worth of isolated
  blit-only fits (B-100 through B-117) had never included the product's own PSRAM/window macros, which is
  exactly what made an earlier install attempt (`TAU_DEV_43`, B-130) unusable.
- **`BLIT_READY()` fail-safe** (B-126): firmware can detect a pre-blit-engine bitstream. Not wired to any
  feature yet (nothing uses the blit engine operationally).
- **The blit-storm Check test (`CT_BLT`) + SDRAM busy-cycle counter** (B-127): built, and now hardware-verified
  (B-146) — PASS on two independent runs, SDRAM 15.8% busy during a sustained 30s load, zero late underruns,
  confirmed measured under continuous audio (not ambiguous — see the audio-continuity indicator below).
- **Audio-continuity indicator** (B-139): `CT_AUD`/`CT_BLT` now track whether playback ran the *whole* test
  window, not just at the start — an on-screen `*` marker plus an explicit `audio_full` field in the decoded
  report. Built after a real ambiguity (B-138) where a Check run's result couldn't be trusted either way.
- **Card install procedure, now written down** (`docs/CARD_INSTALL_PROCEDURE.md`, B-144): backup/verify,
  rebuild the library index for the *destination* core specifically (never copy an old one across — B-136),
  respect `core.json`'s documented field limits (`description<=63`, B-142), and **clear the Pocket's five
  catalog caches** (B-143) — the last of these was skipped for this entire session's early installs and cost
  real time chasing two wrong theories (B-141, B-142) before the actual cause was found.
- **Semver naming for milestone builds** (B-131, `--semver` on `tools/package_sdram_stress.py`): the currently
  installed `TAU_0_5_0_A_4` is the first real use. `TAU_DEV_NN` stays for throwaway bring-up builds.
- **Start now closes the library/legacy-playlist overlays outright** (B-145, owner request): previously only
  Settings had this; Library and the legacy playlist silently discarded Start, requiring one B press per nested
  depth level to back out. **Built and committed, not yet installed or run on hardware.**

## The one open technical thread: B8 (CLUT blit) doesn't close timing

- **What it is**: a new opcode (`OP_CBLIT`, 3'd7) reading one palette index per source word through a new
  256-entry CLUT, targeting a real, already-shipping use case (`fw/meter_thumbs.h` + `set_draw_thumb()`'s meter
  previews, currently dozens of `fb_rect` calls per thumbnail). Full design: `docs/PHASE_F_SPEC.md` section 5,
  "B8 detailed design".
- **RTL + simulation: done and correct** (B-148). New two-state dispatch (`A_CBLIT_RD`/`A_CBLIT_WAIT`) that
  deliberately never touches the shared `A_COPYRD` burst path `OP_COPY`/`OP_BLIT` depend on — that design
  choice held, zero regression on the full `make test-rtl` suite before or after.
- **Quartus fit: does NOT close** (B-150). Small, real, consistent-direction setup violation on both seeds:
  worst case Slow 0C -0.196 ns (seed 1), better case -0.025 ns (seed 2). Hold clean on both.
- **Root cause found** (B-151): the violating paths all land on `glyphbuf`'s own write-data selection network —
  the *exact* shared resource this whole phase has hit marginal three separate times before (B-109's original
  MLAB-era violation, B-111's BAR retiming, B-116's `TAU_BLIT_BLEND` congestion finding). `OP_CBLIT` is simply
  a fourth competing write source into it, alongside `A_COMPOSE`/`A_COPYRD`/`A_KEYDST`/`A_SBLIT`'s own writes.
- **One candidate cause ruled out, conclusively** (B-152/B-154): it is **not** the `BUG_CBLIT_NO_LOOKUP`
  mutation-test ternary — `generate`-gating it so the alternate branch provably doesn't exist in the netlist
  when off produced **bit-for-bit identical timing slack** to the ternary version. Simply adding a fourth
  mutually-exclusive write source is what costs the margin, independent of how that source computes its value.
- **Next step, not yet attempted**: a real retiming fix, the same technique already proven three times this
  session (B-111 BAR, B-114 SBLIT/CHAR) — register the CLUT's contribution (or restructure the write-source
  selection more generally) one cycle earlier, off the same combinational path the other three sources
  currently share. This is real, uncertain engineering work (each of the three prior retiming fixes took real
  investigation, not a template), not a one-line change to try blind.
- **This bitstream must not be installed.** The currently-installed `TAU_0_5_0_A_4` line is the pre-B8
  configuration and is completely unaffected.

## Card state (verified 2026-09-24)

`/Volumes/Pock/Cores/`: `alfatreze.TAU`, `alfatreze.TAU_DIAGNOSTIC`, `alfatreze.TAU_DEV_42`,
`alfatreze.TAU_0_5_0_A_4`. All working. `TAU_0_5_0_A_4` does **not** have B-145's Start-closes-overlays fix yet
(that firmware change landed after this build was packaged).

## VM state

Several `~/tau-local/*-2026092[34]*` directories from this session's fits remain staged on the VM (not
cleaned up) — harmless, just disk usage. The two most recent, relevant ones:
- `b8-g3-s1-20260923` / `b8-g3-s2-20260923` — B-150's original (ternary-based) CBLIT fit, does not close.
- `b152-s2-20260924` — B-152/B-154's `generate`-gated re-fit, identical result, does not close either.

No Quartus build is currently running.

## Other open items, lower priority (pre-existing, not from this session's blit-engine work)

- **`Track changes (10)` fails on every Check run seen this session** (B-138, B-146), cause unexplained. Worth
  investigating: read the `CT_TRK` code path in `fw/suite.inc` and figure out why it doesn't complete within
  its 60s window.
- **B-145's fix needs a hardware run.** Rebuild `player-library-diagnostic-profile` with `SDRAM_BUSY=1`,
  repackage as `--semver 0.5.0-alpha.5` (same RBF as `-alpha.4`, no VM work needed), install following
  `docs/CARD_INSTALL_PROCEDURE.md` in full (don't skip the cache-clear step), and confirm Start actually closes
  Library/the legacy playlist from a nested browse depth.
- Everything else in `docs/PHASE_F_SPEC.md` section 14's order table (Tier 2 items B9-B11, the M10K/RAM-shrink
  track, the spectrum filter bank, audio kernels) is unchanged from before this session and still gated behind
  the items above per the roadmap's own ordering.

## Resume prompt

> Read `docs/SESSION_HANDOFF_2026-09-24.md` first, then `docs/CURRENT_STATUS.md` and `docs/PHASE_F_SPEC.md`
> section 5 ("B8 detailed design"). The blit engine passed its first real hardware load test last session
> (B-146) — that part is done and proven. The open thread is B8 (CLUT blit): RTL/simulation are correct
> (B-148), but its Quartus fit doesn't close timing (B-150/B-151), and the mutation-ternary hypothesis for why
> is conclusively ruled out (B-152/B-154, identical slack with and without it). The real cause is that
> `OP_CBLIT` is a fourth write source into `glyphbuf`'s shared write-data network, the same resource that's
> been marginal three times before in this phase. Next step: a real retiming fix, the same technique already
> proven for BAR/SBLIT/CHAR (B-111/B-114) — find where to register the CLUT's contribution one cycle earlier,
> verify in simulation, then re-fit. Don't install any B8-containing bitstream on the card; the current
> `TAU_0_5_0_A_4` line is unaffected and stays as the reference. Separately, and independently: B-145's
> Start-closes-overlays fix is built and committed but never made it onto the card — that's a firmware-only
> rebuild/repackage/install, no VM work, and a good first task if you'd rather start with something concrete
> and low-risk before the B8 retiming investigation. Follow `docs/CARD_INSTALL_PROCEDURE.md` in full for any
> card write, especially the cache-clear step — skipping it cost real time this session. 31 commits are ahead
> of `origin/main`, not pushed; ask before pushing.
