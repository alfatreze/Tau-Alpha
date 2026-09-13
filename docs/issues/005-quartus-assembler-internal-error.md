# Issue 005 — Phase 1 Quartus Assembler internal error

**Status:** Open — build blocker for SDRAM Phase 1 hardware validation  
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

## Reproduction context

- Source build copy: `/home/taualpha/tau-local/tau-alpha` (local ext4)
- Shared source mount: `/home/taualpha/tau-workspace/tau-alpha` (do not build
  directly; Quartus cannot reliably create `db/` on this 9p mount)
- Tool: `/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh`
- Build target: `make fpga`, project `src/fpga/ap_core`
- The build ran in a managed interactive SSH session. This is distinct from
  issue 004's detached-launch limitation.

## Decision and next investigation

1. Preserve `db/` and `output_files/` in the local VM copy until the failure is
   classified; do not clean it as a first response.
2. **Completed:** the controlled known-good baseline source build assembled and
   passed timing in a separate local copy with the same toolchain.
3. **In progress:** bisect only the small set of RTL/QSF integration changes.
   Isolation result 1 cleared source inclusion. Next connect only the arbiter
   to the framebuffer path. Keep each result in the audit trail with exact
   source revision and evidence tag.
4. Consider a Quartus version/toolchain change only after the controlled
   comparison; it would invalidate direct comparison with the existing
   baseline and requires a new clean baseline build.

No Phase 2 mapped SDRAM work or Pocket diagnostic should start until this issue
has produced a timing-clean programming artifact.
