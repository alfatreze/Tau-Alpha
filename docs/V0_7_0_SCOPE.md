# v0.7.0 scope (owner, 2026-10-26) and the proposed plan

Supersedes the "recommended cut" in `docs/V0_7_0_REVIEW.md` (the owner widened the scope). Research behind it: a read-only pass over the tree on 2026-10-26 (state of each item below is from the code, not from the old headers). **The plan is Claude's proposal; the order stays the owner's.**

## 1. The owner's scope
In: issue 025 fix; recheck issue 020; Unicode names and more language support (CJK / UTF-8 fonts as scoped before); Halcyon presets; Talos 2 reimplementation; FLAC decoder v2 follow-ups; writable font memory; meter module M6; public preset gallery; blit ideas B13/B19/B10; Chladni presets and per-channel waveform presets; more than seven meters and pack signing; gapless acceptance, sidecars and the art sidecar spec; report codes (TPG default, Reed-Solomon, extra context). **Plus a rethink of Info vs Diagnostics, with the Diagnostic Build coming back** (section 4).

## 2. What the research found: much of the list is already built
| Item | Real state | What is left |
|---|---|---|
| Latin-1 text | display built and on hardware; index text is Latin-1 | file and folder NAMES are still ASCII-folded (`sync_media.py`, `tau_library.py`); BUG-001's SD-card test matrix (NFC/NFD/Latin-1/UTF-8, APF 0192 byte-transparency) was never run |
| Extended-A / CJK | not started (CJK parked in FONT_SPEC s11) | needs RTL (font memory capacity or a PSRAM glyph client); Extended-A alone costs about 16 M10K, **free M10K must be re-checked against the `stats-b821` fit report before promising it** |
| Writable font memory | **built, fitted (`fontram-b810`), on hardware, in v0.6.0** (FONT_SPEC header is stale) | polish: pair kerning, Omega `FONT` section, icons still drawn by the old routines, size tuning |
| Halcyon presets | engine, page, 8 built-in presets, PRST loader (user presets from `tau-assets.bin`) built; built-ins on hardware | a Pocket run with a real PRST file; Omega as PRST writer; decision D-S05 (save edited presets, MY SOUND slot: persist cap) |
| FLAC v2 follow-ups | LPC unit wired, Info rows in, old decoder retired, FLAC on hardware (the review's "Open" list is stale) | independent review of the transport glue, hardware A/B of LPC unit vs software, `-O2` option |
| Talos 2 | P1 shipped (T2-00). **P3 (the rewrite) was declined 2026-09-29**; P0 (firmware split above 127 rows) and P2 (idle/fence register) open | RTL; reference-renderer diff for every opcode; revival is the owner's call (done by listing it here) |
| Blits B13/B19/B10 | analysed, none built; B13 cost bigger than scoped, B19 hits the timing-critical write network, B10 value re-assessed down | RTL, ride on Talos 2 |
| M6 | meaningless in Player 2 (every meter but Winamp Bars is a pack); it now means "retire the old player" | decide |
| Preset gallery (D-M12) | parked; nothing on the device needs it | hosting and moderation, no firmware |
| Chladni / waveform presets | Chladni has four presets; Winamp Scope five; further Chladni templates need engine features; per-channel waveform presets not designed | data plus lab twin per preset |
| More than seven meters | 7 slots (static + 6 packs), persistence already holds 20 | METER list scrolling, an 8th pack slot, larger stats array |
| Pack signing | D-M20 chose CRC + cycle limits + strikes, signing assessed and not chosen | a new decision, key custody; a hardware watchdog (RTL) is the real safety gap |
| Gapless / sidecars | ROADMAP says gapless closed (FLAC tone, DEV 167); real-music MP3 (LAME trim) acceptance not recorded; Player 2 reads only `tau-art/cover_128.pal256.timg`, art sizes spec is design only | loopback acceptance, multi-size set and lookup, Omega counterpart |
| TPG | pixel grid is already the DEFAULT (D-R04 was reversed 2026-10-06) and in Player 2 | context items (settings snapshot, feature bits, last errors) firmware only; Reed-Solomon not built (D-R02: none in v1); wall-clock and device ID need RTL |

## 3. Fits: what needs a bitstream
No fit: Latin-1 names, Halcyon, FLAC follow-ups, M6, gallery, presets, more than seven meters, signing alone, gapless acceptance, sidecar sizes, TPG items, font polish, issue 025.
**Needs RTL:** Extended-A / CJK; Talos 2 P2..P4; B13/B19/B10; pack watchdog; wall-clock / device ID; the two cosmetic items waiting (I2S JITTER sum width, `eq_in_l` rename).
Proposal: **one bundled "v0.7 RTL" fit** (cosmetics + wall-clock + watchdog + Talos P2, then the blit opcodes if Talos P2 lands), a **separate glyph-source fit for Extended-A / CJK** (XL, a new bus client in the hottest loop), and **Talos P3 only on the owner's explicit revival** (it changes a hardware-proven engine). Each fit is about 1 h 40 per seed, two seeds for any timing claim. Firmware work does not wait for them.

## 4. Info vs Diagnostics: the rethink
**Today (Player 2):** one core; "Player 2 includes the diagnostics"; there is no Diagnostic Build and no flag in `fw2/` to make one (`fw2/build.sh`, `gen_diag_features.py` and the rule-5 register apply to `fw/` only). The Info page is a table of about 212 rows (decode, seek, FLAC, settings, library, stream, capability bits, rings, packs, meter maxima...) that a normal user cannot read; Settings > Diagnostics (CHECK, CYMO RESAMPLER, 16-BIT I2S SLOT, ACCEPT ALL RATES, REPORT) is in every build. Not yet back: Decode Sweep, Meter Sweep/Trace, Blit Test, stress and soak.

**Proposal.**
1. **Audience tag on the one counter registry** (`counters_table[]`): every row gets `USER` or `DIAG`. The normal build compiles only USER rows (it also saves RAM and cold code); the Diagnostic Build compiles all. One registry, two views, so Omega's decoder and the pixel-grid Report keep one format.
2. **Normal build, "About" instead of Info** (about 10 plain rows): exact version (splash, Info, core list must agree, standing owner rule), card and library (tracks, library state), now playing format (codec, rate, bit depth), ReplayGain applied, EQ preset, storage and settings status in words ("settings saved", "carried over from an older version"), and a one-line support hint. No counters, no hex.
3. **Normal Settings:** Appearance, Audio, Playback, Controls, Help, About. No Diagnostics group.
4. **Support Report stays in the normal build, small:** Select+Start still makes a pixel-grid report, but with a fixed minimal set (version, settings snapshot, library facts, last errors, a few health bits), so a user can still attach a screenshot to a bug report; the full counter grid is Diagnostic only.
5. **Diagnostic Build = `TAU Diagnostics`** (names already exist in `tau_layout.py`; same bitstream and firmware source, a build-time flag `P2_DIAG`): the full counter Info page, Settings > Diagnostics with every toggle, CHECK, the full Report; then the ported suites in order of value: stress/soak, Decode Sweep, Meter Sweep/Trace, Blit Test. Every diagnostic-only piece is a **registered feature** (rule 5), so `fw2` needs its own register (or the existing one extended to `fw2`): this is a decision to record in `docs/DECISIONS.md`.
6. **Heap floor (rule 6):** measure both builds on the shipped 192 KB link; if a build falls under the floor, drop features, never lower the floor.
7. **Release:** two zips again (normal + Diagnostic Build), as the owner's standing rule (memory: release = two zips); `make_release.py --player2` already knows the Stable names.
Open design questions for the owner: is "About" one page or two (user facts / health); does the normal build keep CHECK (a 20-second self-test is useful to users) or is it Diagnostic only; which rows count as USER (my first list is above).

## 5. Proposed order
**A. Now, firmware and data only (no fit; can run in parallel sessions):** (1) issue 025 + recheck 020; (2) the Info/Diagnostics split (design review with the owner, then the registry tag, About page, `P2_DIAG`, two packages); (3) Halcyon: Pocket run with a PRST file, Omega's writer, D-S05 decision; (4) BUG-001 names: run the card test matrix first, decide byte-transparent vs NFC vs ASCII, then `sync_media`, firmware path and Omega in step; (5) gapless loopback acceptance + sidecar sizes; (6) TPG context items; (7) FLAC glue review + A/B; (8) preset work (Chladni, per-channel waveform), more than seven meters, font polish.
**B. The v0.7 RTL fit** (cosmetics, wall-clock, watchdog, Talos P2), launched as early as its list is frozen, because it is the longest pole (about 3 h 30 for two seeds); blits only if Talos P2 lands.
**Update 2026-10-26 (owner):** Talos 2 is revived with the whole graphics path in scope (D-T01..D-T03): its T2+T3+T4 join the v0.7 RTL bundle in B; T5/T6 are v0.7.x or later unless the owner holds the release. **C. Large and risky, owner to confirm:** Extended-A / CJK (glyph source fit), Talos P3, the blit opcodes, the gallery (hosting), Reed-Solomon, pack signing.
**Gate:** the release Pocket run, Omega 0.6.0, the owner's approval (section 2 of `docs/V0_7_0_REVIEW.md`).

## 6. Honest risk
This scope is a few releases of work. Items in A are shippable one by one; B and C are not. **Recommendation: define v0.7.0 as A + B, and let C items ride v0.7.x / v0.8 unless the owner wants to hold the release for them.** If a C item is a promise for 0.7, say which and I will schedule its fit first.
