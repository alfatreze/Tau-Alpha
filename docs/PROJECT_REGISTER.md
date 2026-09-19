# Tau decision, feature, and issue register

**Purpose:** a compact handoff index for humans or future models. It identifies
what Tau has decided, what exists, what evidence supports it, and where the
authoritative detail lives. It does not replace the linked records.

For chronological decisions, alternatives, verification evidence, reversals,
and resource/timing trend, read [AUDIT_TRAIL.md](AUDIT_TRAIL.md) alongside this
register.

## Direct code-review links

These links are intended for reviewers who can access the public GitHub
repository but cannot browse the local workspace. The diagnostic MMIO/owner-mux
integration passed Quartus (A-053); the newer opt-in CPU data-window wiring
passed isolated Quartus as A-074 seed 2. Pocket A-075 proved the bridge
assembled `FFFFFFFF`, while the CPU still received zero. A-077 now proves the
adapter return itself is zero at its ACK, excluding `mp3_soc`'s selector as the
first suspect. The original
Assembler error and its successful isolations/rerun are retained in the issue
and chronological trail; read those records rather than treating this compact
index as a replacement.

| Review area | GitHub source |
|---|---|
| FPGA baseline and reproducible VM build | [FPGA_BUILD.md](FPGA_BUILD.md) |
| SDRAM arbitration policy | [tau_sdram_arbiter.sv](../src/fpga/core/tau_sdram_arbiter.sv), [arbiter testbench](../sim/tb_tau_sdram_arbiter.v) |
| CDC bridge and bounded halfword transactions | [tau_sdram_cpu_bridge.sv](../src/fpga/core/tau_sdram_cpu_bridge.sv), [bridge testbench](../sim/tb_tau_sdram_cpu_bridge.v) |
| Phase 2 address-map, uncached adapter, and end-to-end path | [address decoder](../src/fpga/core/tau_sdram_addr_decode.sv), [Wishbone adapter](../src/fpga/core/tau_sdram_wb_adapter.sv), [bridge owner mux](../src/fpga/core/tau_sdram_bridge_mux.sv), [stand-in path test](../sim/tb_tau_sdram_phase2_path.v), [composed path test](../sim/tb_tau_sdram_composed_path.v), [A-079 mux-return test](../sim/tb_tau_sdram_mux_return_probe.v) |
| Top-level controller integration and diagnostic MMIO | [core_game.vh](../src/fpga/core/core_game.vh), [mp3_soc.v](../src/fpga/core/mp3_soc.v), [QSF source list](../src/fpga/ap_core.qsf) |
| Pocket diagnostic firmware, package, and procedure | [sdram_diag.c](../fw/sdram_diag.c), [Phase 1 packager](../tools/package_sdram_diagnostic.py), [CPU-window packager](../tools/package_sdram_cpu_diagnostic.py), [Phase 1 procedure](SDRAM_POCKET_DIAGNOSTIC.md), [CPU-window procedure](SDRAM_CPU_WINDOW_DIAGNOSTIC.md) |
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
| A-03 | Keep audio-critical state in BRAM; pursue a bounded, framebuffer-priority SDRAM bridge for cold data before any execute-in-place experiment. | Active; Phase 1 contention **Pocket** gate accepted. A-075 **Pocket** evidence proves bridge assembly; A-076 proves final CPU return zero; A-077 proves adapter return zero. A-079's isolated **Quartus** gate is accepted; its pending Pocket result discriminates owner-mux output from adapter capture. | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [audit A-075/A-076/A-077/A-079](AUDIT_TRAIL.md), [CPU-window procedure](SDRAM_CPU_WINDOW_DIAGNOSTIC.md) |
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
| Hardware capacity | Uncached SDRAM CPU data window | A-066 **Pocket** proves `sdram_fb` writes and reads `FFFF`. A-067's delayed bridge capture regressed the previously passing MMIO preflight and is rejected. A-074/A-075 prove bridge `FFFFFFFF`; A-076 proves zero at CPU ACK; A-077 **Pocket** proves adapter zero at ACK. A-079 passes isolated **Quartus** (+0.787 ns setup / +0.282 ns hold) and package verification; its Pocket test discriminates mux output from adapter capture. | [issues 016](issues/016-phase2-cpu-window-first-transaction-stall.md), [017](issues/017-a064-probe-stimulus-mismatch.md), and [018](issues/018-phase2-post-bridge-write-readback.md) |
| Hardware capacity | Cached SDRAM data window and cold-workspace migration | Planned; requires a separate cache-line adapter and successful uncached diagnostic/hardware gate. | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md) |
| Hardware capacity | Cold code execution from SDRAM | Deferred; separate later decision gate | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md) |
| Audio architecture | Profile MP3 stages and evaluate a targeted logic/DSP accelerator (IMDCT, Huffman, dequantization, synthesis filterbank) | Deferred until expanded SDRAM passes its Pocket gate; research only, no RTL commitment | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [PROJECT plan](../PROJECT.md) |

## Known issue register

| ID | Summary | Current status |
|---|---|---|
| [001](issues/001-unicode-playlist-paths.md) | Unicode playlist paths may fail to open. | Open; ASCII path names are the safe workaround. |
| [002](issues/002-pocket-os-unicode-metadata.md) | Pocket OS does not render the superscript alpha in textual metadata. | Resolved by ASCII `TAU` OS labels; graphical branding retains alpha. |
| [003](issues/003-loading-splash-tonemapping.md) | Loading art loses tonal separation on Pocket. | Open; needs a Pocket reference photo and asset tuning. |
| [004](issues/004-vm-quartus-detached-launch.md) | Detached VM Quartus commands exit before compilation starts; VM reboots can also drop the configured shared-folder mount. | Managed SSH build session remains the workaround; A-066 staging waits for an explicit re-mount/copy authorization. |
| [005](issues/005-quartus-assembler-internal-error.md) | Phase 1 Quartus Assembler internal assertion. | Quartus gate passed on fresh source-matched build; original assertion not reproduced; Pocket diagnostics remain. |
| [006](issues/006-vm-quartus-soft-lockup.md) | Stale guest soft-lockup console logs were mistaken for a stalled fresh build. | Resolved as a current-build false alarm; earlier guest lockups remain historical and unexplained. |
| [007](issues/007-sdram-diagnostic-staging.md) | Initial diagnostic objcopy and framebuffer-snapshot staging attempts failed. | Resolved on host; neither failure changed the release ROM or RTL. |
| [008](issues/008-diagnostic-core-identity-mismatch.md) | First diagnostic package used a core folder/shortname mismatch and failed during Pocket core setup. | Resolved: corrected package reaches APF Run; runtime behavior moved to issue 009. |
| [009](issues/009-sdram-diagnostic-nonresponsive.md) | Corrected diagnostic initially stalled on an arbiter handshake deadlock. | Fixed; bounded 5-warm/5-cold Pocket matrix passed. Concurrent-load testing remains. |
| [010](issues/010-stress-platform-id-too-long.md) | TAU SDRAM Stress was missing from the core browser. | Five caches regenerated; corrected platform ID is present; ordinary-browser confirmation pending. |
| [011](issues/011-stress-summary-telemetry-missing.md) | Pocket Select+Start behavior differed from reviewed firmware source. | Mitigated by explicit HUD build; old ROM provenance remains an audit gap. |
| [012](issues/012-stress-progress-hud.md) | Multi-minute stress run progress/results were difficult to follow. | HUD ROM staged for Pocket test; a persistent SD log is separately deferred. |
| [013](issues/013-stress-hud-timer-wrap.md) | HUD raw duration loses full intervals after the 32-bit cycle counter wraps. | Fixed ROM staged and card-verified; Pocket-smoke-test a >72 s pass before performance use. |
| [014](issues/014-standalone-soc-lint-missing-vexriscv.md) | Standalone `mp3_soc` lint cannot elaborate without generated VexRiscv RTL. | Open tooling limitation; use the isolated Quartus configurations as top-level elaboration gates. |
| [015](issues/015-phase2-duplicate-soc-instance.md) | Phase 2 conditional left duplicate `mp3_soc` instance header. | Fixed in source; clean macro-enabled Quartus retry pending. |
| [016](issues/016-phase2-cpu-window-first-transaction-stall.md) | CPU-window diagnostic returns mostly-zero data after later all-ones stores. | Open; A-065 targets the failing all-ones transaction for Pocket boundary evidence. |
| [017](issues/017-a064-probe-stimulus-mismatch.md) | A-064 observed the valid zero store rather than the later all-ones failure. | Resolved by A-065's target selection; the resulting downstream boundary is tracked in issue 018. |
| [018](issues/018-phase2-post-bridge-write-readback.md) | A-065 reaches the SDRAM-domain bridge with `FFFFFFFF` and both controller requests accepted, but later reads still return zero. | Open; A-075 **Pocket** evidence shows A-074's bridge assembled `FFFFFFFF`; A-076 proves final CPU ACK returns zero; A-077 proves adapter output is zero at its ACK. A-079 will discriminate the owner-mux output from adapter capture before any migration decision. |

## Updating this register

Update a row whenever its decision, feature state, evidence level, or linked
issue changes. Add a detailed document first when a change needs reasoning,
measurements, or reproduction steps; then add or revise the row here. Do not
turn a **host** or **Quartus** result into a **Pocket** claim without device
evidence.
