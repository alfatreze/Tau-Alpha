# Session handoff — 2026-09-28: FLAC LPC hardware kernel, repo cleanup, Helios review

**Read this first.** Supersedes `docs/handoffs/SESSION_HANDOFF_2026-09-28_0.6.0_COMBINED.md` for
what it covers (that doc's own "unverified anywhere" flag on the `all6-combined` fit is now resolved,
see below); still correct for its own 0.6.0-alpha.1..15 card history.

## 1. What shipped this session (all pushed to `origin/main`)

**FLAC LPC reconstruction hardware kernel — designed, built, verified, wired in, currently fitting:**

- `docs/research/FLAC_LPC_KERNEL_DESIGN.md` — the active design doc (supersedes
  `FLAC_BITREADER_KERNEL_SCOPING.md`, kept for history). Real motivation, not speculative: FLAC decode
  measures ~99% of realtime on ordinary tracks (B-363), correlating with real reported audio clicks.
- Host proof (B-365/B-366/B-373): a 45-bit signed accumulator is provably sufficient — exhaustive
  synthetic legal-range coverage AND 109 million real-LPC steps captured from 8 real files on the test
  card, 0 mismatches at 45/48/64 bits.
- `src/fpga/core/tau_flac_lpc.sv` + `sim/tb_tau_flac_lpc.v` (B-368): a 5-state sequenced MAC/shift/
  add/push machine, 48-bit accumulator, one operation per clock (this project's own timing rule). 20,000
  golden-model vectors + a sequential push/shift test, 5 mutation hooks all correctly caught (after
  fixing 3 that weren't real mutations on the first attempt — see the doc for what that means for
  trusting a mutation suite you just wrote).
- Wired into `mp3_soc.v` (B-369): `LPC_ENABLE` parameter, MMIO 0x120-0x13C (`docs/MMIO_ALLOCATION.md`),
  fully inert on any bitstream without `TAU_LPC`.
- Firmware caller (B-370): `fw/flac_lpc_hw.h`/`.inc`, redirect wired into `fw/flac.c`'s
  `subframe()`/`subframe_stream()`, gated by `TAU_LPC_FW` (`LPC_FW=1`, default 0 everywhere — no
  hardware result exists yet). Host-verified via `sim/flac_lpc_fw_harness.c` +
  `sim/test_flac_lpc_fw_redirect.py` (in `make test-host`): drives the REAL `subframe()`/
  `subframe_stream()` code 8 ways (clean/mid-subframe-failure/immediate-failure hardware paths). Found
  and fixed 2 real glue bugs this test caught (see B-370 in `docs/AUDIT_TRAIL.md` for the exact bugs —
  both were in the new firmware glue, not the math or the RTL).
- **RTL synthesis bug found and fixed** (B-371/B-372): the first Quartus fit attempt (`lpc-b369`) failed
  both seeds in ~3 minutes — `hist_mem` was driven from two separate `always` blocks, which Icarus
  tolerated silently but Quartus correctly refused (`Error (10028)`). Fixed, re-verified in simulation,
  relaunched. **New skill knowledge**: `KB-068` in the `analogue-pocket-dev` skill records this as a
  general Quartus/Verilog lesson (source-verified) — worth reading before writing any new RTL memory
  array touched from more than one always block.

**Fit status as of this handoff**: `lpc-b372` (both seeds) was still running on the VM when this was
written. Check with `python3 tools/vm_fit.py status lpc-b372`. If it closed timing, the next step is
packaging + a card install + a hardware Check comparing software vs. hardware reconstruction (never
built yet — `docs/research/FLAC_LPC_KERNEL_DESIGN.md` section 7 item 4's own remaining piece). If it
failed, read `~/tau-local/lpc-b372-s<N>/quartus-fit.log` on the VM directly rather than assume why.

**Real, previously-undocumented evidence recovered** (B-371): the `all6-combined` fit from 2026-09-27
(RAM shrink + clk66 + pipelined blend + H2 double buffering + MP3 hardware window, the bitstream
currently on the card as `alfatreze.TAU_0_6_0_A_14`/`_15`) — the prior handoff had explicitly flagged
this fit's *result* as "unverified anywhere in this repo." It is now verified: read directly off the VM,
**Successful, 0 errors, worst-case setup slack +5.695 ns, hold +0.037 ns** — both positive, but the hold
margin is razor-thin, worth knowing if anything else gets added to that same bundle. A real, separate
regression was also found in the same log and NOT yet chased: `glyphbuf` is silently falling back to
M10K instead of MLAB on this combined build (`Warning (10999): can't infer memory for variable
'glyphbuf'`), unlike every standalone blit-engine fit before it.

**Repo cleanup (this session, explicit owner instruction):**

- `.gitignore` gained `/work/`, `/.claude/`, `/UniClaudeProxy/`, `/build/`, `/release/` — these had
  accumulated locally across the whole project's history (`work/` alone ~15 GB) but were never actually
  ignored, so every session's `git status` showed them as noise. Purely additive, nothing deleted.
- Integrated genuinely-orphaned work from another local stream/account after review: `docs/DECISIONS.md`
  (referenced from several already-committed docs but had never itself been committed) and
  `docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md` (see section 2 below — this is the Helios
  review the owner will want to look at next). **Deliberately NOT integrated**: `docs/vendor/
  DOC012312972.pdf` — a confidential vendor datasheet, explicitly and repeatedly left out of every
  commit across this project's entire history; committing it now would silently reverse a standing
  decision, not fix an oversight.
- `dist/Assets/tau/common/{tau.rom,tau-cold.bin}` rebuilt and committed to match the currently-committed
  `fw/` tree (verified deterministic — a plain `bash fw/build.sh release` reproduces the identical
  SHA-256 twice in a row from this exact source).

## 2. Pending: Helios improvements (the owner's next stated focus)

`docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md` (newly integrated this session, see above) is
the document to start from. It is a review, not a spec — read it in full before proposing anything, it
already does the "what exists vs. what doesn't" audit. Headline finding: Helios today is a working but
narrow mechanism (one real caller, `ui_chrome_paint()`) sitting next to **five other component-shaped
subsystems** the project has independently built for adjacent problems (meter draw dispatch, Settings'
two page systems, `helios_excl[]`, and — critically — Helios H2 double buffering, which **closed timing
on real hardware and is sitting unused**, zero firmware reads or writes its registers).

The review's own recommended order (section 5, "Ordered by leverage vs. risk, not by document section
number"):

1. Finish the meter draw contract (`mtr_in_t`, `docs/features/meters/METER_MODULE_SPEC.md` section 3)
   for the 4 meters that already have generated config.
2. Collapse Settings' two page-dispatch mechanisms into one function-pointer table.
3. Give `helios_excl[]` a second real caller to prove it generalizes.
4. The `helios_view_t` registry (`docs/features/HELIOS_SPEC.md` section 11) — now that
   `helios_view_changed()` (B-350/351, this session) exists as the seam to grow from.
5. Retire the remaining 7 legacy meters' inline `if` blocks into the same contract item 1 starts.
6. A real partial-invalidation vocabulary (today there are three separate "redraw everything"
   mechanisms that don't compose — section 3.1 of the review).
7. Wire up H2 double buffering, once item 4 gives it a real flip point to hook into.
8. Revisit the 720p question, once item 6 gives something to measure against.

Nothing here is started beyond what `helios_view_changed()` already shipped. This is a genuine planning
conversation, not a known-answer task — the review names tradeoffs, not a single obvious next step.

## 3. Open loose ends, smallest first

- `glyphbuf` MLAB-inference regression on the `all6-combined` build (section 1 above) — flagged, not
  investigated. Worth a synthesis-only check before trusting "the MLAB win is banked" for this bundle.
- FLAC LPC: no Check test yet comparing hardware vs. software reconstruction on a real track
  (`docs/research/FLAC_LPC_KERNEL_DESIGN.md` section 7 item 4's own remaining piece), and depends on
  `lpc-b372` actually closing timing first.
- `docs/vendor/DOC012312972.pdf` will keep showing as untracked in every `git status` forever, by design
  — do not "fix" this by committing or gitignoring it without a fresh conversation with the owner; it is
  confidential.

## 4. Card state (not touched this session)

Unchanged since the last card write: `alfatreze.TAU_0_6_0_A_14`/`alfatreze.TAU_0_6_0_A_15` on the
all6-combined bitstream (now confirmed genuinely timing-closed, see section 1). No LPC bitstream has
ever been installed — `TAU_LPC` has never reached a Pocket.
