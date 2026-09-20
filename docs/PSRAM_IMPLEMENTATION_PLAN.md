# PSRAM implementation plan

**Status:** P0/P1 done in simulation (A-098, A-099). The new controller/bus RTL and tests
exist but are **not wired** into `core_top.v`, the CPU decode, firmware or any
package; no memory-map change.
This turns `docs/PSRAM_EVALUATION_PLAN.md` (P0-P5, kept as the contract) into an
ordered, build-ready work list. It uses the `analogue-pocket-dev` skill KB and the
SDRAM results A-088..A-097. Evidence grades: **[HW]** our Pocket result,
**[RTL]** read in real source, **[DOC]** documentation, **[EST]** estimate to be
measured, **[OPEN]** must be checked before RTL.

## 0. Progress and decisions

Owner delegated the four decisions (2026-09-20); recorded choices, weighing risk
against benefit:
1. **P0/P1 start now: yes.** Docs and simulation only; no hardware, no shared
   RTL, and it does not compete for the Quartus VM or the card.
2. **`interact.json` persist for diagnostics: yes.** Hardware-validated
   (A-091/A-093); the slot-5/`0188` path is not.
3. **Async-only scope: yes.** A burst engine adds a clock domain and a second
   timing surface for an uncertain gain; revisit only with P4 measurements.
4. **Audit numbering from A-098: yes.** Needed so the PSRAM series is separable.

Status: **P0 done** (contract checked against the datasheet, A-099; static idle
check). **P1 done** (controller, bus wrapper, strict chip model, testbenches,
mutation tests; all in `make test`). **P2 unblocked**: the remaining unknowns are
hardware questions (`docs/PSRAM_TIMING_CONTRACT.md` section 5), not datasheet
gaps. Details in `docs/AUDIT_TRAIL.md` A-098 and A-099. The datasheet check found
and fixed a real read-timing violation in the first controller (tOE / tAADV).

## 1. Analysis of the evaluation plan

### What is right and stays
- Isolated diagnostic first, single client, `clk_sys`, one outstanding request,
  uncached window only, transaction-locked response, no CDC/arbiter/burst engine.
- One die at a time; protect the last word of every die; async before burst.
- Exit gates that require a macro-on Quartus report, a distinct diagnostic core,
  and cold plus warm Pocket evidence before any map or linker change.

### Facts the plan left as TBD, now resolved from the repo
- **Chip-facing pins [RTL]** (`src/fpga/core/core_top.v:46-58`): per chip
  `cram*_a[21:16]`, `cram*_dq[15:0]`, `wait`, `clk`, `adv_n`, `cre`, `ce0_n`,
  `ce1_n`, `oe_n`, `we_n`, `ub_n`, `lb_n`. Both chips are currently tied off to
  idle (`core_top.v:140-150`: high-Z DQ, all controls high, `cre` low, `clk` low).
  Pin locations and 1.8 V standards are already in the `.qsf`.
- **Address structure [RTL/DOC]:** 22 address bits per die = `a[21:16]` plus the
  16 DQ lines used as address in an ADV#-latched address phase (multiplexed
  async access). So `cram_adv_n` is required, not optional; `cram_clk` stays 0
  and `cre` stays 0 in async mode; `WAIT` is an input.
- **Reference controller [RTL]:** `agg23/analogue-pocket-utils/ip/mem/psram.sv`
  (MIT; in the session scratchpad) is a working async/sync controller built from
  cycle counts of the datasheet timings. Its own comments mark two timings
  (data-after-unlatch, OE-after-unlatch) as guesses. Use it for interface shape
  and state sequence only; every number now comes from the AS1C8M16PL datasheet
  (see the timing contract).
  NGPC and the GBA port carry copies (`approaches.md` #12).

### Corrections and gaps in the evaluation plan
1. **Address width inconsistency.** The bus table gives `wb_adr_i` 24 bits "CPU
   word address", but 32 MiB is only 23 CPU-word bits (8 Mi words); 24 bits is
   the *16-bit PSRAM* word address. Fix: `wb_adr_i` = 23-bit CPU word offset; the
   controller derives `psram_word = {wb_adr[22:0], half}` (24 bits) with `[23]`
   chip, `[22]` die, `[21:0]` = `a[21:16]` plus the DQ address phase.
2. **Window placement is safe.** The existing decode
   (`src/fpga/core/tau_sdram_addr_decode.sv`) maps uncached SDRAM to CPU-word
   `0x28040000..28FFFFFF`. `0xA400_0000..0xA5FF_FFFF` is CPU-word
   `0x29000000..0x297FFFFF`: adjacent, no overlap, same limit-compare pattern.
   Everything else must stay a bus error (the cached SDRAM window already does
   this; see `SDRAM_MEMORY_ARCHITECTURE.md`).
3. **Last-word guard at CPU-word granularity.** A 32-bit CPU access covers two
   16-bit PSRAM words, so the guarded PSRAM word `0x3FFFFF` lives in the last CPU
   word of each 8 MiB die: window byte offsets `0x7FFFFC`, `0xFFFFFC`,
   `0x17FFFFC`, `0x1FFFFFC`. Reserve exactly those four CPU words; the controller
   refuses them (sticky flag) rather than issuing the access. The trigger
   sequence is now known (datasheet p.19: two async reads then two async writes at
   `3FFFFFh`, see the timing contract); the guard blocks it entirely.
4. **WAIT is not an async-mode handshake.** The plan asks to model "realistic
   WAIT extension" and to run an "extended WAIT" P3 mode. In async mode the
   reference controller ignores `cram_wait`. Treat WAIT as an observed input
   (latch it into the record) and test slow timing with run-time cycle-count
   dials (P2), not a WAIT mode. Datasheet p.5/p.13 confirms: ignore WAIT in async.
5. **Diagnostic save record is stale.** P2 says to write "the existing bounded
   diagnostic save record once its RBF support is available". That path (slot 5 +
   target 0188) is closed: A-090 / KB-022 (no answer to 0188), replaced by the
   `interact.json` persist channel, A-091 / KB-025 **[HW]**. PSRAM diagnostics use
   the same publishing and `tools/decode_tau_diag_log.py --interact`.
6. **Power-up hold-off.** Cellular RAM needs a minimum time after power before
   the first access (tPU = 150 us, datasheet p.7). Add a fixed hold-off counter
   after reset (default 200 us); keep both chips idle until it expires. P1 covers
   reset mid-operation plus the hold-off.
7. **Timing closure is missing from the plan.** The `.sdc` has no PSRAM I/O
   constraints. P2 must add input/output delays for `cram*`, register controls
   and DQ in IO cells, capture DQ in an input register, and make the read-capture
   cycle a dial. A-093 closed with only +0.111 ns hold slack; KB-011 (seed
   variance) and KB-027 (capture phase is a dial) apply. Build with several seeds.
8. **Mailbox addresses.** Existing MMIO uses `0x8000_0000..` offsets up to at
   least `0x30` (`fw/main.c`, `fw/player.c`). Allocate PSRAM registers at free
   offsets in the existing 4 KiB MMIO page after reading the full RTL MMIO
   decode; do not widen the page.
9. **State the value of PSRAM honestly.** The SDRAM CPU window already provides
   about 60 MiB **[HW]**, and A-096 shows the 13 KiB playlist move alone leaves
   about 12.7 KiB spare, so *capacity* is not today's blocker. PSRAM adds
   **isolation** (no scanout/arbiter sharing) and probably **lower latency**:
   A-094 measured uncached SDRAM at about 48-50 cycles per access at 60 MHz
   **[HW]**. The P1 controller at its deliberately conservative
   defaults takes **22 clocks per 32-bit write and 24 per read** (controller only, datasheet-checked timing,
   `tb_tau_psram_async`, simulation), plus about 3 for the bus wrapper: roughly
   **2x cheaper** than uncached SDRAM. That is a simulated figure; timings can
   only tighten after hardware shows the margin (contract section 5) and the real cost is
   measured in P4. If confirmed, `art_acc` (11 KiB; +0.8-0.9 s per full cover
   decode on SDRAM, A-095) would drop to roughly half that cost, which may still
   be too slow, so it stays a P4 measurement, not a promise.
10. **KB-024 belongs in P1, not only P2.** The PSRAM adapter also sits behind a
    registered CPU-side ACK: return to idle only after the CPU has observed the
    ACK, and assert one controller request per bus beat in simulation.

## 2. Architecture (unchanged in spirit, tightened)

```text
VexRiscv dbus -> tau_sdram_addr_decode (adds psram_uncached)
              -> tau_psram_bus   (classic WB, single outstanding, held response)
              -> tau_psram_async (two 16-bit ops, chip/die select, ADV# phase,
                                  DQ release, guard, timing counters)
              -> cram0_* / cram1_* pins (die selected by CE0#/CE1#)
```

- Everything in `clk_sys` (60 MHz). No CDC, no arbiter. SDRAM path untouched.
- Response contract (KB-008, KB-024): data and `valid` captured in one
  controller-owned register; `ack` derived from that register only; the adapter
  returns to idle two cycles after ACK.
- All new RTL sits behind a `TAU_PSRAM_*` macro (off by default), so a macro-off
  build has no behavioural or resource change. Extend the macro-off tie-off test.

## 3. Ordered work items

Audit ids continue from **A-098**.

### P0 - contract (docs + one test; no hardware)
1. Read the AS1C8M16PL datasheet; write `docs/PSRAM_TIMING_CONTRACT.md`: async
   read/write timing table at 60 MHz (16.67 ns), power-up register state, CE/die
   truth table, power-up hold-off, last-word trigger sequence, WAIT in async
   mode, DQ turn-around. Close every **[OPEN]** above with a source page.
2. Check `.qsf` cram assignments (polarity, 1.8 V standard) against the
   core-template and agg23 pin list.
3. Add a macro-off test that all `cram*` outputs equal the current idle values.
4. Decide the MMIO register offsets (item 8 above).

**Exit:** timing contract reviewed; macro-off test in `make test-rtl`.

### P1 - controller simulation (Icarus, `make test-rtl-psram-*`)
1. `tau_psram_async.sv` + `tau_psram_bus.sv`; behavioural four-die model (8 MiB
   per die) with minimum-timing checks, a DQ-contention check, and a guard check.
2. `tb_tau_psram_async`: walking 0/1, byte/halfword lanes, random, read-after-
   write, die and chip boundaries, reset mid-operation, hold-off.
   `tb_tau_psram_wb_return_regression`: registered-ACK CPU model, one controller
   request per beat, store-then-load, back-to-back reads (KB-024).
3. Mutation checks: removing the response register, or advancing ACK ahead of
   sampled read data, must fail a test.

**Exit:** all pass and both mutations fail; targets added to `test-rtl`.

### P2 - diagnostic core (mailbox)
1. Mailbox registers per the evaluation plan (`ADDR`, `WDATA`, `CTRL`, `STATUS`
   with `BUSY/DONE/TIMEOUT/CE_CONFLICT/GUARD_HIT`, `RDATA`, write-1-to-clear
   sticky bits) plus run-time dials for extra read-capture delay and extended
   write/recovery cycles (default = contract).
2. `fw/psram_diag.c` modelled on `fw/sdram_diag.c`: fixed anchors, walking
   patterns, address-as-data, lane tests, per-die CRC over 1 MiB (guard word
   excluded), results published through `interact.json` persist (KB-025). Screen:
   chip/die, controller response, bus response, byte enables, timeout, sticky
   flags, provenance bar.
3. Packager `--probe-a098` (new core identity, locked hashes); decoder support
   (`--interact --psram`); PSRAM I/O constraints; several Quartus seeds.

**Exit:** Quartus 0 timing failures, no new M10K (stay at 300/308), all RTL and
host tests pass, package hash-checked; NOT installed until reviewed. Audit-trail
entry and CLAUDE.md log line at each step.

### P3 - Pocket matrix (5 cold + 5 warm)
Each run covers all four dies, a die change across a restart, and one pass with
the slow-timing dial. Quit the core (do not pull the card) so APF writes
`interact_persist.json`; compare screen against the decoded persist checksum as in
A-091/A-093. Any mismatch, timeout, `CE_CONFLICT`, guard hit, freeze or missing
persist blocks P4.

**Exit:** 10/10 clean runs with matching persisted results.

### P4 - uncached CPU window
1. Add `psram_uncached` to the decoder and `tau_psram_bus` to the dbus path
   behind the macro; cached alias unmapped (bus error); guard words return a
   defined fault. Extend `tb_tau_sdram_addr_decode` for no-alias and adjacency.
2. Clone the A-093 CPU-window probe: at least 183 deterministic load/store
   checks, provenance bar, transaction-locked probes at controller, adapter and
   CPU return. Then an A-094-style cost probe to settle the latency estimate.
3. Repeat the P3 matrix, a 30-minute idle-player coexistence run, and an
   A-097-style soak.

**Exit:** Pocket-proven CPU round trip and measured access cost; timing and
resource deltas recorded; default Tau build unchanged.

### P5 - one cold-data move (separate approval)
Order, subject to P4 latency: `pl_text` + `pl_off` + `pl_order` (13 KiB), then
`art_acc` (11 KiB) only if PSRAM cost keeps the decode delta small. Needs the
SDRAM promotion gates (`CURRENT_STATUS.md` next gates 2-4) or an explicit decision
that PSRAM promotes independently. Measure memory reclaimed and player impact;
repeat the PSRAM CRC after a session.

## 4. Scheduling and dependencies
- P0 and P1 need no Quartus and no card, so they can start now, in parallel with
  the pending A-097 soak result.
- P2 needs one Quartus slot (about 44 min per build on the VM); do not queue it
  while an SDRAM promotion build is being fitted.
- A passing PSRAM run does not close or affect any SDRAM gate.
- One diagnostic core on the card at a time, each with cache backup and SHA-256
  verification (current practice).

## 5. Stop conditions
As in the evaluation plan (CE overlap, guard side effect, stale or misaligned
response, timing failure, any Pocket mismatch/timeout, regression in normal Tau),
plus: if a mailbox test passes but the CPU-window test fails, stop and record
before changing the controller. That is the A-092 pattern; suspect the bus adapter
and registered ACK (KB-024) before the memory.

## 6. Decisions for the owner
1. Approve starting P0/P1 now (docs and simulation only, no hardware risk).
2. Confirm PSRAM diagnostics use the `interact.json` persist channel.
3. Confirm async-only scope; a 133 MHz synchronous-burst engine stays a separate
   decision record, only if P4 shows async is too slow.
4. Confirm audit numbering from A-098 for the PSRAM series.
