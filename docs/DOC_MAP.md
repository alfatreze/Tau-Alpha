# Document map (the one index), 2026-11-25

What to read, by topic. Evidence for every class is in `research/provenance-2026-11-25/doc_inventory.csv` (path, class, last commit, inbound references, what names it, the rule used). Classes: **CURRENT** (true now), **REFERENCE** (design or spec, still valid), **HISTORY** (evidence, old handoffs, old status), **SUPERSEDED** (replaced, banner names by what), **DUPLICATE** (same file in two places).

Counts on 431 Markdown files (outside `third_party/`): CURRENT 99, REFERENCE 184, HISTORY 85, DUPLICATE 60, SUPERSEDED 3. On 2026-11-25, 53 of them (50 duplicate root copies, 3 superseded) were moved to `archive/` with a banner and a stub left at the old path, so no link breaks. `tools/check_doc_links.py` checks that (run by `make test-host`).

## Start of a session (unchanged, named by CLAUDE.md)
1. [CURRENT_STATUS.md](CURRENT_STATUS.md), then [ROADMAP.md](ROADMAP.md), then the newest file in [handoffs/](handoffs/) (now [SESSION_HANDOFF_2026-11-23_V075_RELEASED.md](handoffs/SESSION_HANDOFF_2026-11-23_V075_RELEASED.md)).
2. Evidence and decisions: [AUDIT_TRAIL.md](AUDIT_TRAIL.md), [DECISIONS.md](DECISIONS.md), [issues/](issues/), rules in [PROJECT_RULES.md](PROJECT_RULES.md), card installs in [CARD_INSTALL_PROCEDURE.md](CARD_INSTALL_PROCEDURE.md).

## By topic
| Topic | Read | Notes |
|---|---|---|
| What the player is | [HOW_IT_WORKS.md](HOW_IT_WORKS.md), [TECHNICAL_SPEC.md](TECHNICAL_SPEC.md), [PERFORMANCE.md](PERFORMANCE.md), [../README.md](../README.md) | |
| Graphics engine and scanout | [TALOS.md](TALOS.md), [HELIOS.md](HELIOS.md), [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md), [features/TALOS2_REIMPLEMENTATION_PLAN.md](features/TALOS2_REIMPLEMENTATION_PLAN.md), [research/TALOS_SCANOUT_CONTRACT.md](research/TALOS_SCANOUT_CONTRACT.md), [research/TALOS2_EXPERT_REVIEW_2026-10-26.md](research/TALOS2_EXPERT_REVIEW_2026-10-26.md) | D-T01..D-T17 |
| SDRAM and PSRAM | [features/SDRAM_CONTROLLER_CONTRACT.md](features/SDRAM_CONTROLLER_CONTRACT.md), [features/SDRAM_CONTROLLER_PLAN.md](features/SDRAM_CONTROLLER_PLAN.md), [memory/](memory/) | the old controller is retired (D-T13) |
| Build, fit, card | [procedures/FPGA_BUILD.md](procedures/FPGA_BUILD.md), [CARD_INSTALL_PROCEDURE.md](CARD_INSTALL_PROCEDURE.md), [DEVELOPMENT_WORKFLOW.md](DEVELOPMENT_WORKFLOW.md), [HOST_TOOLCHAIN.md](HOST_TOOLCHAIN.md) | |
| Meters and the SDK | [features/meters/EXTERNAL_METERS_SPEC.md](features/meters/EXTERNAL_METERS_SPEC.md), [features/meters/METER_MODULE_SPEC.md](features/meters/METER_MODULE_SPEC.md), [../sdk/README.md](../sdk/README.md) | |
| Tau Omega (the companion) | [interop/README.md](interop/README.md), [CROSS_PROJECT_INTERFACE.md](CROSS_PROJECT_INTERFACE.md), [interop/LEDGER.md](interop/LEDGER.md) | Omega links paths here: do not move |
| Provenance, licences | [PROVENANCE.md](PROVENANCE.md), [ATTRIBUTIONS.md](ATTRIBUTIONS.md), [../NOTICE.md](../NOTICE.md), [research/HARPMUDD_REMOVAL_ASSESSMENT_2026-11-25.md](research/HARPMUDD_REMOVAL_ASSESSMENT_2026-11-25.md), [research/HARPMUDD_DISCONTINUATION_STATUS_2026-10-18.md](research/HARPMUDD_DISCONTINUATION_STATUS_2026-10-18.md) | `../legacy-harpmudd/` holds moved files |
| Tests and QA | [tests/QA_PLAN.md](tests/QA_PLAN.md), [tests/TEST_SUITE_SPEC.md](tests/TEST_SUITE_SPEC.md) | `TEST_SCRIPT_*` are one-off scripts of past builds (HISTORY) |
| Older state, handoffs | [handoffs/](handoffs/), [archive/](archive/), [EXECUTION_LOG.md](EXECUTION_LOG.md) | HISTORY |

## Known problems this pass found (owner to decide, not fixed)
- **"Prefer the subdirectory copy" (CLAUDE.md) is wrong for four pairs**: the root copy is the newer, live one: `CARD_INSTALL_PROCEDURE.md` (root 2026-11-22, 223 lines; `procedures/` copy 123 lines), `CROSS_PROJECT_INTERFACE.md`, `JTAG_DEBUG_ACCESS.md`, `MOD_TRACKER_SUPPORT_SPEC.md`. Three more pairs differ and Omega links them, so they stay (`MEDIA_LIBRARY_0.4_SPEC.md`, `METER_CONFIG_SPEC.md`, `THEME_FILE_FORMAT.md`). All were left in place. The 50 root copies that were moved were either byte-identical or older than the subdirectory copy.
- Tau Omega's messages link five paths that do not exist (`docs/METER_MODULE_SPEC.md`, `docs/IMAGE_FORMATS.md`, `docs/THEME_SPEC.md`, `docs/METER_VU_MASTERING_SPEC.md`, plus `docs/FIRMWARE_SYNC.md`): they moved in the 2026-09-27 reorg. A one-line stub at each old path would heal them; not done (it changes what Omega reads).
- `docs/DIAGNOSTIC_RESULT_LOG.md` is identical to `archive/DIAGNOSTIC_RESULT_LOG.md`; left (named by older docs).
