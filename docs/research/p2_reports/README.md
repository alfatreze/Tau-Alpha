# Player 2 Report captures (real Pocket screenshots, fixtures for decoders)

Each `.png` is a screenshot taken on a Pocket with Select+Start on the Report page (the Pocket names it `YYYYMMDD_HHMMSS`, which is also the capture time); each `.table.txt` is what `python3 tools/decode_tau_suite.py --grid <png> --table` prints from it (the Info rows, `SR_T_INFOTEXT`, tag 26: one self-describing row per line, label then value). Rows are text, so a new row needs no decoder change; the tag numbers did not change since tag 28 (`report_tags_max`).

| File | Build | What it shows | Rows |
|---|---|---|---|
| `p2_report_dev181_20261023_075824.png` | TAU DEV 181 (`0.6.1-dev.181+af4200e7`), tau-assets.bin merged by hand | the first report with the pack rows (`PACKS RUN 0000007F`, `PACKS SEEN 7`, `PACK ERR`, `PACK FILE`), the boot timings, `LAST STATE/W0/W1`, `METER ...` rows; no statistics rows | 210 |
| `p2_report_dev182_20261023_201451.png` | TAU DEV 182 (`0.6.1-dev.182+cb9ab09a`) | adds `CAP STATS`, `STATS WINDOWS/N/OVF/RMS/CORR` (the hardware audio statistics block); `STATS OVF 4`, `HW UNDER EV 3`, `DRY 3`, `GOV PANIC 5` in that session | 216 |
| `p2_report_dev184_20261026_091638.png` | TAU DEV 184 (`0.6.1-dev.184+3c7d6520`), packs from `tau-packs.bin` | adds **`PACK SRC`** (2 = the core's own tau-packs.bin, 1 = the MPKB section of tau-assets.bin, 0 = none); `PACKS RUN 0000007F`, `BOOT PACKS 15,944,595 cyc`, no underruns | 217 |

The rows new since v0.6.1 are listed in `docs/AUDIT_TRAIL.md` B-838 (the screenshots of those three builds are the real captures for them). The pixel-grid format itself is TPG2 (`tools/tpg.py`, `docs/features/BARCODE_STUDY.md`). These are the owner's own captures (nothing in them but counters and the firmware stamp).
