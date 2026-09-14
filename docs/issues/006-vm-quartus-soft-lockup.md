# 006 — Stale VM console logs mistaken for active Quartus soft-lockup

**Status:** Resolved as a current-build false alarm; earlier guest lockups remain historical and unexplained
**First observed:** 2026-09-14
**Evidence level:** **host** and **Quartus**

## Context

After controlled Quartus isolations 1–5, the required next gate was a fresh
build of the current FPGA source from a local ext4 VM copy. The source snapshot
was `/home/taualpha/tau-current-b729a7b`.

## Initial observation and provisional interpretation

The UTM guest console showed buffered Linux `BUG: soft lockup` messages naming
`quartus_fit`, `quartus_asm`, and `quartus_sta`, plus `systemd` watchdog
timeouts. The text console was at a guest login prompt. The managed build
session produced no output during a 30-second poll, and the first SSH probe
used non-interactive `BatchMode`; it reported authentication failure because
it could not answer the password prompt. These facts initially led to a
provisional conclusion that the fresh build might be stalled. That conclusion
was reversed after an interactive SSH login and timestamp reconciliation.

## Corrected chronology and current build result

An authenticated read-only shell reported guest boot time `2026-09-13 12:07:22`.
The latest visible kernel watchdog timestamp was about 3,784.94
seconds after boot, around `2026-09-13 13:10`, over eleven hours before the
fresh build began on 2026-09-14. Those console lines were stale scrollback, not
evidence that the current build was soft-locked.

The exact-source snapshot's Quartus flow report says **Successful** at
`2026-09-14 01:24:08`; `ap_core.done` is stamped `01:27:38`. It generated
`ap_core.sof` and `ap_core.rbf` and completed timing analysis. No Quartus
process was running when inspected later because the build had already
finished. See [issue 005](005-quartus-assembler-internal-error.md) and audit
entry [A-024](../AUDIT_TRAIL.md).

The macOS QEMU CPU/memory samples and qcow2 modification times collected at
07:04–07:07 were more than five hours after the build finished. They show later
VM activity and host memory pressure, not Quartus progress or resource
conditions during the compile. The user recalls full-screen Crunchyroll
playback during the build; it remains a possible but unmeasured confounder,
not an established cause of delay or guest lockups.

## Impact and disposition

- The fresh Quartus build gate passed; no restart or rerun was needed.
- The original guest watchdog messages may indicate an earlier soft-lockup
  episode, but this record cannot attribute them to the Sep 14 compile.
- Pocket behavior remains unverified. The passing build is not hardware
  validation.

## Next investigation

Proceed to the controlled Pocket SDRAM diagnostic. If guest soft-lockups recur
during a future build, capture guest boot time, contemporaneous kernel journal,
Quartus stage/report timestamps, and host/VM resource measurements before
classifying the cause. See audit entries A-021–A-024 for the retained initial
interpretation and correction.
