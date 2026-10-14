# Project rules (shared by Claude Code and Codex)

The eight rules every session follows. `CLAUDE.md` imports this file; `AGENTS.md` points here. Longer reasoning lives in the linked documents.

1. **Concurrent sessions: use worktrees, and check the branch before committing.** A plain `git checkout <branch>` moves `HEAD` for the whole directory and every other session with it (2026-10-01: a commit landed on the wrong branch). For a second task use `git worktree add ../tau-alpha-<name> <branch>` (or the Agent tool's `isolation: "worktree"`). Run `git branch --show-current` immediately before every commit in a shared tree. Commit only your own files; never stage another session's uncommitted work. Commit verified work promptly.
2. **Never `git reset` or `git stash` on the shared tree** (not even for a "clean baseline" measurement): it destroyed other sessions' work twice (2026-09-27/28). Build to a separate output directory and diff, or use a worktree. Never modify this repository's upstream tracking references.
3. **When you launch a Quartus fit, say how long it will take and the local finish time** ("expect it done around HH:MM"), computed from the launch time and this bundle's own earlier fits (the shipped family lands near 1h40-1h45). Repeat or correct it whenever status is asked. `tools/vm_fit.py launch` is the way to start one.
4. **Card installs go through `tools/install_dev_core.py`** (dry run first, `--yes` after approval, `--card /Volumes/Pock`, never hand-typed), and **ask before any SD-card write**. The procedure is `docs/CARD_INSTALL_PROCEDURE.md`; when it changes, change the script, its test and the doc together.
5. **New diagnostic-only code is a registered feature** in `fw/diag_features.json` (never a bare `#if TAU_DIAGNOSTIC`). **If a build falls under its heap floor, drop features; never lower the floor** (`fw/build.sh` prints the cheapest to drop). Details and the owner's standing decisions: `docs/features/DIAG_FEATURES.md`.
6. **Measure against the shipped 192 KB link** (`RAM_192K=1 CLK66=1 SDRAM_BUSY=1`); a 256 KB build is not what ships. `tools/check_heap_gap.py` does it.
7. **Never copy copyleft code into this MIT project.** Read it for ideas only and record the inspiration in `docs/PROVENANCE.md` (decision D-P01).
8. **After publishing a release, compare the release's assets with `release/SHA256SUMS.txt`.** A stray workflow zip once slipped into a published release (B-687). Pass `--repo alfatreze/Tau-Alpha` to `gh release create` (the default target is the upstream fork parent).

Record every finished change as an entry in `docs/AUDIT_TRAIL.md` (B-NNN, with the evidence label and what was and was not run on a Pocket).
