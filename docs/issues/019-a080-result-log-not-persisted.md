# Issue 019 — A-080 result log is not persisted on Pocket

## Status

**Resolved 2026-09-19** (A-091, Pocket evidence). Original **Pocket** failure
observed 2026-09-19; history below is retained.

### Resolution

Two independent faults, neither in SDRAM:

1. **Wrong bridge base (A-082..A-087).** The log write/read used `0xF8000000`;
   the datatable is bridged at `0xF8002000`. Fixed in A-088; `READ D ERR 0 DATA
   544C4F47` then confirmed the record reached slot 5.
2. **`0184`/`0188` never persisted a file.** Even with the address fixed, slot 5
   in APF's table was correct (size 64, table intact), `0188` flush was
   unanswered after 10 s (A-090), the second read timed out on the occupied
   bridge, and the SD file stayed zero after Quit (A-089, A-090). The
   `nonvolatile` parameter (A-089) made no difference.

**Fix:** publish the record through the `interact.json` persist channel (A-091):
16 words, 31-bit encoded, stored by APF on Quit to `Settings/<core>/Interact/
_core/interact_persist.json`, decoded by `tools/decode_tau_diag_log.py
--interact`. Verified on hardware: screenshot `20260919_232118.png` matches the
decoded file (checksum `0x478E986A`). Evidence in
`work/diagnostics/sdram-cpu-probe-a091/pocket-result/` and AUDIT_TRAIL A-088 to
A-091. The slot-5/`0184` path is abandoned; the earlier "Next diagnostic"
paragraph below is superseded.

Not resolved by this issue: the SDRAM CPU return-path failure (181/183 at
`0xA0200000`), which remains open elsewhere.

## Observed result

The A-080 CPU SDRAM probe reaches its normal terminal result: 183 readback
checks, 181 failures, first byte address `0xA0200000`, expected `0xFFFFFFFF`,
actual `0x00000000`. Pocket saved two native screenshots at:

- `Memories/Screenshots/20260919_193036.png`
- `Memories/Screenshots/20260919_210853.png`

Both render the same expected diagnostic screen. The installed A-080 RBF and
ROM match their package hashes. But the dedicated 64-byte save file remains
entirely zero and the decoder rejects it with `bad magic: 0x00000000`.

## What this proves

- The test UI and existing A-079 mux diagnostic execute normally.
- The saved file exists at the expected isolated A-080 path.
- A-080 persistence is **not Pocket verified**; no diagnostic result may be
  recovered from the file.

## What it does not prove

- Whether target `0184` was never issued, was rejected by Pocket, read the
  wrong bridge address, or completed without updating the selected slot.
- Whether target `0188` was reached; it runs only after a successful write.
- Any new conclusion about the SDRAM data-path failure.

## Next diagnostic

**A-082 Pocket result:** Screenshot `20260919_214432.png` reports `WRITE D ERR
0` and `FLUSH T ERR 0`. The target write command completed with Pocket result
code zero; the target flush command did not complete before the core's local
timeout. The 64-byte save file remains all zero. The same screen recorded 183
checks / 180 failures, first failing address `0xA0200000`, expected
`0x00000001`, actual `0x00000000`; this remains SDRAM diagnostic output, not a
log-path conclusion.

## Next diagnostic

**Post-Quit result:** After a full Pocket root-menu Quit and SD-card remount,
the A-082 file remains exactly 64 zero bytes. Pocket's normal nonvolatile
shutdown path did not persist the completed `0184` write.

The next firmware-only probe must use target `0180` to read slot 5 back into a
safe APF-visible datatable location before exit, then render its first word.
This distinguishes “write command completed but did not change the slot” from
“slot changed but save-file persistence failed.” No SDRAM logic changes are
justified by this issue.

**A-083 Pocket result:** Screenshot `20260919_215538.png` reports `WRITE D ERR
0`, `FLUSH T ERR 0`, and `READ D ERR 0 DATA 00000000`. The target read command
completed but returned the slot's original zero word immediately after the
acknowledged write; the mounted-card file is likewise 64 zero bytes. This
rules out a simple “only deferred persistence failed” explanation. It does
*not* prove why the write was a no-op: the APF slot may not be active/open, or
the target API may have a different slot-lifecycle requirement.

**Next diagnostic — A-084:** Firmware-only, same verified rev-23 RBF. Before
the result write, request APF's slot-5 descriptor with `0190`, copy that
descriptor byte-for-byte from the established response buffer to the separate
`0192` parameter buffer, then issue `0192` to open it. This mirrors the
working production playlist implementation and avoids assuming an undocumented
descriptor layout. Its screen renders `OPEN`, `WRITE`, `FLUSH`, and `READ`
outcomes. A successful immediate `READ ... DATA 544C4F47` is the gate before
investigating deferred persistence; an open/read error is evidence for the
slot-definition/lifecycle path. No SDRAM logic changes are in scope.

**A-084 Pocket result:** Screenshot `20260919_220843.png` reports `OPEN D ERR
0`, `WRITE D ERR 0`, `FLUSH T ERR 0`, and `READ T ERR 0`; the card file remains
the original 64 zero bytes. Opening was accepted, but the immediate post-open
read did not answer. This matches the production player's documented observed
behavior: `0192` completes before the newly opened slot is ready for subsequent
I/O. It does not establish that a settled write will fail.

**Next diagnostic — A-085:** Firmware-only, same RBF. After `0192`, issue
bounded 100-ms `0180` reads at roughly 30-ms intervals and require two
consecutive successful responses (at most 16 attempts) before the existing
write/flush/read sequence. The terminal screen adds `READY D/T/- #nn`; its
attempt count is Pocket evidence for the delay rather than an arbitrary sleep.
No SDRAM logic changes are in scope.

**A-085 Pocket result:** Screenshot `20260919_221941.png` reports `OPEN D ERR
0`, `READY D #02`, `WRITE D ERR 0`, `FLUSH T ERR 0`, and `READ T ERR 0`.
The card file remains zero. Readiness succeeds promptly, so pre-write slot
settling is no longer the active explanation. However, the test sent `0188`
before its readback; a timed-out flush can occupy the one-command bridge and
make the later read timeout non-diagnostic.

**Next diagnostic — A-086:** Firmware-only, same RBF. Preserve A-085's proven
two-read readiness gate, but reorder the terminal operations to write →
immediate readback → flush. This makes the readback a direct verdict on the
acknowledged write, independent of the known flush timeout. No SDRAM logic
changes are in scope.

**A-086 Pocket result:** Screenshot `20260919_222659.png` reports `OPEN D ERR
0`, `READY D #02`, `WRITE D ERR 0`, `READ D ERR 0 DATA 00000000`, and
`FLUSH T ERR 0`. Because READ completed before FLUSH, the zero is a direct
write-path failure, not a flush-timing artifact. The card file remains 64 zero
bytes. The fault is still not localized between the firmware payload buffer,
target bridge address, APF write semantics, and slot mapping.

**Next diagnostic:** Display local datatable words 200–203 immediately before
0184. Comparing that source payload with target readback will distinguish a
missing payload from an APF address/slot mapping fault. No SDRAM logic change
is justified by A-086.
