# Phase 1 SDRAM Pocket diagnostic

## Purpose and evidence boundary

This developer-only build verifies that the fitted Phase 1 MMIO mailbox can
write and read external SDRAM on a real Analogue Pocket without touching the
normal player firmware. A green `PASS` is **Pocket** evidence for the bounded
mailbox and tested memory locations only. It is not evidence for mapped SDRAM,
cached aliases, CRC/soak coverage, or playback under concurrent SDRAM traffic.

The diagnostic is a separate Pocket core/platform (`alfatreze.TAU_SDRAM_DIAG`
and `tau_sdram_diag`), so it can be installed beside the ordinary TAU player.
Its filesystem shortname is `TAU_SDRAM_DIAG`, exactly matching the core folder;
the friendly platform name remains **TAU SDRAM Diagnostic**.

## Safety boundary

The SDRAM controller addresses 16-bit words. The 400×360 framebuffer starts at
word zero and uses 184,320 words (360 KiB); Tau reserves the first 1 MiB for
framebuffer ownership and guard space. The diagnostic touches only even word
addresses from `0x00080000` through `0x000FFFFE`, corresponding to byte offsets
1–2 MiB. No player data has been assigned to that region in Phase 1.

The test is destructive inside that currently unused 1 MiB region. It does not
write BRAM, playlist/audio files, the Pocket SD card, or the normal TAU package.

## What this first gate tests

- Four fixed values: `00000000`, `FFFFFFFF`, `AAAAAAAA`, and `55555555`.
- Walking one and walking zero across all 32 data bits.
- 64 sparse address-as-data locations written as a set and then read back as a
  set, so address aliasing is not hidden by an immediate write/read pair.
- All four byte enables and both halfword enable combinations, verifying that
  untouched lanes retain their previous value.
- A 0.5-second per-operation timeout.
- 183 readback checks in a passing run.

The first failure retains word address, expected value, and actual value on the
screen. Status registers also retain a pass/fail signature, failure count,
first address, and first actual value for later instrumented capture.

## Reproduce the host artifacts

With the successful VM `ap_core.rbf` staged at
`work/diagnostics/sdram/fpga/ap_core.rbf`:

```sh
bash fw/build.sh sdram-diag
python3 tools/package_sdram_diagnostic.py
python3 tools/check_ui_snapshot_renderer.py
python3 tools/visual_review.py
```

The side-by-side Pocket tree is written to:

`work/diagnostics/sdram/pocket`

The package script writes `SHA256SUMS.txt`. For the 2026-09-14 candidate:

| Artifact | SHA-256 |
|---|---|
| VM `ap_core.rbf` | `0c00362795f22486af8aece80d1a3c6b1eb857e0783699a7fa6394163b1a6dc7` |
| Pocket `bitstream.rbf_r` | `b6719655bfddbdd06e9b135c1beeb1c82aa4c6559fa3ad4c3b095005a7534065` |
| Diagnostic `tau.rom` | `9b20a0c1cade4f260364a7551fb3366b169acc7408c639977138939dddf43a9f` |

### First installed candidate — superseded

On 2026-09-14 this candidate was copied to the mounted exFAT volume
`/Volumes/Pock`. Recursive source/destination comparisons passed for the new
core and asset directories, both platform files compared byte-for-byte, and
the card-side bitstream and ROM hashes match the table above. This is **host**
installation evidence only; the diagnostic has not yet been launched on Pocket.
That first package failed at Pocket core setup because its manifest shortname
used spaces while its core folder used underscores. It is superseded by the
identity-matched package documented in
[issue 008](issues/008-diagnostic-core-identity-mismatch.md); do not use the
earlier installation as SDRAM evidence.

The corrected identity-matched candidate was subsequently reinstalled and
flushed to `/Volumes/Pock`. Card-side folder/manifest identity and staged-file
hashes pass. Pocket relaunch remains the confirmation gate.

## Pocket procedure

1. Copy the generated `Cores`, `Assets`, and `Platforms` folders to the root of
   the Pocket SD card, merging folders when prompted. The 2026-09-14 candidate
   is already installed on the card mounted as `Pock`.
2. Under **Media Players**, launch **TAU SDRAM Diagnostic**.
3. Wait for a green `PASS`. A passing screen must say `READBACK CHECKS 183` and
   `FAILURES 0`.
4. Photograph the complete screen. Press A and repeat at least five times.
5. Power the Pocket fully off, cold boot, and repeat at least five times.
6. If any run fails, photograph the complete screen before pressing anything;
   retain the first word address, expected value, actual/timeout code, and run
   number. Do not proceed to mapped SDRAM or data migration.

## Acceptance and next gate

Pass this sub-gate only if all ten warm/cold runs report 183 checks and zero
failures with no display corruption or stall. Then build the separate
concurrency diagnostic: at least a 1 MiB CRC/pattern sweep while the highest
bitrate MP3 plays through each visualizer, with explicit audio-underrun and
framebuffer-stall evidence. FLAC remains outside the current acceptance matrix.
