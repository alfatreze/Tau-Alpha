# Issue 018 — CPU all-ones write completes but reads back zero after bridge

**Status:** open; reproduced on Pocket with A-065/A-066/A-074. A-076's
CPU-facing return-path discriminator has passed its isolated **Quartus** fit
but has not yet run on Pocket. A-066's
controller-boundary recorder proves the controller return is all ones. A-067's
delayed capture candidate passed focused simulation/Quartus but failed the
established MMIO preflight on Pocket and was rejected. A-074 restores the
known-good contract and adds bridge-response provenance; its isolated Quartus
seed-2 fit and local package are complete. A-074's first Pocket run completed
183 checks with 181 failures; its 49-cell bridge provenance decodes to
`G-R-G` (response seen, not zero, all ones).

**Card state:** A-066 replaced only A-065 on the mounted Pocket card and its
bitstream/ROM hashes match the package manifest. This is **host** evidence; it
does not establish that Pocket has rebuilt its catalog or that the core runs.

## Pocket observation

A-065 recorded the complete cell sequence:

```text
GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGG
```

The first request is the expected CPU preflight read (cells 0–7 and 12–15
green; CTI 8–11 and WE 16 red). The target all-ones store is present at the CPU
source (17–20 green). The bridge's retained SDRAM-domain evidence is also
complete: it saw a mapped write, captured `FFFFFFFF` with all byte enables,
and both lower/upper controller operations were accepted (29–34 green).

The screen still reports `A0200000`, expected `FFFFFFFF`, actual `00000000`,
with 182 failures from 183 checks.

Cells 22–23 and 26–27 are red even though the later bridge evidence is green.
Those adapter/mux field observations are sampled from registered signals and
are not transaction-locked; they are an instrumentation-timing caveat, not
evidence that the payload changed there.

## Boundary

The failure is now after the bridge assembles the all-ones response and before
the CPU receives its later readback. A-074's Pocket result makes the bridge
assembly itself an observed-good boundary. The remaining suspect path is the
owner-mux/Wishbone response return (`DAT_MISO`/ACK) into the CPU. This does not
authorize a claim about cached access or cold migration.

## A-066 controller-boundary recorder

A-066 retains eleven additional controller facts after A-065's 35 bridge-path
cells: the `sdram_fb` latch data/masks, the registered WRITE command and
external DQ/DQM values, and the first CPU-owned read request/`READ_OUTPUT`
halfword. The read sample deliberately waits for `READ_OUTPUT`; the earlier
`p0_data_available` notification is not itself an external-bus sample.

The focused controller test uses Verilator rather than Icarus because the
upstream `sdram_fb` uses an unpacked SystemVerilog struct that Icarus does not
support. It independently observes `WRITE` with DQ `FFFF` and DQM `00`, then
drives a zero read halfword and proves the retained flags are all-one write
evidence plus zero (not all-one) read evidence. This is **simulation**
evidence only; it models neither Pocket routing nor SDRAM storage.

## A-066 Pocket result and identified bridge timing defect

The photographed 46-cell bar decodes to:

```text
GGGGGGGGRRRRGGGGRGGGGGRRGGRRGGGGGGGGGGGGGGGGRG
```

Cells 35–43 are green: `sdram_fb` latched the CPU's `FFFF` write with both
byte lanes, issued the WRITE with DQ `FFFF` and DQM `00`, then saw the target
CPU read and data. Critically, cell 44 (read data is zero) is red while 45
(read data is all ones) is green. The controller's `READ_OUTPUT` therefore
contains `FFFF`; the CPU diagnostic still reports `00000000`.

Code review identifies the mismatch. `sdram_fb` intentionally raises
`p0_data_available` one cycle before `READ_OUTPUT`, but
`tau_sdram_cpu_bridge` previously sampled `m_q` on that first indication. It
therefore latched the stale early value, immediately ended the burst, and
never sampled the fast input register's valid halfword. The candidate fix adds
one capture state per halfword, retaining the first notification only as a
timing marker and sampling `m_q` on the next asserted cycle. Its bridge test
now models early zero then valid data and passes. That conclusion did not
survive Pocket evidence: A-067's MMIO preflight returned `5DB54350` rather
than its `43505550` write/read pattern, while A-066 had passed the same gate.
The capture delay is therefore reverted. A-074 keeps the original controller
client timing and latches the assembled 32-bit bridge response (seen/zero/all
ones) for the following all-ones CPU read, separating bridge assembly from the
later mux/Wishbone return path.

## Next gate

The A-074 seed-2 fit is signed off for hardware screening: 0 errors, +0.662 ns
multicorner setup slack, +0.119 ns hold slack, and TNS 0. Its raw RBF and
bit-reversed package hashes are recorded in the diagnostic procedure, and the
local package is verified. Collect one cold-boot diagnostic result when card
transfer is available. A-067 remains the rejected hardware regression and
A-066 the controller-side failing reference; do not use this unproven CPU
window for cold data. A-075 now classifies the bridge response as all ones on
Pocket. A-076 adds a focused CPU-facing ACK/data return-path probe. Its
2026-09-19 isolated Quartus fit completed with 0 errors, +0.972 ns setup,
+0.268 ns hold, and raw RBF SHA-256
`9ef62ebc4002abf4f5d29c84c59c08d997c18369d55e7c134beac5b97c832ef1`.
Its distinct package and one cold-boot Pocket run are the next hardware gate.
