# Tau alpha technical spike

## Baseline decision

Start from HarpMudd v1.4.0 at commit
`7ef8f0fb84abd4ae5a6a187805fd910351ae57dc` and keep its audio path intact.
The upstream core already outputs 400x360 at 60 Hz from a 12 MHz pixel clock.
That maps exactly 4x to the Pocket's 1600x1440 panel and should be treated as
the default Tau alpha target unless hardware measurements disprove it.

Do not rename the core, change the raster, or redesign the renderer before the
baseline package has played representative files on hardware. Keeping the
upstream identifiers initially also lets the supplied v1.4.0 bitstream and ROM
serve as a known-good control.

## Spike outcomes

The spike is complete when we have evidence for all of the following:

1. The unmodified v1.4.0 package boots on a Pocket at 400x360.
2. MP3 CBR/VBR and FLAC playback remain clean for a 30-minute stress run.
3. Album-art load, each visualizer, seeking, queue browsing, EQ, shuffle, and
   repeat do not produce audible underruns.
4. Firmware rebuilds from this checkout and the rebuilt ROM matches the
   baseline behaviour.
5. Quartus rebuilds the FPGA image and reports timing/resource utilization.
6. A Tau diagnostic screen can display frame cadence, draw-queue pressure,
   audio FIFO low-water mark, and decoder headroom without affecting playback.

## Build order

Run these in order; each step preserves a known-good fallback:

1. `make test` -- host-side regressions that need no FPGA toolchain.
2. `make check-firmware` -- inventory the local cross-compiler.
3. `make firmware` -- rebuild only the SD-loaded RISC-V firmware.
4. Copy only the rebuilt ROM to a test SD card and repeat the audio matrix.
5. `make check-fpga` on the FPGA build host.
6. `make fpga` -- rebuild the unchanged RTL and capture timing/resource reports.
7. `make package` -- bit-reverse and assemble the Pocket distribution.
8. Add diagnostics behind a compile-time flag; repeat the same matrix.

## Hardware test matrix

Use short, legally shareable test assets plus one 30-minute playlist:

| Area | Minimum cases | Pass condition |
|---|---|---|
| MP3 | MPEG-1 CBR 320 kbps; VBR; MPEG-2 low rate; mono | No glitch, stall, or incorrect duration |
| FLAC | 16/44.1 stereo; 24/44.1 stereo; 24/48; mono | No glitch; unsupported hi-res rejected clearly |
| Metadata | UTF-8-ish tags, missing tags, long title, missing art | UI remains responsive and bounded |
| Artwork | baseline JPEG, progressive JPEG, PNG, corrupt image | Decode or explicit reason; no crash |
| Transport | pause, seek ramp, 1 s seek, skip, stop, resume | Correct state and no stale audio |
| Load | every meter, art slide, playlist scroll, EQ changes | No audible underrun |
| End state | repeat off/all/one and shuffle | Correct queue transition |

## Guardrails

- Audio FIFO service always outranks visual work.
- Keep the 60 MHz CPU, 100 MHz SDRAM, and hardware EQ unchanged in the first
  diagnostic build.
- Keep framebuffer commands coarse; avoid CPU-side pixel loops.
- Preserve the 512-word row stride even though only 400 pixels are visible.
- Treat 400x360 as a measured constraint for Figma until profiling says the UI
  needs a different tradeoff.
- A new Tau core identity comes after baseline equivalence so both builds can be
  installed side-by-side during development.

## Current environment status (2026-09-12)

- Repository cloned and pinned on branch `tau/technical-spike`.
- Upstream packaged v1.4.0 ROM and bitstream are present.
- Host playlist regression passes.
- Python 3 is available.
- xPack RISC-V Embedded GCC 15.2.0-1 for Darwin arm64 is installed in the
  ignored `toolchain/` directory and auto-detected by the build scripts.
- The rebuilt firmware is byte-for-byte identical to the shipped v1.4.0 ROM:
  150,504 bytes, SHA-256
  `90e2506802af0d9a80d56da5e7218cf63a6d8dceea36bccda5cbdf215c4c6280`.
- Quartus is not available on this macOS host; FPGA recompilation therefore
  remains pending on an x86-64 Windows or Linux host.
- Icarus Verilog 13.0 is installed and the framebuffer/draw engine,
  target-command bridge, bit-exact EQ, PCM underrun decay, and EQ-cycle benches
  pass. The EQ engine is busy for 116 of 1,250 clocks per 48 kHz sample (9.2%).
- Two upstream simulation-maintenance issues were repaired: declaration order
  in `mp3_fb.sv` and obsolete target-command ports in `tb_tgt_cmd.v`. Neither
  change alters synthesized behaviour.
- Upstream `fw/build.sh` was made checkout-relative and path-safe so this
  workspace path (which contains spaces) is supported on macOS, Linux, and Git
  Bash.

## Host strategy

This checkout is on Apple Silicon macOS. The firmware can build locally with
the same xPack RISC-V Embedded GCC 15.2.0-1 used upstream; xPack publishes a
native Darwin arm64 archive. A Homebrew `riscv64-elf-` toolchain is a reasonable
fallback, but matching upstream first removes a compiler-version variable.

Quartus Prime 25.1 Standard/Lite runs on Windows and Linux, not macOS. Use an
x86-64 Windows or Linux build host for the FPGA image. A dedicated or remote
machine is preferable to emulating an x86-64 Linux VM on Apple Silicon because
Quartus is large and FPGA compiles are CPU- and memory-heavy. The project QSF
records `25.1std.0 Lite Edition`, and Cyclone V is supported by Lite, so no paid
Standard license should be necessary for this core.
