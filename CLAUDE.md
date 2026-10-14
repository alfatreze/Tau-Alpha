# Tau Alpha: project instructions

## 1. Who does what
- **Claude (this session):** does the work in this repo: design, firmware, RTL, host tests, tools, docs, card installs and Quartus fits on the VM, following the rules below.
- **Codex (optional second opinion):** reads `AGENTS.md` (which points it at the `analogue-pocket-dev` skill). `tools/triad/` and `docs/DEVELOPMENT_WORKFLOW.md` describe an experimental Codex-plans, local-Qwen-writes-a-patch workflow; it is not part of the day-to-day path. There is no Aider and no automatic adversarial-review step.
- **The owner** decides priorities, approves card writes and VM fits, and tests on the Pocket.

## 2. Start of a session
1. Read `docs/CURRENT_STATUS.md` (one page: what is shipped, what is installed, what is in flight), then `docs/ROADMAP.md` (the single "what is next" list; the owner sets the order), then the newest file in `docs/handoffs/`.
2. `docs/AUDIT_TRAIL.md` is the evidence log (A-NNN SDRAM/UI era, B-NNN since; log every hardware result with an evidence label). `docs/DECISIONS.md` holds recorded owner decisions; `docs/issues/` the open issues. Feature specs are in `docs/features/`; prefer the subdirectory copy when a file exists twice.
3. Commit only your own files; never stage another session's uncommitted work. Ask before any SD-card write and follow the backup / clear-catalog / verify-SHA-256 / eject procedure in `docs/CARD_INSTALL_PROCEDURE.md`. **Do the install with `tools/install_dev_core.py` (dry run first, `--yes` after approval), never hand-typed; pass `--card /Volumes/Pock` (the tools default to CARDWRITE). When the procedure changes, change the script, its test (`sim/test_install_dev_core.py`) and the doc together.**
4. The old chain of per-session "Newest" pointers that used to live here is archived in `docs/archive/CLAUDE_SESSION_START_CHAIN_2026-10-14.md`.

## 3. Strict Project Rules
- Never modify this repository's upstream tracking references.
- Record each finished change as an entry in `docs/AUDIT_TRAIL.md` (B-NNN, with the evidence label and what was and was not run on hardware). The old per-turn log that lived in this file is `docs/EXECUTION_LOG.md`.
- **Never run `git reset`/`git stash` against the shared working tree for a "clean baseline"
  comparison (e.g. "stash my edits, measure something, pop the stash").** This repo has multiple
  concurrent sessions/agents working in the same checkout at once; a reset or a colliding stash-pop
  has already silently destroyed other sessions' uncommitted work more than once (2026-09-27/28 night
  session, see AUDIT_TRAIL.md around that date -- real work was lost and had to be redone twice
  before this rule was added). If a task needs a "before" comparison, build to a separate output
  directory and diff the results, or do the work in an isolated git worktree (`Agent` tool's
  `isolation: "worktree"`, or `git worktree add`) instead of touching the shared tree's index/stash.
  Commit real, verified work promptly rather than leaving it sitting uncommitted for long stretches --
  a stray reset elsewhere can't destroy a commit that already exists.
- **A plain branch does NOT isolate concurrent sessions sharing this checkout -- a worktree does.**
  `git checkout <branch>` changes what `HEAD` points to for the ENTIRE directory, so switching
  branches in the shared tree moves every other session's `HEAD` out from under it too (2026-10-01: one
  session's branch switch silently landed another session's commit on the wrong branch -- recovered via
  `git cherry-pick` into a separate worktree, nothing lost, but it should never have happened). If the
  owner asks for "a new branch" for a second concurrent task, treat that as a request for an isolated
  **worktree** (`git worktree add ../tau-alpha-<name> <branch>`, matching the already-established
  `tau-alpha-cymo` sibling-directory pattern, or the `Agent` tool's `isolation: "worktree"`) unless the
  prior session has already finished and the shared tree is free. Always run `git branch --show-current`
  immediately before any commit in the shared tree, not just `git status` for file changes -- a stray
  checkout can move `HEAD` without touching the working files at all, so file-level status alone won't
  catch it.
- **Whenever a Quartus fit is launched (`tools/vm_fit.py launch`), state an estimated duration and an
  estimated LOCAL finish clock time in the same message that reports the launch** -- not just "typical
  50 min-1h45m" as an abstract range, but "expect it done around HH:MM" computed from the actual launch
  time. Re-state this estimate (or correct it) whenever asked to check status. Base the estimate on this
  exact macro bundle's own prior fits where known (most of this project's recent history is the same
  `blit_g3_poly_blend_ram192_clk66_dbuf_lpc*` family, which has consistently landed near the top of the
  range, ~1h40-1h45m), falling back to the general range for an unfamiliar bundle.

## 3b. Diagnostic-Build features and RAM (read before adding or removing anything diagnostic)
Free hot RAM is scarce (the shipped 192 KB link: release about 9 KB, Diagnostic Build 1.6 KB by default). Everything that exists only in the Diagnostic Build is a **registered feature** in `fw/diag_features.json` (17 so far; kind `dx` diagnostic or `fx` incubating feature; full guide `docs/features/DIAG_FEATURES.md`, plan `docs/features/RAM_DIET_PLAN.md`).
- **New diagnostic-only code is a new registered feature, never a new bare `#if TAU_DIAGNOSTIC`** (`sim/test_diag_features.py` fails if a bare site is added). Steps: register it, wrap its code, menu row, page-table row and hooks in `TAU_DX_<ID>` / `TAU_FX_<ID>`, keep the release behaviour in the `#else` (Info rows keep their slot and read "-"), prove the default builds are byte-identical (hash `tau.rom` and `tau-cold.bin` before and after), then run `python3 tools/diag_cost.py` to record its cost.
- **If a build falls under its heap floor, drop, do not lower the floor.** `fw/build.sh` prints the cheapest features to drop (`DIAG_DROP=<ids>`, or `DIAG_PRESET=perf|slim`); each feature's class and "safe to drop" note say what dropping it does to the remaining tests. Only `cymo_toggle` changes the default audio path; `stress` is the only large hot cost (2.9 KB).
- **A feature that proved itself graduates** (`FEATURES_ON=<id>` in the release target) or is deleted; do not leave it behind the flag indefinitely.
- Standing owner decisions: tempo ships in the normal release (`fw/build.sh` default); the MP3 ring stays at 24 KB; the QR encoder is archived (`archive/qr_encoder/`, tag `archive/qr-encoder`), the pixel grid is the only report view; `PolyphaseStereo` stays hot (it is the stereo fallback and playback must work without the cold image).
- Measure with the shipped flags (`RAM_192K=1 CLK66=1 SDRAM_BUSY=1`); `tools/check_heap_gap.py` does. A 256 KB build is not what ships.

## Subagent delegation policy

Before spawning a subagent (Task/Agent tool), check whether it's actually worth the overhead:
- Only delegate when the work is verbose/high-volume (test runs, log scanning, multi-file research,
  large doc fetches) or genuinely parallelizable. Small, quick lookups stay in the main context —
  a subagent's own startup cost (loading CLAUDE.md, skills, MCP tool defs) isn't worth it for trivial work.
- Never spawn a subagent just to "be thorough" — spawn one because its output would otherwise bloat
  the main conversation.

When delegating, write the subagent prompt to minimize its cost and the tokens it returns:
- Scope it tightly: state the exact question/deliverable, the specific files/paths/URLs it needs
  (not "explore the codebase"), and an explicit output format so it doesn't over-explain.
- Explicitly cap output: tell it to report back a short summary or the specific artifact needed —
  not full transcripts, full file contents, or step-by-step narration — since only the returned text
  re-enters the main context.
- Pick the cheapest model that can do the job: use `model: haiku` for mechanical/simple subagent tasks
  (formatting, running a command and reporting pass/fail, simple lookups), and reserve a stronger model
  for subagents doing real reasoning or judgment calls.
- Keep the spawn prompt itself short — everything in it enters the subagent's context immediately,
  on top of whatever CLAUDE.md/skills/MCP tools it auto-loads.
- Prefer one well-scoped subagent over several redundant ones; batch independent lookups into a single
  subagent call when they don't need separate context isolation.
- If a task doesn't clearly need isolation (verbose output, parallelism), just do it directly instead
  of delegating.

## AUDIT COMPLIANCE TARGETS
- **Code Style:** Ensure consistent naming conventions and formatting.
- **Security:** Regularly review for potential vulnerabilities.
- **Performance:** Optimize code for efficiency.
