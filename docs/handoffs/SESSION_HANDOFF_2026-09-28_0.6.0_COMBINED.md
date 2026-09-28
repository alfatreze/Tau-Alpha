# Session handoff — 2026-09-28: 0.6.0 combined-feature night

Supersedes `docs/handoffs/SESSION_HANDOFF_2026-09-27_V050_AND_06_RTL.md` for everything below (that
file is still correct for the state it describes — v0.5.0 released, five RTL features each proven
individually, none combined yet). This doc picks up from there and reports what actually landed in
`main` overnight 2026-09-27/28, verified against `git log`, `docs/AUDIT_TRAIL.md` and a second
unmerged worktree branch, not just transcribed from a running commentary.

**How this doc was built:** read straight off `git log --oneline` on `main` (current tip `a5fc0d1`),
cross-checked against `docs/AUDIT_TRAIL.md`'s tail (last real entry there is B-346) and against
`git reflog`. Several claims that were relayed for this handoff could not be found in either place and
are called out explicitly below as unconfirmed, rather than restated as fact.

## What actually landed (verified against real commits)

1. **v0.5.0 shipped and published** (tag pushed, GitHub release live) — unchanged from the prior handoff.
2. **Five 0.6.0 RTL features, each individually fit-proven** (per the prior handoff): pipelined alpha
   blend (B-327), 192 KB RAM shrink, clk_sys 66.667 MHz, Helios H2 double buffering, persist widening
   16→32 words.
3. **RAM shrink + clk66 combined into one CORE_VERSION rev 26 interlock** — `2c09524`. New nested
   `ifdef` in `mp3_soc.v` (rev 26 = `0x4D50331A`, checked ahead of the standalone rev-24/rev-25
   branches), a strict firmware-side acceptance macro in `fw/player.c`, and `sim/test_clk66.py`
   extended to prove the full four-combination interlock truth table. `make rtl-lint`/`test-rtl`/
   `test-host` all pass. Commit message states this was **a redo of work already lost once to a
   concurrent `git reset`** on the shared tree.
4. **The `all6-combined` qsf bundle was committed** (`tools/blit_g3_poly_blend_ram192_clk66_dbuf_qsf_append.txt`,
   landed in `da8fa0c`) — blend + MP3 hardware window unit + H2 double buffering + clk66 + RAM shrink,
   all six macros together. **The actual Quartus fit result for this combined bundle is NOT recorded
   anywhere in this repo** — no AUDIT_TRAIL entry, no fit.summary reference, no seed selection. If a
   fit was run and closed timing on both seeds as reported to this handoff's author, **that result was
   never written down and should be treated as unverified until an AUDIT_TRAIL entry or a collected
   RBF confirms it.** Do not assume it happened; check `docs/AUDIT_TRAIL.md`'s tail and the VM
   (`~/tau-local/`) for a matching directory before trusting it.
5. **A "seed 1 vs seed 2 hardware corruption" story could not be verified.** No commit, no
   AUDIT_TRAIL entry, and no doc anywhere in the repo describes a placement-specific defect on
   `blend-pipe-b327` seed 1, or an `alfatreze.TAU_0_6_0_A_2`/`A_6`-related corruption, or a
   root-cause/re-seed resolution. `docs/AUDIT_TRAIL.md`'s last entry is B-346 (persist widening,
   packaged as `alfatreze.TAU_0_6_0_A_6`, **not installed, not run on hardware**). Numbering in the
   audit trail never reaches `A_11`. **If this genuinely happened, it was never committed to the
   repo and needs to be written up properly (a real AUDIT_TRAIL entry with the actual seed, symptom,
   and evidence) before anyone relies on it.** Do not repeat the "A2/A6 corruption, root-caused,
   resolved by re-seeding" claim as fact without finding that evidence first.
6. **Rounded-rect corners found broken system-wide, real fix, `8d529d1`.** Confirmed by reading
   `fw/rc_lut.h`: the hardware's 16-entry corner-cut LUT (`R_RC_IDX`/`R_RC_DATA`, MMIO 0xD4/0xD8,
   built for B11 back in `docs/AUDIT_TRAIL.md` B-231/B-235) reset to all-zero and **no firmware ever
   wrote it**, so every hardware `OP_RRECT` draw silently degraded to a plain square. Fixed by wiring
   `rc_lut_ensure()` into `fb_round_rect_on()`. Found by source review, not a fresh hardware read — no
   AUDIT_TRAIL entry and no screenshot confirms rounded corners actually appear on a Pocket after this
   fix. **Still needs a hardware run.** A general lesson from this (testing a config-load path, not
   just an opcode/probe path) is now `KB-067` in the `analogue-pocket-dev` skill.
7. **MASTER VU meter built end-to-end** — `8d529d1`. `fw/vu_master.inc`/`fw/vu_master_core.h`/
   `fw/vu_segment_table.h`, three presets, ported into the shared browser preview lab
   (`tools/meters/preview/meters/vu_master.js`, `tau_tags.js` for real MP3/FLAC tag parsing), plus a
   third independent decoder-only CPU% profiler accumulator set. `make rtl-lint`/`test-rtl`/
   `test-host` pass; golden-frame tests confirm the JS lab port is bit-exact with firmware. Not yet
   run on a Pocket.
8. **Chladni EMBER/OCEAN colour presets** — `8fb74f3`. A paint enum param (LINE/EMBER/OCEAN) on the
   existing LATTICE geometry, default 0 byte-identical to prior behaviour. Commit message states this
   was **a redo of work lost once to a concurrent `git reset`**.
9. **Legacy `.m3u` playlist fallback removed entirely** — `45630c3`. Library is now the only playback
   path: playlist buffers/state, the playlist overlay, Resume, Shuffle, the library on/off page and
   the How-it-works screen are all gone; associated persist words retired (not repurposed, per the
   project's append-only convention). Heap gap increases on every target (release +8.5 KB), confirming
   real removal. `make rtl-lint`/`test-rtl`/`test-host` pass.
10. **`docs/` reorganized by type** — `23abfd4`. 94 files moved into `docs/features/`, `docs/handoffs/`,
    `docs/research/`, `docs/tests/`, `docs/procedures/`, `docs/memory/`; manifest at
    `docs/DOCS_REORG_MANIFEST_2026-09-27.md`. **A real leftover from this reorg, found while writing
    this handoff:** several files are now duplicated at both the old root path and the new
    subdirectory path (e.g. `docs/PHASE_F_SPEC.md` **and** `docs/features/PHASE_F_SPEC.md`,
    `docs/HELIOS_SPEC.md` **and** `docs/features/HELIOS_SPEC.md`, similarly for
    `SESSION_HANDOFF_2026-09-27_V050_AND_06_RTL.md`) — the root copies are stale (last touched before
    the reorg commit) and were never deleted, only the `docs/features/`/`docs/handoffs/` copies were
    updated with fixed cross-references. **Anyone reading this repo's docs should prefer the
    subdirectory copy (`docs/features/…`, `docs/handoffs/…`) and treat a same-named file still sitting
    directly under `docs/` as stale.** This was not cleaned up in this pass — it's a real, moderately
    large cleanup (dozens of files to check and `git rm`) better done deliberately in its own turn.
11. **A build-breaking regression, found and fixed** — `a5fc0d1`. Growing the meter count to 16
    (adding `VIZ_VU_MASTER`) overflowed a fixed hardware-blit thumbnail-stash row budget
    (`THUMB_LIVE_SLOTS`, between `ART_STASH_Y` and the Chladni plane at the hard end of the
    framebuffer's address space), breaking **every** firmware build target, including the plain
    `release`. Caught only by actually rebuilding firmware after a meter-registry regeneration — `make
    test-host` alone did not catch it. Fixed by excluding `VIZ_VU_MASTER` from the compacted
    hardware-blit thumbnail cache (same software-RLE fallback an old pre-blit-engine bitstream already
    uses). Once fixed, `RAM_192K=1 CLK66=1 SDRAM_BUSY=1 player-library-diagnostic-profile` links with
    9,616 B free against a 4,096 B floor; `RAM_192K+CLK66 release` has 17,184 B free (6,144 B floor);
    `player-library-diagnostic` has 11,568 B free. `dist/` was rebuilt with the plain `release` target
    to carry the fix into the shipped v0.5.0 image, which had the identical latent bug.
12. **Concurrent-session collisions, real and repeated.** `git reflog --all | grep reset` shows several
    genuine `reset: moving to HEAD` events across different worktrees overnight; at least two commit
    messages (`2c09524`, `8fb74f3`) explicitly say they are redoing work lost that way. Mitigated by a
    new standing rule in `CLAUDE.md` section 3 (`65db559`, forbidding `git reset`/`git stash` against
    the shared tree for baseline comparisons) and by using isolated worktrees for real edits going
    forward — this handoff itself is being written from one.
13. **`alfatreze.TAU_0_6_0_A_11` could not be confirmed.** `docs/AUDIT_TRAIL.md`'s numbering for the
    0.6.0-alpha series only reaches `A_6` (B-346, persist widening, packaged NOT installed). No commit
    or doc mentions packaging or installing an `A_11` build, and no AUDIT_TRAIL entry describes a
    combined RAM_192K+CLK66+blend+dbuf+poly hardware install or its heap-gap figure. **Treat any
    "alpha.11 installed, awaiting owner test results" claim as unverified until a real AUDIT_TRAIL
    entry documents the packaging and install.**
14. **Audio-first track-load feature: scoped, not merged.** A separate worktree branch
    (`worktree-agent-ae51f3422c96ce2dc`, tip `ae8c284`, based on the pre-0.6.0-night commit `e081929`)
    contains `docs/features/AUDIO_FIRST_TRACK_LOAD_SPEC.md` and `tools/check_art_load_order.py` — docs
    and a host-side regression lock only, no firmware or RTL changes. It found that two of the three
    requested behaviours already exist (B-075's cover-hold, the existing fade-in on `pcm_flush()`) and
    that a real reorder (audio starting before art decode) is not a safe bare change given the PCM FIFO
    depth (43 ms) vs. art decode cost (up to 2.8 s). **This branch has NOT been merged into `main`** as
    of this handoff — it sits on an old base and needs a rebase/merge before its content is usable
    alongside the rest of tonight's work. A fresh session should check whether it has landed by the
    time it starts (`git log --oneline --all | grep -i audio-first` or similar) rather than assume
    either way.

## What a fresh session should read first

Per this project's `CLAUDE.md` section 2b convention (numbered pointers, newest first):
1. **This document** (`docs/handoffs/SESSION_HANDOFF_2026-09-28_0.6.0_COMBINED.md`) for tonight's
   verified state.
2. `docs/handoffs/SESSION_HANDOFF_2026-09-27_V050_AND_06_RTL.md` for the five-RTL-features-individually
   state this doc builds on.
3. `docs/AUDIT_TRAIL.md`'s tail (currently ends at B-346) for the detailed hardware/build evidence
   trail behind items 1–2 and 6–9 above.
4. Before trusting any claim about a combined all-six-feature hardware fit, an `A_11` install, or a
   seed-corruption story: **search `docs/AUDIT_TRAIL.md` and `git log` yourself** — as of this handoff
   none of those three are documented anywhere in the repo (see items 4, 5, 13 above).

## Immediate next steps (real, not aspirational)

- Confirm or properly document the `all6-combined` Quartus fit result (item 4) — either find the real
  fit output on the VM and write an AUDIT_TRAIL entry, or run it.
- If the seed-corruption story (item 5) is real, write it up properly with real evidence before it's
  repeated in any other doc.
- Hardware-test the rounded-rect fix (item 6) and the MASTER VU meter (item 7) — neither has a Pocket
  read yet.
- Merge or rebase the audio-first track-load branch (item 14) if that work is still wanted.
- Clean up the docs-reorg duplicate files (item 10) — a `git rm` pass over the stale root-level copies,
  done deliberately rather than folded into an unrelated commit.
