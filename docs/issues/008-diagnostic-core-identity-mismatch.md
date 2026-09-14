# Issue 008 — Diagnostic core identity mismatch

**Status:** Resolved; corrected package reaches APF Run, runtime issue tracked separately
**Date:** 2026-09-14
**Evidence level:** **Pocket | code-review | host**

## Observed behavior

Launching the first side-by-side diagnostic package repeatedly produced:

```text
Load error in 'core'
General Error
```

followed by:

```text
Error in core setup
```

The diagnostic firmware never appeared, so this is a package/core-setup failure
and provides no evidence about the SDRAM mailbox.

## Cause

The installed directory was `Cores/alfatreze.TAU_SDRAM_DIAG`, while
`core.json` declared author `alfatreze` and shortname `TAU SDRAM DIAG`.
Analogue's core-definition documentation requires the folder to use
`AuthorName.CoreName`, with those components corresponding to the manifest's
`author` and `shortname`. The spaces/underscores therefore produced an invalid
identity at core setup.

This is the evidence-backed leading cause. It becomes confirmed only when the
corrected package reaches the diagnostic screen on Pocket; retain that
distinction if another setup error remains.

## Correction

The diagnostic shortname is now `TAU_SDRAM_DIAG`, exactly matching the existing
folder. `tools/package_sdram_diagnostic.py` asserts the derived identity before
publishing a bundle, preventing another mismatched package.

The platform's user-facing name remains **TAU SDRAM Diagnostic**; underscores
are confined to the filesystem/core shortname.

The corrected package was installed on `/Volumes/Pock` on 2026-09-14. Host-side
verification confirmed the installed folder/manifest identity and byte-matched
the core, assets, platform metadata, artwork, bitstream, and ROM against staging;
writes were flushed before detach. The subsequent Pocket developer log reaches
`Run` and Reset Exit without an APF load/setup error, clearing this package
issue. The missing visible result is tracked separately as
[issue 009](009-sdram-diagnostic-nonresponsive.md).

## Impact and recovery

The fault is limited to the separate diagnostic package. The normal
`alfatreze.TAU` core, player ROM, and SDRAM/FPGA logic are unchanged. Replace
the three diagnostic package paths on the SD card when it is remounted, then
launch again. Do not count the failed setup attempts in the ten-run SDRAM gate.

## Next gate

The corrected package reaches APF Run. A subsequent `PASS 183 / FAILURES 0`
remains the first Pocket SDRAM evidence; runtime display/mailbox diagnosis now
continues under issue 009.
