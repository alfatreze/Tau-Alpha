# About page (normal build) and the Info/Diagnostics split: rows to review

Status: **PROPOSAL for the owner's review, 2026-10-26** (decisions D-U01: the About page is as large as the useful user information needs; D-U02: the normal build has no Check). Nothing is built. Parent proposal: `docs/V0_7_0_SCOPE.md` section 4.

## Principles
1. **Words, not numbers.** Every row is something a person reads without a manual ("Settings: carried over from an older version"), never a counter or a hex value.
2. **Only what a user can act on or wants to know.** If it only helps debugging, it is in the Diagnostic Build.
3. **Grouped sections, scrolling like Info** (Up/Down), each section collapsed to a heading when the page opens? (question 5).
4. Rows come from a small user-facing table (`about_rows[]`) that formats existing fields into words; the raw counter table (`counters_table[]`) gets an audience tag (`USER`/`DIAG`) and the normal build compiles only what the About rows and the support Report need.

## Sections and rows (the source says where the value comes from today)
### TAU
| Row | Example | Source |
|---|---|---|
| Version | `0.7.0` | the full version stamped by the packagers (Info `FIRMWARE`) |
| Build | `0.7.0+a1b2c3d4` (Stable shows the plain version; Dev and Preview show the exact build) | same |
| Channel | Stable / Preview / Dev | core id, derived at build time |
| Hardware version | `4D50331B` (shown small: needed in a bug report) | `CORE VERSION` |
| Made by | `alfatreze, with Tau Omega` | fixed text |
| Website | `github.com/alfatreze/Tau-Alpha` | `core.json` `url` |
| Licence | `MIT; third-party parts keep their licences` | fixed text |

### Credits and licences (needed for compliance; one screen)
Helix MP3 decoder (RPSL 1.0), Tamzen and Space Grotesk fonts (their licences), HarpMudd (the original player and the SDRAM display path, MIT) until the inherited code is retired, Analogue (the Pocket platform). Source: `docs/ATTRIBUTIONS.md`, `NOTICE.md`.

### Library
| Row | Example | Source |
|---|---|---|
| Library | `143 tracks` / `No library: sync with Tau Omega` / `Library could not be read` | `LIB STATE`, `LIB TRACKS`, `LIB ERR` in words |
| Albums, artists | `12 albums, 9 artists` | new: the library header already holds them |
| Last scanned | `by Tau Omega, 2026-10-24` | new: index header date (if the index carries it) |

### Playing now (shown only while a track is loaded)
| Row | Example | Source |
|---|---|---|
| Format | `MP3 44.1 kHz stereo, 128 kbps` / `FLAC 24-bit 96 kHz` | `MP RATE`, `MP CH`, track details |
| ReplayGain | `Track gain -4.3 dB` / `Off` / `Album gain` | `MP RG MODE`, `MP RG FACTOR` in dB |
| Equaliser | `Halcyon: WARM` / `Off` | `HAL SEL`, the preset name |
| Speed | `1.00x` / `1.50x stretch (pitch kept)` | `MP SPEED IDX`, `MP TEMPO IDX` |
| Output | `48 kHz, resampled` / `48 kHz` | `MP RATE`, the resampler state |
| Volume | `73%` | `MP VOLUME` |

### Look and feel
Theme, light or dark, accent colour, font, meter (name), album art on or off, screen blank time. Source: the settings fields and names (`THEME`, `MODE`, `COLOUR`, `FONT`, `METER CUR` as a name).

### Content on the card
| Row | Example | Source |
|---|---|---|
| Themes | `2 custom themes loaded` | `THEME FILES` |
| EQ presets | `8 built in, 3 yours` | `HAL USER` |
| Meters | `7 of 7 loaded` / `5 of 7: 2 could not be loaded` | `PACKS SEEN`, `PACKS RUN` |
| Settings | `Saved` / `Carried over from an older version` / `Reset: the settings file could not be read` | `SET MIGRATED`, `SET STORES`, `SET DIRTY`, the load result |

### Health in words (what a user might want when something sounds wrong)
| Row | Example | Source |
|---|---|---|
| Audio | `No dropouts` / `3 dropouts since start` | `UNDER RING` + `DRY` + hardware events, summarised (see question 6) |
| Card reads | `No errors` / `2 read errors (retried)` | `ST FAILS`, `ST RETRIES`, `LP OPEN ERR` |
| Running for | `1 h 12 min` | `UPTIME` |

### Help and support
How to open Controls and Help; **"Select+Start on this page makes a support report (a picture of numbers) to send with a bug report"**; where to send it (the repository). The support Report holds a fixed minimal set: exact version and hardware version, library facts, the settings snapshot, the About rows' values, the last few errors, a few health bits: no counters.

## What moves to the Diagnostic Build (`TAU Diagnostics`, `P2_DIAG`)
The Check page, Settings > Diagnostics (the Check, the resampler and 16-bit toggles, accept all rates, the full Report), the raw Info counter table (about 280 rows: decode, seek, FLAC, library, stream, rings, governor, GFX, FNC, BLIT, packs, stats, meter maxima) and, later, the ported sweeps, stress and Blit Test suites. The Diagnostic Build also carries the About page.

## Mechanics
* One audience tag per row in `counters_table[]`; `P2_DIAG` selects the build; each diagnostic-only piece is a registered feature (rule 5: extend the register to `fw2`, record the decision in `docs/DECISIONS.md`).
* `about_rows[]`: a table of `{section, label, formatter, source}`; formatters are pure and host-tested against fixtures (each state of each row), like `nowplaying_core.h`.
* Heap floor measured on both builds on the shipped 192 KB link (rule 6); the normal build ends smaller than today because the counter names and Check code are not linked.
* Two zips again (normal and `TAU Diagnostics`), per the standing release rule.

## Questions for the owner
1. **Credits and licences on the About page** (my recommendation: yes, one screen, it is the honest place for them)?
2. **Health section** in the normal build: keep (three rows), or leave all health to the Diagnostic Build?
3. **Uptime** is a debugging aid; keep it as "Running for" or drop it?
4. **Library**: do you want albums and artists counts and the scan date (needs the library header to carry them; I believe it does for the counts, not for the date)?
5. **How the page opens:** all sections expanded in one scrolling list (like Info today), or section headings you open one at a time?
6. **Audio dropouts:** summarise `DRY` and hardware empty events into one honest number, or show nothing until they are attributed (the counts are currently unreliable: 1 to 108 events for one stall)? I lean to showing `DRY` (a real audible dropout) only.
7. **The separate Controls and Help pages stay as they are?**
