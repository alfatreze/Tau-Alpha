# PSRAM early evaluation plan

**Status:** proposed evaluation; no PSRAM RTL, memory map, firmware placement,
or package behaviour is changed by this document.

## Recommendation

Evaluate Pocket PSRAM as the next independent external-memory path, but do not
replace or shortcut the outstanding SDRAM Phase 2 investigation. The two paths
serve different purposes:

| Memory | Current owner | Best role in Tau | Evaluation risk |
|---|---|---|---|
| SDRAM | Framebuffer / draw engine | Large burst-oriented display storage | Shared controller, 60/100 MHz CDC, scanout deadlines |
| PSRAM | None | CPU-addressable cold data and later file/decode staging | New board controller and PSRAM protocol timing |

PSRAM is the more promising capacity route for CPU-owned cold data because it
is completely unused and need not share a real-time framebuffer port. It must
nevertheless start as an isolated diagnostic, not as an immediate linker
escape hatch. Keep the active SDRAM diagnostic and all normal Tau behaviour
unchanged while this spike runs.

Pocket exposes two 16 MiB AS1C8M16PL Cellular PSRAM chips, each containing two
independently selected dies: 32 MiB total. Each die is 4 Mi 16-bit words (8
MiB); no operation may enable both `CE0#` and `CE1#` on the same chip. The
official hardware guidance permits either low-latency asynchronous accesses or
up to 133 MHz synchronous bursts. This evaluation deliberately begins with
asynchronous access at the existing 60 MHz system clock. It is slower than a
future burst engine but has the smallest clocking, pin, and response-timing
surface. [Pocket external-memory documentation](https://www.analogue.co/developer/docs/external-hardware)

## Lessons carried forward from SDRAM

| SDRAM learning | PSRAM decision |
|---|---|
| A locally passing end-to-end test did not prove the physical response handoff; Pocket A-079 observed zero at the mux despite an all-one bridge response. | Make every diagnostic response transaction-locked from the first design. Capture data and completion in the same controller-owned response register; assert ACK only from that register. Probe the controller, bus adapter, and CPU-facing return at separate boundaries. |
| The 60 MHz CPU / 100 MHz SDRAM crossing, owner mux, and controller completion contract were the high-risk edges. | Put the first PSRAM controller, adapter, and diagnostic client in `clk_sys` (60 MHz). Do not introduce a CDC, arbiter, or burst engine until the simple single-client path passes Pocket tests. |
| A complete integration can fit Quartus and still fail on Pocket. | Require focused simulation, a macro-enabled Quartus timing/resource report, a separately identified diagnostic package, and cold/warm Pocket evidence before memory-map or linker changes. |
| Diagnostic provenance and fixed, repeatable evidence made regressions localizable. | Use a distinct PSRAM diagnostic core identity, hash-lock its RBF and ROM, display controller/bus evidence on screen, and write the existing bounded diagnostic save record once its RBF support is available. |
| Broad aliases could silently point to BRAM; cache-line traffic adds a separate protocol. | Start with one explicitly decoded **uncached** PSRAM window. The cached alias stays unmapped until its own line-fill/burst design and tests are approved. |
| Product assets and audio cannot be the first testers for a new memory port. | Use an isolated diagnostic region and a diagnostic-only core. Do not move `pl_text`, artwork state, DMA buffers, decoder arena, ring, stack, or code in the evaluation. |

## Target topology and provisional map

The physical controller needs two chip-select choices and one die-select
choice. A 24-bit PSRAM *word* address maps as follows; CPU-visible addresses
remain byte addressed.

```text
CPU uncached word address [23:0]
  [23]          -> PSRAM chip: cram0 / cram1
  [22]          -> selected die: CE0# / CE1# (exactly one low)
  [21:16]       -> cram*_a
  [15:0]        -> address phase on cram*_dq
```

For the first diagnostic, decode only `0xA400_0000–0xA5FF_FFFF` as an
uncached 32 MiB byte window. This remains a **provisional** address map until
the existing SDRAM decoder and this new decoder are exercised together. All
other non-BRAM/MMIO ranges must fault or remain unmapped; they must never fall
through to BRAM by low-address truncation.

The controller translates each 32-bit CPU access into two ordered 16-bit
PSRAM operations, preserves all four byte enables, and returns a single held
32-bit response. The first version supports classic, one-beat Wishbone only:
no speculative read, no CTI/BTE burst, no cache fill, and one outstanding
request. A controller `busy` indication is advisory; the adapter must wait for
the controller's registered response-valid event rather than derive ACK from a
fixed cycle count.

## PSRAM-specific safety rules

1. **One die at a time.** The controller must drive exactly one of `CE0#` and
   `CE1#` low for an active transaction on the selected chip, and leave both
   high when idle. Add a simulation assertion and a hardware-visible sticky
   fault for illegal simultaneous selection.
2. **Protect every die's final word.** The official guidance warns that the
   software configuration sequence can be accidentally recognised at local
   word address `0x3FFFFF`. Reserve those four physical locations during the
   first diagnostic. Before general memory use, service each one with an FPGA
   shadow register so normal reads/writes retain memory semantics without
   exposing a configuration sequence.
3. **Begin asynchronous.** Keep `CRE` inactive and use the datasheet's
   asynchronous setup/hold/access constraints. The reference controller in
   `agg23/analogue-pocket-utils` is useful for interface shape only: its source
   labels some timing values as guesses, so values must be rederived from the
   AS1C8M16PL datasheet and checked against the chosen clock period before
   reuse. [Reference controller](https://github.com/agg23/analogue-pocket-utils)
4. **Safe turn-around.** Release `cram*_dq` before a read response and retain
   byte lane controls for the full read/write interval. Simulate read-after-
   write and alternating read/write operations; do not assume a behavioural
   memory model will reveal board-level DQ turn-around errors.

## Staged work and exit gates

### P0 — freeze the contract and baseline

- Record the exact AS1C8M16PL asynchronous timing table selected for 60 MHz,
  the pin polarity, die/chip selection truth table, and the last-word guard.
- Review the current `.qsf` PSRAM assignments: all CRAM pins are already
  assigned 1.8 V I/O standards and output termination. Preserve them unless a
  timing report identifies a specific justified change.
- Create a macro-off shell test that proves all CRAM pins retain the current
  idle high-impedance/high-control state.

**Exit:** reviewed timing contract; macro-off regression passes with no
behavioural or fitted-resource change.

### P1 — controller-only simulation

- Add an async, single-controller `tau_psram_async` module beneath a thin
  `tau_psram_bus` wrapper; keep it independent of `sdram_fb` and its arbiter.
- Model all four dies, realistic `WAIT` extension, DQ release, byte masks,
  read latency variation, reset during an operation, and each final-word
  guard address.
- Assert: one chip/die selection, no DQ drive during reads, control-signal
  minimum timings, data capture only on response-valid, and no phantom reply
  after reset.

**Exit:** focused simulation passes walking patterns, lane writes, random
read/write, read-after-write, die/chip boundary transitions, and reset/WAIT
cases. The test must intentionally fail if the response register is removed
or ACK is advanced ahead of sampled read data.

### P2 — unmapped diagnostic mailbox

- Connect the controller to diagnostic MMIO registers only; do not change the
  VexRiscv map. Firmware issues one request, polls completion with a timeout,
  and verifies a readback before issuing another.
- Exercise 1 MiB in each die, excluding the guarded final word. Use fixed
  anchors, walking one/zero, address-as-data, alternating patterns, all byte
  and halfword lanes, and per-die CRC summaries.
- Surface the selected chip/die, registered controller response, bus response,
  byte enables, timeout, and sticky CE-conflict/last-word flags in the
  diagnostic display and save record.

**Exit:** a macro-enabled Quartus build has zero timing failures, no unexpected
M10K allocation, and all controller, diagnostic, and existing RTL regressions
pass. Package the RBF and ROM as a new PSRAM-only developer core; validate
identity and hashes before Pocket installation.

### P3 — Pocket correctness and stability matrix

- Run the standalone diagnostic on Pocket after both cold and warm boots.
- Require five cold and five warm clean runs. Each run covers all four dies,
  records CRCs and error flags, and includes a reset/restart between at least
  two die changes.
- Repeat with a deliberately extended `WAIT` test mode, then return to normal
  timing. Any mismatch, timeout, CE conflict, unexpected configuration flag,
  freeze, or failed save record blocks P4.

**Exit:** 10/10 complete Pocket runs with zero errors and matching persisted
results. This establishes bounded diagnostic access only—not cacheability,
audio concurrency, or product use.

### P4 — uncached CPU window

- Add the explicit `0xA400_0000–0xA5FF_FFFF` selector beside the existing
  BRAM/MMIO/SDRAM decodes. First keep its cached counterpart unmapped.
- Reuse the Phase 2 CPU-window diagnostic shape: 183 or more deterministic
  CPU load/store checks, fixed 49-cell provenance bar, and transaction-locked
  controller → adapter → CPU response probes. Retain signals at every ACK;
  never sample a free-running debug wire after the transaction.
- Run the P3 matrix again and a 30-minute idle-player coexistence check. PSRAM
  has no framebuffer owner, but this detects unintended shared-clock, reset,
  pin, or resource regressions.

**Exit:** CPU data round trip is Pocket-proven in the uncached window; default
Tau continues to build and run unchanged; timing and resource deltas are
recorded.

### P5 — one cold-data migration (separate approval)

Only after P4, select one non-real-time, non-DMA object—`pl_text` is the
leading candidate—and move it as explicitly initialized/cleared data. Measure
real memory reclaimed and playback impact. Run the normal player and the
visualizer/transport matrix, then repeat the PSRAM CRC diagnostic after a
player session.

**Exit:** no audio underrun, display corruption, data corruption, or startup
regression; protected BRAM reservations remain intact. Cached PSRAM, the ring,
decoder state, and execute-in-place code stay out of scope.

## Decision points and stop conditions

- **Prefer PSRAM for cold CPU data** if P4 passes with a clean fit and no
  measurable player regression. It isolates CPU capacity from SDRAM scanout.
- **Stay with the simple async controller** until a measured workload shows it
  cannot meet cold-data latency/bandwidth needs. A 133 MHz synchronous-burst
  engine is an optimization project with a new clock domain and must have its
  own decision record.
- **Stop immediately** on CE overlap, a last-word configuration side effect,
  stale/misaligned response data, timing failure, any Pocket mismatch/timeout,
  or a regression in normal Tau. Preserve the diagnostic artifact and record
  the failure before changing the controller.
- **Do not use a passing PSRAM diagnostic to close SDRAM issue 018.** It can
  unblock capacity exploration, but it does not validate the SDRAM bridge/mux
  response path.

## Deliverables

1. Timing-and-protocol decision note, controller/bus modules, and focused
   simulations.
2. A dedicated `TAU PSRAM Diagnostic` firmware/RBF/package with reproducible
   hash manifest, screen evidence, and persistent result decode.
3. Quartus resource/timing comparison against the current macro-off baseline.
4. Pocket cold/warm test log, then an explicit P4/P5 decision record. No
   feature migration is implied by this plan.
