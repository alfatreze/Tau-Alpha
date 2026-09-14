# 013 — Stress HUD duration wraps after 71.58 seconds

**Status:** Fixed, rebuilt, and staged; long-duration Pocket validation deferred to a future stress session
**Date:** 2026-09-14
**Evidence level:** **code-review | Pocket**

## Observation

During the completed visualizer matrix, the HUD's reported pass time did not
agree with the player display. For example, `ST BARS P1 61% 01:09` appeared
while the player showed `02:24 / 04:38`; similarly, the Levels screen reported
`01:09` while its music display showed `02:04 / 03:13`.

## Cause

`mp3_soc.v` exposes `R_CYCLES` as a 32-bit counter incremented every 60 MHz
`clk_sys` edge. Its period is `2^32 / 60,000,000 = 71.582788...` seconds. The
first HUD implementation calculated a duration with one raw unsigned
subtraction (`now - pass_started`). It therefore reports only the modulo-71.58
second remainder of any longer pass.

The player time is based on decoded audio-frame progress, so it is neither the
same clock nor necessarily aligned to the start of stress. It is evidence that
the HUD value is implausible, not an independent duration measurement.

## Fix and verification gate

The stress-only firmware now samples `R_CYCLES` every main-loop iteration and
adds each modular delta to a whole-seconds plus remainder accumulator. This
does not require a 64-bit runtime helper or any RTL change. Since normal loop
iterations are vastly shorter than 71.58 seconds, each delta is unambiguous.

The dedicated stress ROM was rebuilt at 127,876 bytes (SHA-256
`1860455e5fbdc6f222194be0a7c5ed97990cdaf18ab5a77a76368d7c3bb2cd5e`),
packaged, copied to `/Assets/tau_sdram_strs/common/tau.rom`, and byte-verified
on the mounted card. The prior HUD ROM (SHA-256 `61d39f955bde907fb63f59cca1c560ad31ea965c9d588627252db585b2bc3b9a`)
is retained under `work/diagnostics/sdram-stress/pre-timer-wrap-rom-2026-09-14/`.

No dedicated retest is required for the completed SDRAM contention gate. At a
future convenient stress session, let any loaded pass run beyond 72 seconds and
confirm the HUD continues past `01:11` without a mismatch, timeout, audio
dropout, or display corruption. Until that Pocket result exists, do not use the
handwritten `L` times as throughput data.
