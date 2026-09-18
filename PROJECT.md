# Tau project

Tau is an Analogue Pocket music player core derived from
[HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player) v1.4.0.
It preserves the upstream Git history and copyright notice; see `NOTICE.md` for
full provenance and third-party licenses.

**Current preview:** v0.1.0 · **Core identity:** `alfatreze.TAU` ·
**Platform:** `tau` / Media Players

**Current technical status (2026-09-18):** Phase 1 SDRAM access and contention
gates are accepted on Pocket. Phase 2 A-074 seed 2 fits cleanly and its first
Pocket run proves the bridge assembled `FFFFFFFF`, but the CPU still receives
zero. A-076 adds a return-path probe; focused RTL tests pass, while its
Quartus/Pocket gates remain pending.

## Completed

- Established the HarpMudd v1.4.0 baseline at upstream commit
  `7ef8f0fb84abd4ae5a6a187805fd910351ae57dc`.
- Created a separate Tau package so HarpMudd and Tau can coexist on one Pocket:
  `/Cores/alfatreze.TAU/`, `/Assets/tau/`, and `/Platforms/tau.json`.
- Added Tau platform and author artwork, converted to the Pocket monochrome
  asset format.
- Added a full-screen loading asset with contextual status text and an
  indeterminate progress segment, supplied as a compact deferred RLE asset.
- Confirmed on Pocket hardware: package appears under **Media Players**;
  platform artwork and author icon load; MP3 playback, seeking, album artwork,
  and visualizers work.
- Confirmed Pocket OS does not render the superscript alpha in metadata. OS
  labels now use ASCII `TAU`; the alpha brand mark remains in graphical work.
- Added host regression tests for playlists, loading-asset structure, package
  identity, framebuffer draw operations, target commands, PCM underrun decay,
  and bit-exact EQ.
- Added `make visual-review` and a named UI-state manifest. The loading frame
  is asset-exact; empty-library, playlist-error, no-art now-playing,
  playlist-browser, paused, stopped, seeking, long/missing metadata, and toast
  states now have reproducible
  400×360 RGB565 framebuffer models that read the production font ROM, glyph
  metrics, colour values, layout strings, and fractional-scale behaviour.
  All eleven visualizer families now also have frozen deterministic models;
  these represent framebuffer composition, not live Pocket capture.
  Remaining legacy references and pending state fixtures are labelled honestly
  in `work/previews/index.html`.
- Defined the future settings architecture: Appearance, Audio, Playback, and
  Advanced capability/opt-in layers.
- Completed the initial SDRAM architecture audit and selected a staged hybrid
  direction: retain audio-critical code/data in BRAM, add a bounded CPU bridge
  to the proven framebuffer SDRAM controller, migrate cold data first, and gate
  cold-code execution on hardware results. See
  `docs/SDRAM_MEMORY_ARCHITECTURE.md`.
- Rebuilt the unmodified FPGA baseline with Quartus Prime Lite 25.1std on the
  supported Linux x86-64 build VM. The final fit uses 5,587 / 18,480 ALMs
  (30%), 300 / 308 RAM blocks (97%), and 11 / 66 DSP blocks (17%); all timing
  checks pass. The full build took 42m 58s. See `docs/FPGA_BUILD.md`.
- Started SDRAM Phase 1 with a standalone, simulation-tested port arbiter. It
  gives framebuffer traffic priority at idle arbitration points and locks each
  accepted owner through completion. A paired asynchronous CPU bridge now also
  passes isolated 60/100 MHz simulation, translating each 32-bit operation to
  two bounded 16-bit requests. Both are integrated into the live controller
  path behind dormant diagnostic MMIO registers. A fresh Quartus build of the
  current FPGA source passed and produced `.sof`/`.rbf` artifacts; controlled
  Pocket diagnostics still gate any SDRAM data migration.
- Added a separate Phase 1 SDRAM diagnostic firmware and side-by-side Pocket
  package. It performs 183 fixed-pattern, walking-bit, sparse address-as-data,
  and byte/halfword-lane readback checks above the framebuffer's 1 MiB guard.
  Host compilation, package identity/bit-reversal checks, RTL regressions, and
  running/pass/fail framebuffer fixtures pass. Pocket completed five warm and
  five cold passes, 183 checks each with zero failures; see
  `docs/SDRAM_POCKET_DIAGNOSTIC.md`.
- Completed Phase 1 SDRAM Pocket readback and concurrent visualizer contention
  testing; see the named Pocket evidence in `docs/SDRAM_POCKET_DIAGNOSTIC.md`
  and `docs/SDRAM_CONTENTION_DIAGNOSTIC.md`. Added an opt-in Phase 2 uncached
  CPU data path: explicit address decoder, held classic-Wishbone adapter, and
  shared bridge-owner mux are wired into `mp3_soc` / `core_game.vh`. The
  end-to-end RTL read/write path test and full `make test-rtl` suite pass.
  `TAU_PHASE2_WINDOW` is required to select the new map; the default core keeps
  the old map. The first enabled Quartus attempt caught a duplicate SoC
  instance declaration and stopped before fitting; the source is corrected.
  The fresh isolated retry passed Quartus (7,796 registers, 300/308 RAM blocks,
  11/66 DSP blocks, zero TNS, and 0.120 ns minimum reported hold slack). The
  current macro-off/default branch also passed Quartus (7,609 registers,
  300/308 RAM blocks, 11/66 DSP blocks, zero TNS, and 0.118 ns minimum reported
  hold slack). A-074 seed 2 then passed with +0.662 ns setup, +0.119 ns hold,
  and TNS 0; its Pocket run completed 183 checks with 181 failures while the
  bridge-response cells reported `G-R-G` (assembled `FFFFFFFF`, CPU result
  still zero). A-076 now instruments the CPU-facing return path; no normal
  player data uses mapped SDRAM.
- Documented battery/power work: real in-core battery state is blocked by the
  current documented openFPGA API, while internal efficiency instrumentation is
  viable later.

## Current constraints

The canonical cross-reference for architectural decisions, feature status,
evidence level, and known issues is `docs/PROJECT_REGISTER.md`. The
chronological decision/reversal/evidence trail and resource trend are in
`docs/AUDIT_TRAIL.md`.

- Target raster remains 400×360 at 60 Hz: an exact 4× map to Pocket's
  1600×1440 display. No raster change is planned without measurements.
- Firmware builds locally. FPGA recompilation is verified on the project’s
  Linux x86-64 VM; Quartus must build on its local ext4 working copy, not the
  macOS shared-folder mount.
- The VM's detached SSH launcher currently exits before a Quartus build starts;
  use a managed interactive session until investigated. See
  `docs/issues/004-vm-quartus-detached-launch.md`.
- Fresh Phase 1 Quartus flow passed on 2026-09-14 and generated both `.sof` and
  `.rbf`. Fit: 5,706 ALMs, 7,414 registers, and 299 / 308 RAM blocks (97%).
  Timing has zero TNS and positive slack, but the tightest reported hold slack
  is only 0.119 ns; re-run timing after any CDC/clocking change. No Pocket
  diagnostic has been performed. See issue 005 and audit entry A-024.
- The soft-lockup messages initially mistaken for current-build activity were
  stale console logs from the guest's 2026-09-13 boot, over 11 hours before the
  Sep 14 build. Later host CPU/memory samples were also taken hours after the
  build completed, so they cannot establish whether full-screen video affected
  build time. Details and correction: `docs/issues/006-vm-quartus-soft-lockup.md`.
- Firmware uses 152,088 bytes (84.4% of the current usable RAM budget).
- A runtime settings-home prototype does not fit the protected firmware
  memory layout; see `docs/SETTINGS_RUNTIME_BUDGET.md`. Do not reduce decoder,
  DMA, stack, or linker-heap reservations merely to accommodate UI code.
- The external SDRAM remains framebuffer-only for product features. A bounded
  diagnostic MMIO bridge and owner mux are fitted; A-075 proves the bridge
  response on Pocket, but the CPU-facing Wishbone return path is not yet
  proven. A-076's focused return probe passes RTL simulation; its Quartus and
  Pocket gates, followed by concurrent playback/CRC, must pass before any
  mapped SDRAM feature is enabled. See audit A-076 and issue 018.
- Loading art currently uses a 16-entry RGB565 palette; on-device tonal tuning
  awaits a Pocket reference photo.
- Playlist paths with Unicode names remain a known compatibility investigation;
  short ASCII names are the current safe baseline.

## Plan

### 1. Snapshot baseline — in progress

Continue replacing each legacy visual reference with a reproducible 400×360
production framebuffer model or an asset-exact decode. The next fixtures are
future settings/battery states. Device capture
remains necessary for Pocket OLED behaviour.

### 2. Tune the loading artwork

Use the exact asset capture alongside a Pocket photo to separate background,
glow, waveform, text, and progress luminance bands. Retest on hardware.

### 3. Validate the opt-in uncached SDRAM CPU window — next technical work

The A-074 seed-2 diagnostic build is accepted for the bridge-response boundary
and its Pocket result is recorded, but the CPU-facing return path still fails.
A separate A-076 firmware diagnostic/probe must first pass Quartus and Pocket;
stage only that separately named macro-enabled RBF and run the documented
return-path test. After it passes, add concurrent
1 MiB CRC and playback stress before migrating at least 24 KiB of cold
playlist/artwork workspace. Preserve framebuffer priority, introduce no
unplanned M10K use, and require zero audio underruns or display corruption.
Do not implement a cached-window adapter until this sub-gate passes; cold-code
execution is a separate later gate. Full context and the explicit cache/burst
limitations are in `docs/SDRAM_MEMORY_ARCHITECTURE.md` and A-054.

### 3a. Evaluate targeted FPGA audio acceleration — deferred until SDRAM works

After the expanded SDRAM data path has a timing-clean build and passes its
Pocket concurrency/stability gate, profile the MP3 decode pipeline to identify
whether specific stages still limit playback or consume meaningful CPU time.
Then evaluate a small FPGA logic/DSP accelerator for one measured bottleneck,
with candidate stages including IMDCT, Huffman decode, dequantization, and the
subband synthesis/polyphase filterbank. Keep the software decoder as the
reference and fallback; do not assume a full codec-chip recreation is needed.

Compare CPU-only and accelerated implementations for bit-exact output,
throughput/latency, ALM/DSP/M10K use, clock timing, SDRAM contention, and audio
underruns. Consider FPGA sound-generator projects as implementation references.
Treat a PMOD I2S2 controller as a possible serial-audio-interface reference
only: it does not provide MP3 decode acceleration, and its fit with the Pocket
audio path must be established separately. This is a research gate, not a
committed RTL feature.

### 4. In-app settings — memory-budget gate

Complete the Figma interaction model and measure a minimal implementation
after the SDRAM data phase recovers its budget. Start with named theme,
visualizer, and EQ views, then Playback. Advanced options require both an
Advanced-capable build and explicit user opt-in.

### 5. Diagnostics and efficiency instrumentation

After the snapshot suite is reliable, add guarded performance counters for
decoder headroom, framebuffer pressure, SD activity, and audio FIFO margin.
Only introduce persistent logs with a dedicated safe `/Saves/tau/` slot and
power-loss tests. Do not add a battery meter until a documented API exposes
real battery telemetry.

### 6. Hardware and release discipline

Run the complete audio/transport matrix after each firmware change. Rebuild the
RTL and capture timing/resource reports on a supported Quartus host before any
RTL change is released. Publish release archives only after package validation,
hardware confirmation appropriate to the change, and a `PROJECT.md` update.

## Publication rule

Update this file before every GitHub push or release when the published project
state has materially changed. Record what was verified on hardware separately
from host-side checks, keep the next action current, and preserve HarpMudd
attribution.
