# Tau-Alpha ↔ Tau Omega: how the two projects share documentation

Tau-Alpha (this repo) is the Pocket core: the firmware, the RTL, and the on-disk formats the Pocket
actually reads and writes. Tau Omega is a separate, independent project (a companion desktop app)
that writes files onto the same SD card and reads files back off it — a sync tool and, now, a
consumer of Tau-Alpha's own report/config formats. They are built by different sessions, on
different schedules, and neither should ever have to guess what the other currently does.

This file is the rule, not a status report. For the current state of any specific interface, read
the spec it points at — this file only says how the two projects are meant to relate to each other
and stay that way without drifting into silent disagreement.

## 1. Tau-Alpha is the source of truth. Always.

Every on-disk format the Pocket reads or writes — `data.json`/`interact.json` layouts, data slot
contents (`tau-library.tdb`, `tau-cold.bin`, the still-planned `tau-meters.cfg`), the Check/QR report
format (`fw/suite_core.h`), persisted-settings encoding (`fw/settings.inc`) — is defined here, by
what the firmware actually does, not by what either project's documentation *says* it does when
those two disagree. Tau Omega's own specs are downstream, derived documents: useful for that
project's own implementation, never authoritative over this one.

**Consequence:** if a Tau Omega document and a Tau-Alpha document disagree about a wire format,
Tau-Alpha's actual firmware source (not even Tau-Alpha's own docs, if those have drifted) is what's
real. This has already happened twice in the other direction — see section 4.

## 2. Never share literal files. Reference, then verify against the real artifact.

Neither project copies the other's source files, generated binaries, or fixtures into its own tree.
A Tau Omega session that needs to know a wire format reads the relevant Tau-Alpha spec (this repo's
`docs/*.md`, `tools/*.py`, `fw/*.h`) and, critically, **checks it against a real captured artifact**
(a real `interact_persist.json` from a card, a real `tau-library.tdb`, a real QR-decoded report) —
never against a fixture written from memory of what the format "should" look like. Tau Omega's own
`docs/FIRMWARE_SYNC.md` states the lesson plainly, twice, because it was learned the hard way both
times (section 4): a test fixture that invents the shape instead of copying it from a real artifact
will happily agree with buggy code forever, because both are wrong in the same way.

The one thing that *does* cross freely is real captured data for testing — screenshots, decoded QR
text, `interact_persist.json` files pulled from an actual card — copied once into whichever project's
`testdata/` needs it, with its provenance recorded (which card, which core, which session), never
regenerated from a guess.

## 3. How a change gets communicated

Tau-Alpha does not push notifications to Tau Omega — there is no mechanism for that, and building
one would be its own project. Instead:

- Every Tau-Alpha spec this file points at (section 5) carries a plain "as of \<date\>, tau-alpha
  \<version/commit\>" marker, so a reader can tell at a glance whether it might be stale relative to
  what they're looking at.
- A change to any interface surface (a new data slot, a new persist id, a new QR report tag, a
  renamed/reordered enum) gets logged in `docs/AUDIT_TRAIL.md` as it happens — that log is already
  the durable record of *why* something changed, and is the first place a Tau Omega session should
  search when its own sync check finds something unexpected.
- Tau Omega owns the actual re-check: `docs/FIRMWARE_SYNC.md` in that repo is where it records what
  it last verified, against which Tau-Alpha version, and what conflicts are still open. **Re-running
  that check is Tau Omega's responsibility, on its own schedule** (its own file says: after every
  Tau-Alpha release, and when a named feature — e.g. the blit engine — lands). Tau-Alpha does not
  maintain a mirror of that checklist; duplicating it here would just create a second copy to keep
  in sync, which is the exact problem this file exists to avoid.

## 4. Precedent: what this has already caught

Two real bugs were found this way, not hypothetically (full detail in Tau Omega's own
`docs/FIRMWARE_SYNC.md`):

- **`data.json`'s real shape is `{"data": {"data_slots": [...]}}`**, not a flat `data` array. Tau
  Omega's fixture had invented the flat shape; every real Tau core (which nests it correctly) was
  reported as "legacy", gating the whole media-library feature behind a check that could never pass
  on real hardware.
- **`interact_persist.json` nests under `interact_persist`**, not at the JSON root. Same failure
  mode: a hand-written fixture agreed with hand-written code, and the real Settings screen silently
  returned nothing from every real card.

Both were found only once real captured artifacts replaced invented fixtures — the concrete reason
section 2's rule exists.

## 5. Current interface surfaces (pointers, not copies)

| Surface | Tau-Alpha's authoritative source | Notes |
|---|---|---|
| Data slots (which id is which file) | `tools/tau_data_slots.py`, `docs/features/MEDIA_LIBRARY_0.4_SPEC.md` §2-4 | Assigned as shipped in v0.5.0: 5 library index, 6 cold image (`tau-cold.bin`), 7 cover image (opened by name, `TAU_ART_TIMG`), 8 `tau-assets.bin`. The old "slot 6 double-booked" note (media-library spec §3) is resolved: art moved to slot 7 |
| Persisted settings (`interact_persist.json`) | `fw/settings.inc` (the `SW_*` enum and `set_wr`/`set_rd` mapping), `fw/player.c` | **2026-10-04 (B-599):** the `SW_POL` word now carries two fields: bit 0 = theme polarity (Dark 0 / Light 1), bits 4-5 = ReplayGain mode (0 Off, 1 Track, 2 Album); an older file has 0 in bits 4-5 = Off, no new persist id. The register behind this is a hardwired 4-bit index, already fully used with the library on — relevant to any future persisted meter setting, see `docs/features/meters/METER_CONFIG_SPEC.md` §4 |
| Check/QR diagnostics report | `fw/suite_core.h` (the `SR_T_*` tag enum and each tag's own layout comment), `tools/decode_tau_suite.py` | Diagnostic-Build-only by standing decision; a release-core card has no Check report |
| Cover/thumbnail files (`TIM1`, `*.timg` in `<album>/tau-art/`) | `tools/tau_image.py` (container layout in its docstring), `docs/features/IMAGE_FORMATS.md` | Firmware reader shipped in v0.5.0 (path `tau-art/cover_128.pal256.timg`, palette 256 at 128 px) and hardware-confirmed for MP3 and FLAC albums; container still not formally frozen (D-I05) |
| Visualizer/meter identity and config | `docs/features/meters/METER_CONFIG_SPEC.md` (new, this pass) | The `VIZ_*` enum is append-only — a saved index must never be reinterpreted after a firmware update |
| Meter list order and retired meters (2026-09-26) | `fw/player.c` `viz_order[]` and the `VIZ_RETIRED_*` slots, `docs/features/meters/METER_CONFIG_SPEC.md` "Update 2026-09-26" | List order is NOT enum order any more; retired numbers 2, 7, 9, 11 mean "no meter" (7 is also the saved form of Bars in its mirrored layout); Chladni is 14; the Meter slider max in `interact.json` is 14. Persisted `Meter` value = the enum number |
| **`tau-assets.bin` (`TAUA` container: `THEM` themes and `METR` per-meter presets), data slot 8 (built 2026-09-26)** | `docs/features/THEME_FILE_FORMAT.md`, `tools/tau_assets.py` (reference writer/reader), `fw/assets_core.h` (firmware reader), `themes/*.json` (theme source shape) | Format is **not frozen** until a card has read a real file on hardware. Role numbers follow the `TR_*` enum in `fw/theme.h` (append-only). Omega's writer should be checked against a file captured from a card, not a fixture invented from the doc. `METR` needs `tools/meters_schema.json` (generated registry, ids append-only) to know parameter order and widths |
| **Meter registry `tools/meters_schema.json`, `SR_T_METERCFG` (tag 20), `SR_T_METERTRACE` (tag 21), meter preview lab** (built 2026-09-26, M1-M4) | `tools/gen_meters.py` (writes the schema, `docs/METER_REGISTRY.md`, `docs/METER_CAPABILITIES.md`), `tools/meters/preview/build.py` (one-file lab), `tools/decode_tau_suite.py --trace` | Omega consumes `meters_schema.json` from a tagged release and the lab as a versioned vendor drop (record its SHA-256); slot 8 is assigned |
| **Chladni meter behaviour reference** (lab built 2026-09-25, firmware module B-276 onward) | `docs/features/meters/CHLADNI_METER_SPEC.md` (algorithm, tiling/symmetry, template design), `tools/lab/chladni_lab.html` (standalone Q14-kernel preview, not generated from `meters_schema.json`), `fw/chladni_core.h` (the portable fixed-point core the firmware module shares), `sim/test_chladni_core.py` (golden values) | Chladni is the one registered meter with no JS twin in the generic `tools/meters/preview/` lab yet (METER_MODULE_SPEC's M5 note) — this standalone lab is still the authoritative behaviour reference for its field kernel, tiling and parity rules. If Omega ever previews or edits `chladni` presets (`meters/chladni/meter.json` has 10 tunable parameters + 4 presets), check against this lab and the golden test, not a reimplementation guessed from the spec prose alone |
| **TPG pixel grid report (barcode study, 2026-10-04; layout TPG2 since the first hardware test)**: an alternative to the QR code for every report page | `tools/tpg.py` (format in its docstring; reference encoder/decoder), `tools/decode_tau_suite.py --grid`, `fw/tpg.h` (the firmware's pixel source), `docs/features/BARCODE_STUDY.md` | The report's binary record is written straight into the screenshot's RGB565 pixels: **robust is the default** (mode R: 4x4 cells, 2 bits per channel, up to 6,059 B, survives JPEG q80+ and box/Lanczos/nearest/bicubic resizing, no error correction), **lossless** (mode L, lossless PNG only, up to 259,184 B) when the report is bigger. TPG2 puts the stream in a **centred square block** whose side comes from a fixed ladder (`LADDER_L`/`LADDER_R` in `tools/tpg.py`): try each step, read the first pixels (mode L: big-endian 16-bit values) or the first 22 cells (mode R) and keep the one that reads the `TPG2` magic. TPG1 (stream from pixel (0, 0)) is the older layout, still decoded, and is what the three captures in Omega's testdata use. Omega needs only a PNG reader and CRC32; the payload is the same `TD` record `decode_tau_suite.py` parses. Pixel fidelity was hardware-confirmed with four test patterns (100% exact). **Firmware status: behind `TAU_TPG=1` (Diagnostic Build, Check result page, X cycles QR / grid L / grid R); not yet run on a Pocket with a real report, and the format is not frozen until it has been.** Do not write Omega's decoder against the docstring alone: capture a real Check grid screenshot from a card first and verify against it |
| **`SR_T_INFOTEXT` (tag 26) and `SR_T_NOWPLAYING` (tag 27)** (2026-10-04, grid builds; tag 25 is `SR_T_LOAD2`) | `fw/suite_core.h` (layout comments), `tools/decode_tau_suite.py` (`infotext`: list of `row`/`label`/`value`; `nowplaying`: `state`, `queue_pos`, `queue_len`, `title`, `artist`, `album`; `--table` prints the Info rows) | Repeatable tag 26: one Info page row as shown on screen (row index, label, 0, value). Tag 27 once per report. Older decoders skip both as unknown tags. **The six TPG2 captures in Omega's testdata (`20261004_1947*`-`1949*`) were made BEFORE the merge with the `SR_T_LOAD2` work and carry these two entries as tags 25 and 26**; their container (TPG2) is valid, but an Omega decoder should treat their entry tags as legacy and re-capture with a build from main to get tags 26/27. Omega's `taud` module should add both and re-check against a real grid capture (Settings > Diagnostics > Info > A, press X for the grid) |
| **`SR_T_HEAP` (tag 23)**, peak heap in the Check report (2026-10-02, B-514) | `fw/suite_core.h` (layout comment), `tools/decode_tau_suite.py` (`heap`: `peak_bytes`, `heap_size`, `free_bytes`) | New tag, two u32 (peak heap bytes since boot, heap region size), present on every Check run like `SR_T_STACK` (tag 16). Older decoders skip it as an unknown tag; Omega's `taud` module should add tag 23 (value width 4 bytes) to its tag and width tables and re-check against a real capture when one exists |
| **Data slot 3 (legacy Playlist, `playlist.m3u`) removed from `data.json`** (2026-10-02, B-517) | `dist/Cores/alfatreze.TAU/data.json`, `fw/playlist.inc` (`PL_TEMPLATE_SLOT_ID` = 5) | The slot only existed as the 0192 open-by-name descriptor template; that role moved to slot 5 (the library index). A card no longer needs a root `playlist.m3u`, and a package check that expects slot 3 should drop it (Omega's `card.rs` test fixture still lists it with `parameters 0x2`; harmless, library detection reads slot 5). Hardware-unverified until a Pocket run |

Add a row here whenever a new interface surface is created — this table, not either project's
memory of the conversation that created it, is what a future session should find first.

| **Halcyon `PRST` section of `tau-assets.bin`** (2026-10-07, B-626) | `tools/halcyon_assets.py`, `docs/features/HALCYON_DATA_FORMAT.md` (format, limits, the APO/AutoEQ import rules), `tools/tau_assets.py` parses it | Tau Omega's exporter must reproduce the bytes (control presets and raw Q2.22 biquad presets, peak-safe preamp, stability checks) and re-check the file with its own reader; the firmware loader does not exist yet. **Settings > Diagnostics > Info now has 36 rows** (new `HW GAIN`, B-625): the Info export header reads `36 ROWS`; the decoder must take the row count from the record, not assume 35. |

## Change log (Tau-Alpha side; Tau Omega re-checks on its own schedule)
- 2026-10-07 (branch `ram-diet`): the firmware no longer draws report codes as QR. The pixel grid (TPG, `fw/tpg.h`, decoder `tools/tpg.py`, `tools/decode_tau_suite.py --grid`) is the only view; the record bytes (`SR_T_*`, `TAUD1`) are unchanged. Tau Omega's `taud` module decodes QR only (`rqrr`), so it cannot read reports from builds on this branch until it ports the grid decoder. QR captures from older cores stay decodable (the host tools keep `--qr`). The QR encoder is archived in `archive/qr_encoder/` (tag `archive/qr-encoder`).
