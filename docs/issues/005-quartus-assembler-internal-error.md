# Issue 005 — Phase 1 Quartus Assembler internal error

**Status:** Quartus build gate passed on 2026-09-14; controlled Pocket
diagnostics remain required
**First observed:** 2026-09-13  
**Evidence level:** **Quartus** only; no programming artifact or Pocket test

## What happened

The Phase 1 SDRAM diagnostic integration compiled on the x86-64 Ubuntu 22.04
VM with Quartus Prime Lite 25.1std.0 Build 1129 and device `5CEBA4F23C8`.
Analysis/synthesis and the Fitter completed successfully. The final Assembler
then stopped with Quartus's internal assertion:

```text
Internal Error: Sub-system: ASM,
File: /quartus/comp/asm/asm_model_generator.h, Line: 217
u2b_bcm_netlist != NULL
```

The top-level flow consequently reported an unsuccessful compile. There is no
`ap_core.sof`, `ap_core.rbf`, assembler summary, or timing result. A subsequent
standalone `quartus_asm` invocation against the retained successful fitter
database also exited without creating an artifact.

## Known-good comparison

An isolated build of the exact baseline source revision
`7ef8f0fb84abd4ae5a6a187805fd910351ae57dc` completed on the same VM, same
Quartus version, and same target device on 2026-09-13. It generated both
artifacts and passed timing in 47m12s. This rules out a blanket unsupported
host/device installation claim. The failure is specific to the Phase 1 source
or configuration delta, but the individual trigger is not identified yet.

## Fit-only result

| Metric | Baseline | Phase 1 fitted | Delta |
|---|---:|---:|---:|
| ALMs | 5,587 | 5,661 | +74 |
| Registers | 7,217 | 7,422 | +205 |
| RAM blocks | 300 | 299 | -1 |
| Block-memory bits | 2,380,928 | 2,380,416 | -512 |
| DSP | 11 | 11 | 0 |
| PLL | 1 | 1 | 0 |

These figures establish fit capacity only. They do not establish timing,
programming-file validity, SDRAM operation, or Pocket stability.

## Isolation result 1 — source inclusion only

**Status:** Passed, 2026-09-13 (**Quartus**)

Starting from exact baseline revision `7ef8f0fb84abd4ae5a6a187805fd910351ae57dc`,
the two new files `tau_sdram_arbiter.sv` and `tau_sdram_cpu_bridge.sv` were
added to the QSF as `SYSTEMVERILOG_FILE` sources. Neither module was connected
to the live top level. The complete build succeeded and generated both
`ap_core.sof` and `ap_core.rbf`; the timing analyzer ran successfully.

This rules out source-file inclusion, SystemVerilog parsing, and the QSF source
declarations as the direct assembler trigger. It does not validate either
module's live logic; the next variant connects only the arbiter to the existing
framebuffer controller path, while keeping the CPU bridge/MMIO out of circuit.

The restarted run took 2h42m due to the interrupted VM session and a slower
post-restart fit/timing pass. That duration is operational evidence only; it is
not used as a performance claim.

## Isolation result 2 — arbiter in the live framebuffer path

**Status:** Passed, 2026-09-13 (**Quartus**)

Starting from the same baseline-plus-sources variant, the arbiter was inserted
between `mp3_fb` and `sdram_fb`. Every CPU-side arbiter input was tied inactive;
the CPU bridge and its MMIO signals were absent. The complete flow succeeded in
2h53m: fitter 1h53m49s, assembler 6m02s, and timing analyzer 48m07s. It created
both `ap_core.sof` and `ap_core.rbf`.

The fitter used 5,624 ALMs and 300 RAM blocks. This clears the live framebuffer
request/response and write-source route through the arbiter as the direct
assembler trigger. It is a Quartus build result only; it is not a Pocket
display or audio validation.

The next variant instantiates the CPU bridge and connects its SDRAM-side port
to the arbiter while holding its system request input inactive. That isolates
bridge/CDC integration from `mp3_soc` MMIO integration.

## Isolation result 3 — bridge/CDC live, system request inactive

**Status:** Passed, 2026-09-13 (**Quartus**)

Starting from isolation 2, `tau_sdram_cpu_bridge` was instantiated with its
60 MHz system side held inactive and its 100 MHz SDRAM-side port connected to
the arbiter. The MMIO registers and `mp3_soc` interface expansion remained
absent. The complete flow succeeded in 3h23m10s: fitter 2h19m53s, assembler
18m20s, timing analyzer 40m03s. It generated both `ap_core.sof` and
`ap_core.rbf`.

Fit: 5,522 ALMs, 7,104 registers, 2,380,416 block-memory bits, and 299 RAM
blocks. Timing analysis completed with no failure (slow-model setup slack
1.021 ns; slow-model `clk_74a` hold slack 0.297 ns). This is a build result,
not a functional CPU-transfer or Pocket result.

This clears source inclusion, the live framebuffer arbiter route, and the
live-but-idle bridge/CDC route as direct assembler triggers. A final source-diff
audit found one non-SDRAM delta still absent from isolations 1–3: the earlier
`mp3_fb` declaration-order change made for standards-strict simulation. It was
present in the failing full build, so test it independently before attributing
the remaining failure to the `mp3_soc` SDRAM diagnostic MMIO/interface wiring.
Only then test MMIO/top-level wiring in a controlled variant; do not change the
architecture or Quartus version first.

## Isolation result 4 — residual `mp3_fb` declaration-order change

**Status:** Passed, 2026-09-13 (**Quartus**)

Starting from isolation 3, apply only the eight-added/two-removed-line
`mp3_fb` declaration-order change present in the failing full build. No SDRAM
MMIO or top-level control wiring was added. The complete flow succeeded in
1h03m46s: fitter 41m42s, assembler 58s, timing analyzer 15m48s. It generated
both `ap_core.sof` and `ap_core.rbf`.

Fit and timing match isolation 3: 5,522 ALMs, 7,104 registers, 2,380,416
block-memory bits, 299 RAM blocks; slow-model setup slack 1.021 ns and
`clk_74a` hold slack 0.297 ns. This clears the declaration-order change as the
direct assembler trigger.

The remaining untested delta is now precisely the `mp3_soc` SDRAM diagnostic
MMIO register/interface expansion plus its `core_game.vh` signal wiring. Build
that variant next. If it reproduces the assertion, split its small register-map
change from the top-level connection before considering any architecture or
toolchain change.

## Isolation result 5 — MMIO and top-level diagnostic wiring

**Status:** Passed, 2026-09-13 (**Quartus**)

Starting from isolation 4, add the current `mp3_soc` SDRAM diagnostic mailbox
register/interface expansion and its `core_game.vh` wiring to the already-live
bridge. This is the final remaining functional FPGA delta from the original
failing Phase 1 integration. The complete flow succeeded in 42m19s: fitter
32m21s, assembler 1m09s, timing analyzer 3m44s. It generated both
`ap_core.sof` and `ap_core.rbf`.

Fit: 5,706 ALMs, 7,414 registers, 2,380,416 block-memory bits, and 299 RAM
blocks. Timing analysis completed with no failure (slow-model setup slack
1.034 ns; tightest shown hold slack 0.283 ns). A source-tree comparison against
the current FPGA tree, excluding Quartus build outputs, found only nonfunctional
`core_game.vh` comment/format/declaration-placement differences.

The original assembler assertion is therefore **not reproducible** with this
functionally equivalent design and the same toolchain. Do not claim the failure
was caused by MMIO, bridge, arbiter, or `mp3_fb`. Its remaining classification
is a one-off Quartus build-state/tool failure. The next required gate is a
fresh local-ext4 build from the exact current source commit, followed by the
controlled Pocket diagnostic; no architecture/toolchain change is justified.

## Reproduction context

- Source build copy: `/home/taualpha/tau-local/tau-alpha` (local ext4)
- Shared source mount: `/home/taualpha/tau-workspace/tau-alpha` (do not build
  directly; Quartus cannot reliably create `db/` on this 9p mount)
- Tool: `/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh`
- Build target: `make fpga`, project `src/fpga/ap_core`
- The build ran in a managed interactive SSH session. This is distinct from
  issue 004's detached-launch limitation.

## Final fresh current-source build

**Status:** Passed, 2026-09-14 (**Quartus**)

The fresh local-ext4 snapshot `/home/taualpha/tau-current-b729a7b` completed the
full Quartus flow. Its directory name identifies the source snapshot as
`b729a7b`; the guest copy has no Git executable/metadata, so its commit could
not be queried in place. A recursive comparison of `src/fpga` with the current
shared project found no RTL/QSF/source differences; the only file difference
was `apf/build_id.mif`, which the Quartus pre-flow script regenerates with the
current build identifier. `c5_pin_model_dump.txt`, `incremental_db/`, and
`output_files/` are generated build products.

The flow report says **Successful** at 01:24:08; `ap_core.done` is stamped
01:27:38. Quartus reports 39m28s total flow time (analysis/synthesis 5m04s,
fitter 29m59s, assembler 1m03s, timing analyzer 3m22s). Both `ap_core.sof`
(2.4 MiB) and `ap_core.rbf` (1.8 MiB) were generated. The fitted device was
Cyclone V `5CEBA4F23C8` using Quartus Prime Lite 25.1std.0 Build 1129.

Fit: 5,706 / 18,480 ALMs (31%), 7,414 registers, 2,380,416 / 3,153,920 block
memory bits (75%), 299 / 308 RAM blocks (97%), 11 / 66 DSP blocks (17%), and
1 / 4 PLLs (25%). This matches controlled isolation 5's resource counts.
Timing analysis completed with TNS 0 and positive slack in the reported
corners. The minimum reported setup slack is 0.914 ns; the tightest reported
hold slack is 0.119 ns (Fast 1100mV, 0C model). The positive hold margin is
small and must be rechecked after any clocking/CDC changes.

This is Quartus evidence only. It does not confirm SDRAM transfers, Pocket
stability, or audio/video behavior. The controlled Pocket diagnostic is the
next hardware gate.

## Decision and next investigation

1. Preserve `db/` and `output_files/` in the local VM copy until the failure is
   classified; do not clean it as a first response.
2. **Completed:** the controlled known-good baseline source build assembled and
   passed timing in a separate local copy with the same toolchain.
3. **Completed:** isolations 1–5 cleared every functional FPGA delta from the
   original Phase 1 source, including MMIO/top-level wiring. The original
   assembler assertion did not reproduce.
4. **Completed:** the fresh source-matched build generated valid programming
   files and passed timing. Proceed to the controlled Pocket diagnostic; no
   data migration follows automatically.

The Quartus build gate is cleared. Pocket diagnostics and the project's SDRAM
concurrency/stability matrix remain required before Phase 2 data migration.
