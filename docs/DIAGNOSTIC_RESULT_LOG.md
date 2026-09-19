# Diagnostic result log

> **Superseded 2026-09-19 (A-091).** The slot-5 / `0184` / `0188` design below
> never persisted a file on Pocket (issue 019). The record is now published
> through `interact.json` persist words and decoded with
> `tools/decode_tau_diag_log.py --interact`. The schema and 64-byte record
> layout below still apply; the transport, location and verification gates do
> not.

## Purpose

The CPU SDRAM diagnostic currently presents its result on the Pocket display.
This facility also writes the terminal result to a dedicated, fixed-size save
file. It removes manual transcription from the evidence path without allowing
diagnostic firmware to write music, artwork, playlists, or user settings.

## Design

| Item | Decision |
| --- | --- |
| Data slot | ID 5, `Diag result log` only |
| Location | Core-specific `Saves/<platform>/<core>/last-result.tlog` |
| Lifetime | One latest-result record; deliberately overwrites the prior record |
| Transfer | Target `0184` data-slot write followed by Target `0188` flush |
| Transfer buffer | `mf_datatable` words 200--215, bridge address `0xF8002320` |
| Size | Exactly 64 bytes / 16 big-endian 32-bit words |
| Integrity | `TLOG` magic, schema/length word, XOR checksum |

The buffer was selected after the known protected ranges: APF's slot table
occupies words 0--63, the filename response and parameter buffers occupy
64--191, settings use 192--199, and Pocket build metadata begins at 224.

The packaging tool pre-creates a zeroed 64-byte save file. This avoids relying
on unverified deferred-slot creation timing. The slot is nonvolatile,
defer-loaded, writable, core-specific, and size-limited to 64 bytes.

## Record schema 1

| Word | Meaning |
| --- | --- |
| 0 | `TLOG` (`0x544C4F47`) |
| 1 | Schema 1 and length 64 (`0x00010040`) |
| 2 | Stage in low byte; bit 8 failure; bit 9 timeout |
| 3 | FPGA/firmware interlock version |
| 4 | Run counter since this core boot |
| 5 | Core cycle counter at record creation |
| 6--7 | Readback checks, failures |
| 8--10 | First failing address, expected, actual |
| 11 | `STAT0` terminal status |
| 12 | XOR checksum of magic and words 0--11 |
| 13--15 | Reserved, zero |

Stages: 1 = version interlock, 2 = mailbox preflight, 3 = CPU preflight
readback, 4 = normal test completion.

Decode a recovered file with:

```sh
python3 tools/decode_tau_diag_log.py /Volumes/Pock/Saves/<platform>/<core>/last-result.tlog
```

## Verification gates

1. **RTL simulation:** `make test-rtl-tgt` proves write and flush command
   selections traverse the CDC command adapter and wait for their own result.
2. **Firmware build:** `bash fw/build.sh sdram-cpu-readback` compiles the record
   writer. The host decoder has a schema/checksum fixture.
3. **Quartus:** a new bitstream is mandatory because the `0188` flush pulse and
   three-bit target selector add RTL wiring. The interlock was advanced to rev
   23 so a stale bitstream refuses the new diagnostic ROM.
4. **Pocket:** run the diagnostic once; inspect the save file after the screen
   reaches a terminal result. Its decoded values must match the screen and the
   target command must return success. This remains pending.

No conclusion about SDRAM correctness, owner-mux behavior, or command-path
correctness may cite this log until gate 4 succeeds on hardware.

## Historical A-084 lifecycle gate

A-083 showed a completed target write and immediate target read returning zero,
so A-084 first activates the isolated diagnostic slot using APF's own
descriptor (`0190` response copied unchanged into the established `0192`
parameter buffer). The terminal screen reports `OPEN`, `WRITE`, `FLUSH`, and
`READ`; photograph all four. Only `OPEN D ERR 0` followed by
`READ D ERR 0 DATA 544C4F47` demonstrates that the record reached the active
slot before core exit. This is Pocket/APF evidence, not SDRAM-path evidence.

## Current A-086 lifecycle gate

A-085 reached `READY D #02`, but its read followed a timed-out flush and was
therefore inconclusive. A-086 keeps the descriptor-copy open and two-read
readiness gate, then writes the record, reads it back immediately, and only
afterward attempts flush. Photograph `OPEN`, `READY`, `WRITE`, `READ`, and
`FLUSH`. `READ D ERR 0 DATA 544C4F47` proves the record reached the active slot
before flush; this is Pocket/APF evidence, not SDRAM-path evidence.
