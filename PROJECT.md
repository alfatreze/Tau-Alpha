# Tau project

Tau is an Analogue Pocket music player core derived from
[HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player) v1.4.0.
It preserves the upstream Git history and copyright notice; see `NOTICE.md` for
full provenance and third-party licenses.

**Current preview:** v0.1.0 · **Core identity:** `alfatreze.TAU` ·
**Platform:** `tau` / Media Players

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
- Documented battery/power work: real in-core battery state is blocked by the
  current documented openFPGA API, while internal efficiency instrumentation is
  viable later.

## Current constraints

- Target raster remains 400×360 at 60 Hz: an exact 4× map to Pocket's
  1600×1440 display. No raster change is planned without measurements.
- Firmware builds locally. FPGA recompilation is verified on the project’s
  Linux x86-64 VM; Quartus must build on its local ext4 working copy, not the
  macOS shared-folder mount.
- Firmware uses 152,088 bytes (84.4% of the current usable RAM budget).
- A runtime settings-home prototype does not fit the protected firmware
  memory layout; see `docs/SETTINGS_RUNTIME_BUDGET.md`. Do not reduce decoder,
  DMA, stack, or linker-heap reservations merely to accommodate UI code.
- The external SDRAM is currently framebuffer-only. CPU access requires new
  60/100 MHz CDC, arbitration, address decoding, and hardware contention tests;
  no feature may treat the proposed SDRAM map as implemented yet.
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

### 3. SDRAM capacity gate — next technical work

Implement the staged decision in `docs/SDRAM_MEMORY_ARCHITECTURE.md`. Begin with
a bounded diagnostic CPU bridge and shared arbiter, then migrate at least 24 KiB
of cold playlist/artwork workspace. Preserve framebuffer priority, introduce no
unplanned M10K use, and require zero audio underruns or display corruption in the
Pocket stress matrix. Cold-code execution is a separate later gate.

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
