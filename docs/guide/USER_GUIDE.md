# Tau user guide

Everything you need to install and use the Tau music player (v0.6.0-alpha.1). For the project overview see the
[README](../../README.md); for problems and test results see [Diagnostics](DIAGNOSTICS.md); for preparing your music
see [Media and tools](MEDIA_AND_TOOLS.md).

Contents: [Installing](#installing) · [Playing](#playing) · [What it shows](#what-it-shows) ·
[Media library](#media-library) · [Themes and colours](#themes-and-colours) ·
[Meters](#meters) · [Equalizer](#equalizer) · [Playback speed](#playback-speed) · [Screen blanking](#screen-blanking) ·
[FLAC](#flac) · [Known limitations](#known-limitations)

## Installing

Copy the `Cores`, `Platforms` and `Assets` folders onto the root of your Pocket's SD card, merging with what's
already there. Then drop your `.mp3` and `.flac` files into:

```text
/Assets/tau/common/
```

They can live in subfolders under that path; an `Artist/Album` layout works without rearranging. `tau.rom` is the
firmware and has to stay in that folder; the core won't start without it. Installing is a merge: nothing in
your music folder is ever written to by the core.

A second core, **TAU Diagnostic Build**, is released beside the normal one for testers. Use TAU for everyday listening
(see [Diagnostics](DIAGNOSTICS.md)).

## Playing

Tau needs a [media library](#media-library) -- it is the only way to browse and play your music. At launch, with a
library built, it reopens what you were last playing (loaded, not started). With no library yet, you get a
getting-started screen telling you to run the sync tool; a file picked from the Pocket's own **Load MP3** menu still
plays once, but does not browse or queue anything.

The controls (checked against the firmware input handling in `fw/player.c`):

| Pocket | Action |
|---|---|
| **A** | *Tap*: play / pause |
| **Start** | Open the settings menu (colour, theme, meter, equalizer, repeat, speed and more). In any menu or the library, Start closes it from any depth |
| **Left** / **Right** | *Tap*: previous / next track (library queue) |
| **Left** / **Right** | *Hold*: seek, faster the longer you hold (5 s, then 10 s, then 30 s per step) |
| **Select** + **Left** / **Right** | Seek one second |
| **Up** / **Down** | Volume, in 5% steps |
| **B** | Restart the current track from the beginning |
| **X** | Cycle the meter (eleven styles) |
| **Y** | Cycle the EQ preset (eight) |
| **Select** | *Tap*: open the library |
| **Select** + **X** | Next preset of the current meter (Chladni presets; Bars switches between its normal and mirrored layout) |
| **Select** + **Y** | Fullscreen meter (Chladni and the Winamp meters only) |
| **L** / **R** | Cycle the accent colour (19 colours) |
| **Select** + **L** | Repeat: off, all, one |
| **Select** + **Down** | Screen blank: off, 1, 5, 10, 30 min |

Track changes and seeking work while paused or stopped. Changing track takes a moment: the file has to be opened, its
tag read and its artwork loaded; restarting the current one is instant.

In every menu and list, **Right** goes forward (opens or selects, like **A**) and **Left** goes back (like **B**); on a
switch or the volume, Left and Right change the value instead.

Volume, accent colour, repeat, the meter and the EQ preset carry over between sessions, in step with **Core
Settings** in the Analogue menu. The **theme and mode** (Dark / Light) and the **meter Configure settings** are *not*
remembered yet: they return to their defaults each launch.

The album art panel and screen-blank timeout reset each launch. Everything saved lives in `/Settings/alfatreze.TAU/`;
delete that folder to reset. Nothing is written to your music folder.

## What it shows

<img src="../screenshot.png" width="280" align="right" alt="Player screen: Feel Good Inc. by Gorillaz, track 6 of Demon Days 2005, encoded 128 kbps 44.1 kHz by LAME3.90, above a bar meter with the album cover at the right; below, a PLAYING label with repeat and volume indicators and the EQ preset ROCK, track 5 of 14, 02:31 of 03:41, and a progress bar">

- **Title and artist** from the file's tag. One with no readable tag shows its filename, which is usually the song name anyway.
- **Album art** from the tag's embedded image (baseline JPEG), or from a pre-scaled cover file if you have made one
  (see [Media and tools](MEDIA_AND_TOOLS.md#cover-art-tim1)). Tracks without a cover show no panel; a cover that can't be
  decoded shows the panel with the reason in it.
- **A meter**, cycled with **X** (see [Meters](#meters)).
- **Elapsed and total time**, with a progress bar.
- **A repeat indicator**, dimmed rather than hidden when off, the **EQ preset name**, and the position in the library queue.
- **Bitrate and sample rate**, with the encoder that made the file where it says so, for example `128 kbps - 44.1 kHz - LAME3.100`.

CBR and VBR **MPEG-1 and MPEG-2** Layer III at every standard bitrate and sample rate, mono or stereo, plus FLAC (see
[below](#flac)). MPEG-2 covers the lower sample rates common in spoken-word recordings.<br clear="right">

## Media library

Point the sync tool at your music and it builds a browsable library on the card:

```bash
python3 tools/sync_media.py /path/to/your/music --all-tau --library
```

That copies the audio (converting nothing, tags untouched), embeds folder covers into files that don't have their own,
writes ASCII-only names and playlists (some characters have no glyph on the Pocket's font), and builds the index
(`tau-library.tdb`) the player reads at startup. Re-run it any time your music changes; it only touches what's
different. All options: [Media and tools](MEDIA_AND_TOOLS.md).

With the library open:

| Pocket | Action |
|---|---|
| **Up** / **Down** | Move the cursor; hold to run through a long list |
| **L** / **R** (shoulder buttons) | Jump to the next / previous letter |
| **Right** or **A** | Open a folder, or play a track |
| **Left**, **B** or **Select** | Go back a level, or close |
| **Start** | Close the library from any depth |
| **X** | Play everything under the cursor (an artist, an album, or a playlist) |

**Artists**, **Albums**, **Tracks** (every track, A-Z) and **Shuffle All** are the four top-level views; playlists carried
over from your own `.m3u` files sit alongside them as **Lists** -- a plain text file with one track per line, in the same
folder as its tracks or naming a path under it; lines starting with `#` are ignored, so exported playlists work as-is. The
sync tool picks these up automatically; a folder with no playlist of its own gets one generated for it. History is kept:
after a restart the library reopens what you were last playing, loaded, not started, so nothing plays without you pressing
anything.

Tracks advance automatically. **Repeat**: off stops at the end, *all* loops, *one* repeats the current track. **Shuffle
All** plays in a random order and never repeats a track until the rest have played; with **Repeat all**, each pass round
the list is freshly shuffled.

## Themes and colours

**Settings > Appearance** (Start opens Settings) has the rows COLOUR, THEME, MODE, METER, ALBUM ART and SCREEN BLANK.

- **THEME** picks one of the built-in themes, **TAU** or **OCEAN**, plus up to four more from a `tau-assets.bin` file if you
  have one (see [Media and tools](MEDIA_AND_TOOLS.md#the-theme-and-meter-file-tau-assetsbin)). Info shows THEME FILE.
- **MODE** switches **Dark** and **Light**. Text stays crisp in Light mode: the display hardware has a second text-weight
  table for it.
- **COLOUR** (also the **L** / **R** shoulder buttons on the player screen) is the accent: a **19-colour palette**: BLACK,
  WHITE (the default), GLOW, seven TRANS_ shades (CLEAR, SMOKE, RED, ORANGE, GREEN, BLUE, PURPLE) and eight CLASSIC_ shades
  (YELLOW, ORANGE, RED, PINK, BLUE, GREEN, INDIGO, SILVER), plus ALUMINUM. The names follow the Pocket's own colour editions.

The chosen theme, mode and accent colour are all remembered across a restart.

## Meters

Cycle with **X**, or choose one in Settings > Appearance > METER (the list order below is the firmware's order):

| Meter | What it shows |
|---|---|
| **Winamp Scope** | Waveform scope with smoothing and trail |
| **Winamp Bars** | 4 to 16 spectrum bars with easing and falling peak caps |
| **Chladni** | Chladni-pattern figures that respond to the music; fullscreen view with Select+Y, presets with Select+X |
| **Bars** | Classic level bars (up or mirrored layout) |
| **Waterfall** | Scrolling spectrogram |
| **Phase Scope** | Stereo phase (Lissajous) scope |
| **Oscilloscope** | Waveform |
| **VU** | Twin analogue-style VU needles |
| **Waveform** | Scrolling waveform |
| **Peak Dots** | Peak dots on the spectrum |
| **Spectrum** | 16-band spectrum analyser |

Sources: `meters/*/meter.json` and `fw/meter_gen_order.h`. The Magic Eye, L/R Levels, Mirrored Bars (now a layout of Bars)
and cassette meters no longer exist as separate choices.

**Configure.** Settings > Appearance > METER > CONFIGURE opens a page for the current meter's parameters: presets, band
count, easing, attack and release, peak cap and fall (Winamp Bars), scope smoothing and trail (Winamp Scope), and the
Chladni parameters. Use Up/Down to pick a row and Left/Right to change it; the live meter stays on screen above. These settings
are **session-only** (not remembered across a restart yet). In the Diagnostic Build the page can also show the configuration
as a QR code.

Meter drawing waits for the display beam, so it does not tear.

## Equalizer

**Y** cycles eight presets. The current one is named in the mode row, dimmed on `FLAT`.

| | |
|---|---|
| **FLAT** | true bypass; bit-identical to no EQ at all |
| **BASS** | low shelf lift, gentle upper-mid dip |
| **ROCK** | smile curve: lows and highs up, mids back |
| **POP** | presence lift around 2-4 kHz |
| **JAZZ** | warm lows, relaxed upper-mid |
| **CLASSICAL** | gentle warmth, honest mids, eased upper mids, air |
| **VOCAL** | mid forward, lows trimmed |
| **TREBLE** | high shelf lift |

Presets are loudness-matched, so switching changes the tone without changing how loud the music seems. Design notes:
[EQ_DESIGN.md](../EQ_DESIGN.md).

## Playback speed

Choose it in **Settings > Playback > Speed**: 0.85x, 0.95x, 1.00x, 1.10x or 1.20x. It's meant for spoken word: pitch rises
with the speed, so music sounds wrong. Off every launch. The Diagnostic Build can offer more (1.30x, 1.50x, 1.75x, 2.00x, 2.50x)
through Diagnostics > ALL SPEEDS, for experiments; since the MP3 synthesis window moved into hardware, MP3 playback was clean
at 1.75x in owner tests, but those speeds are not offered in the normal build yet.

## Screen blanking

**Select + Down** cycles the timeout: off, 1, 5, 10, 30 minutes. The screen goes black after that long without a button press,
and any button wakes it without doing anything else; reaching for a sleeping player shouldn't pause it. Playback carries on
regardless. Resets to off each launch, and it blacks the picture rather than powering down: a core can't reach the Pocket's
backlight, so it's for a dark room, not for battery.

## FLAC

Drop `.flac` files in with everything else and they play the same way: tags, album art, meters, seeking.

| | supported |
|---|---|
| Sample rate | up to **48 kHz** |
| Bit depth | 8, 16, 20 and 24-bit |
| Channels | mono and stereo |

That covers CD rips and most libraries. Hi-res (88.2, 96, 176.4 and 192 kHz) is out, along with 32-bit and multichannel.
Anything the core can't play says so on screen and names the file's own format, so you aren't left guessing.

The limit is the CPU, not a setting: a 24-bit 44.1 kHz track already uses about 80% of the time available, and the same music
at 96 kHz needs nearly twice what the chip can do. Converting a hi-res album to 44.1 kHz is still lossless, and on headphones
from a handheld it isn't a difference you're going to hear. (The hardware window unit accelerates MP3 only.) More detail:
[FLAC.md](../FLAC.md).

## Known limitations

- **FLAC up to 48 kHz.** Hi-res files are turned away with the reason on screen; see [FLAC](#flac).
- **Baseline JPEG album art only** for embedded covers, and a cover that can't be shown says so rather than silently going
  missing. A progressive JPEG shows **PROG. JPEG** in the art panel; anything else that won't decode (a PNG cover, a damaged
  image) shows **COVER ERROR**. Re-saving the cover as a baseline JPEG fixes it. A track with no embedded cover at all shows no
  panel, which is different and intended. A pre-scaled `.timg` cover sidesteps this (see [Media and tools](MEDIA_AND_TOOLS.md)).
- **File and folder names must be plain ASCII.** A name with an accented letter or another non-ASCII character may fail to open
  and the track is skipped as unreadable. `tools/sync_media.py` copies a library to the card with the names converted (`ä`
  becomes `a`, characters with no plain equivalent are removed) and rewrites the playlists to match.
- **Big embedded covers are slow to appear** when there is no pre-scaled cover file. Decoding takes about as long as the picture
  file is heavy: a 455 px cover of 255 KB took about 5 s, a 1.5 MB cover about 16 s. Make the `.timg` cover (about 90 ms), or
  re-save covers at around 100 KB. The same cover is not decoded again for the rest of the album.
- **Meter Configure settings are not yet remembered across a restart.** Theme, mode and the accent colour now are;
  the meter presets were left out of `interact.json`'s persisted list to stay under its 16-entry display cap.
- **Speeds above 1.20x are Diagnostic Build only** for now. The old note that 1.2x could distort in dense passages dates from
  before the hardware MP3 window unit; it is expected to be much better now but has not been re-measured as a release claim.
- **`Track changes` in the Diagnostic Build's Check** still fails; playback itself is unaffected. The release and diagnostic builds
  may restore the last track differently after a restart.
- See the [roadmap](../ROADMAP_PUBLIC.md) for what is planned.

## Next

[Diagnostics](DIAGNOSTICS.md) · [Media and tools](MEDIA_AND_TOOLS.md) · [How it works](../HOW_IT_WORKS.md)
