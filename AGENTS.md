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
