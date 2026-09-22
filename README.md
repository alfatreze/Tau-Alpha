# TAUᵅ — Analogue Pocket Music Player

A music player for the Analogue Pocket. It plays MP3 and FLAC straight off the
SD card, with album art, tags and meters.

Decoding runs in software, on a RISC-V CPU built into the Pocket's FPGA.

Current version **v0.4.0**.

Tau is a derivative of
**[HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player)**
v1.4.0 by HarpMudd. The project intentionally retains the complete upstream Git
history, copyright notice, and inherited release history in
[CHANGELOG.md](CHANGELOG.md). Tau's new identity, packaging, artwork pipeline,
technical plans, and subsequent changes are maintained by alfatreze. See
[NOTICE.md](NOTICE.md) for provenance and third-party licensing boundaries, and
[PROJECT.md](PROJECT.md) for the current milestone ledger and plan.

## Companion app

**[Tau Omega](../Tau%20Omega/)** is the desktop companion (macOS and
Windows) for building and syncing the Tau media library onto the Pocket's SD
card, and for managing Tau (and other openFPGA) cores and their media. It is a
separate project and repository, built as the product version of
`tools/tau_library.py` and `tools/sync_media.py`.

## Installing

Copy the `Cores`, `Platforms` and `Assets` folders onto the root of your
Pocket's SD card, merging with what's already there. Then drop your `.mp3` and
`.flac` files into:

```text
/Assets/tau/common/
```

They can live in subfolders under that path — an `Artist/Album` layout works
without rearranging. `tau.rom` is the firmware and has to stay in that
folder — the core won't start without it.

## Playing

At launch the core loads **`playlist.m3u`** — that name specifically, not any
playlist it finds. Choose a different one with **Load Playlist** and it becomes
the one that loads from then on, so you only have to pick it once.

With no `playlist.m3u` and nothing remembered you get a getting-started screen;
press **Analogue** and choose **Load MP3** or **Load Playlist**. The same menu
switches either at any time, and whatever you pick starts playing.

The controls:

| Pocket | Action |
|---|---|
| **A** | *Tap* — play / pause |
| **Start** | Open the settings menu (colour, meter, equalizer, repeat, speed and more) |
| **Left** / **Right** | *Tap* — previous / next track |
| **Left** / **Right** | *Hold* — seek, faster the longer you hold |
| **Select** + **Left** / **Right** | Seek one second |
| **Up** / **Down** | Volume, in 5% steps |
| **B** | Restart the current track from the beginning |
| **X** | Cycle the meter (ten styles) |
| **Y** | Cycle the EQ preset (eight) |
| **Select** | *Tap* — playlist browser; *Hold* — show / hide the album art panel |
| **L** / **R** | Cycle the accent color (12 shades) |
| **Select** + **L** | Repeat: off → all → one |
| **Select** + **R** | Shuffle on / off |
| **Select** + **Down** | Screen blank: off → 1 → 5 → 10 → 30 min |

Track changes and seeking work while paused or stopped. Changing track takes a
moment — the file has to be opened, its tag read and its artwork decoded;
restarting the current one is instant.

Volume, accent color, repeat, shuffle, the meter and the EQ preset carry over
between sessions, in step with **Core Settings**. **Where you were in a
playlist can be remembered too** — switch on **Resume playback** in Core
Settings. It holds one place, for the last playlist you used, and **Load MP3**
records no position at all — so for an audiobook, use a playlist; a one-line
`.m3u` is enough.

The album art panel and screen-blank timeout reset each launch. Everything
saved lives in `/Settings/alfatreze.TAU/` — delete that folder to reset.
Nothing is written to your music folder.

## What it shows

<img src="docs/screenshot.png" width="280" align="right" alt="Player screen: Feel Good Inc. by Gorillaz, track 6 of Demon Days 2005, encoded 128 kbps 44.1 kHz by LAME3.90, above a bar meter with the album cover at the right; below, a PLAYING label with repeat, shuffle and volume indicators and the EQ preset ROCK, track 5 of 14, 02:31 of 03:41, and a progress bar">

- **Title and artist** from the file's tag. One with no readable tag shows its
  filename, which is usually the song name anyway.
- **Album art** from the tag's embedded image — baseline JPEG. Tracks without a
  cover show no panel; a cover that can't be decoded shows the panel with the
  reason in it.
- **Eleven meters**, cycled with **X**: bars, waterfall, L/R levels, phase
  scope, oscilloscope, twin analogue VU needles, scrolling waveform, mirrored
  bars, peak dots, a magic eye and a 16-band spectrum analyser.
- **Elapsed and total time**, with a progress bar.
- **Repeat and shuffle indicators**, dimmed rather than hidden when off, the
  **EQ preset name**, and the position in the playlist.
- **Bitrate and sample rate**, with the encoder that made the file where it
  says so — `128 kbps - 44.1 kHz - LAME3.100`.

Tapping **Select** opens the [media library](#media-library) if you have one, or the
[playlist browser](#playlists) otherwise, until you close it.

CBR and VBR **MPEG-1 and MPEG-2** Layer III at every standard bitrate and sample
rate, mono or stereo, plus FLAC — see [below](#flac). MPEG-2 covers the lower
sample rates common in spoken-word recordings.<br clear="right">

## Media library

Point the sync tool at your music and it builds a browsable library on the card:

```bash
python3 tools/sync_media.py /path/to/your/music --all-tau --library
```

That copies the audio (converting nothing, tags untouched), embeds folder covers into files that
don't have their own, writes ASCII-only names and playlists (some characters have no glyph on the
Pocket's font), and builds the index (`tau-library.tdb`) the player reads at startup. Re-run it any
time your music changes; it only touches what's different.

With a library present, **Select** opens it instead of the plain playlist browser:

| Pocket | Action |
|---|---|
| **Up** / **Down** | Move the cursor — hold to run through a long list |
| **L** / **R** (shoulder buttons) | Jump to the next/previous letter |
| **Right** or **A** | Open a folder, or play a track |
| **Left**, **B** or **Select** | Go back a level, or close |
| **X** | Play everything under the cursor (an artist, an album, or a playlist) |

**Artists**, **Albums**, **Tracks** (every track, A–Z) and **Shuffle All** are the four top-level
views; playlists carried over from your own `.m3u` files sit alongside them. History is kept: after
a restart the library reopens what you were last playing — loaded, not started, so nothing plays
without you pressing anything.

Turning the library off (Settings > Library) makes the player behave exactly as it did before one
existed: the core menu's **Load MP3** / **Load Playlist** pick a file or list directly, and Select
opens the plain playlist browser below. This is **Legacy Playlist Mode**; a small note explains it
the first time you see it, and Settings > How it works has the fuller version. No library is ever
required — everything below applies whether or not you build one.

## Playlists

A plain text file with one track per line, saved as `playlist.m3u` in
`/Assets/tau/common/`:

```text
Feel Good Inc.mp3
Rhinestone Eyes.mp3
Demon Days/01 Intro.mp3
```

Names are relative to the folder the playlist is in, so a playlist can sit
beside its tracks in an album folder or in `common/` naming tracks below it.
Either works, so an `Artist/Album` library needs no rearranging. Lines starting
with `#` are ignored, so exported playlists work as-is.

Any other filename is picked with **Load Playlist**, and becomes the one that
loads at launch from then on — provided its name is short enough to be
remembered.

### The playlist browser

<img src="docs/playlist_browser.png" width="280" align="right" alt="Playlist browser: a PLAYLIST 12 of 14 header above nine filename rows, with Gorillaz - Feel Good Inc. highlighted mid-list and marked by a cursor; the transport row, times and progress bar stay visible underneath">

**Tap Select** to browse the playlist on screen. It opens on the track that's
playing, so you always start from where you are. The transport row, the times
and the progress bar stay put underneath, so nothing about what's playing is
hidden while you look.

| Pocket | Action |
|---|---|
| **Up** / **Down** | Move the cursor — hold to run through a long list |
| **L** / **R** (shoulder buttons) | Page up / down a screenful at a time |
| **Y** | Jump back to the track that's playing |
| **A** or **Right** | Play whatever's under the cursor |
| **Select**, **B** or **Left** | Close without changing anything |

In every menu and list, **Right** goes forward (opens or selects, like **A**) and **Left** goes back (like **B**);
on a switch or the volume, Left and Right change the value instead.

Rows show filenames rather than tags — a tag lives inside its file, so naming
every row would mean opening all 256 of them. With shuffle on, the list is the
play queue, so scrolling down shows what's actually coming rather than the file
order.<br clear="right">

### Limits

| | Limit | What happens past it |
| --- | --- | --- |
| Tracks per playlist | 256 | Says how many were dropped |
| `.m3u` file size | 12 KB | Same — about 48 characters per line at 256 tracks |
| Remembered playlist name | any length, if listed in `playlists.m3u` — otherwise 12 characters | Falls back to `playlist.m3u` next launch |

### Remembering which playlist you were using

The core reopens the list you last used at the next launch. It has one
settings word to remember it in, which holds twelve characters — so on its
own, `Shenanigans.m3u` comes back and `Goose - Shenanigans Nite Club.m3u`
does not.

**List a playlist in `playlists.m3u` and the limit goes away.** It's a plain
list of the playlists on the card, and only the ones listed are remembered:

```text
Crash Test Dummies - God Shuffled His Feet.m3u
Goose - Shenanigans Nite Club.m3u
Live/Phish - Hampton 1997.m3u
```

Write it in any text editor and save it beside your playlists, in
`/Assets/tau/common/`. The core searches it by name at boot, so a
playlist can be called anything you like. Order doesn't matter and you can add
or remove lines freely — entries are matched by name, not by position.

Without the file nothing changes: names of twelve characters or fewer are still
remembered on their own, so an existing card keeps working exactly as it did.

Resume follows the same path. The core remembers the track and the second you
stopped on, but it finds them through the playlist it reopens — so if the
playlist can't be reopened, resume comes back at the start of `playlist.m3u`
instead. Resume covers the whole playlist, all 256 tracks.

Tracks advance automatically. **Repeat**: off stops at the end, *all* loops,
*one* repeats the current track. **Shuffle** plays in a random order and never
repeats a track until the rest have played; with **Repeat all**, each pass round
the list is freshly shuffled.

A misspelled or missing filename costs that one track — the core steps over it
and says how many it skipped.

## Equalizer

**Y** cycles eight presets. The current one is named in the mode row, dimmed
on `FLAT`.

| | |
|---|---|
| **FLAT** | true bypass — bit-identical to no EQ at all |
| **BASS** | low shelf lift, gentle upper-mid dip |
| **ROCK** | smile curve — lows and highs up, mids back |
| **POP** | presence lift around 2–4 kHz |
| **JAZZ** | warm lows, relaxed upper-mid |
| **CLASSICAL** | gentle warmth, honest mids, eased upper mids, air |
| **VOCAL** | mid forward, lows trimmed |
| **TREBLE** | high shelf lift |

Presets are loudness-matched, so switching changes the tone without changing how
loud the music seems.

## Playback speed

Choose it in **Settings > Playback > Speed**: 0.85×, 0.95×, 1.00×, 1.10× or
1.20×. It's meant for spoken word: pitch rises with the speed, so music sounds
wrong. Off every launch. 1.2× is about the limit — faster would mean decoding
more frames a second than the CPU can do.

## Screen blanking

**Select + Down** cycles the timeout: off, 1, 5, 10, 30 minutes. The screen
goes black after that long without a button press, and any button wakes it
without doing anything else — reaching for a sleeping player shouldn't pause
it. Playback carries on regardless. Resets to off each launch, and it blacks
the picture rather than powering down: a core can't reach the Pocket's
backlight, so it's for a dark room, not for battery.

## Diagnostics

Tau can show what it is doing inside, so that a problem can be described with
numbers instead of "it stutters". There are two levels.

### The Info page (in every build)

**Start** opens Settings; choose **Diagnostics > Info**. It is read-only and
updates once a second:

| Row | What it tells you |
|---|---|
| **Firmware** / **FPGA rev** | The player version and the version of the FPGA design under it. They must belong together. |
| **SDRAM window** | `OK` when the Pocket's SDRAM was found and passed its start-up check. |
| **Window read** | Cycles for one read of that memory. Normal is well under 400. |
| **Free RAM** | Spare on-chip memory. |
| **Playlist** / **List clipped** | Tracks loaded, and whether the list was cut at the limit. |
| **Track** | Format, bitrate and sample rate of what is playing. |
| **Underruns** | Times the audio ran out of data since start. A track change or a screenshot adds one; a count that climbs while music plays untouched is a problem. |
| **Draw stall** | Milliseconds the CPU waited on the screen drawing, since start. |
| **Load ms** | The last track load in four parts: header, size probe, album art, total. Tells you if a slow start is the cover. |

### The Diagnostic Build (a separate core, for testers)

A second core, **TAU Diagnostic Build**, is released beside the normal one
(`alfatreze.TAU_DIAGNOSTIC_<version>_<date>.zip`). It is the same player on the same
FPGA design, with the test menus switched on under **Settings > Diagnostics**.
It installs next to TAU and has its own settings; use TAU for everyday
listening, because the stress tests deliberately load the memory system.

| Menu | What it is for |
|---|---|
| **Tests > Window test** | Writes and reads back a pattern in the extra memory the player uses. Expect `PASS 89`. |
| **Tests > Read / Write cycles** | Timing of that memory as `fastest/average/slowest` cycles. Expect about 48/57/335 read and 31/38/350 write. |
| **Tests > Playlist check** | Confirms the playlist memory still reads back correctly. Expect `PASS 13`. |
| **Stress > Level** | Adds memory traffic (R1 light, R2 and R3 heavy) while music plays, to look for glitches. |
| **Stress > Soak** | Runs that traffic for 5 to 60 minutes and reports pass or fail. |
| **Stress > Status** | Live result: passes, failures, early and **late** underruns, slowest access. Late underruns must stay 0. Early ones follow track changes and are not a fault. |
| **ALL SPEEDS** | Adds speeds above 1.20× to the speed list, for experiments. Off at every start. |

**A run:** open the core, start music, open the test or stress page, note the
result, then **Quit** to the menu (some results are only saved when you quit).

### Check: one button that produces a report (Diagnostic Build only)

**Settings > Diagnostics > Check** (Diagnostic Build) runs a fixed set of checks in about 30 seconds while music keeps
playing: the SDRAM and PSRAM memory tests and their speed, the cold-code path, the library, 15 seconds of playback
counters and what the start-up found. The result page lists each check as PASS, FAIL or SKIPPED (playback is skipped if
nothing is playing) and gives a verdict. **B** stops a run, **Y** runs it again, **A** shows the report as a **QR code**.

Take a screenshot of the result page and of the QR page (**Menu + Start**). The QR code carries the whole report
(build, memory timings, load times, library, error codes) and is read exactly from the screenshot; the 36-character code
under the verdict is a short fallback. When you **Quit** the core, the Pocket also saves a four-number summary in
`Settings/<core>/Interact/_core/interact_persist.json`. The Diagnostic Build has four profiles, chosen on the first page with the d-pad (Up/Down picks the row, Left/Right changes it):
**USER CHECK** (about 30 s), **STANDARD** (adds 10 track changes, 20 cold-code runs and three 30-second stress levels, about
6 minutes), **FULL** (STANDARD plus a soak) and **ENDURANCE** (the soak alone, with a cold-code test every minute). The soak
length (5, 15, 30 or 60 minutes) and stress level (R1-R3) can be set; FULL starts at 5 minutes and ENDURANCE at 30.
While a long profile runs you can leave the screen alone; **B** stops it and switches the stress traffic off.

Send the screenshots (and that file if asked). For the report on a computer:
`python3 tools/decode_tau_suite.py --qr screenshot.png` (needs `opencv-python`), or `--interact persist.json`, or `--code`
with the short code.

### Sending results

Open an issue at <https://github.com/alfatreze/Tau-Alpha/issues> and include:

1. Which core and version (TAU or Diagnostic Build, and the version on the Info page), and your Pocket firmware version.
2. What you did and what you expected.
3. **Screenshots**: press **Menu + Start** on the Pocket. They are saved as PNG
   files on the SD card in `Memories/Screenshots/`. Take one of the Info page
   and one of the result page. (The screenshot buttons also reach the player, so
   a track may pause; tap **A** to carry on. That adds one to the underrun count.)
4. For a failed test, the row that failed and the numbers it showed, and how
   long the test had been running.
5. A short description of the music: format, bitrate, whether it has a cover and its size.

A `PASS` on the Info and Tests pages together with a screenshot is enough for a
good report; you do not need to send anything else from the card. With the Check
it is even simpler: screenshots of its result page and QR page are the report.

## FLAC

Drop `.flac` files in with everything else and they play the same way — tags,
album art, meters, seeking.

| | supported |
|---|---|
| Sample rate | up to **48 kHz** |
| Bit depth | 8, 16, 20 and 24-bit |
| Channels | mono and stereo |

That covers CD rips and most libraries. Hi-res — 88.2, 96, 176.4 and 192 kHz —
is out, along with 32-bit and multichannel. Anything the core can't play says
so on screen and names the file's own format, so you aren't left guessing.

The limit is the CPU, not a setting: a 24-bit 44.1 kHz track already uses about
80% of the time available, and the same music at 96 kHz needs nearly twice what
the chip can do. Converting a hi-res album to 44.1 kHz is still lossless, and
on headphones from a handheld it isn't a difference you're going to hear.

## How it works

There's no audio decoder chip in the Pocket, so the FPGA is loaded with a
RISC-V CPU and the decoders run on it as software, with the audio queue and the
equalizer built as hardware around it.

If that sounds interesting, the longer version — including the two Analogue
framework bugs that had to be found first — is in
[docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md).

## Known limitations

- **FLAC up to 48 kHz.** Hi-res files are turned away with the reason on
  screen; see [FLAC](#flac) for why, and what to convert them to.
- **Baseline JPEG album art only,** and a cover that can't be shown says so
  rather than silently going missing. A progressive JPEG shows **PROG. JPEG**
  in the art panel; anything else that won't decode — a PNG cover, a damaged
  image — shows **COVER ERROR**. Re-saving the cover as a baseline JPEG fixes
  it. A track with no embedded cover at all shows no panel, which is different
  and intended. See [ROADMAP.md](ROADMAP.md).
- **Playlists are capped at 256 tracks**, or 12 KB of `.m3u` text — whichever
  comes first, which allows about 48 characters per line. A playlist that runs
  past either says so instead of quietly playing fewer.
- **A playlist with a name longer than 12 characters needs a `playlists.m3u`
  entry to be remembered.** Without one it plays fine but won't be the list
  that loads next launch, and resume won't follow it. A `playlists.m3u` that
  exists but doesn't list that playlist has the same effect as none at all. See
  [Remembering which playlist you were using](#remembering-which-playlist-you-were-using).
- **File and folder names must be plain ASCII.** A name with an accented
  letter (`é`, `ä`) or another non-ASCII character may fail to open and the
  track is skipped as unreadable. `tools/sync_media.py` copies a library to the
  card with the names converted (`ä` becomes `a`, characters with no plain
  equivalent are removed) and rewrites the playlists to match.
- **Big covers are slow to appear.** Decoding takes about as long as the
  picture file is heavy, not as the picture is large: a 455 px cover of 255 KB
  took about 5 s, a 1.5 MB cover about 16 s. Re-save covers at around 100 KB
  (the screen shows them at 92 px) and they appear at once. The same cover is
  not decoded again for the rest of the album.
- **1.2× speed can distort in dense passages.** It needs up to 54.8 MHz of the
  60 available, so the decoder occasionally can't keep up. Normal speed is
  unaffected.

## Credits

This core stands on other people's work. Where code is included, the name comes
from that source file's own copyright header.

- **[Helix MP3 decoder](https://github.com/ultraembedded/libhelix-mp3)** —
  © 1995–2002 [RealNetworks, Inc.](https://www.realnetworks.com), released to
  the [Helix Community](https://helixcommunity.org) under the
  [RPSL 1.0](https://helixcommunity.org/content/rpsl). Included in full, under
  its own license, unmodified.
- **[VexRiscv](https://github.com/SpinalHDL/VexRiscv)** soft CPU — Charles Papon
  ([Dolu1990](https://github.com/Dolu1990)) (MIT).
- **[picojpeg](https://github.com/richgel999/picojpeg)** — Rich Geldreich
  ([richgel999](https://github.com/richgel999)), with changes from Chris
  Phoenix (public domain).
- **SDRAM controller and i2s audio bridge** — Adam Gastineau
  ([agg23](https://github.com/agg23)) (MIT).
- **[Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/)** — Robert
  Bristow-Johnson. The equalizer's shelf and peaking filter formulas are his;
  the coefficients here are generated from them.
- **[openFPGA framework](https://www.analogue.co/developer)** —
  [Analogue](https://www.analogue.co).
- **[Inter typeface](https://rsms.me/inter/)** — Rasmus Andersson
  ([rsms](https://github.com/rsms)) — SIL Open Font License 1.1, bundled at
  [`third_party/font/OFL.txt`](third_party/font/OFL.txt). The font ROM the core
  draws with is generated from it and is a derivative under the same license.
- **Original MP3 Player core, firmware, UI, integration, and Tau's v1.4.0
  baseline** — [HarpMudd](https://github.com/harpmudd), from
  [HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player).
- **Tau project direction, identity, artwork, and modifications after the
  v1.4.0 baseline** — [alfatreze](https://github.com/alfatreze).

Two more shaped the design without ending up in it. Both decided something, which
is why they are credited at all:

- **[minimp3](https://github.com/lieff/minimp3)** —
  [lieff](https://github.com/lieff) (CC0). Measured against Helix and rejected —
  over three times slower, because it is floating point and this CPU has no FPU.
  Its test vectors were the measurement input either way.
- **[PicoRV32](https://github.com/YosysHQ/picorv32)** — Claire Xenia Wolf
  ([clairexen](https://github.com/clairexen)) (ISC). The first CPU tried. It
  needed 114–351 MHz to decode in real time depending on configuration, which is
  what sent the design to VexRiscv.

## License

HarpMudd's original code and Tau's own code and modifications are
[MIT licensed](LICENSE), with both copyright notices preserved. MIT was retained
because it is the upstream project's license and keeps contributions and reuse
straightforward without attempting to relicense the original work.

Everything under `third_party/` keeps its own, and MIT here relicenses none of
it. Two carry real obligations:
[Helix](third_party/libhelix-mp3/docs/RPSL.txt) is RPSL 1.0, a per-file
source-disclosure license, so it is vendored in full and unmodified;
[Inter](third_party/font/OFL.txt) is SIL OFL 1.1, and the font ROM generated
from it is a derivative under the same terms. The rest are MIT, ISC or public
domain — see [Credits](#credits).

## Upstream support

Tau preserves HarpMudd's original support link as an acknowledgement of the
project it builds on:

💛 **[Support HarpMudd via PayPal](https://www.paypal.com/donate/?hosted_button_id=S22WV924XU2ME)**

Tau does not currently configure a project funding link.
