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

Add a row here whenever a new interface surface is created — this table, not either project's
memory of the conversation that created it, is what a future session should find first.
