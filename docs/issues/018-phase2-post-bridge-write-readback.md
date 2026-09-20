# Issue 018 — CPU all-ones write completes but reads back zero after bridge

**Status:** **Resolved 2026-09-20 (A-093, Pocket PASS: 183 checks, 0 failures, persisted record `failures 0`).** Root cause identified 2026-09-20 (A-093: the Wishbone adapter re-accepted each completed beat, so every ACK carried the previous beat's data); RTL fix simulated, Quartus and Pocket confirmation pending. Earlier text: open; reproduced on Pocket with A-065/A-066/A-074. A-076's
CPU-facing return-path discriminator and A-077's adapter-return discriminator
have each passed isolated **Quartus** fit and Pocket execution. A-066's
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
the adapter returns its later readback. A-074/A-075 make bridge assembly an
observed-good boundary. A-076 shows the final CPU return is zero and A-077
shows the adapter return is already zero at its ACK; the `mp3_soc` registered
selector is therefore no longer the primary suspect. The remaining suspect
path is the owner-mux response (`wb_done`/`wb_rdata`) into the adapter, or the
adapter's capture timing of that response. This does not authorize a claim
about cached access or cold migration.

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
Its distinct package is installed with hash-verified RBF/ROM provenance. The
Pocket run reports 183 checks / 181 failures and the final return cells
`G-G-R`: `dACK` was observed while `dDAT_MISO` was zero, not all ones. This
narrows the live failure to the adapter/mux response path or the registered
`mp3_soc` return selector. The next diagnostic must retain the adapter's
`sdram_wb_cpu_rdata` when its ACK occurs, so it can be compared with A-076's
already-proven final CPU-bus zero. A-077 implements that focused adapter-return
discriminator; simulation shows its all-ones result as `G-R-G`. Its isolated
2026-09-19 Quartus fit completed with 0 errors, +0.801 ns setup, +0.115 ns
hold, and raw RBF SHA-256
`53b11ee8fbfd2ff401a8a84255c88c4edd994333210933dfb1825d8b6bc6806f`.
Its separate package is installed with hash-verified RBF/ROM provenance. Its
Pocket run reports 183 checks / 181 failures at `A0200000`, expected
`FFFFFFFF`, actual `00000000`; after calibration against the stable first 46
cells its final cells are `G-G-R`. The adapter therefore acknowledges with
zero rather than all ones. The next bounded A-079 probe must retain owner-mux
`wb_done` and `wb_rdata` at the target read: mux all ones with adapter zero
implicates the adapter capture/ACK timing, while mux zero implicates the
owner-mux latch or its preceding bridge handoff. A-079's focused simulation
and isolated **Quartus** gates have passed (43m55s, +0.787 ns setup / +0.282 ns
hold); its hash-locked package is card-installed with only A-077 superseded,
and its Pocket gate reports 183 checks / 181 failures at `A0200000`, expected
`FFFFFFFF`, actual `00000000`. The calibrated final cells are `G-G-R`: mux
completion is present but mux return data is zero. This excludes the adapter
and `mp3_soc` selector as primary suspects. The immediate next step is a
production-timing bridge-system-response → mux regression, not a timing change
or cold-data migration.
The initial ordinary-browser
absence is classified as
stale/inconsistent Pocket catalog data: core-list caches contained A-077 while
platform/category indexes did not. The five regenerable indexes were
byte-backed-up and cleared; cold catalog rebuild is required before treating
the missing menu entry as a package failure.
