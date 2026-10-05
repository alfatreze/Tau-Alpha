
# TAUᵅ — Analogue Pocket Music Player

A music player for the Analogue Pocket. It plays MP3 and FLAC straight off the SD card, with a browsable
library, album art, tags, themes, an equalizer and a set of live meters.

Decoding runs in software on a RISC-V CPU built into the Pocket's FPGA. Since v0.5.0 the largest single cost
of MP3 decoding (the synthesis filterbank) runs in FPGA hardware instead, and since v0.6.0 the same is true
for FLAC's LPC/FIXED reconstruction; the screen is drawn by a small 2D drawing engine ("Talos") rather than
pixel by pixel from the CPU.

Current version **v0.6.0** (4 October 2026), pre-release **0.6.0-alpha.3**. See [CHANGELOG.md](CHANGELOG.md).

<img width="400" height="360" alt="20260930_235633" src="https://github.com/user-attachments/assets/103e9513-db6b-42d2-8834-debe4b39251d" />
<img width="400" height="360" alt="20260930_235713" src="https://github.com/user-attachments/assets/1194cfac-1036-40ce-a656-de22e0866354" />

## What Tau does

- **MP3 and FLAC.** CBR and VBR MPEG-1 and MPEG-2 Layer III at every standard bitrate and sample rate, mono
  or stereo; FLAC up to 48 kHz, 8/16/20/24-bit. Tags and embedded album art are read from the files.
- **Media library.** Artists, Albums, Tracks and Shuffle All, built on the card by a sync tool, plus your own
  `.m3u` playlists imported as Lists. The library is required: it is the only way to play more than one file.
- **Themes.** Two built-in themes (TAU and OCEAN), each in Dark and Light, a 19-colour accent palette, and
  optional extra themes from a `tau-assets.bin` file. Theme and Dark/Light mode are remembered across a
  restart.
- **Eleven meters.** Winamp Scope, Winamp Bars (with a Configure page: presets, band count, easing, peak
  caps), a Chladni-pattern meter (with a fullscreen view), Bars, Waterfall, Phase Scope, Oscilloscope, VU,
  Waveform, Peak Dots and a 16-band Spectrum. Level and spectrum measurement run in hardware.
- **Fast covers.** A pre-scaled cover file shows an album's art in about 90 ms instead of decoding the
  embedded JPEG (2.6 to 15.8 s).
- **Hardware decode assist.** MP3's synthesis filterbank and FLAC's LPC/FIXED reconstruction both run in the
  FPGA, bit-exact with the software decoder and with automatic fallback to it. More CPU headroom, so
  playback stays clean at speeds that used to stutter.
- **Equalizer.** Eight loudness-matched presets, built as hardware (five biquads per channel).
- **Playback speed** 0.85x to 2.00x for MP3 (FLAC plays at 1.00x), for spoken word; the pitch rises with the speed.
- **Diagnostics.** An Info page in every build, and a separate Diagnostic Build with one-button Check
  profiles, a Meter Sweep and QR-code reports that can be decoded from a screenshot.

<br clear="right">
<img width="400" height="360" alt="20260930_235724" src="https://github.com/user-attachments/assets/1be1db5c-2ad3-4db2-b370-ca151b2401ef" />
<img width="400" height="360" alt="20260930_235740" src="https://github.com/user-attachments/assets/21790d13-cb8c-418c-bbd8-40cc0bf34c23" />

## Performance at a glance

**TBD** means the measurement is planned but no real hardware number has come back yet. The full page, with
sources and the honest limits, is [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

| What we checked | In plain terms | The numbers | Roughly speaking |
|---|---|---|---|
| MP3 decoding cost of the filterbank | Share of decode time spent in the stage that moved into hardware | 22% at 1.0x, was 55-59% (B-309) | **about 2.5x smaller share** |
| Speed headroom | How fast playback can run before audio breaks up | Clean at 1.75x (was stuttering at 1.25x); clean through 2.0x in Diagnostic Build owner tests | **much more headroom** |
| Album cover appearance | Time from track load to cover on screen | about 90 ms with a `.timg` file, vs 2.6-15.8 s decoding the JPEG | **roughly 30x to 175x faster** |
| Room left for new features | Free heap on the chip's own memory (release build) | 49,712 bytes (v0.5.0); 30,528 before menus and library moved to PSRAM | **about 1.6x more than before cold code** |
| Drawing the classic bar meter | Drawing steps per frame | 36, was 72 | **50% less work per frame** |
| Music stability under the heaviest load | Does audio ever stutter with flat-out drawing for 30 s | 0 late underruns in every run so far; blit storm used 15.8% of SDRAM cycles | **no glitches found** |
| Cost of running cold code from PSRAM | Extra time for code kept off-chip | worst case about 28,800 cycles per draw, about 1.7% of one audio frame | **a small, deliberate trade** |
| New Winamp meters vs the classic bars | Draw cost, measured | Estimated 16-32 commands/frame; **measured (Meter Sweep): TBD** | **TBD** |
| Battery life with the new visuals | Whether fancy meters drain faster | **TBD** | **TBD** |
| Menu and library responsiveness | Snappiness after code moved to PSRAM | **TBD** | **TBD** |


## Install

1. Copy the `Cores`, `Platforms` and `Assets` folders onto the root of the Pocket's SD card, merging with what is there.
2. Put your `.mp3` / `.flac` files in `/Assets/tau/common/` (subfolders such as `Artist/Album` are fine). `tau.rom` must stay there.
3. Build a library: `python3 tools/sync_media.py /path/to/music --all-tau --library`. This is required -- there is no other way to browse or queue more than one file.
4. Start the core from the Pocket menu. Press **Select** for the library, **Start** for Settings.

Full controls, playlists, settings and known limits: [docs/guide/USER_GUIDE.md](docs/guide/USER_GUIDE.md).

<br clear="right">

## Documentation

| Page | What is in it |
|---|---|
| [User guide](docs/guide/USER_GUIDE.md) | Controls, playlists, library, themes, meters, EQ, speed, FLAC, known limitations |
| [Diagnostics](docs/guide/DIAGNOSTICS.md) | The Info page, the Diagnostic Build, Check profiles, Meter Sweep, QR reports, how to send results |
| [Media and tools](docs/guide/MEDIA_AND_TOOLS.md) | Preparing media: `sync_media.py`, the library index, TIM1 covers, `tau-assets.bin`, the card installer, the Tau Omega companion app |
| [Technical specification](docs/TECHNICAL_SPEC.md) | FPGA design, memory map, audio path, draw engine, firmware architecture, file formats, memory budgets, verification |
| [Talos](docs/TALOS.md) | The 2D draw engine: opcodes, the MMIO register model, what's hardware-confirmed vs shelved, roadmap |
| [Helios](docs/HELIOS.md) | The UI controller built on Talos: dirty-region tracking, beam-aware drawing, double buffering, roadmap |
| [Performance](docs/PERFORMANCE.md) | Every measured number with its source, tradeoffs and honest limits, how we measure |
| [For core developers](docs/DEVELOPERS.md) | Issues faced and fixes, useful techniques, building, testing, Quartus fits, repository map, contributing |
| [Attributions and licences](docs/ATTRIBUTIONS.md) | Everything Tau builds on, what is MIT and what keeps its own licence |
| [Roadmap and status](docs/ROADMAP_PUBLIC.md) | What is done and what is next |
| [How it works](docs/HOW_IT_WORKS.md) | The interesting parts of the build, told as a story |
| [Documentation map](docs/README.md) | Index of every document in `docs/` (specs, plans, handoffs, test scripts, issues) |

## Companion app

**[Tau Omega](../Tau%20Omega/)** is the desktop companion (macOS and Windows) for building and syncing the
Tau media library onto the Pocket's SD card and for managing Tau (and other openFPGA) cores and their media.
It is a separate project and repository, built as the product version of `tools/tau_library.py` and
`tools/sync_media.py`.

## Provenance

Tau is a derivative of **[HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player)** v1.4.0 by
HarpMudd. As of last count 60% had already been re-written, with quite a bit still to re-work. The project intentionally retains the complete upstream Git history, copyright notice, and inherited
release history in [CHANGELOG.md](CHANGELOG.md). Tau's new identity, packaging, artwork pipeline, technical
plans, and subsequent changes are maintained by alfatreze. See [NOTICE.md](NOTICE.md) for provenance and
third-party licensing boundaries, [docs/ATTRIBUTIONS.md](docs/ATTRIBUTIONS.md) for the full credits, and
[PROJECT.md](PROJECT.md) for the current milestone ledger and plan.

## License

HarpMudd's original code and Tau's own code and modifications are [MIT licensed](LICENSE), with both
copyright notices preserved. Everything under `third_party/` keeps its own license, and MIT here relicenses
none of it: [Helix](third_party/libhelix-mp3/docs/RPSL.txt) is RPSL 1.0 and [Inter](third_party/font/OFL.txt)
is SIL OFL 1.1. Details, and the rule that GPL RTL may be studied but never copied in, are in
[docs/ATTRIBUTIONS.md](docs/ATTRIBUTIONS.md).

## Upstream support

Tau preserves HarpMudd's original support link as an acknowledgement of the
project it builds on:

💛 **[Support HarpMudd via PayPal](https://www.paypal.com/donate/?hosted_button_id=S22WV924XU2ME)**
☕️ **[Buy Alfatreze a Coffee or help out with my redbulls, Claude and Codex Subs 😇](https://buymeacoffee.com/alfatreze)**
