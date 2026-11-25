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
| Data slots (which id is which file) | `tools/tau_data_slots.py`, `docs/MEDIA_LIBRARY_0.4_SPEC.md` §2-4 | Assigned as shipped in v0.5.0: 5 library index, 6 cold image (`tau-cold.bin`), 7 cover image (opened by name, `TAU_ART_TIMG`), 8 `tau-assets.bin`. The old "slot 6 double-booked" note (media-library spec §3) is resolved: art moved to slot 7 |
| Persisted settings (`interact_persist.json`) | `fw/settings.inc` (the `SW_*` enum and `set_wr`/`set_rd` mapping), `fw/player.c` | The register behind this is a hardwired 4-bit index, already fully used with the library on — relevant to any future persisted meter setting, see `docs/METER_CONFIG_SPEC.md` §4 |
| Check/QR diagnostics report | `fw/suite_core.h` (the `SR_T_*` tag enum and each tag's own layout comment), `tools/decode_tau_suite.py` | Diagnostic-Build-only by standing decision; a release-core card has no Check report |
| Cover/thumbnail files (`TIM1`, `*.timg` in `<album>/tau-art/`) | `tools/tau_image.py` (container layout in its docstring), `docs/IMAGE_FORMATS.md` | Firmware reader shipped in v0.5.0 (path `tau-art/cover_128.pal256.timg`, palette 256 at 128 px) and hardware-confirmed for MP3 and FLAC albums; container still not formally frozen (D-I05) |
| Visualizer/meter identity and config | `docs/METER_CONFIG_SPEC.md` (new, this pass) | The `VIZ_*` enum is append-only — a saved index must never be reinterpreted after a firmware update |
| Meter list order and retired meters (2026-09-26) | `fw/player.c` `viz_order[]` and the `VIZ_RETIRED_*` slots, `docs/METER_CONFIG_SPEC.md` "Update 2026-09-26" | List order is NOT enum order any more; retired numbers 2, 7, 9, 11 mean "no meter" (7 is also the saved form of Bars in its mirrored layout); Chladni is 14; the Meter slider max in `interact.json` is 14. Persisted `Meter` value = the enum number |
| **`tau-assets.bin` (`TAUA` container: `THEM` themes and `METR` per-meter presets), data slot 8 (built 2026-09-26)** | `docs/THEME_FILE_FORMAT.md`, `tools/tau_assets.py` (reference writer/reader), `fw/assets_core.h` (firmware reader), `themes/*.json` (theme source shape) | Format is **not frozen** until a card has read a real file on hardware. Role numbers follow the `TR_*` enum in `fw/theme.h` (append-only). Omega's writer should be checked against a file captured from a card, not a fixture invented from the doc. `METR` needs `tools/meters_schema.json` (generated registry, ids append-only) to know parameter order and widths |
| **Meter registry `tools/meters_schema.json`, `SR_T_METERCFG` (tag 20), `SR_T_METERTRACE` (tag 21), meter preview lab** (built 2026-09-26, M1-M4) | `tools/gen_meters.py` (writes the schema, `docs/METER_REGISTRY.md`, `docs/METER_CAPABILITIES.md`), `tools/meters/preview/build.py` (one-file lab), `tools/decode_tau_suite.py --trace` | Omega consumes `meters_schema.json` from a tagged release and the lab as a versioned vendor drop (record its SHA-256); slot 8 is assigned |
| **Meter settings file `Saves/<platform>/<core>/tau-meter-settings.bin` (`TMSF` v1, data slot 11, v0.7.5)** | `fw2/cores/mset_file_core.h` (the format and the writer), `tools/meter_settings.py` (reference reader), golden vectors `docs/research/meter_settings/` (`expected.json`), `docs/features/meters/METER_SAVE_FILE_SPEC.md` section 4, decisions D-M25, D-M27, D-M30 | Written by the Pocket (no seed, none shipped): Omega may read it, **never overwrite, edit or delete it**, never leave a file of another size than 8,192 bytes (the Pocket stops with "Error in framework file id [11] size bad" and the core does not start); a version above 1 is left alone. `tau-compat.json` lists it as role `user`, slot 11, `seed: false`, format `TMSF` and states `requires_omega.meter_settings_version`. |
| **Meter bin `tau-meters.bin` (`TPKB` v2, data slot 10) and its golden vectors** | `tools/meter_bin.py`, `fw2/cores/meterbin_core.h`, `docs/research/meter_bin/` (`expected.json`), `docs/features/meters/EXTERNAL_METERS_SPEC.md` | `boot_code` is the firmware's answer, including its own rule 20 NOFALLBACK (a bin without a fallback row is refused whole); `reader_boot_code` where the reader alone differs; a v1 bundle is never read by the firmware. |
| **Persist layout 2 and its golden vectors** | `fw2/cores/settings_core.h`, `tools/persist_layout.py`, `docs/research/persist_layout/golden_vectors.json`, `tools/persist_registry.json` | Regenerated 2026-11-23 when every shipped meter's `params_rev` became 1 (D-M28): the tune keys of Chladni and VU Master changed. |
| **Platform category (`Platforms/<platform>.json`)** | `dist/Platforms/tau.json`, `tools/package_dev_build.py` | `Media` since v0.7.5 (was `Media Players`); a tool that matches it by text must accept both. |

Add a row here whenever a new interface surface is created — this table, not either project's
memory of the conversation that created it, is what a future session should find first.

## Addendum 2026-10-14 (B-727): the index's optional track details section
`tau-library.tdb` may carry a ninth, optional section of 32-byte per-track technical records (codec, rate, channels, bit depth, bitrate, file size, ReplayGain gains and peaks, LAME delay/padding, encoder string, embedded cover size, sidecar present) that the player's track details page shows. Format: `docs/MEDIA_LIBRARY_0.4_SPEC.md` section 16 and `tools/tau_details.py` (reference extractor). Tau Omega's index writer should add it when it can read those fields; an index without it still works (the page then shows the tags only). Older firmware loads an index that has it.

## Change log (Tau-Alpha side; Tau Omega re-checks on its own schedule)
- 2026-10-07 (branch `ram-diet`): the firmware no longer draws report codes as QR. The pixel grid (TPG, `fw/tpg.h`, decoder `tools/tpg.py`, `tools/decode_tau_suite.py --grid`) is the only view; the record bytes (`SR_T_*`, `TAUD1`) are unchanged. Tau Omega's `taud` module decodes QR only (`rqrr`), so it cannot read reports from builds on this branch until it ports the grid decoder. QR captures from older cores stay decodable (the host tools keep `--qr`). The QR encoder is archived in `archive/qr_encoder/` (tag `archive/qr-encoder`).
- 2026-10-16 (Player 2, branch `player2`): **index text is Latin-1** (`tools/tau_library.py` `latin1_text()`, one byte per character, accents kept; the file NAMES on the card stay ASCII). Tag text under ID3 encoding 0 is read as UTF-8 when it is valid UTF-8, else Windows-1252. Sorting folds marks (`Édith` sorts under E). Tau Omega's index writer should follow the same rules; the firmware draws 0xA0..0xFF as the plain letter until the Latin-1 font lands (`docs/features/FONT_SPEC.md`). New record format `TFNT` (a font) is specified in `FONT_SPEC.md` section 3.2 and written by `tools/gen_font_atlases.py`; a `FONT` section of `tau-assets.bin` will carry it (not yet read by the firmware). Meter tuning (persist ids 20-23) and the meter-config QR export (`SR_T_METERCFG`, X on the Configure page) are new surfaces too: `fw2/cores/tune_core.h`, `tools/decode_tau_suite.py --grid`.

## Mailbox
Open questions, answers and rehearsal packages between the two projects go through `docs/interop/` (protocol in its README, open items in `LEDGER.md`).
