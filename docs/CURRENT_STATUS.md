# Current engineering status

**Snapshot:** 2026-09-19, after A-086 was installed on the mounted Pocket SD
card. The next hardware result is pending.

## Executive state

The expanded SDRAM CPU window is not validated. The independent diagnostic
sequence consistently reaches the Pocket and reports a CPU readback failure at
`0xA0200000` (typically 181–182 failures out of 183). This is probe evidence,
not a final SDRAM conclusion. The unresolved hardware boundary remains the
SDRAM-domain bridge system response into the owner-mux return latch; no cold
player-data migration is authorised.

The separate persistent diagnostic-log path is isolated from the player and
uses only APF data slot 5:

| Probe | Pocket observation | Interpretation |
|---|---|---|
| A-080 | File stayed zero; no command status shown | Inconclusive |
| A-082 | `WRITE D ERR 0`, `FLUSH T ERR 0`; file zero | Write acknowledged; flush timed out |
| A-083 | `WRITE D ERR 0`, `FLUSH T ERR 0`, `READ D ERR 0 DATA 00000000` | Immediate read saw zero; lifecycle uncertain |
| A-084 | `OPEN D ERR 0`, `WRITE D ERR 0`, `FLUSH T ERR 0`, `READ T ERR 0` | Open accepted; post-open I/O was too early or bridge remained occupied |
| A-085 | `OPEN D ERR 0`, `READY D #02`, `WRITE D ERR 0`, `FLUSH T ERR 0`, `READ T ERR 0` | Read followed a timed-out flush and was inconclusive |
| A-086 | `OPEN D ERR 0`, `READY D #02`, `WRITE D ERR 0`, `READ D ERR 0 DATA 00000000`, `FLUSH T ERR 0`; file zero | Direct write-path failure confirmed before flush |

Current A-086 card artifacts:

- RBF: `c892ae7089484e099090391b6f7aba3551d1b58cedb8415f3ead6eef42099e16`
- ROM: `b19a8e6b22e4092bc7963e8882a13f5d7562ea5b1390d094330fe30ca3d51173`
- Save: `Saves/tau_sdram_prb86/alfatreze.TAU_SDRAM_PRB86/last-result.tlog`
- Initial save hash: `f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b`

## Implementation and verification facts

- A-080 is the verified rev-23 FPGA image; A-082–A-086 are firmware-only and
  require no new Quartus fit.
- Target sequencing uses a completion sequence counter, not APF's sticky
  `done` level. `make test-rtl-tgt` passes read, write, and flush CDC tests.
- The result record is 64 bytes (`TLOG`, schema/length, status, counters,
  first-failure values, XOR checksum) in datatable words 200–215; readback uses
  word 216.
- APF buffers remain separated: 0190 response words 64–127 and 0192 parameter
  words 128–191. The slot table at 0–63 and settings at 192–199 are protected.
- A-084/A-085 copy APF's 0190 descriptor into the 0192 buffer before opening
  slot 5. A-085 added two successful bounded reads after opening. A-086 reads
  immediately after write and only then attempts flush because a timed-out
  flush may occupy the one-command bridge.

Verification status: host build/package checks pass; RTL target simulation
passes; the reused A-080 RBF remains Quartus-verified. A-086 Pocket evidence
confirms a target write-path failure while SDRAM remains independently
unresolved. Do not promote either path to “passing” without matching screenshot
and card evidence.

## A-087 (Pocket result recorded)

Firmware-only source-buffer discriminator packaged at
`work/diagnostics/sdram-cpu-probe-a087/pocket` (ROM SHA-256
`1dff6c3fde7c82040aa3390a96fcf0739fd5fcdec52dfbc976daafd34c8b0fda`; same RBF).
Result: local words 200–203 hold `544C4F47 00010040 00000304 4D503317`, but target readback and the save file are zero. The payload reaches the datatable; the APF slot-5 write/mapping is what fails.

## A-088 (Pocket result: slot write fixed, flush/persistence open) — likely root cause

A-082..A-087 used datatable bridge base `0xF8000000`; the correct base is
`0xF8002000`. A-088 fixes it (ROM SHA-256 `c3e21457...dccc`, same RBF), bundle at
`work/diagnostics/sdram-cpu-probe-a088/pocket`. A-087's "APF slot-5 write fails"
conclusion is superseded: A-088 read back `544C4F47` from slot 5. The SD file is still zero; `0188` flush times out.

## A-089 (Pocket result: no change; parameters ruled out)

Packaging-only: slot 5 parameters `0x22` -> `0x86` (adds nonvolatile bit) to test whether the flush timeout / zero file is a slot-attribute problem. Same ROM and RBF as A-088.

## A-090 (Pocket result: flush unanswered; no persistence)

Firmware-only: slot-5 table size/integrity, 10 s flush with timing, post-flush re-read; parameters back to `0x22`. Result: table OK (size 64, intact), first read `544C4F47`, `0188` flush unanswered after 10 s, second read times out, file zero. Upstream ROADMAP shows `0184` is not a proven persistence route and `interact.json` is; next step is that channel.

## A-091 (Pocket result: persistence channel works)

Result published via interact.json persist (16 words, APF-stored on Quit) instead of `0184`/`0188`. ROM SHA-256 `63c89cf6...1534`, same RBF, bundle at `work/diagnostics/sdram-cpu-probe-a091/pocket`. Decode with `tools/decode_tau_diag_log.py --interact`. **Result:** after Quit the persist file decoded with a valid checksum (183 checks, 181 failures, first at `0xA0200000`, expected `FFFFFFFF`, actual `0`). The log path is solved; the SDRAM return-path fault is still open.

## A-092 (installed on card, result pending)

Firmware-only SDRAM discriminator (mailbox vs CPU window, distinctive patterns), raw words via interact.json; ROM SHA-256 `d8a991e8...8a57`, same RBF, bundle at `work/diagnostics/sdram-cpu-probe-a092/pocket`. Decode: `tools/decode_tau_diag_log.py --interact --raw`.

## A-092 result and A-093 root cause

A-092 (mailbox vs CPU window) showed the SDRAM and CPU writes are fine and CPU
reads lag by one beat when back-to-back. A-093 reproduces this in simulation
(`make test-rtl-sdram-wb-return`): `tau_sdram_wb_adapter` released to IDLE one
cycle after ACK while `mp3_soc` still presented the finished beat, so it issued a
duplicate bridge request and every following ACK carried the previous beat's
data. Fixed with a second release cycle; all simulation and host gates pass.

## A-093 Pocket result

The fixed-adapter build (A-093) **passes** the 183-check CPU-window matrix on
Pocket: 0 failures, screenshot and decoded persist record agree (checksum
`0x18511A0D`). Issue 018 is resolved. This validates only the limited uncached
data path; cached access, sustained contention, and cold-data migration remain
unauthorised.

## Next gates for promotion (informed by the analogue-pocket-dev skill KB)

1. **A-094 cost probe** (done, Pocket): an uncached access costs about 48-50
   cycles net at 60 MHz (about 0.8 us), worst single access about 360 cycles
   (6 us), 0 mismatches. Next: count accesses per candidate buffer to decide
   which moves pay.
2. **Margin (A-097 soak PASSED: 264M checks, 30 min, 0 failures; coverage and CAS/seed work remain; A-100 coverage PASSED: 52 address-line checks and 3 CRC rounds, 0 failures, worst access 360 cycles):** run a long soak of the CPU-window matrix, and confirm the
   controller's CAS latency / clock phase (KB-021, OQ-6); the A-093 fit has only
   +0.111 ns hold slack, so build promotion RTL with several seeds and read
   fast-corner hold (KB-011).
3. **Contention (A-102 PASSED at realistic load: seed-4 RBF, 4 tracks x levels 0-3, 0 late underruns, 0 mismatches, S 0, worst access 373 cycles, up to 22.6k ops/s; saturation, FLAC and cold-boot repeat still open):** the matrix passes with scanout running; add real MP3 playback
   and count audio underruns during concurrent CPU-window traffic
   (SDRAM_MEMORY_ARCHITECTURE.md Pocket hardware gate).
4. **Product build (A-101 DONE: four seeds all closed timing, 300/308 RAM blocks, hold +0.105..+0.123 ns, setup +0.39..+0.52 ns; seed 4 selected, RBF `ed34a6bc...90eb`):** enable the window in a probe-free RBF (macro-off legacy
   decode is still broad), keep 300/308 RAM blocks, re-verify all gates.
5. **First data move (A-095 analysis):** `pl_text` + `pl_off` + `pl_order`
   (13 KiB) behind the uncached alias costs tens of ms per playlist load and is
   adequate; `art_acc` (11 KiB) would add ~0.8-0.9 s to a full cover decode, so it
   waits for a restructure or the cached window; the 24 KiB exit target needs it.
   Original note: the cached window still returns a bus error and needs its own
   beat-decomposing adapter (KB-024 regression applies).

A-096: a minimal 7-row settings menu costs about 3.0 KiB and misses the link by
608 B today; the 13 KiB playlist move alone would leave about 12.7 KiB spare, so
the 24 KiB target is not supported by a measured settings need.

No cold player-data migration is authorised until these gates pass.

Two further cold-boot A-093 runs passed (user-reported); the card's later persist file independently shows 183 checks, 0 failures.

See [issue 019](issues/019-a080-result-log-not-persisted.md) and
[AUDIT_TRAIL.md](AUDIT_TRAIL.md) for the detailed reversal and evidence record.
