# Tau project working agreements

## UI framebuffer snapshots are required

Every distinct UI state that is added or materially changed must have a named,
reproducible framebuffer snapshot for review. A UI change is not complete until
its corresponding snapshot has been generated and inspected.

- Capture the lowest-level output available: the actual packaged asset or the
  rendered 400x360 framebuffer, not merely the Figma/source artwork.
- Give each state a stable name, such as `boot-loading`, `empty-library`,
  `playlist-error`, `now-playing`, `paused`, `seeking`, `playlist-browser`, and
  each visualizer mode.
- Include materially different substates where they change composition,
  clipping, contrast, or interaction feedback.
- Read layout, color, type, and state constants from production sources where
  practical so the snapshot cannot silently drift from the build.
- Add the state to `make visual-review`; captures belong in `work/previews/`
  and are regenerable rather than release inputs.
- Validate the native 400x360 image before any enlarged presentation copy.
- State clearly when a capture is an approximation. A desktop capture can
  verify layout, palette, RGB565 conversion, clipping, and draw order, but a
  Pocket hardware photo remains authoritative for panel gamma and brightness.
- When a hardware photo disagrees with the capture, retain both as evidence and
  tune against the device without weakening deterministic framebuffer checks.

The current loading-state implementation is the first fixture:
`make visual-review` generates
`work/previews/tau-loading-framebuffer.png` from the shipped RLE/RGB565 asset.

## Publication ledger

`PROJECT.md` is the public, concise source of truth for completed milestones,
current constraints, and the next staged plan. Update it before every push or
release to GitHub whenever the published state has materially changed. Its
summary must distinguish hardware-confirmed outcomes from host-only checks and
must retain the project's upstream provenance.

## Decision and issue handoff record

The documentation is also the project handoff record for future humans and
models. Do not leave material reasoning only in a chat transcript.

- Record an architectural or implementation decision in `docs/` when it
  affects scope, interfaces, trade-offs, resource/timing budget, validation
  strategy, or a later implementation choice. State the decision, alternatives
  considered, evidence, consequences, and any remaining gate.
- Record a failed, blocked, surprising, or unresolved technical result as a
  numbered file in `docs/issues/`. Include reproduction context, observed and
  expected behaviour, impact, evidence/log location where practical, the safe
  workaround, and the next investigation step.
- Update the relevant decision or issue after new evidence; do not silently
  overwrite a previous conclusion. Mark whether a result is simulated,
  host-tested, Quartus-verified, or confirmed on Pocket hardware.
- Keep `PROJECT.md` concise, but link or name the detailed record whenever it
  changes the project’s next action or confidence level.
- Before asking another model to continue, ensure the current working tree,
  verification state, active build status, and known risks are documented.

### Audit-entry requirements

For every material implementation, decision, reversal, or failed approach,
append a chronological entry to `docs/AUDIT_TRAIL.md`. A useful entry contains:

- a stable sequence ID and date;
- the decision or change, including alternatives considered and why they were
  rejected or deferred;
- the affected hot path / cold path classification, or an explicit statement
  that neither is affected;
- verification method and evidence location, tagged **code-review**, **host**,
  **simulation**, **Quartus**, or **Pocket** — never upgrade the claim beyond
  its actual evidence;
- resource/timing figures when hardware is touched, including a comparison to
  the prior recorded fit; and
- outcome, workaround/revert if applicable, remaining risk, and next gate.

Retain reversals and dead ends. A corrected conclusion must link to the earlier
entry rather than replacing it, because false-but-plausible assumptions are
valuable audit evidence. Do not invent trend values: mark a metric pending
until it is measured.
