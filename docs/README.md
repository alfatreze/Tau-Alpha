# Documentation map

Everything in `docs/`, grouped by purpose. Start with the guides; the rest are the internal record: specifications, plans, session handoffs, test scripts and issues.
The project README is [../README.md](../README.md).

## Guides and public pages

| Document | What it is |
|---|---|
| [guide/USER_GUIDE.md](guide/USER_GUIDE.md) | Using Tau: controls, playlists, library, themes, meters, EQ, speed, FLAC, known limitations |
| [guide/DIAGNOSTICS.md](guide/DIAGNOSTICS.md) | Info page, Diagnostic Build, Check, Meter Sweep, QR reports, sending results |
| [guide/MEDIA_AND_TOOLS.md](guide/MEDIA_AND_TOOLS.md) | Preparing media and using the host tools |
| [TECHNICAL_SPEC.md](TECHNICAL_SPEC.md) | The systems inside the core, with a memory map and budgets |
| [PERFORMANCE.md](PERFORMANCE.md) | Measured performance, tradeoffs and how we measure |
| [DEVELOPERS.md](DEVELOPERS.md) | For core developers: building, testing, fits, lessons, repository map |
| [ATTRIBUTIONS.md](ATTRIBUTIONS.md) | Credits and licences |
| [ROADMAP_PUBLIC.md](ROADMAP_PUBLIC.md) | Public status and what is next |
| [HOW_IT_WORKS.md](HOW_IT_WORKS.md) | The interesting parts of the build, as a story |
| [FLAC.md](FLAC.md) | FLAC on this core |
| [EQ_DESIGN.md](EQ_DESIGN.md) | The preset equalizer design |

## Status, plans and registers (internal)

| Document | What it is |
|---|---|
| [CURRENT_STATUS.md](CURRENT_STATUS.md) | One-page state of the project |
| [ROADMAP.md](ROADMAP.md) | The one ordered "what is next" list (draft) |
| [ARCHITECTURE_ROADMAP.md](ARCHITECTURE_ROADMAP.md) | Long-range architecture phases (720 last) |
| [DECISIONS.md](DECISIONS.md) | Decisions register, including rejected options |
| [PROJECT_REGISTER.md](PROJECT_REGISTER.md) | Decision, feature and issue register |
| [AUDIT_TRAIL.md](AUDIT_TRAIL.md) | Every result with its evidence label (A-NNN and B-NNN series) |
| [CROSS_PROJECT_INTERFACE.md](CROSS_PROJECT_INTERFACE.md) | How Tau and the Tau Omega companion share documentation |
| [DEVELOPMENT_WORKFLOW.md](DEVELOPMENT_WORKFLOW.md) | The Codex/Qwen/Claude conversational workflow |
| [REUSABLE_LOCAL_AGENT_HDL_GUIDE.md](REUSABLE_LOCAL_AGENT_HDL_GUIDE.md) | A reusable local agent workflow for HDL projects |
| [QA_PLAN.md](QA_PLAN.md) | QA plan for the speed-1.2x branch |
| [FULL_AUDIT_2026-09-23.md](FULL_AUDIT_2026-09-23.md) | Full audit and review of RTL, plan and risks |
| [TAU_TECHNICAL_SPIKE.md](TAU_TECHNICAL_SPIKE.md) | The original technical spike |

## Specifications and designs

| Document | What it is |
|---|---|
| [MMIO_ALLOCATION.md](MMIO_ALLOCATION.md) | Every memory-mapped register |
| [PHASE_F_SPEC.md](PHASE_F_SPEC.md) | The blit engine (Talos), M10K release, spectrum, kernel decisions |
| [HELIOS_SPEC.md](HELIOS_SPEC.md) | Helios, the UI layer over Talos |
| [PHASE_G_SPEC.md](PHASE_G_SPEC.md) | Cold code and data out of on-chip RAM |
| [ALPHA_BLEND_ANALYSIS.md](ALPHA_BLEND_ANALYSIS.md) | Why alpha blend timing failed, and the pipelined fix |
| [MP3_FILTERBANK_KERNEL_DESIGN.md](MP3_FILTERBANK_KERNEL_DESIGN.md) | The MP3 synthesis-window unit design and fit |
| [RAM_SHRINK_192K_PLAN.md](RAM_SHRINK_192K_PLAN.md) | What the 192 KB RAM shrink takes and buys |
| [MEDIA_LIBRARY_0.4_SPEC.md](MEDIA_LIBRARY_0.4_SPEC.md) | The library index format and design |
| [MEDIA_LIBRARY_0.4_BRIEF.md](MEDIA_LIBRARY_0.4_BRIEF.md), [MEDIA_LIBRARY_0.4_PROMPT.md](MEDIA_LIBRARY_0.4_PROMPT.md) | The library brief and the session prompt |
| [PLAYLIST_SDRAM_MOVE_SPEC.md](PLAYLIST_SDRAM_MOVE_SPEC.md) | Moving playlist buffers behind the SDRAM window |
| [COVER_TIMG_READER.md](COVER_TIMG_READER.md), [IMAGE_FORMATS.md](IMAGE_FORMATS.md) | Fast covers: the reader and the format study |
| [THEME_SPEC.md](THEME_SPEC.md), [THEME_FILE_FORMAT.md](THEME_FILE_FORMAT.md) | Theme roles and the `tau-assets.bin` format |
| [METER_MODULE_SPEC.md](METER_MODULE_SPEC.md), [METER_CONFIG_SPEC.md](METER_CONFIG_SPEC.md) | Meter modules, manifests and configuration |
| [METER_CAPABILITIES.md](METER_CAPABILITIES.md), [METER_REGISTRY.md](METER_REGISTRY.md) | Generated meter capability and registry tables |
| [CHLADNI_METER_SPEC.md](CHLADNI_METER_SPEC.md) | The Chladni meter algorithms |
| [HARDWARE_METER_IDEAS.md](HARDWARE_METER_IDEAS.md) | Saved ideas for hardware meter helpers |
| [TEST_SUITE_SPEC.md](TEST_SUITE_SPEC.md) | The on-device test suite and the TAUD1 report |
| [SETTINGS_ARCHITECTURE.md](SETTINGS_ARCHITECTURE.md), [SETTINGS_RUNTIME_BUDGET.md](SETTINGS_RUNTIME_BUDGET.md) | The settings menu design and its budget |
| [FIRMWARE_MODULARIZATION_PLAN.md](FIRMWARE_MODULARIZATION_PLAN.md) | Firmware modularization (parked) |
| [MOD_TRACKER_SUPPORT_SPEC.md](MOD_TRACKER_SUPPORT_SPEC.md) | Tracker/MOD support (parked idea) |
| [BATTERY_AND_POWER_PLAN.md](BATTERY_AND_POWER_PLAN.md) | Battery status and power-efficiency plan |

## Memory, PSRAM and SDRAM

| Document | What it is |
|---|---|
| [SDRAM_MEMORY_ARCHITECTURE.md](SDRAM_MEMORY_ARCHITECTURE.md) | The SDRAM memory architecture decision |
| [SDRAM_POCKET_DIAGNOSTIC.md](SDRAM_POCKET_DIAGNOSTIC.md), [SDRAM_CPU_WINDOW_DIAGNOSTIC.md](SDRAM_CPU_WINDOW_DIAGNOSTIC.md), [SDRAM_CONTENTION_DIAGNOSTIC.md](SDRAM_CONTENTION_DIAGNOSTIC.md) | The SDRAM diagnostic phases |
| [DIAGNOSTIC_RESULT_LOG.md](DIAGNOSTIC_RESULT_LOG.md) | The early diagnostic result log |
| [PSRAM_EVALUATION_PLAN.md](PSRAM_EVALUATION_PLAN.md), [PSRAM_IMPLEMENTATION_PLAN.md](PSRAM_IMPLEMENTATION_PLAN.md), [PSRAM_TIMING_CONTRACT.md](PSRAM_TIMING_CONTRACT.md) | PSRAM plans and the timing contract |
| [UPSTREAM_MEMORY_AUDIO_KNOWLEDGE.md](UPSTREAM_MEMORY_AUDIO_KNOWLEDGE.md) | Upstream knowledge for SDRAM, PSRAM and FPGA audio |

## Build, install and debug

| Document | What it is |
|---|---|
| [FPGA_BUILD.md](FPGA_BUILD.md) | The Quartus build baseline and resource tables |
| [CARD_INSTALL_PROCEDURE.md](CARD_INSTALL_PROCEDURE.md) | The SD card install checklist behind `tools/install_dev_core.py` |
| [JTAG_DEBUG_ACCESS.md](JTAG_DEBUG_ACCESS.md) | JTAG, SignalTap and in-system probes on this project |

## Upstream and other-project reviews

| Document | What it is |
|---|---|
| [HARPMUDD_UPSTREAM_1.5_REVIEW.md](HARPMUDD_UPSTREAM_1.5_REVIEW.md) | HarpMudd v1.5 review: what to port and drop |
| [A088_UPSTREAM_CHECKS.md](A088_UPSTREAM_CHECKS.md) | Early checks derived from HarpMudd upstream |
| [OPENFPGAOS_REVIEW.md](OPENFPGAOS_REVIEW.md) | openfpgaOS review and CPU clock options |

## Session handoffs (history)

Newest first. Each says what it supersedes.

| Document | What it is |
|---|---|
| [SESSION_HANDOFF_2026-09-26_ALPHA29_MPOLY.md](SESSION_HANDOFF_2026-09-26_ALPHA29_MPOLY.md) | alpha.29 and the MP3 window unit (latest) |
| [SESSION_HANDOFF_2026-09-26_ALPHA22.md](SESSION_HANDOFF_2026-09-26_ALPHA22.md) | alpha.22 |
| [SESSION_HANDOFF_2026-09-26_ALPHA17.md](SESSION_HANDOFF_2026-09-26_ALPHA17.md) | alpha.17 |
| [SESSION_HANDOFF_2026-09-25_PHASE_F_CONTINUED.md](SESSION_HANDOFF_2026-09-25_PHASE_F_CONTINUED.md) | Phase F continued, B11 |
| [SESSION_HANDOFF_2026-09-25_BLIT_TEST_ROOT_CAUSE.md](SESSION_HANDOFF_2026-09-25_BLIT_TEST_ROOT_CAUSE.md) | Blit Test root cause |
| [SESSION_HANDOFF_2026-09-24_BLIT_TEST.md](SESSION_HANDOFF_2026-09-24_BLIT_TEST.md), [SESSION_HANDOFF_2026-09-24.md](SESSION_HANDOFF_2026-09-24.md) | The Blit Test hang |
| [SESSION_HANDOFF_2026-09-22_RELEASE_0.4.md](SESSION_HANDOFF_2026-09-22_RELEASE_0.4.md) | Release 0.4.0 |
| [SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md](SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md), [SESSION_HANDOFF_2026-09-21.md](SESSION_HANDOFF_2026-09-21.md), [SESSION_HANDOFF_PSRAM_2026-09-21.md](SESSION_HANDOFF_PSRAM_2026-09-21.md) | Release 0.3.0, the SDRAM window, PSRAM |
| [SESSION_HANDOFF_2026-09-20.md](SESSION_HANDOFF_2026-09-20.md) | The earliest handoff (superseded) |
| [archive/CURRENT_STATUS_history_2026-09-26.md](archive/CURRENT_STATUS_history_2026-09-26.md) | The old long status file |

## Test scripts

User test scripts for specific builds; kept as examples of how a run was scripted.

| Document | What it tests |
|---|---|
| [TEST_SCRIPT_ALPHA34.md](TEST_SCRIPT_ALPHA34.md) | 0.5.0-alpha.34 (the release smoke-test basis) |
| [TEST_SCRIPT_B071.md](TEST_SCRIPT_B071.md) | Cold code for library, settings, playlist and covers |
| [TEST_SCRIPT_B049.md](TEST_SCRIPT_B049.md) | Cold code (Diagnostic Build) |
| [TEST_SCRIPT_B046.md](TEST_SCRIPT_B046.md) | The cold image |
| [TEST_SCRIPT_B041.md](TEST_SCRIPT_B041.md) | Library history, load-not-play, counters |
| [TEST_SCRIPT_B039.md](TEST_SCRIPT_B039.md) | Library playlists and Legacy mode |
| [TEST_SCRIPT_B037.md](TEST_SCRIPT_B037.md) | The first library run |
| [TEST_SCRIPT_B024.md](TEST_SCRIPT_B024.md) | Album art in PSRAM |
| [LIBRARY_PHASE0_TEST.md](LIBRARY_PHASE0_TEST.md) | Absolute-path open test |

## Issues

Numbered issue write-ups in [issues/](issues/). Notably: [020](issues/020-now-playing-title-clipped-at-45-chars.md) title clipping (fixed),
[021](issues/021-boot-restore-release-vs-diagnostic-mismatch.md) boot-restore mismatch (parked), [001](issues/001-unicode-playlist-paths.md) non-ASCII playlist paths (worked around by the sync tool).
The full list is 001 to 021 in that folder.

## Images

`screenshot.png`, `playlist_browser.png` (used by the README and the user guide), and preview renders: `font_preview.png`, `font_preview_body.png`, `gradient_preview.png`, `gradient_preview_4x.png`, `idle_preview.png`,
`idle_preview_err.png`, `splash_preview.png`. `docs/vendor/` holds a vendor datasheet PDF used for the PSRAM timing contract.
