# BUG-002: long track titles are clipped on the now-playing screen (about 45 characters)

**Status:** Confirmed on hardware (owner report, B-033 run, 2026-09-21); not fixed. Found while testing phase 0 with the longest title on the test card.

## Observed
Track 3 of the phase 0 playlist (`04. Giant Warrior ~ Torumekian Army ~ Her Royal Highness Princess Kushana.mp3`, tag title 69 characters) showed on the now-playing screen as `... Her Royal H` and the marquee scrolled only that clipped text. The playlist screen showed the full name.

## Cause (source read, matches the observed cut)
The now-playing title is held in `track_title[48]` (`fw/player.c`, also the artist and album buffers and the copies `stale_ref_title[48]`, `last_title[48]`, `prev_try`). The ID3/FLAC readers copy at most `sizeof(track_title)` bytes, so anything past about 45 characters never reaches the screen. The marquee itself has room (`ui_marquee_t.text[64]`) and would scroll a longer title. The playlist reads the file name from its own text buffer, hence it was complete.

## Fix options
1. Raise the title (and artist, album) buffers to 64 to match the marquee: about +16 B each and +16 B for each copy (well under 200 B in total); heap gap is 18.7 KiB with art in PSRAM, 7.8 KiB with it in BRAM.
2. Above 64: raise `ui_marquee_t.text` too (80 or 96); cost a few tens of bytes. Long titles are rare, 64 covers the test card's worst case (69 needs 72).
Recommendation: title 80, artist 64, album 64, marquee text 80, done together with the phase 2 firmware (the library index stores titles up to 63 characters after the tool truncates them; the tool limit and the firmware limit should be the same number). Test in the host harness with a 100-character tag, then on the Pocket with the phase 0 track.

## Relation to the library
The library list rows draw titles from the index (own buffer, up to 63), so it is not affected, but now-playing after a library play is.
