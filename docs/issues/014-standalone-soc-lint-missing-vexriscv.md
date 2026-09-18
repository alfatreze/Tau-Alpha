# 014 — Standalone mp3_soc elaboration lacks CPU RTL and has forward references

**Status:** Open tooling limitation; Quartus integration compile is the current
authoritative elaboration gate
**Date:** 2026-09-14
**Evidence level:** **host**

## Observation

After connecting the opt-in Phase 2 data path in `mp3_soc.v`, standalone
Verilator lint was attempted with:

```sh
verilator --lint-only --timing -Wno-fatal --bbox-unsup \
  --top-module mp3_soc -Isrc/fpga/core \
  src/fpga/core/mp3_soc.v \
  src/fpga/core/tau_sdram_addr_decode.sv \
  src/fpga/core/tau_sdram_wb_adapter.sv \
  src/fpga/core/eq_biquad.v src/fpga/core/pcm_fifo.v
```

It stopped because `VexRiscv` is not available as a standalone module source in
the local RTL tree. Several width warnings in `eq_biquad.v` were also emitted;
they predate this integration and are outside this issue's scope. The command
did not establish whether either `mp3_soc` generate branch fully elaborates.

An Icarus elaboration attempt used `-i` to tolerate that missing module. It
stopped earlier in `mp3_soc.v` because `wr_reload` refers to `mmio_reg` and
`R_RELOAD` before those declarations appear later in the source. That
source-order issue predates this Phase 2 change; this attempt likewise did not
elaborate the complete SoC.

## Impact

Local lint/elaboration cannot independently validate the top-level SoC wiring
without the generated CPU module or a faithful stub and source-order cleanup.
Component and bridge-path simulation still works, but it does not replace full
top-level synthesis.

## Workaround and next step

Use Quartus on the supported Linux VM as the top-level elaboration/synthesis
gate. The first `TAU_PHASE2_WINDOW`-enabled attempt in
`/home/taualpha/tau-local/phase2-window-a054-20260914` found the duplicate
instance recorded in issue 015 and stopped before fitting; the corrected-source
retry is running in `/home/taualpha/tau-local/phase2-window-a056-20260914`.
Follow a successful opt-in build with a fresh macro-off build.
Do not modify the published QSF to enable the diagnostic window by default. A
minimal CPU bus stub and `mp3_soc` integration bench may be added later if it
provides useful coverage beyond those authoritative builds.

See audit entry A-054 and `docs/SDRAM_MEMORY_ARCHITECTURE.md` for the gated
Phase 2 status.
