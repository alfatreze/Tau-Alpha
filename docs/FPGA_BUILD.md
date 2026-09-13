# FPGA build baseline

## Verified baseline

On 13 September 2026, the unmodified Tau RTL compiled successfully with:

- Quartus Prime Lite Edition 25.1std.0 Build 1129 (Linux x86-64)
- Cyclone V device support
- target device `5CEBA4F23C8`
- Ubuntu 22.04 x86-64 VM

The full compile completed in **42m 58s**. It produced `ap_core.rbf` and
`ap_core.sof`, and passed all reported timing checks.

| Resource | Fit |
|---|---:|
| ALMs | 5,587 / 18,480 (30%) |
| Registers | 7,217 |
| Block memory bits | 2,380,928 / 3,153,920 (75%) |
| RAM blocks | 300 / 308 (97%) |
| DSP blocks | 11 / 66 (17%) |
| PLLs | 1 / 4 (25%) |

The limiting timing margin is a 0.025 ns hold slack in the fast 0C timing
model. There are no setup or hold failures, but this is not a basis for
unmeasured clocking changes.

## Phase 1 SDRAM integration build — assembler blocked

On 13 September 2026, the Phase 1 SDRAM diagnostic integration completed
analysis, synthesis, and fitting successfully on the same VM and Quartus
installation. The fitter reported the following preliminary resource result:

| Resource | Baseline | Phase 1 fitted result | Delta |
|---|---:|---:|---:|
| ALMs | 5,587 | 5,661 | +74 |
| Registers | 7,217 | 7,422 | +205 |
| Block memory bits | 2,380,928 | 2,380,416 | -512 |
| RAM blocks | 300 | 299 | -1 |
| DSP blocks | 11 | 11 | 0 |
| PLLs | 1 | 1 | 0 |

This is **not a completed FPGA build**. Quartus's final Assembler terminated
with the internal assertion `u2b_bcm_netlist != NULL` in
`asm_model_generator.h:217`, so it did not create an `.sof`/`.rbf` and timing
analysis did not run. An isolated `quartus_asm` retry also created no programming
artifact. Treat the fitter values as fit-only evidence, not a timing or Pocket
result. Full evidence and the next safe investigation step are retained in
[issue 005](issues/005-quartus-assembler-internal-error.md).

## Build layout

The host project directory is shared into the VM at:

`/home/taualpha/tau-workspace/tau-alpha`

Quartus cannot create its `db/` directory reliably on that 9p shared mount.
Treat it as the source-of-truth only. Compile from a local ext4 copy at:

`/home/taualpha/tau-local/tau-alpha`

The macOS `toolchain/` directory must be excluded: it is host-specific and not
required for FPGA compilation.

## Reproducible build

Inside the VM, refresh the local build copy after committing or otherwise
settling the source you want to compile:

```sh
rm -rf /home/taualpha/tau-local/tau-alpha
mkdir -p /home/taualpha/tau-local/tau-alpha
cd /home/taualpha/tau-workspace/tau-alpha
tar --exclude=./toolchain --exclude=./src/fpga/db \
  --exclude=./src/fpga/incremental_db \
  --exclude=./src/fpga/output_files -cf - . | \
  tar -C /home/taualpha/tau-local/tau-alpha -xf -
cd /home/taualpha/tau-local/tau-alpha
QUARTUS_SH=/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh \
  make check-fpga
QUARTUS_SH=/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh \
  make fpga
```

Quartus reports and generated images remain in the local working copy under
`src/fpga/output_files/`. They are build artifacts, not source files to commit.

For a live stage log while the build runs:

```sh
tail -n 30 -F /home/taualpha/tau-local/tau-alpha/src/fpga/output_files/ap_core.flow.rpt
```

`Ctrl-C` stops only the log viewer.
