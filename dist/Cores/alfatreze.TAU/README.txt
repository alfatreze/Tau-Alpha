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
  Meter and EQ     tau-assets.bin in Assets/tau/common/ (extra meters,
  packs            Halcyon EQ presets; made by Tau Omega)
  ReplayGain       read from the tags in your files; Tau Omega can write them


TWO WAYS TO DO IT
-----------------
TAU OMEGA (free, Mac / Windows / Linux), the full toolbox
  https://github.com/alfatreze/Tau-Omega/releases/latest
  - copies music to the card and builds the library, name sidecar and covers
  - converts any format to MP3 or FLAC for TAU
  - edits tags and covers, writes ReplayGain, builds playlists
  - installs and updates TAU and your meter and EQ presets
  - checks the whole card

POCKET SYNC / PUPDATE PLUGIN "TAU Library" (small, inside the updater)
  https://github.com/alfatreze/tau-alpha-pocket-sync
  - Check the card (read only): what is missing or out of date
  - Rebuild the library: index, name sidecar and covers for the music that
    is already on the card; shows the plan first, and offers to rename names
    the Pocket cannot open (it keeps a backup)
  - does NOT copy music, convert, edit tags, or install cores: use Tau Omega

By hand: python3 tools/sync_media.py MUSIC_DIR --all-tau --library
(tools in https://github.com/alfatreze/Tau-Alpha)


Updating TAU later never touches Assets/tau/common/ (your music, index,
covers and presets stay as they are).

Guide and controls: https://github.com/alfatreze/Tau-Alpha
