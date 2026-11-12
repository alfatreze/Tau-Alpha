# Persistence by theme: one word per area (LAYOUT 2 added 2026-11-12, P2.3: see the last section, which supersedes the meter words and the tuning record described below) (BUILT 2026-10-23 as B-825, host-proven; the new layout saves and restores on a Pocket (TAU DEV 182..184), the MIGRATION from the legacy layout has NOT yet run on a Pocket: every core id keeps its own settings file, so the DEV builds started from empty files; B-842)

## The limits (all measured, not guessed)
* Settings survive only through `interact.json` persist variables: the Pocket takes **16** (B-456, hardware-confirmed), each a `slider_u32` of **31 usable bits** (`0..2147483647`): about **496 bits** in all. 15 variables are declared (ids 10, 11, 12, 15, 16, 20..23, 27..32), one is free.
* No other channel works: a core-written save file never persisted on a Pocket (issue 019; the nonvolatile slots hung Quit and boot and destroyed libraries, `docs/A088_UPSTREAM_CHECKS.md`). A new persisted value is NOT an RTL change (the register file is 32 words wide); a refit is not needed.
* Today about 250 of the 496 bits mean anything: volume, colour, repeat, Halcyon preset, theme, meter choice each take a whole word for 2 to 7 bits; presets take 4 bits a meter (7 meters in two words); ONE tuning (124 bits, ids 20..23); the last played track 62 bits (ids 31, 32).

## The layout as built (by area; word = the register-file word an id points at, the ids stay)
| Word (id) | Area | Fields (bits) | Free |
|---|---|---|---|
| 0 (10) | **Playback and layout version** | layout version bits 30-27 (0 = legacy), which tuning slot was saved last bit 26; room for shuffle, stretch, speed | 26 bits |
| 1 (11) | **Appearance** | colour 5, theme 6, polarity 1, font 3 | about 16 (cover style, blank timeout, ...) |
| 2 (12) | **Audio** | volume 7, repeat 2, ReplayGain mode 2, Halcyon preset 5 | about 15 |
| 5 (15) | **Meters 1** | selected meter 6, presets of meters 0..5 (4 bits each) | bit 30 mark |
| 6 (16) | **Meters 2** | the preset position of meter 6, the preset ids of meters 0..2 (7 bits each, id + 1, 0 = none; B-968) | bit 30 mark |
| 15 (30) | **Meters 3** | the preset ids of meters 3..6 (7 bits each) | bit 30 mark |
| 19, 20 (31, 32) | **Last played** | unchanged (two 31-bit words, 14-bit track, 27-bit check) | |
| 8..11 (ids 20..23) and 16, 17, 18, 21 (ids 27, 28, 29, **33 new**) | **Tuning pool** | two slots of four words: two meters keep a full tuning each; the slot saved least recently is evicted | |

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

## As built: what differs from the proposal
* **Presets are 4 bits, not 3** (Layered Wave has more than 8 presets) for the POSITION in a pack's own list; the list shows at most seven meters (the generator asserts it), so the positions of meters 0..5 sit in word 5 and meter 6's in word 6. **B-968: the preset FILE a meter was set to is remembered by its id** (7 bits each, id + 1, 0 = none, ids 0..126), in the bits of words 6 and 15 that no field used (a card saved earlier reads 0 = none). The tuning (`cores/tune_core.h`) is anchored to the file's id + 1 while the meter runs on files, else to the position; its preset field grew from 4 to 7 bits and its check seed changed, so a tuning saved in the earlier layout is refused (reset once).
* **Tuning pool: two slots**, one tuning each, instead of a variable-length pool: a slot is the existing 124-bit record, so a tuned meter keeps up to the same number of parameters as before and two meters keep theirs. The record header carries a 5-bit meter number (was 3); a tuning saved before this build is NOT carried over (its header differs), presets and everything else are.
* **The Core Settings sliders are gone** for Colour, Volume, Repeat, Meter and Halcyon (owner's choice): the words are named "(internal) ..." in `interact.json` and are changed only inside the player.
* **Resumable migration:** each word a legacy word is converted INTO (1, 2, 5, 6, 15) carries bit 30 as the "already new" mark, and the words are written in a fixed order (`sf_write_order`) in which a legacy word is overwritten only after every new word built from it, word 0 last. A power loss after any write is resumed by the next boot (host-tested after every prefix). My first design ("migrate again from the legacy words") was wrong: the words already written destroy the legacy values; the test that simulated an interruption found it.
* **The wide-file probe** (`wide_probe`) used to leave a probe value in word 16 (the old theme word) and now restores words 16 and 0; a power loss inside the probe itself (microseconds a boot) is not covered.
* **No refit and no Omega change in this repo:** Tau Omega must mask the narrowed fields and read `persist_registry.json` (new meanings since v0.7.0; id 33 is new).

## Layout 2 (external meters, P2.3, 2026-11-12): HOST-ONLY, not yet on a Pocket (the hardware migration test is P2.7)
Owner decision D-M23: per-meter settings stay in the 16 persist words (no settings file, no data slot); what is remembered is the selected meter and its preset, and two tunings. Spec: `docs/features/meters/EXTERNAL_METERS_SPEC.md` section 5. Reference: `tools/persist_layout.py` (Python, Omega mirrors it); firmware: `fw2/cores/settings_core.h`, `fw2/cores/tune_core.h`; the ids of the seven shipped meters: `fw2/cores/shipped_meter_ids.h` (**placeholders until P2.0**). Every persist id keeps its word (id = word + 10); the 16-variable limit (KB-079) is unchanged: ids 10, 11, 12, 15, 16, 20..23, 27, 28, 29, 30, 31, 32, 33 as before.

| Word (id) | Layout 2 | Bits used | Free |
|---|---|---|---|
| 0 (10) | layout version **2** (bits 30..27), which tuning slot was saved last (bit 26) | 5 | bits 25..0 |
| 1 (11) | appearance: colour 5, theme 6, polarity 1, font 3; bit 30 = converted mark (layout 0 -> 2 only) | 15 + mark | bits 29..15 |
| 2 (12) | audio: volume 7, repeat 2, ReplayGain 2, Halcyon preset 5; bit 30 = converted mark | 16 + mark | bits 29..16 |
| 5 (15) | **the selected meter's `id_hash`: the low 31 bits** (bit 31 of the 32-bit hash is not stored; 0 = none = Winamp Bars) | 31 | none |
| 6 (16) | **its preset**: position in the pack's own list bits 3..0, preset file id + 1 bits 10..4 (0 = none); bits 16..11 **scratch** (the meter index a migration came from; ignored by the player); bit 29 = converted mark | 11 + 6 scratch + mark | bits 28..17, 30 |
| 15 (30) | **free** (layout 1 held the preset ids of meters 3..6; a migration empties it) | 0 | 31 bits |
| 8..11 (20..23), 16, 17, 18, 21 (27, 28, 29, 33) | the tuning pool, two slots of four words (below) | 248 | |
| 19, 20 (31, 32) | last played track, unchanged | 62 | |

**The tuning record** (`tune_core.h`; check 8 | **key 20** | preset 7 | count 4 | entries from **bit 39**): `key` = the low 16 bits of the meter's `id_hash` followed by the low 4 bits of its `params_rev` (`tune_key`), so a tuning follows its meter, and a tuning whose meter was removed or whose parameter set changed (`params_rev` bumped: an edit in the pack's TOC entry) is not applied. `preset` is the anchor as before (preset file id + 1, else the position). The check seed changed (0x5A -> 0x3C), so a layout-1 record is refused by the layout-2 reader and vice versa. **Capacity: 85 bits of entries per slot** (layout 1: 100) because the header grew by 15 bits (5-bit index -> 20-bit key): a parameter costs 6 + its value width, so e.g. six 8-bit parameters fit; entries that do not fit are dropped in index order, as before.

**Migration (one time, resumable; layout 0 -> 2 and layout 1 -> 2)** (`sf_migrate_words_t` / `sf_migrate_resume_t`, writes in `sf_write_order` = words 1, 2, **6, 5**, 15, 16, 17, 18, 21, 8..11, 19, 20, then word 0 (the version) last):
* *Layout 1:* word 5's meter index (0..62) -> the shipped meter's hash (an index that is not one of the seven selects nothing, 0); word 6 = the **selected** meter's preset position and file id + 1 (the other meters' presets are dropped) + the old index as scratch + mark; word 15 emptied; each tuning slot: a record already in layout 2 is kept, a layout-1 record of a shipped meter is **re-keyed** with its preset anchor and entries (those that no longer fit the 15 bits shorter header are dropped), anything else becomes empty. Whether a kept tuning's **preset still exists** is decided when it is applied (its anchor must equal the selected preset's), not in the migration. Re-keying needs the parameter maxima of the shipped meters (entry widths): `settings_init_t(vals, tune_max_of)` takes them; without them (`settings_init`, which P2.3 left unchanged for P2.4) every layout-1 tuning is dropped.
* *Layout 0:* every field is read as before and packed into layout 2; the old single tuning is dropped as always.
* *Why word 6 before word 5, and the scratch:* word 5 holds 31 bits of hash and cannot carry a mark, so the migration marks word 6 (bit 29, absent from every layout-0/1 word 6) and keeps the old index in it: a power cut between the two writes is resumed by rebuilding word 5 from that index. Word 15 is emptied after word 6 has used it.
* *The one loss:* a power cut INSIDE a tuning slot's four writes leaves a record that mixes the two layouts; it fails its check and that one tuning is lost (the owner's rule: at worst a meter's settings are lost). One torn record in 256 passes the 8-bit check; its entries are validated against the meter when applied and its key must match a meter. Cuts at every other point lose nothing (host-tested after every prefix of the writes, both layouts).
* *Compatibility until P2.4:* `SF_METER`, `SF_MPRE0..6`, `SF_MPID0..6` stay in the enum as fields **without storage** (always 0, never written) so `fw2/ui/appearance.c` compiles; the selection and presets therefore do NOT persist in a build that has P2.3 but not P2.4. They cost 12 bytes of `settings.c` RAM more than layout 1 (the image end moved by 16 B; the floor assert still passes); P2.4 deletes them (net 48 bytes smaller).
* *Not decided here:* the fresh-card defaults are in `dist/Cores/alfatreze.TAU/interact.json` (word 0 = 2 << 27, word 5 = 0, word 6 = 1 << 29, word 15 = 0: the packed defaults, checked by `sim/test_p2_settings.py`); `tools/persist_registry.json` lists ids 10, 15, 16, 20..23, 27..30, 33 as re-meant `since` the pending tag v0.8.0 (set it to the release that ships layout 2).
