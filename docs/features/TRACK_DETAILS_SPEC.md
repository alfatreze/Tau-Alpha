# Track details page (owner request 2026-10-14): design notes, nothing built

**Ask.** In the library/playlist track list, selecting a track (a dedicated key, not play) opens a subpage with everything we know about it: all metadata plus the technical facts. Trigger: three test tracks with the same title could not be told apart, and a file name or a technical line would have answered it.

**The one hard constraint [READ].** The player streams ONE file through ONE data slot (the host `0192` open, KB-043); reading another file's head means switching that slot and disturbs the track being played. So details of a track that is NOT playing can only come from the **library index** (`tau-library.tdb`, built on the host); details of the PLAYING track can come live from the decoder and tags.

**Options.**
1. *Index only (no firmware read):* show what the index already has (title, artist, album, track number, year, duration, path, format) and add the **file name** (the path's last part), file size and a "title shared with N tracks" hint. Cheap, works for every track, cannot show codec details.
2. *Extend the index (host tool):* `sync_media.py` / `tau_library.py` already parse every file; add a details section to the index (index version, with the loader ignoring unknown sections, B-137 forward-compat): sample rate, bit depth, channels, bitrate or VBR, codec/encoder string, LAME delay and padding, ReplayGain gain and peak (track/album), cover kind and size (embedded JPEG pixels, tau-art sidecar yes/no), FLAC MD5 present, file size. Every track then has full details with no slot switch; cost is index size (about 40 bytes per track: 139 tracks = 6 KB, 10,000 tracks = 400 KB, to be weighed against the PSRAM index budget).
3. *Live page for the playing track:* the existing Info rows (TRACK, COVER, REPLAYGAIN, LOAD MS, GAP) plus a per-track view of the decoder state (frame size, FLAC block size, VBR flag, ReplayGain in force, cap). No index change.

**Recommended:** 1 now (file name and path, the immediate need), 2 as the real feature (Tau Omega writes the same fields), 3 for the playing track. Trigger key: Y is free in the library list (B back, A play, X favourite, Start close).
**Decisions to take:** the key; whether index version 2 is acceptable; which technical fields matter most; whether the page is reachable from the now-playing screen too.
