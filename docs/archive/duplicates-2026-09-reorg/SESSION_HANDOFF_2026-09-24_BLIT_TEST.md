# Session handoff — 2026-09-24 (B8 milestone, Blit Test, and an open hang)

**Read this first**, then `docs/CURRENT_STATUS.md`. `docs/AUDIT_TRAIL.md` entries **B-156 through
B-178** are this session's detailed log — this file is the summary, that file is the evidence.

Git: commits through `f3b047f`, not pushed. Working tree clean.

## TL;DR

1. **B8 (CLUT blit) is fully done and proven on real hardware** — timing-closed, firmware-
   integrated, survives a sustained 30s Blit Storm load test with continuous audio (B-164).
2. **A new diagnostic feature, "Blit Test," was built** (Settings > Diagnostics > Blit Test) —
   exercises every draw-engine opcode individually, full screen, no audio needed. Three opcodes
   (`OP_RUN`, `OP_BAR`, `OP_SBLIT`) had never been issued from firmware before this at all.
3. **Blit Test hangs on real hardware, and four different fix attempts have not resolved it.**
   The card is currently left as-is (owner has cable access "in a few hours") rather than attempt
   a fifth blind fix. See section "The open hang" below.
4. **ISSP (In-System Sources and Probes) is built, fit-proven, and its JTAG readback procedure is
   documented and verified against the real tooling** — ready to actually diagnose the hang the
   moment a JTAG cable is available. This is the recommended next step, not another guess.
5. **A real cross-project boundary rule was set and enforced**: Tau Alpha and the sibling `Tau
   Omega` repo must never share literal files, only reference each other's. Two violations found
   and fixed this session (an ID-counter collision, a duplicated fixture file).

## B8: done

RTL/simulation-correct (B-148), the timing violation's exact gate chain diagnosed (B-157), the
retiming fix closed on the first re-fit attempt (B-158/B-159), firmware-integrated (B-160), a real
boot-blocking bug found and fixed (B-162), and proven on real hardware under sustained load with
real audio (B-164) — SDRAM busy 15.8%, identical to the pre-CBLIT baseline, zero added cost. The
`Track changes` Check failure remains open and pre-existing (unrelated to B8, not caused or fixed
by this session's work) — instrumented with real diagnostic capture (B-165) so the next run
explains *why* it fails, not yet run.

## Blit Test: built, but hangs — the open thread

**What it is** (B-166): a new Diagnostics page, 8 opcodes × 3 concurrency levels × 2s windows,
tiling cells across the full 400×360 visible screen, own QR report. Deliberately separate from
Check (which assumes playback context).

**What's gone wrong, in order:**

1. **B-168**: refused ("BLIT ENGINE NOT DETECTED") on first try — a pure code-ordering bug
   (`BLIT_READY()` checked before the probe that sets it ran). Fixed, verified in source.
2. **B-170**: hung on card, same way as the earlier boot hang (B-162). Hypothesis: `blit_probe()`
   was the only code anywhere that ever wrote a non-default `DST_STRIDE`. Rewritten to use default-
   stride addressing (same convention as every proven-working `OP_BLIT` caller). **This did not
   fix it** — hung again, exactly the same way, on the very next install.
3. **B-174**: added draw-based checkpoint breadcrumbs inside `bt_begin()` (the only place any
   "begin"-style function in this codebase draws synchronously from an input-handler edge, rather
   than the normal deferred per-frame dispatch every other page uses). **Showed nothing at all** —
   not even the first checkpoint — ruling out `blit_probe()`/CBLIT as the sole cause.
4. **B-176**: added memory-only crumbs (raw SDRAM writes, no `fb_wait()`, cannot hang by
   construction) plus a readback display on the idle page. **The hang got *worse*** — now happens
   on merely opening the page, before pressing A to start at all, and the new crumb-readback text
   line is completely absent from the screen (not garbled — just never drawn), while the
   surrounding lines (existing, unchanged code) render fine.

**Current honest assessment**: four fix attempts based on source-level hypotheses have each been
individually reasonable but none has resolved it, and the newest attempt made things worse in a way
that's hard to explain from source alone (a page-open-time hang, on a line whose own read operation
cannot hang). This has exceeded what source-reading can diagnose. **Do not attempt a fifth blind
fix** — the next real step is live hardware tracing.

**Card state**: `TAU_0_5_0_A_12` (B-176's build, the one that hangs on page open) is what's
currently installed, left as-is deliberately (owner instruction). Cores: `TAU`, `TAU_DIAGNOSTIC`,
`TAU_DEV_42`, `TAU_0_5_0_A_12`.

## ISSP: the real next step

Built (B-172), fit-proven clean on the first attempt (B-173: setup +1.99/+1.87 ns, RAM identical to
the non-ISSP build at 299/308 — near-zero cost), and its JTAG readback procedure is written up and
verified against the live `system-console` tool (B-178) — **not yet exercised with a real cable**.

**`docs/JTAG_DEBUG_ACCESS.md` section 6 has the full procedure.** Short version:
1. `quartus_pgm -m jtag -o "p;ap_core.sof"` from `~/tau-local/issp-synth-20260924/src/fpga/output_files/`
   (no SD card write — loads over whatever's already running).
2. Reproduce the hang.
3. `system-console --cli` (real path: `quartus/sopc_builder/bin/system-console`, NOT the main `bin/`).
4. `set p [lindex [get_service_paths issp] 0]; set c [open_service issp $p]; issp_read_probe_data $c`
5. Decode against section 6.2's bit table: `astate[3:0]` (which dispatch state it's stuck in),
   `fifo_fill[12:4]`, then 6 mode-flag bits (which opcode's own path is active).

This directly answers "what is the draw engine actually doing" without guessing — the tool this
whole debugging thread has needed since B-162.

**Never in the release or the normal Diagnostic Build** — `TAU_ISSP` is its own qsf variant
(`tools/blit_g3_issp_qsf_append.txt`), per explicit owner instruction.

## Cross-project boundary rule (Tau Alpha / Tau Omega)

Set explicitly this session, owner instruction: **the two repos must never share literal files,
only reference each other's.** Two real violations found and fixed (B-156):
- `AUDIT_TRAIL.md`'s bare `B-NNN` counter had been reused across both repos and collided twice —
  fixed by moving Tau Omega's own work entirely out of this file's ID series (a one-line pointer
  to its own docs instead).
- B-133 had literally copied the same 13 screenshot fixtures into both repos — fixed by keeping
  `Tau Omega/testdata/screenshots/` as the one canonical copy, deleting this repo's duplicate.

If this pattern needs checking on the Tau Omega side too, a prompt for that is in this
conversation's own history (search for "cross-project" there) — not repeated here since it's
Tau Omega's own action item, not this repo's.

## Other open items (pre-existing, not from this session)

- `Track changes` Check failure — now instrumented (B-165), not yet re-run.
- Tier 2 items (B9-B11) and the M10K/RAM-shrink track — next per `PHASE_F_SPEC.md` section 14's
  own ordering, once the hang is resolved.

## Resume prompt

> Read `docs/SESSION_HANDOFF_2026-09-24_BLIT_TEST.md` first, then `docs/CURRENT_STATUS.md`. B8
> (CLUT blit) is done and proven on real hardware (B-164) — that thread is closed. The open thread
> is Blit Test (a new Diagnostics page, B-166): it hangs on real hardware, and four fix attempts
> based on source-level hypotheses (B-168 ordering bug, B-170 stride rewrite, B-174 draw-based
> checkpoints, B-176 memory-only crumbs) have not resolved it — the last one made it worse in a way
> that's hard to explain from source (hangs on merely opening the page now, before pressing A).
> **Do not attempt another blind source-level fix.** The real next step is ISSP (already built,
> fit-proven, `docs/JTAG_DEBUG_ACCESS.md` section 6 has the full readback procedure verified
> against the live `system-console` tool) — the owner has JTAG cable access "in a few hours" from
> this handoff. When they do: load the ISSP `.sof` via JTAG (`quartus_pgm`, no SD card write needed,
> path is in the doc), reproduce the hang, read the probe, and decode `astate`/`fifo_fill`/the mode
> flags per section 6.2's table to find out what the draw engine is actually stuck on. Card is
> currently `TAU_0_5_0_A_12` (the hanging build), left as-is deliberately — don't reinstall
> anything until there's a real diagnosis to act on. Separately: the cross-project rule (Tau Alpha
> and Tau Omega must never share literal files, only reference each other's) was set this session
> and enforced on this side; if Tau Omega hasn't been checked for the same issue, that's its own
> follow-up. Commits are through `f3b047f`, not pushed; ask before pushing.
