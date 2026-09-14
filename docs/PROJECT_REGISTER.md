# Tau decision, feature, and issue register

**Purpose:** a compact handoff index for humans or future models. It identifies
what Tau has decided, what exists, what evidence supports it, and where the
authoritative detail lives. It does not replace the linked records.

For chronological decisions, alternatives, verification evidence, reversals,
and resource/timing trend, read [AUDIT_TRAIL.md](AUDIT_TRAIL.md) alongside this
register.

## Direct code-review links

These links are intended for reviewers who can access the public GitHub
repository but cannot browse the local workspace. The current SDRAM integration
has passed a full Quartus flow, while Pocket validation remains pending. The
original Assembler error and its successful isolations/rerun are retained in
the issue and chronological trail; read those records rather than treating
this compact index as a replacement.

| Review area | GitHub source |
|---|---|
| FPGA baseline and reproducible VM build | [FPGA_BUILD.md](FPGA_BUILD.md) |
| SDRAM arbitration policy | [tau_sdram_arbiter.sv](../src/fpga/core/tau_sdram_arbiter.sv), [arbiter testbench](../sim/tb_tau_sdram_arbiter.v) |
| CDC bridge and bounded halfword transactions | [tau_sdram_cpu_bridge.sv](../src/fpga/core/tau_sdram_cpu_bridge.sv), [bridge testbench](../sim/tb_tau_sdram_cpu_bridge.v) |
| Phase 2 address-map and uncached adapter preflight | [address decoder](../src/fpga/core/tau_sdram_addr_decode.sv), [decoder testbench](../sim/tb_tau_sdram_addr_decode.v), [Wishbone adapter](../src/fpga/core/tau_sdram_wb_adapter.sv), [adapter testbench](../sim/tb_tau_sdram_wb_adapter.v) |
| Top-level controller integration and diagnostic MMIO | [core_game.vh](../src/fpga/core/core_game.vh), [mp3_soc.v](../src/fpga/core/mp3_soc.v), [QSF source list](../src/fpga/ap_core.qsf) |
| Pocket diagnostic firmware, package, and procedure | [sdram_diag.c](../fw/sdram_diag.c), [diagnostic packager](../tools/package_sdram_diagnostic.py), [Pocket procedure](SDRAM_POCKET_DIAGNOSTIC.md) |
| Current integration test/build status | [AUDIT_TRAIL.md](AUDIT_TRAIL.md), [issue 005](issues/005-quartus-assembler-internal-error.md), [issue 006](issues/006-vm-quartus-soft-lockup.md), [PROJECT.md](../PROJECT.md) |
| VM build detachment workaround | [issue 004](issues/004-vm-quartus-detached-launch.md) |

**Evidence labels:** **Pocket** = observed on an Analogue Pocket; **Quartus** =
compiled and timing/resource checked; **host** = automated host test or local
firmware build; **design** = agreed intent, not implementation evidence.

## Architectural decisions

| ID | Decision | Status / evidence | Record |
|---|---|---|---|
| A-01 | Keep the 400×360 RGB565 raster at 60 Hz. It maps exactly 4× to Pocket’s 1600×1440 display; no 640×480 change is planned. | Active; **Pocket** baseline | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [technical spike](TAU_TECHNICAL_SPIKE.md) |
| A-02 | Tau is a separate, attributed HarpMudd-derived core with a separate Pocket package identity. | Active; **Pocket** package verified | [PROJECT.md](../PROJECT.md), [NOTICE.md](../NOTICE.md) |
| A-03 | Keep audio-critical state in BRAM; pursue a bounded, framebuffer-priority SDRAM bridge for cold data before any execute-in-place experiment. | Active; Phase 1 contention **Pocket** gate accepted. Phase 2 explicit decode and mapped-window adapter remain preflight work. | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [contention diagnostic](SDRAM_CONTENTION_DIAGNOSTIC.md) |
| A-04 | Do not reduce decoder arena, DMA ring, stack, or linker reservations to fit a settings UI. | Active; **host** measured | [settings runtime budget](SETTINGS_RUNTIME_BUDGET.md) |
| A-05 | In-app settings are gated on recovered memory. Appearance, Audio, Playback, and opt-in Advanced are the intended grouping. | Active; **design** | [settings architecture](SETTINGS_ARCHITECTURE.md) |
| A-06 | Advanced capabilities must be both compiled in and explicitly enabled by the user; a build flag alone never exposes them. | Active; **design** | [PROJECT.md](../PROJECT.md) |
| A-07 | Use a Linux x86-64 Quartus environment and compile from a local ext4 copy, not the macOS 9p shared mount. | Active; **Quartus** baseline | [FPGA build](FPGA_BUILD.md), [issue 004](issues/004-vm-quartus-detached-launch.md) |
| A-08 | Do not claim an in-core battery meter until Pocket/openFPGA exposes documented battery telemetry. Measure efficiency separately first. | Active; **design** | [battery and power plan](BATTERY_AND_POWER_PLAN.md) |

## Feature register

| Area | Feature | Status / evidence | Source record |
|---|---|---|---|
| Core packaging | TAU core identity, Media Players category, platform/author art | Complete; **Pocket** confirmed | [PROJECT.md](../PROJECT.md) |
| Playback | MP3 playback | Complete; **Pocket** confirmed | [README](../README.md) |
| Playback | Seek and transport | Complete; **Pocket** confirmed | [PROJECT.md](../PROJECT.md) |
| Presentation | Album artwork | Complete; **Pocket** confirmed | [PROJECT.md](../PROJECT.md) |
| Presentation | Eleven visualizer families and deterministic framebuffer fixtures | Implemented; playback visualizers **Pocket** confirmed, fixture coverage **host** | [PROJECT.md](../PROJECT.md) |
| Library | M3U playlist parsing and playlist browser | Implemented; parser **host** tested; paths with non-ASCII names remain unresolved | [README](../README.md), [issue 001](issues/001-unicode-playlist-paths.md) |
| Audio | Hardware preset EQ | Implemented; bit-exact **host** tests; broader Pocket regression retained in QA plan | [EQ design](EQ_DESIGN.md) |
| Playback | Playback speed, resume, screen blanking | Implemented in the current baseline; Pocket regression coverage is specified, not re-confirmed in the current SDRAM branch | [README](../README.md), [QA plan](QA_PLAN.md) |
| Playback | FLAC | Present upstream/in code path but deliberately not accepted as Tau hardware-verified in this project cycle | [FLAC record](FLAC.md) |
| Loading | Branded full-screen splash, status text, indeterminate progress | Implemented; **Pocket** image observed, tonal tuning pending | [issue 003](issues/003-loading-splash-tonemapping.md) |
| Settings | Persistent framework settings words | Implemented baseline capability | [how it works](HOW_IT_WORKS.md) |
| Settings | In-app settings screen | Deferred until SDRAM data-capacity gate | [settings runtime budget](SETTINGS_RUNTIME_BUDGET.md) |
| Diagnostics | Performance/SD/audio FIFO counters and safe persistent efficiency log | Planned after SDRAM and snapshot gates | [PROJECT.md](../PROJECT.md), [battery plan](BATTERY_AND_POWER_PLAN.md) |
| Hardware capacity | Diagnostic SDRAM MMIO access | **Pocket** bounded gate passed: 5 warm + 5 cold runs, each 183 checks / 0 failures; concurrent-load gate remains | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [Pocket diagnostic](SDRAM_POCKET_DIAGNOSTIC.md), [issue 009](issues/009-sdram-diagnostic-nonresponsive.md) |
| Hardware capacity | SDRAM contention stress player | Contention gate accepted: ten named modes completed cleanly; Eye repeat was explicitly waived after visual review and LED repeated. Timer validation is deferred and non-blocking. | [contention diagnostic](SDRAM_CONTENTION_DIAGNOSTIC.md), [issue 010](issues/010-stress-platform-id-too-long.md) |
| Hardware capacity | SDRAM stress evidence/telemetry | HUD safely consumes Select+Start and rendered on Pocket; old ROM source provenance remains historical gap. Raw timer values wrap after 71.58 s and are not accepted as durations. | [issue 011](issues/011-stress-summary-telemetry-missing.md), [contention diagnostic](SDRAM_CONTENTION_DIAGNOSTIC.md) |
| Hardware capacity | SDRAM stress progress HUD | Pocket UI/functionality verified. Corrected wrap-safe ROM is staged; one >72 s Pocket smoke test remains. Persistent SD log deferred. | [issue 012](issues/012-stress-progress-hud.md) |
| Hardware capacity | Cached SDRAM data window and cold-workspace migration | Planned; blocked on concurrent-load stress gate | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md) |
| Hardware capacity | Cold code execution from SDRAM | Deferred; separate later decision gate | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md) |
| Audio architecture | Profile MP3 stages and evaluate a targeted logic/DSP accelerator (IMDCT, Huffman, dequantization, synthesis filterbank) | Deferred until expanded SDRAM passes its Pocket gate; research only, no RTL commitment | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [PROJECT plan](../PROJECT.md) |

## Known issue register

| ID | Summary | Current status |
|---|---|---|
| [001](issues/001-unicode-playlist-paths.md) | Unicode playlist paths may fail to open. | Open; ASCII path names are the safe workaround. |
| [002](issues/002-pocket-os-unicode-metadata.md) | Pocket OS does not render the superscript alpha in textual metadata. | Resolved by ASCII `TAU` OS labels; graphical branding retains alpha. |
| [003](issues/003-loading-splash-tonemapping.md) | Loading art loses tonal separation on Pocket. | Open; needs a Pocket reference photo and asset tuning. |
| [004](issues/004-vm-quartus-detached-launch.md) | Detached VM Quartus commands exit before compilation starts. | Workaround active: managed interactive SSH build session. |
| [005](issues/005-quartus-assembler-internal-error.md) | Phase 1 Quartus Assembler internal assertion. | Quartus gate passed on fresh source-matched build; original assertion not reproduced; Pocket diagnostics remain. |
| [006](issues/006-vm-quartus-soft-lockup.md) | Stale guest soft-lockup console logs were mistaken for a stalled fresh build. | Resolved as a current-build false alarm; earlier guest lockups remain historical and unexplained. |
| [007](issues/007-sdram-diagnostic-staging.md) | Initial diagnostic objcopy and framebuffer-snapshot staging attempts failed. | Resolved on host; neither failure changed the release ROM or RTL. |
| [008](issues/008-diagnostic-core-identity-mismatch.md) | First diagnostic package used a core folder/shortname mismatch and failed during Pocket core setup. | Resolved: corrected package reaches APF Run; runtime behavior moved to issue 009. |
| [009](issues/009-sdram-diagnostic-nonresponsive.md) | Corrected diagnostic initially stalled on an arbiter handshake deadlock. | Fixed; bounded 5-warm/5-cold Pocket matrix passed. Concurrent-load testing remains. |
| [010](issues/010-stress-platform-id-too-long.md) | TAU SDRAM Stress was missing from the core browser. | Five caches regenerated; corrected platform ID is present; ordinary-browser confirmation pending. |
| [011](issues/011-stress-summary-telemetry-missing.md) | Pocket Select+Start behavior differed from reviewed firmware source. | Mitigated by explicit HUD build; old ROM provenance remains an audit gap. |
| [012](issues/012-stress-progress-hud.md) | Multi-minute stress run progress/results were difficult to follow. | HUD ROM staged for Pocket test; a persistent SD log is separately deferred. |
| [013](issues/013-stress-hud-timer-wrap.md) | HUD raw duration loses full intervals after the 32-bit cycle counter wraps. | Fixed ROM staged and card-verified; Pocket-smoke-test a >72 s pass before performance use. |

## Updating this register

Update a row whenever its decision, feature state, evidence level, or linked
issue changes. Add a detailed document first when a change needs reasoning,
measurements, or reproduction steps; then add or revise the row here. Do not
turn a **host** or **Quartus** result into a **Pocket** claim without device
evidence.
