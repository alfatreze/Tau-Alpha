# Session handoff — 2026-09-25 (Phase F continued: Blit Test root cause closed, meters+picojpeg go cold, B11 built)

**Read this first.** Supersedes nothing (it's a continuation of the same day's earlier
`SESSION_HANDOFF_2026-09-25_BLIT_TEST_ROOT_CAUSE.md`, which is still correct history — its own
open item, B-194, is now fully resolved, see below). `docs/AUDIT_TRAIL.md` entries **B-195 through
B-207** are this session's detailed log; this file is the summary.

Git: **all work committed and pushed through `1278cc7`.** Working tree clean apart from the
standing untracked dirs (`.claude/`, `work/`, `docs/vendor/`, `release/`, `UniClaudeProxy/`,
`dist/.metadata_never_index`) — none of these are ever committed, by long-standing convention.

## What's in flight RIGHT NOW

**A Quartus fit for B11 is running on the Quartus VM as this handoff is written.** Launched
~07:40 WEST, still running (`quartus_fit`, past synthesis/map, in timing analysis) as of the last
check. Typical time: 50 min–1h45m from launch.

- Staged at `~/tau-local/blit-b11-s2-20260925` on the VM, from `git archive HEAD` at commit
  `1278cc7` (the exact commit now on `origin/main`).
- qsf: `SEED 2` + the exact proven `tools/blit_g3_qsf_append.txt` combination (the same
  `TAU_PHASE2_WINDOW`/`TAU_PSRAM_PROBE`/`TAU_PSRAM_WINDOW`/`TAU_PSRAM_IFETCH` + CRAM
  `FAST_*_REGISTER` lines + `TAU_MLAB_MIGRATE`/`TAU_FONT_REPACK`/`TAU_SDRAM_BUSY`/`TAU_BLIT` config
  that B-134/B-149/B-157 already closed timing on). No new macro needed for B11 — `cmd_op`'s
  widening and `OP_RRECT` are unconditionally compiled in, same as every opcode since B1.
- **To check on it:** `ssh -i ~/.ssh/taualpha_vm_ed25519 -p 2222 taualpha@127.0.0.1`, then
  `ps aux | grep -i quartus` (a live `quartus_map`/`quartus_fit` process, or none if it finished —
  check `~/tau-local/blit-b11-s2-20260925/quartus-b11-s2.log` and
  `src/fpga/output_files/ap_core.fit.summary` for the result). See
  `docs/JTAG_DEBUG_ACCESS.md` section 1 for the full VM/PATH setup if a fresh session needs it.
- **Reasoning for expecting a clean result (not a guarantee):** `OP_RRECT`'s corner segments reuse
  `OP_RECT`/`OP_BAR`'s existing constant-data burst write path (`p0_wr_stream=0`, `char_fg`-driven)
  — unlike B8's `OP_CBLIT`, which added a fourth write source into `glyphbuf`'s already-marginal
  shared write-select network (the whole B-150/B-157 saga), B11 never touches that path at all.
- **Once it's done:** read `ap_core.fit.summary` (want: 0 errors, RAM blocks unchanged from the
  last no-blend fit, i.e. ~298/308 — B11 adds no M10K, just 80 bits of flops for the corner-cut
  table), then the four-corner timing slack via `quartus_sta`/`report_timing` if anything looks
  marginal. If clean: copy the RBF off the VM, package a test build (`tools/package_sdram_stress.py
  --diagnostic-profile --number NN`, next number after 49), install, and get a first hardware read
  via the Blit Test's own opcode coverage (or a small firmware helper calling `fb_rrect_on()` if one
  gets written) — **no firmware wiring for B11 exists yet at all**, RTL/sim only so far.

## The whole day's arc, in order (see `docs/AUDIT_TRAIL.md` for full detail on each)

1. **B-192/B-193 (root cause, closed):** the entire B-166..B-191 "Blit Test hangs" saga was a raw
   CPU pointer into `0xA0000000`-relative addresses — the draw engine's own guard region, below the
   CPU's real uncached SDRAM window (`PL_SDRAM_BASE`/`0xA0100000`, a full 1 MiB later). Three
   functions shared the bug (`bt_crumb()`/`bt_crumb_read()`, `blit_probe()`,
   `set_thumb_flat_build()`), all fixed and hardware-confirmed.
2. **B-194 → B-197 (MILESTONE, closed):** a *new* anomaly after those fixes — the Blit Test ran far
   past its ~48s design duration without completing. Root cause: **not a hang at all.**
   `bt_advance()`'s terminal branch set `bt_state` to `BT_DONE`/`BT_QR` but never set `set_dirty`,
   the one flag the whole UI redraw dispatch (`fw/player.c:10882`) gates on — so the screen just
   froze on the last running frame forever, indistinguishable from a real hang. One-line fix.
   Hardware-confirmed: all 24 (op, level) windows PASS, 0% stall.
3. **B-198:** meter/blit integration — `ui_draw_dynamic()`'s plain-bars mode now issues one
   `fb_bar()` (`OP_BAR`) per column instead of two `fb_rect()` calls. Hardware-confirmed, 0 late
   underruns.
4. **B-199/B-200/B-201:** found a real conflict — `fw/cold.inc`'s Phase G1 rule ("nothing that runs
   per audio frame may be cold") directly forbade the planned RAM-shrink work, written a day before
   the blit engine existed. Measured it properly instead of assuming: three hardware scenarios
   (idle playback, forced-eviction-every-call, always-cold 12 KB worst case) — **all PASS, 0 late
   underruns.** Rule corrected.
5. **B-202 (MILESTONE):** the real conversion — `ui_draw_dynamic()` split into a thin hot wrapper +
   `ui_draw_dynamic_cold()` (new G4 step 4, `TAU_G4>=3`), same `COLD_READY()` fail-safe every other
   G4 step uses. Hardware-confirmed: 27,308 cycles worst-case (~1.73% of the 26.3ms budget), 0 late
   underruns, **+19,520 B heap gap freed.**
6. **B-203:** picojpeg moved to cold code too (the other named piece of the ~29 KB target). Needed
   `objcopy --rename-section` since picojpeg.c compiles as a separate object file — vendored source
   untouched. **+11,456 B measured** (bigger than the ~8 KB estimate). Hardware-confirmed: real cover
   art (including the largest test image, FLAC-embedded) renders correctly. **Combined with the
   meters: +30,992 B, clearing the ~29 KB the RAM shrink needs.** Both stay opt-in
   (`G4=3`/`PICOJPEG_COLD=1`) pending the owner's own ENDURANCE soak before promoting to `release`'s
   defaults. Caught and fixed a real process mistake along the way: the objcopy step's first cut
   would have silently changed `release`'s shipped ROM with zero hardware validation — fixed with an
   explicit opt-in toggle.
7. **B-204:** built section 4.1's peak-usage gate — `fw/start.S` paints the whole 16 KB stack with a
   sentinel at boot (universal, every build, ~48 B cost); `stack_high_water()`/new `SR_T_STACK`
   report tag reads the high-water mark back, wired into every Check run. First hardware number:
   **1,980 B of 16,384 (12%) peak usage**, large margin, under a demanding FULL Check run.
8. **B-205 (MILESTONE):** B11 (hardware rounded-rect) built and verified in RTL/simulation.
   `cmd_op` widened 3→4 bits (mechanical, zero regression). New `OP_RRECT` opcode with a bounded,
   LUT-driven corner sequencer (`R_RC_IDX`/`R_RC_DATA`, MMIO 0xD4/0xD8). Verified with hand-computed
   test cases + a new mutation hook (`BUG_IGNORE_RC_CUT`, confirmed caught — found and fixed a real
   bug in the *test itself* along the way, an unbounded `wait()` a mutation could make unreachable).
   Full `make test-rtl` passes clean. **This is the Quartus fit currently running** (see above).
9. **B-206:** B10 (RLE source blit) scoped alongside B11, then redirected to design-only after two
   real gaps surfaced while starting its RTL: the RLE byte source needs staging into SDRAM first (a
   cost the design assumed it avoided), and `OP_CBLIT`'s `clut_raddr` can't be reused unmodified (a
   live-`p0_q`-timed address vs. B10's already-decoded register). Both resolved on paper in
   `PHASE_F_SPEC.md`, refined to avoid a live `w*h` multiply. **Not built** — owner's explicit call,
   its own dedicated pass later.

## Card state

- `TAU_0_5_0_A_12`, `TAU_DEV_42`, `TAU_DEV_44`, `TAU`, `TAU_DIAGNOSTIC` — untouched all day.
- **`TAU_DEV_47`**: the real `G4=3` meter-cold conversion, `CT_COLDFRAME` confirmed PASS (27,308
  cycles). **Owner's ENDURANCE soak on this core is still pending** (deferred to their own
  convenience — do not remove or overwrite this core until that's done).
- **`TAU_DEV_49`**: the stack high-water-mark build (`SR_T_STACK` confirmed working, 1,980/16,384 B
  first reading). **The owner's own worst-case browse-while-playing-with-every-meter-mode exercise
  is still pending** (also deferred — the measurement is cumulative since boot, so ordinary use is
  the actual test; no reboot in between or the sentinel resets).
- `TAU_DEV_45`/`46`/`48` were all throwaway experiment builds for B-199/200/201/203, retired after
  their results were recorded — already removed from the card.

## Open items, in likely order

1. **The B11 Quartus fit result** (in flight, see above) — the immediate next thing to check.
2. **If B11's fit is clean:** package, install, get a first hardware smoke test. No firmware calls
   `OP_RRECT` yet at all (`fb_rrect_on()` doesn't exist) — that's still to be written, gated on
   `BLIT_READY()` with the existing software `fb_round_rect_on()` as fallback, matching B8's own
   integration pattern (B-160).
3. **Owner's own ENDURANCE soak** on `TAU_DEV_47` and **worst-case stack exercise** on `TAU_DEV_49**
   — both needed before promoting `G4=3`/`PICOJPEG_COLD=1` to `release`'s defaults, or before trusting
   the actual `RAM_WORDS` 256→192 KB shrink (section 4.1's own gate: `measured peak + 16 KB margin <
   192 KB`).
4. **B10's actual build** — designed, not started; needs its own dedicated verification pass given
   its higher complexity (data-dependent source consumption, no precedent in this codebase).
5. **The pre-existing `Track changes` Check failure** — open since B-138, unrelated to any of this
   day's work, never investigated. Still just sitting there.
6. Once meters+picojpeg are proven safe long-term (item 3): the actual `RAM_WORDS` 256→192 KB RTL
   change (two power-of-two arrays, 128+64 KB) + its own Quartus fit — the actual payoff of this
   whole track, not yet started.

## Quick facts a fresh session will want

- VM: `ssh -i ~/.ssh/taualpha_vm_ed25519 -p 2222 taualpha@127.0.0.1`. **A non-interactive SSH session's
  `$PATH` does NOT include Quartus** — always `export PATH=/home/taualpha/intelFPGA_lite/25.1std/quartus/bin:$PATH`
  first (confirmed the hard way this session).
- Card: `/Volumes/Pock` when mounted. Full write procedure: `docs/CARD_INSTALL_PROCEDURE.md`
  (backup → hash-verify copy → rebuild library index if media is shared from another core → clear
  the five catalog caches → clean junk → eject). Screenshots and the persist file may not appear on
  a remounted card until the core is properly Quit, not just ejected (a real gotcha hit this
  session).
- A JTAG reload (`quartus_pgm -m jtag`) makes the Pocket look like it spontaneously rebooted from the
  owner's side — expected, not a new fault, if you (or a prior turn) just issued one.
- `bash fw/build.sh player` / `bash fw/build.sh release` **share the same default output path**
  (`dist/Assets/tau/common`) — running one after the other silently clobbers whichever was built
  last. Always `git status --short dist/` after a sanity-check build of a non-`release` target, and
  rebuild `release` again if it shows a diff you didn't intend.
- New build toggles this session added: `G4=3` (the real meter-cold conversion, opt-in, default is
  still `G4=2`), `PICOJPEG_COLD=1` (opt-in, default off), `TAU_COLD_FRAME_PROBE=1` +
  `COLDFRAME_BIG=1`/`COLDFRAME_EVICT=1` (the synthetic-probe experiment infra, superseded by the
  real `G4=3` measurement but left in place for reference).
