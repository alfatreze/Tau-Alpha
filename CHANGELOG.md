# Changelog

What changed in each release, newest first.

## v0.6.0-alpha.4 — 7 October 2026

- **Volume is a dB taper with a click-free ramp** (100 positions over 60 dB); hold Up/Down repeats the volume. Saved
  volumes sound quieter than before (same number, different loudness); the default is higher to compensate.
- **ReplayGain** (Off / Track / Album) under Settings > Audio, reading ID3 and FLAC tags, attenuate-only. The setting
  now saves correctly and the row is shown in the menu.
- **FLAC 24-bit files are rounded, not truncated**, when reduced to 16 bits (removes a small DC offset and distortion).
- **Cymo resampler** and the 16-bit output switch toggle without a level step (Diagnostic Build); hardware gain stage
  and 24-bit EQ coefficient groundwork are in the firmware as probes that stay off on this bitstream.
- **Report codes** (Check, Info) are shown as a pixel grid by default, with the QR code one press away.
- **Diagnostic Build:** gap-latency and hardware-gain Info rows, MP3 LAME gapless tag parsing, Halcyon EQ data format
  and tools (not active), more Check measurements. `Track changes` still fails in Check (a test issue).
- **Audiobook tempo** is built (MP3 only, Settings > Playback) but remains a diagnostic feature.
- The bitstream is the same as alpha.3 (no hardware changes).

## v0.6.0-alpha.3 — 4 October 2026

- **MP3 playback uses much less CPU.** On a stereo MP3 at 1.00x the processor was busy 55% of the time; it is now
  44%. Two firmware changes: the hardware synthesis-window unit now works while the CPU runs the next slot's
  software transform (the CPU used to wait for it), and audio samples are pushed to the output FIFO in bursts
  instead of one status check per sample. Output is sample-for-sample identical (checked against the old path on
  the host); the extra headroom goes to meters, speed changes and the UI. The bitstream is the same as alpha.2.
- **Diagnostic Build: stress tests no longer fill the screen with noise.** The SDRAM stress pump overlapped the
  second display buffer used by the double-buffered UI; it now works on memory nothing else uses.
- **Diagnostic Build: new report and measurement tools.** Check and the Info page can show their report as a pixel
  grid as well as a QR code, and the Check report now includes CPU load over the audio window and where the time
  goes (decode, meters, sample push).
- **Release tooling.** Packaging now refuses a firmware that the bitstream would refuse at boot (the cause of the
  alpha.1 normal core not starting).
- **Known limits.** `Track changes` still fails in the Diagnostic Build's Check (a test issue). Audiobook tempo is
  built but not in this release.

## v0.6.0-alpha.2 — 4 October 2026

- **FLAC decodes much faster.** The residual (Rice) decoding was rewritten around a 32-bit window with an
  inline leading-zero count. On a hard 48 kHz stereo file (415 kbps) the CPU went from fully busy (0% idle,
  12 FIFO stalls in 30 s) to 45% idle with none. Output is bit-identical to before (checked on random
  streams and on eight real files, including a 96 kHz 24-bit one). Speed changes remain MP3 only.
- **Layered Wave meter**, a new nested-envelope meter, plus fixes for meter flicker and for the Configure
  screen; Settings and fullscreen meters no longer steal time from the audio.
- **Start opens Settings** (on release), **Start+Y** jumps to Meter > Configure; **hold Up/Down repeats the
  volume**; the speed list goes up to 2.00x for MP3.
- **Better diagnostics.** A HEADROOM row on the Info page (idle CPU, worst second, projected maximum speed),
  a library-file-missing screen, CPU LOAD and fullscreen label fixes. The Diagnostic Build also counts every
  FIFO stall and reports the heap peak in Check.
- **Audio hand-off fix (bitstream).** The resampler's input hand-off no longer repeats or drops samples
  (hardware-confirmed); the resampler is on by default only in the Diagnostic Build, for 44.1 kHz files at
  1.00x.
- **Cover and splash colours fixed.** Cover art and the meter previews had their palette shifted by one
  slot (a hardware write-address skew); the hardware now writes the right slot and the firmware detects
  which behaviour it is on. The boot splash uses a 256-colour palette. Meter previews now draw correctly in
  both display buffers, and the embedded-JPEG cover fallback uses less on-chip RAM.
- **Known limits.** `Track changes` still fails in the Diagnostic Build's Check (a test issue). Audiobook
  tempo is built but not in this release.

## v0.6.0-alpha.1 — 30 September 2026

- **FLAC decoding on hardware.** Like MP3's synthesis filterbank in v0.5.0, FLAC's LPC/FIXED sample
  reconstruction (the largest single share of FLAC decode cost) now runs in the FPGA, bit-exact with the
  software version and falling back to it if the unit ever disagrees.
- **More on-chip memory and a faster clock.** The CPU's on-chip RAM was reorganised (freeing 64 KB) and the
  system clock raised from 60 to 66.667 MHz -- both built and closed together on real hardware with real
  timing margin, and firmware now fits the smaller RAM comfortably (15.8 KB of free heap to spare).
- **Settings persist properly.** Theme and Dark/Light mode are now remembered across a restart (previously
  reset every time); two leftover "Load Audio File" / "Load Playlist" actions from the old playlist mode
  are gone from Core Settings.
- **Meter fixes.** Fullscreen Winamp Bars no longer clip at 127 rows -- the hardware field behind them was
  widened. Winamp Scope's fade trail no longer accumulates into a solid smear on repeated use. The
  Chladni-pattern meter no longer goes blank after leaving Settings, fullscreen or the Configure page. All
  four live-preview meters (Bars, Scope, Chladni, VU Master) now animate correctly on the Configure page
  instead of freezing. The MASTER VU meter, listed since v0.5.0 but never actually wired in, now works.
- **Smoother menu transitions.** Settings menu changes now cross-fade using the display hardware's own
  alpha blend instead of a software colour trick.
- **Known limits.** `Track changes` still fails in the Diagnostic Build's Check (a test issue, playback is
  unaffected; carried over from v0.5.0). Meter preset choices (not theme/mode) still reset on restart --
  there is persistence storage to spare, but the display file's own hard 16-entry cap left them out of
  this round. Speeds above 1.20x remain Diagnostic Build only.

## v0.5.0 — 27 September 2026

- **Themes.** Settings > Appearance has a THEME and a MODE row. Two built-in themes (TAU and OCEAN), each in Dark and Light,
  and the accent colour palette now matches its reference colours (the old values were wrong in green and blue). Text stays
  crisp in Light mode: the display hardware has a second text-weight table for it. Extra themes can be loaded from a
  `tau-assets.bin` file beside the music (format: `docs/THEME_FILE_FORMAT.md`); Info shows THEME FILE and METER FILE. The
  chosen theme and mode are not remembered across a restart yet.
- **Faster covers.** A pre-scaled cover file (`tau-art/cover_128.pal256.timg`, written by `tools/sync_media.py --art-variants`)
  shows an album's cover in about 90 ms instead of decoding the embedded JPEG (2.6 to 15.8 s). Albums without one still use
  the embedded cover. The sync tool now finds covers in MP3 and FLAC tags and, failing that, any image in the album folder.
- **MP3 decoding on hardware.** The synthesis-window stage of the MP3 decoder (the largest single cost) now runs in the
  FPGA, bit-exact with the software version and falling back to it if the unit ever disagrees. Result: much more headroom;
  playback stays clean at speeds that used to stutter, and the visualisers get more of the CPU.
- **Meters.** Winamp Bars and Winamp Scope with a Configure page (Settings > Appearance > METER: CONFIGURE: presets, band
  count, easing, peak caps), a Chladni-pattern meter (with a fullscreen view: Select+Y, presets on Select+X), hardware level
  and spectrum measurement, and a waterfall and waveform that follow the theme. Bars resume after a menu closes (a stuck
  yield could freeze them). Meter settings are not remembered across a restart yet.
- **Menus.** Full-screen menus with a persistent action bar; text on them now sits directly on the page instead of on grey
  boxes. Start closes any menu from any depth.
- **Under the hood.** Hardware-assisted drawing (blit engine, rounded rectangles, palette blits), tear-free meter drawing
  (waits for the display beam), more free on-chip memory (49.7 KB free in the release build; the 64 KB RAM shrink is built
  and fit-proven but not in this release, since the firmware does not fit it yet). New
  developer tooling is documented in the repository; the Diagnostic Build gained Meter Sweep, Info export by QR code and
  more Check tests.
- **Known limits.** `Track changes` still fails in the Diagnostic Build's Check (a test issue, playback is unaffected). The
  release and diagnostic builds may restore the last track differently after a restart. Speeds above 1.20x are Diagnostic
  Build only for now.

## v0.4.0 — 22 September 2026

- **Media library.** Point the sync tool at your music (`tools/sync_media.py --library`) and Select opens a
  browsable library instead of a plain playlist: Artists, Albums, Tracks and Shuffle All, playlists carried over
  from your `.m3u` files, and history that resumes what you were playing (loaded, not started) after a restart.
  A library is entirely optional; without one the player works exactly as before (now called Legacy Playlist Mode,
  explained under Settings > How it works).
- **More free memory.** Cold, less-time-critical code (the settings menus, the library browser, the playlist
  overlay, the loading screen, and cover-art handling) now runs from the Pocket's PSRAM instead of sitting
  permanently in on-chip memory, the same way the album-art buffer already did. This roughly quadruples the
  player's free memory with no change to playback; a bitstream without PSRAM instruction support falls back
  cleanly (no library, no menus, single-file playback still works).
- **Left and Right are consistent everywhere.** In every menu, list and the playlist, Right now opens or selects
  (like A) and Left goes back (like B); on a switch or the Volume row they still change the value. Previously
  Left/Right in the settings menus and the playlist paged through the list, which on a short list looked like it
  jumped to the start or end; that paging moved to the shoulder buttons (L/R) in the playlist.
- **Menus scroll continuously** on a held Up/Down, matching the playlist and library lists (previously one tap
  moved one row).
- **Fixed:** Left/Right in the settings menus (including Volume) briefly seeked the currently playing track
  instead of only changing the setting. Also fixed: album art could go blank after changing tracks within an
  album, and a track change while browsing a library could report "no playlist" instead of skipping.
- **Playlist list restyled** to match the settings and library rows (taller, centred, the same selection style)
  instead of the older, denser list look.
- A separate Diagnostic Build (`alfatreze.TAU_DIAGNOSTIC`) ships as before, with the library, the developer test
  menus, and a new one-button **Check** (Settings > Diagnostics > Check): a self-test with a plain-language
  result, a QR code carrying the full report, and longer soak profiles for testers. It is not in the normal
  release.

## v0.3.0 — 21 September 2026

- **PSRAM support.** The bitstream now drives the Pocket's PSRAM chips as a second memory area
  (32 MiB, checked on every start). The album-art working buffer (about 11 KiB) moved there,
  which leaves about 11 KiB more free memory in the player. Validated with a 1,000-pass
  window soak (1.05 billion checks, zero failures) and a 30-minute stress soak on real playback
  with no late underruns.
- **New platform artwork** for the core in the Pocket's menu.
- **Speed list, meter previews and the settings menu** from the 0.2 releases are unchanged.
- **A separate Diagnostic Build** (`alfatreze.TAU_DIAGNOSTIC`, its own zip) is released with every version:
  the same player and FPGA design with the Tests, Stress and All Speeds menus switched on. The README now
  explains what the diagnostics are for and how to send results.
- Known limits: file and folder names with accented characters cannot be opened by the player
  (use plain ASCII names; the sync tool in `tools/sync_media.py` converts them). Album art that is
  very large or lightly compressed takes several seconds to appear (a 455 px cover of 255 KB took
  about 5 s), so re-encode covers to about 100 KB or less for a fast start.
- If PSRAM is not detected at start, album art is switched off and everything else keeps working.

## v0.2.2 — 21 September 2026

- **Greyscale meter previews**, so they sit well on every accent colour, and stored more compactly
  (about 1 KiB less memory than the colour versions).

## v0.2.1 — 21 September 2026

- **Meter previews in the meter list** — each of the eleven meters shows its artwork next to
  the name (Settings > Appearance > Meter).
- **Speed is a list** in Settings > Playback: 0.85x, 0.95x, 1.00x, 1.10x and 1.20x. (1.25x and
  above stutter on some material at this clock speed, so they are not offered.)
- Fixed: opening the settings while the playlist was open drew the playing track over the top
  row.

## v0.2.0 — 21 September 2026

- **Settings menu** — tap **Start**. Appearance (colour, meter, album art, screen blank),
  Audio (equalizer, volume) and Playback (repeat, shuffle, resume, speed). Colour, meter,
  equalizer, repeat and screen blank open a list: move with **Up**/**Down**, **A** selects,
  **B** goes back. **Start** no longer stops playback; **B** still returns to the start of the track.
- **Full-screen playlist and settings**, with a position counter and a hint line.
- **1.2x moved into Settings > Playback.** A long press of **A** no longer changes speed.
- **Diagnostics > Info** in Settings: firmware, FPGA revision, memory, playlist, track, underruns,
  draw stall and load timings at a glance.
- **The playlist now lives in the Pocket's SDRAM**, freeing 13 KiB of on-chip memory (the room
  the settings menu needed). Validated with a 32-minute soak (279 million checks), a whole-window
  address-line and CRC test, and playback under heavy memory load, all with zero failures.
- New bitstream: SDRAM CPU window enabled (the SDRAM read-return bug is fixed) and no diagnostic
  overlay on screen.

*Everything below this line is inherited upstream history from HarpMudd MP3 Player (v1.x). Tau's own numbering restarts at v0.2.0 above, so a lower Tau number is newer than a higher upstream number.*

## v1.4.0 — 21 August 2026

- **Playlist browser** — tap **Select**. **Up**/**Down** moves,
  **Left**/**Right** pages, **Y** jumps to the playing track, **A** plays.
- New meter: a **16-band spectrum**, bass to treble. Eleven meters now.
- **A volume icon** beside the repeat and shuffle indicators.
- **MPEG-2 files play** — the low sample rates common in spoken word.
- **Playlists can have long names again.** List them in a `playlists.m3u`; only
  twelve characters were remembered before, so a long name never came back.
- **FLAC tracks load faster** — measuring the length no longer blocks the load,
  and opening one steps over the embedded cover art instead of reading it.
- **The meters have more headroom**, and the spectrum reads in decibels, so
  loud albums no longer flatten against the top.
- **1.2x needs a longer hold on A**, and shows a marker while it's on.
- **A cover that can't be shown says so** — **PROG. JPEG** or **COVER ERROR**.
- Fixed: a light **stutter two seconds into every FLAC**.
- Fixed: **volume had no effect on FLAC**.
- Fixed: **seeking a FLAC left the clock permanently wrong**. Since 1.3.0.
- Fixed: **a FLAC from a playlist barely seeked**, and showed a wrong bitrate.
- Fixed: **resume started at the beginning**, or on the wrong track past 128
  entries.
- Fixed: **the encoding line often stayed blank for FLAC**.
- Fixed: **elapsed time ran at double speed on MPEG-2 files**.
- Small tidying: the full codec name shows (**FLAC 16-bit** was appearing as
  *FLAC 16-*), rounded corners on the selected playlist row, softened ends on
  the progress bar, and the playlist limits are written down in the README.

## v1.3.0 — 15 August 2026

- **FLAC playback** — up to 48 kHz, 16 or 24-bit, with tags, album art, meters
  and seeking. Covers CD rips and most libraries.
- A file the core can't play now says **why**, naming its own format, instead of
  failing generically.
- **Playlists work in subfolders**, so an `Artist/Album` library needs no
  rearranging.
- Playlists can hold **256 tracks**, up from 128.
- Meters are more accurate — about half the loudness peaks never used to reach
  the display.
- Long titles scroll in more cases; some were clipped instead.
- Fixed: a playlist or track pick that didn't register should now be rarer.
- Fixed: the loading `...` animation was invisible about half the time.
- Fixed: the first press of **A** on a freshly loaded track said STOPPED
  instead of PAUSED.
- Fixed: faint flicker on the mirrored-bars and peak-dots meters.

## v1.2.0 — 13 August 2026

- Resume now works with **any** playlist, not just `playlist.m3u` — it
  remembers which list you were in. Switch it on in Core Settings.
- New meter: a **magic eye**, a pair of EM84 tubes that light the panel. Press
  **X** to reach it.
- Files with no ID3 tag show their **filename** instead of a placeholder.
- Switching playlists shows a loading indicator and pauses while it works, so a
  slow load no longer looks like a failed one — and a pick the core misses
  recovers on its own within a few seconds instead of needing a retry.
- Fixed: long titles painted through the info panel's border.
- Fixed: the meters sat on a flat panel instead of the background gradient.
- Fixed: the startup screen flickered.

## v1.1.0 — 11 August 2026

- **Resume where you left off** in a playlist — the track and your position in
  it. Switch it on in Core Settings.
- **1.2× playback speed** for spoken word. Hold **A**.
- Screen blanking moved to **Select + Down**.
- The album art panel and the screen-blank timeout are no longer remembered
  between launches — their saved slots went to resume. Everything else carries
  over from v1.0.0.
- Fixed: seeking landed in the wrong place on files with no Xing header, and
  their total time was wrong too.
- Fixed: Load MP3 could lose your pick to a playlist reload.

## v1.0.0 — 10 August 2026

First public release. Plays your own MP3s off the SD card, with album art, ID3
tags, nine meters, an eight-preset equalizer and playlists.
