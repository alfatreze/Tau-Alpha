# Interop: how Tau-Alpha and Tau Omega pass work to each other

Proposed 2026-10-26 (owner asked for a working approach). Builds on `docs/CROSS_PROJECT_INTERFACE.md`, which stays the rule (Tau-Alpha is the source of truth; never share literal files; verify against a real artifact). This folder is the **mailbox**.

## The problem it fixes
Messages travel by the owner pasting between two sessions. Answers get lost (Omega's second round left four questions unanswered), corrections are re-explained, and nobody can see what is still open.

## The approach: one outbox each, one ledger, fixtures by reference
1. **Each project writes only to its own outbox and reads the other's** (read-only, as today):
   * Tau-Alpha: `docs/interop/to-omega/NNN-YYYY-MM-DD-title.md` (this repo)
   * Tau Omega: `docs/interop/to-alpha/NNN-YYYY-MM-DD-title.md` (Omega's repo; Omega's call to adopt it)
2. **A message is short and numbered.** Header: `From`, `Date`, `Replies to` (message number or none), then a list of items, each with an id (`Q-017` question, `D-` decision, `R-` request, `F-` fact/correction) and, for questions, who must answer. Replies answer by id, so nothing is silently skipped.
3. **One ledger** (`LEDGER.md`, this repo) lists every open id with owner and date; closed ones move to its bottom. Whoever answers edits their own side's message, and the Tau-Alpha session mirrors the status into the ledger. The owner reads the ledger, not the chat.
4. **Fixtures are referenced, not copied:** a message names a repo path plus its SHA-256 (and the commit). The other side reads it in place; if it needs a pinned copy it asks (R-id) and the owner approves the exception. The one sanctioned exception so far: `rehearsal-*` folders (a throwaway release package for Omega to test install/check-card/removal), clearly named, SHA-256 listed, never in `release/`.
5. **The owner's job shrinks to one sentence per session:** "read `docs/interop/` of the other project and answer the open ids". No re-pasting.
6. **Status lines:** every message ends with `Open for you:` (ids) and `Open for us:` (ids).

## What stays out
No shared code, no shared schema files copied across, no automatic sync. Contracts remain the generated files (`tau-compat.json` and its schema) that Tau-Alpha publishes.
