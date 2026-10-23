# Persistence by theme: one word per area (PROPOSAL, 2026-10-23, owner chose the direction; layout details to confirm)

## The limits (all measured, not guessed)
* Settings survive only through `interact.json` persist variables: the Pocket takes **16** (B-456, hardware-confirmed), each a `slider_u32` of **31 usable bits** (`0..2147483647`): about **496 bits** in all. 15 variables are declared (ids 10, 11, 12, 15, 16, 20..23, 27..32), one is free.
* No other channel works: a core-written save file never persisted on a Pocket (issue 019; the nonvolatile slots hung Quit and boot and destroyed libraries, `docs/A088_UPSTREAM_CHECKS.md`). A new persisted value is NOT an RTL change (the register file is 32 words wide); a refit is not needed.
* Today about 250 of the 496 bits mean anything: volume, colour, repeat, Halcyon preset, theme, meter choice each take a whole word for 2 to 7 bits; presets take 4 bits a meter (7 meters in two words); ONE tuning (124 bits, ids 20..23); the last played track 62 bits (ids 31, 32).

## Proposed layout (by area; word = the register-file word an id points at, the ids stay)
| Word (id) | Area | Fields (bits) | Free |
|---|---|---|---|
| 0 (10) | **Playback and layout version** | layout version 4 (0 = legacy), shuffle 1, time-stretch mode 2, speed index 4, queue/library flags 8 | about 12 |
| 1 (11) | **Appearance** | colour 5, theme 6, polarity 1, font 3 | about 16 (cover style, blank timeout, ...) |
| 2 (12) | **Audio** | volume 7, repeat 2, ReplayGain mode 2, Halcyon preset 5 | about 15 |
| 5 (15) | **Meters 1** | selected meter 6, presets of meters 0..7 (3 bits each) | 1 |
| 6 (16) | **Meters 2** | presets of meters 8..17 | 1 |
| 15 (30) | **Meters 3** | presets of meters 18..27 | 1 |
| 19, 20 (31, 32) | **Last played** | unchanged (two 31-bit words, 14-bit track, 27-bit check) | |
| 8..11, 16, 17, 18 (20..23, 27, 28, 29) + one new id | **Tuning pool** | 8 words = 248 bits: tunings of several meters, most recently saved first, the oldest evicted | |

* **Meters:** 3 bits a preset holds 8 presets a meter (Layered Wave has 8; a METR file may bring up to 8): 28 meters, and a fourth meters word would give 38. The meter choice is 6 bits (63 meters).
* **Tunings:** the saved tuning stays a delta from its preset (`cores/tune_core.h`): `check 8 | meter 6 | preset 3 | count 4 | count x {index 6, value n bits}`. A pool of 248 bits holds two full 124-bit tunings or about five typical ones (2 to 5 changed parameters); the oldest is evicted.

## Migration (a saved card must not lose its settings)
* Word 0 carries the **layout version** (new layout = 1, legacy = 0, a word that was a volume 0..100 reads as legacy).
* First boot on the new layout: version 0 -> read the legacy words (volume w0, colour w1, repeat w2, meter w5, Halcyon w6, theme w16, mode/ReplayGain w17, presets w18 and w15, tuning w8..11, last played w19/20), convert, then write every new word, **word 0 last** (a power loss in between leaves version 0, so the next boot migrates again from the still-valid legacy values).
* The tuning moves from words 8..11 to the pool; the legacy check byte (`tune_check`) validates the old record once.
* `tools/persist_registry.json` records each id's meaning by version (the mechanism already used for id 27); Tau Omega reads it. **Omega must mask the narrowed fields** (volume is no longer a whole word); Omega repo untouched, schema change documented here.

## Pure core and tests (host-first)
`cores/settings_core.h` keeps one table for every persisted field (word, shift, width, min, max, default); the new table replaces the old one and the migration is a second pure function. `sim/test_p2_settings.py` gets: every field round-trips through the words; a legacy image of every shipped layout (0.6.1 DEV 178..181 words) migrates to the same settings; a power loss between the writes migrates again; out-of-range values fall back to the default; the pool evicts the oldest and never reads a half-written tuning (check byte); mutants (a shift off by one, word 0 written first, eviction of the newest).

## Decisions for the owner
1. The area split in the table (is volume with Audio, shuffle and stretch with Playback?).
2. How many tunings the pool keeps (two full ones, or about five small ones) and the eviction rule (oldest saved, or least used).
3. Meter capacity: 28 now, a fourth words gives 38.
