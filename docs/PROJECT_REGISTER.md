# Tau decision, feature, and issue register

**Purpose:** a compact handoff index for humans or future models. It identifies
what Tau has decided, what exists, what evidence supports it, and where the
authoritative detail lives. It does not replace the linked records.

For chronological decisions, alternatives, verification evidence, reversals,
and resource/timing trend, read [AUDIT_TRAIL.md](AUDIT_TRAIL.md) alongside this
register.

## Direct code-review links

These links are intended for reviewers who can access the public GitHub
repository but cannot browse the local workspace. The full SDRAM integration
has fitter evidence but is blocked by a Quartus Assembler internal error;
controlled isolations 1–3 have separately passed complete Quartus flows.
Pocket validation remains pending. Read the current issue and chronological
trail rather than treating this compact index as a replacement for them.

| Review area | GitHub source |
|---|---|
| FPGA baseline and reproducible VM build | [FPGA_BUILD.md](FPGA_BUILD.md) |
| SDRAM arbitration policy | [tau_sdram_arbiter.sv](../src/fpga/core/tau_sdram_arbiter.sv), [arbiter testbench](../sim/tb_tau_sdram_arbiter.v) |
| CDC bridge and bounded halfword transactions | [tau_sdram_cpu_bridge.sv](../src/fpga/core/tau_sdram_cpu_bridge.sv), [bridge testbench](../sim/tb_tau_sdram_cpu_bridge.v) |
| Top-level controller integration and diagnostic MMIO | [core_game.vh](../src/fpga/core/core_game.vh), [mp3_soc.v](../src/fpga/core/mp3_soc.v), [QSF source list](../src/fpga/ap_core.qsf) |
| Current integration test/build status | [AUDIT_TRAIL.md](AUDIT_TRAIL.md), [issue 005](issues/005-quartus-assembler-internal-error.md), [PROJECT.md](../PROJECT.md) |
| VM build detachment workaround | [issue 004](issues/004-vm-quartus-detached-launch.md) |

**Evidence labels:** **Pocket** = observed on an Analogue Pocket; **Quartus** =
compiled and timing/resource checked; **host** = automated host test or local
firmware build; **design** = agreed intent, not implementation evidence.

## Architectural decisions

| ID | Decision | Status / evidence | Record |
|---|---|---|---|
| A-01 | Keep the 400×360 RGB565 raster at 60 Hz. It maps exactly 4× to Pocket’s 1600×1440 display; no 640×480 change is planned. | Active; **Pocket** baseline | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [technical spike](TAU_TECHNICAL_SPIKE.md) |
| A-02 | Tau is a separate, attributed HarpMudd-derived core with a separate Pocket package identity. | Active; **Pocket** package verified | [PROJECT.md](../PROJECT.md), [NOTICE.md](../NOTICE.md) |
| A-03 | Keep audio-critical state in BRAM; pursue a bounded, framebuffer-priority SDRAM bridge for cold data before any execute-in-place experiment. | Active; bridge/arbiter **simulation** tested; full integration fitter passed but Assembler blocked; isolation flows 1–3 **Quartus** passed | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [issue 005](issues/005-quartus-assembler-internal-error.md) |
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
| Hardware capacity | Diagnostic SDRAM MMIO access | RTL integrated; **simulation** tests pass; full fit completed but final Assembler failed; controlled Quartus isolations are narrowing the trigger; no Pocket claim | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [issue 005](issues/005-quartus-assembler-internal-error.md) |
| Hardware capacity | Cached SDRAM data window and cold-workspace migration | Planned; blocked on successful diagnostic gate | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md) |
| Hardware capacity | Cold code execution from SDRAM | Deferred; separate later decision gate | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md) |
| Audio architecture | Profile MP3 stages and evaluate a targeted logic/DSP accelerator (IMDCT, Huffman, dequantization, synthesis filterbank) | Deferred until expanded SDRAM passes its Pocket gate; research only, no RTL commitment | [SDRAM architecture](SDRAM_MEMORY_ARCHITECTURE.md), [PROJECT plan](../PROJECT.md) |

## Known issue register

| ID | Summary | Current status |
|---|---|---|
| [001](issues/001-unicode-playlist-paths.md) | Unicode playlist paths may fail to open. | Open; ASCII path names are the safe workaround. |
| [002](issues/002-pocket-os-unicode-metadata.md) | Pocket OS does not render the superscript alpha in textual metadata. | Resolved by ASCII `TAU` OS labels; graphical branding retains alpha. |
| [003](issues/003-loading-splash-tonemapping.md) | Loading art loses tonal separation on Pocket. | Open; needs a Pocket reference photo and asset tuning. |
| [004](issues/004-vm-quartus-detached-launch.md) | Detached VM Quartus commands exit before compilation starts. | Workaround active: managed interactive SSH build session. |
| [005](issues/005-quartus-assembler-internal-error.md) | Full SDRAM integration reaches Quartus fitter but fails in the final Assembler. | Open; isolations 1–3 passed, residual source/MMIO differences under controlled test. |

## Updating this register

Update a row whenever its decision, feature state, evidence level, or linked
issue changes. Add a detailed document first when a change needs reasoning,
measurements, or reproduction steps; then add or revise the row here. Do not
turn a **host** or **Quartus** result into a **Pocket** claim without device
evidence.
