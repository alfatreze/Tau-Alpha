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

## Phase 1 SDRAM integration build — successful Quartus gate; Pocket testing pending

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

The first complete Phase 1 attempt did fail in Quartus's final Assembler with
the internal assertion `u2b_bcm_netlist != NULL` in
`asm_model_generator.h:217`; that attempt did not create programming files or
run timing analysis. The failure was not reproduced by controlled isolations
1–5 or by the fresh current-source rerun below. Keep the original failure in
the audit history; do not attribute it to a specific RTL change.

### Fresh current-source rerun — 2026-09-14

The full Quartus flow in `/home/taualpha/tau-current-b729a7b` reported
**Successful**. The report totals 39m28s: Analysis & Synthesis 5m04s, Fitter
29m59s, Assembler 1m03s, and Timing Analyzer 3m22s. It produced both
`ap_core.sof` and `ap_core.rbf`.

| Resource | Fitted result |
|---|---:|
| ALMs | 5,706 / 18,480 (31%) |
| Registers | 7,414 |
| Block memory bits | 2,380,416 / 3,153,920 (75%) |
| RAM blocks | 299 / 308 (97%) |
| DSP blocks | 11 / 66 (17%) |
| PLLs | 1 / 4 (25%) |

Timing analysis reported TNS 0 and positive slack in all listed corners. The
minimum setup slack is 0.914 ns; the tightest hold slack is 0.119 ns in the
Fast 1100mV, 0C model. That positive hold margin is narrow and must be
rechecked after clocking or CDC changes. The resource counts match controlled
isolation 5. The source snapshot was compared against the current shared FPGA
tree; differences were limited to regenerated `apf/build_id.mif` and generated
Quartus outputs. The VM snapshot has no Git metadata, so the directory label
`b729a7b` could not be validated with `git rev-parse` inside the guest.

This is **Quartus** evidence only, not Pocket validation. The fitted resource
usage also leaves only nine RAM blocks; the next gate is the controlled Pocket
diagnostic and concurrency/stability matrix before any SDRAM data migration.
See [issue 005](issues/005-quartus-assembler-internal-error.md) and
[audit entry A-024](AUDIT_TRAIL.md).

The successful `ap_core.rbf` was copied without modification into the host
diagnostic staging area. Its SHA-256 is
`0c00362795f22486af8aece80d1a3c6b1eb857e0783699a7fa6394163b1a6dc7`.
The side-by-side Pocket diagnostic package and exact firmware procedure are in
[SDRAM_POCKET_DIAGNOSTIC.md](SDRAM_POCKET_DIAGNOSTIC.md). This artifact copy
does not add a new Quartus result or change the resource/timing evidence above.

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
