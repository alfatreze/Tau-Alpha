# Archived: the QR encoder (2026-10-07, branch ram-diet)

Removed from the firmware at the owner's decision: the pixel grid (TPG, `fw/tpg.h`) is the only report view, Tau Omega has no users yet and phone scanning is not needed.
Everything is here as it was, and the git tag `archive/qr-encoder` points at the last commit that still had it in the tree.

| File | What it was |
|---|---|
| `qrcode.h` | the QR encoder the report pages drew (ISO/IEC 18004, level L, versions up to 38) |
| `qr_tables.h`, `gen_qr_tables.py` | its tables and their generator |
| `qr_harness.c`, `test_qr.py` | the host test that ran the real encoder under the RISC-V simulator against `segno` (needed `work/venv-qr`) |

Left in place on purpose: `tools/decode_tau_suite.py --qr` (reads QR captures from older cores, host only), `tools/lab/barcode_lab.py` (the study that chose the grid), and the `SR_T_*` record format, which is unchanged: a report is the same bytes whichever way it is drawn.

To restore: `git checkout archive/qr-encoder -- fw/qrcode.h fw/qr_tables.h`, add `#include "qrcode.h"` to `fw/report.inc` and give `rep_draw()` its QR branch back (see the tag's `fw/report.inc`).
Cost it removed: about 4.4 KB of cold code and about 90 B of hot RAM, plus the 13 KB PSRAM working buffer at `0xA4003000`.
