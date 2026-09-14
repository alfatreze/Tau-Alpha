# Issue 007 — SDRAM diagnostic staging failures

**Status:** Resolved on host; retained as implementation evidence
**Date:** 2026-09-14
**Evidence level:** **host**
## Context and expected result

The Phase 1 diagnostic added a dedicated `sdram-diag` firmware target and three
new framebuffer snapshots. The target was expected to write its ROM beneath
`work/diagnostics/sdram/` without changing the release ROM, and the snapshot
suite was expected to render all declared states.

## Failure 1 — missing target output directory

The first diagnostic invocation compiled and linked successfully, then objcopy
failed with:

```text
work/diagnostics/sdram/tau.rom: No such file or directory
```

`fw/build.sh` created its default output directory before target selection, but
`sdram-diag` changed `OUT` afterwards. No diagnostic ROM was written and the
existing release `dist/Assets/tau/common/tau.rom` was not overwritten.

**Resolution:** create the final selected output directory after the target
switch. The rebuilt diagnostic links to 4,895 bytes and emits a 4,872-byte ROM.

## Failure 2 — snapshot renderer color import

The first snapshot check failed with:

```text
NameError: name 'UI_BG' is not defined
```

Existing player fixtures normally begin with the dynamic gradient and had not
imported the flat `UI_BG` constant used by the diagnostic screen.

**Resolution:** read `UI_BG` from `fw/player.c`, as the other production colors
are read, and extend the check to require distinct running/pass/fail fixtures.
The suite now reports 24 deterministic 400×360 RGB565 fixtures and all three
diagnostic states have been inspected.

## Impact and workaround

Both failures were host-side build/review tooling defects caught before Pocket
installation. Neither changed RTL, the release ROM, playback data, or the
audio hot path. The fixes are permanent; no active workaround remains.

## Remaining gate

Host success does not validate the physical SDRAM. Follow
[the Pocket procedure](../SDRAM_POCKET_DIAGNOSTIC.md) and retain any device
failure as a new Pocket-evidence issue rather than reopening these resolved
staging failures.
