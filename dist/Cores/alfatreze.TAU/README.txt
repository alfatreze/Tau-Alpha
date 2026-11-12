TAU for the Analogue Pocket: what goes on the card
===================================================

TAU plays MP3 and FLAC from the SD card. The core itself is installed;
the music is yours to add. This page says what is needed and what is optional.

WHAT THIS RELEASE INSTALLED (do not edit)
  Cores/alfatreze.TAU/            the core
  Assets/tau/alfatreze.TAU/       the firmware (tau.rom) and its images
  Platforms/tau.json + _images/   the entry in the Pocket menu


MANDATORY: your music and a library index
-----------------------------------------
1. Put .mp3 and .flac files in  Assets/tau/common/
   Folders such as Artist/Album are fine.
2. Build the library index  Assets/tau/common/tau-library.tdb
   Without it you cannot browse or queue more than one file.
   Rebuild it every time you add or remove music.

Limits: names must be plain ASCII on the card (the Pocket cannot open other
characters), and a full track path must stay under 200 bytes. Other formats
(m4a, ogg, opus, wav, aac, ape) are not played: convert them first.


OPTIONAL: nice to have
----------------------
  Album covers     <album>/tau-art/cover_128.pal256.timg  (shows in about
                   90 ms; without it TAU decodes the cover inside the track,
                   which can take seconds)
  Playlists        .m3u / .m3u8 files next to the music
  Name sidecar     tau-names.bin + tau-names.json in Assets/tau/common/
                   (keeps your original names and tags; made with the library)
  Themes and       tau-assets.bin in Assets/tau/common/ (up to four extra
  meter presets    colour themes, plus meter presets)
  ReplayGain       read from the tags in your files; Tau Omega can write them


WHO DOES WHAT
-------------
TAU OMEGA: free app for Mac / Windows / Linux
  https://github.com/alfatreze/Tau-Omega/releases/latest
POCKET SYNC / PUPDATE PLUGIN "TAU Library": small, inside the updater
  https://github.com/alfatreze/tau-alpha-pocket-sync

  - Copy music: puts your music on the card ............ Omega
  - Convert: other formats into MP3 or FLAC ............ Omega
  - Library index: browse and queue your music ......... Omega, plugin
  - Name sidecar: keeps your original names and tags ... Omega, plugin
  - Covers: fast cover files for every album ........... Omega, plugin
  - Rename: fixes names the Pocket cannot open ......... Omega, plugin
  - Card check: what is missing or out of date ......... Omega, plugin
  - ReplayGain: even loudness between tracks ........... Omega
  - Playlists: builds .m3u playlists ................... Omega
  - Themes and meter presets: makes tau-assets.bin ..... Omega

The plugin changes nothing until it has shown you the plan, and keeps a
backup of every file it renames.

By hand: python3 tools/sync_media.py MUSIC_DIR --all-tau --library
(tools in https://github.com/alfatreze/Tau-Alpha)


Updating TAU later never touches Assets/tau/common/ (your music, index,
covers and presets stay as they are).

Guide and controls: https://github.com/alfatreze/Tau-Alpha
