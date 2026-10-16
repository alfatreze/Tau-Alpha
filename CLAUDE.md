# Tau Alpha: project instructions

## 1. Who does what
- **Claude (this session):** does the work in this repo: design, firmware, RTL, host tests, tools, docs, card installs and Quartus fits on the VM, following the rules below.
- **Codex (optional second opinion):** reads `AGENTS.md` (which points it at the `analogue-pocket-dev` skill). `tools/triad/` and `docs/DEVELOPMENT_WORKFLOW.md` describe an experimental Codex-plans, local-Qwen-writes-a-patch workflow; it is not part of the day-to-day path. There is no Aider and no automatic adversarial-review step.
- **The owner** decides priorities, approves card writes and VM fits, and tests on the Pocket.

## 2. Start of a session
1. Read `docs/CURRENT_STATUS.md` (one page: what is shipped, what is installed, what is in flight), then `docs/ROADMAP.md` (the single "what is next" list; the owner sets the order), then the newest file in `docs/handoffs/`.
2. `docs/AUDIT_TRAIL.md` is the evidence log (A-NNN SDRAM/UI era, B-NNN since; log every hardware result with an evidence label). `docs/DECISIONS.md` holds recorded owner decisions; `docs/issues/` the open issues. Feature specs are in `docs/features/`; prefer the subdirectory copy when a file exists twice.
3. Commit only your own files; never stage another session's uncommitted work. Ask before any SD-card write and follow the backup / clear-catalog / verify-SHA-256 / eject procedure in `docs/CARD_INSTALL_PROCEDURE.md`. **Do the install with `tools/install_dev_core.py` (dry run first, `--yes` after approval), never hand-typed; pass `--card /Volumes/Pock` (the tools default to CARDWRITE). When the procedure changes, change the script, its test (`sim/test_install_dev_core.py`) and the doc together.**
4. **One status format:** `docs/ROADMAP.md` is the ORDER of work (the owner sets it), `docs/CURRENT_STATUS.md` is where things stand NOW (one page), `docs/handoffs/` hold the detail of one session. Do not add "newest" pointers or a log to this file; the old chain is archived in `docs/archive/CLAUDE_SESSION_START_CHAIN_2026-10-14.md`, the old log is `docs/EXECUTION_LOG.md`.

## 3. Rules
@docs/PROJECT_RULES.md

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
