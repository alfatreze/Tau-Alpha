# Review: release system, `tau-compat.json` and the Tau Omega interaction (2026-10-08)

Scope: everything on branch `release-system`:
- `RELEASE_SYSTEM_SPEC.md` sections 1-11;
- `tools/tau_compat.py` (build, verify, layout, `check-card`);
- `tools/make_release.py` hook;
- `tools/install_dev_core.py` step 3c;
- the published JSON Schema;
- how Tau Omega will consume it (`crates/tau-core/src/update.rs`, `assets.rs`, read today).

Each finding was checked in the code. Severity: **High** = can lose user data, brick a boot, or make Omega act on a wrong
fact. **Medium** = wrong or missing information Omega needs, or a fragile path. **Low** = hardening.

## High

**H1. Omega's `tau-assets.bin` writer destroys sections it does not write.** `assets.rs` writes only `THEM` and says so
("any other section (such as `METR`) is not carried, so writing replaces the whole file"). The firmware now reads `METR`
(meter presets) and `PRST` (Halcyon **user** EQ presets, B-641). Editing a theme in Omega therefore deletes the user's
saved EQ presets on the card. The manifest already lists all three sections, but it does not say "keep them".
- *Mitigation (Omega, urgent):* round-trip unknown sections byte for byte. Refuse to write if a section cannot be
  carried.
- *Tau side:* add `"preserve_unknown_sections": true` to the TAUA format entry (additive key, schema stays 2).
  Publish a reference file with THEM+METR+PRST as a shared fixture that Omega's tests must round-trip unchanged.

**H2. `persist_ids_changed` misses meaning changes that do not touch `interact.json`.** It diffs name, type, default
and range. A real case already shipped: B-599 packed the ReplayGain mode into bits 4-5 of the `SW_POL` word
(`fw/settings.inc`) with no `interact.json` change. Bit-packed words and enum-valued ids (meter enum, Halcyon preset)
can change meaning invisibly.
- *Mitigation:* a checked-in persist registry (`tools/persist_registry.json`: id, owner symbol, meaning version, one-line
  meaning). `tau_compat` reports an id as changed when its meaning version differs from the previous release.
- A test fails when `interact.json` persist entries and the registry disagree (new or removed ids, renamed ids).
- Review rule: any edit to `set_wr32`/`set_rd32` packing bumps that id's version. This cannot be fully automated; the
  registry makes it a reviewed fact instead of a memory.

**H3. `bitstream_core_version` is evaluated against today's `mp3_soc.v`, not the RTL the fit was built from.** The fit
manifest already records `commit` and `rtl_dirty`. If the CORE_VERSION ifdef table changes between the fit and the
release, the manifest states a wrong version. Omega would then say "pairing verified" for a pair that black-screens.
- *Mitigation:*
  - Evaluate `git show <commit>:src/fpga/core/mp3_soc.v`.
  - Refuse a manifest with `rtl_dirty: true` unless `--bitstream-version` is given.
  - Put the commit into the compat file (see M1).
- *Latent:* `fit_manifest.macros_of` treats `NAME=0` as undefined, but Verilog `` `ifdef NAME `` is true for any
  definition. No current bundle uses `=0` (checked). Make the CORE_VERSION evaluator and the bundle checker refuse
  `=0` for the macros that select CORE_VERSION.

**H4. "owned" files live in the platform-wide `common/` folder.** `tau.rom`, `tau-cold.bin` and `tau-loading.bin` are
owned by whichever core installed last on platform `tau`. Today only one core uses `tau`, so it holds. The moment a
second core shares the platform (the section 4 channel plan, or a user installing two builds), two layouts claim the
same path with different hashes. `check-card` would then report one core as broken, and an Omega install of one would
silently change the other's firmware. This is the cold-image hazard from section 3, now encoded in the contract.
- *Mitigation:* a hard prerequisite for any second core on a platform. Set `data.json` bit 1 on slots 1/4/6 (files move
  to `Assets/<plat>/<core>/`); the layout generator already handles that path.
- Until then, `layout()` should refuse to build a release that declares a platform another known release core also
  declares.

**H5. Omega cannot tell which release is on a card.** `core.json` says `0.6.0` for every alpha, so Omega's
`SameDateDifferentBuild` verdict exists because identity is guessed from version plus date.
- *Mitigation (no Tau change):* Omega identifies the installed release by hashing the card's owned files and
  matching them against `packages[].layout` of known compat files. This is `tau_compat.package_match` applied to a card.
  It turns "probably alpha.4" into "exactly alpha.4" or "unknown build".
- *Mitigation (Tau, later):* the full SemVer in `core.json` `version` (section 4), after the section 7 probe confirms
  how the Pocket shows it.

## Medium

**M1. The tree-derived fields are not traceable.** Library version, asset sections, report tags, format versions and the
cover name come from the working tree at release time. Nothing records which tree, and `verify` on a later checkout
reports false differences.
- *Mitigation:* additive `"source": {"commit": "...", "dirty": false}`. `make_release.py` refuses a dirty tree, as it
  already refuses an unaudited RBF.

**M2. "Previous release" is chosen by hand, and skipped updates are not covered.** A wrong `--previous` silently
under-reports changes. A user going from alpha.2 to alpha.4 needs the union of both releases' changes.
- *Mitigation:* additive `"previous_release": "v0.6.0-alpha.3"`. `make_release.py` checks the given zips are that
  release's (from its `SHA256SUMS.txt` or a local `release/` copy).
- Contract rule for Omega: when the card's release is older than `previous_release`, fetch the intermediate compat
  files (each about 5 KB) and union `persist_ids_changed`. A release without a compat file means "unknown, warn".

**M3. A failed post-install check leaves a half-installed core.** Step 3c runs after the copy. On failure the new core
is on the card with old catalog caches. What the Pocket does with a stale cache plus new files is untested (B-143 shows
caches matter).
- *Mitigation:* move every check that does not depend on the new files to before writing. That covers the formats of
  the existing index, `tau-assets.bin` (including a file given by `--assets`), covers and settings. Only the owned
  hashes stay after the copy.
- On an owned-hash failure, restore the replaced core from the backup automatically, then stop.

**M4. Feature pairing is not in the manifest.** It has `rom_needs` (TAUFWNEED) but not the bitstream's features, so
Omega can check the CORE_VERSION pairing but not the feature pairing. That is the silent "NO UNIT" failure B-653 exists
to stop.
- *Mitigation:* additive `bitstream_features`, built from the fit manifest's macros through `fit_manifest.FEATURES`.
- `build` refuses a package whose `rom_needs` is not covered (same rule as `check_fw_bitstream_pair --bitstream-manifest`).

**M5. Dev and local packages have no manifest.** Omega's Packages flow installs local zips (dev builds never reach
GitHub). Those are the builds most likely to differ, yet Omega must fall back to hard-coded rules for them.
- *Mitigation:* `package_dev_build.py` writes a single-package `tau-compat.json` into the package root, as it already
  does with `bitstream-manifest.json` (never copied to the card). Release `v<X.Y.Z>-dev.<NN>`, `prerelease: true`.
  The installer then uses it automatically.
- Agree with Omega to look for `tau-compat.json` next to or inside a local package (the in-zip copy from section 5).

**M6. A shipped file always wins over its intended role.** `layout()` marks any shipped file a data slot names as
`owned`. If a future packager put a sample `tau-assets.bin` inside the zip (today it sits next to the package), the
user's file would become "owned" and be overwritten on install.
- *Mitigation:* `build` refuses when a name in `NOT_SHIPPED` (user/generated) appears in the zip.

**M7. Our schema checker implements a subset of JSON Schema.** Omega will use a real validator, so a keyword we do not
enforce could pass here and fail there, or the reverse.
- *Mitigation:* a test that the schema file uses only the supported keywords.
- When `jsonschema` is installed, also validate with it in `sim/test_tau_compat.py`. Skip with a note otherwise; no new
  dependency.

**M8. The library index root is implicit.** The index embeds `/Assets/<plat>/common/` and track opens use it. The layout
says where the index goes but not the root it must contain. That is the B-136 failure, and it matters as soon as Preview
uses another platform.
- *Mitigation:* add `"root": "/Assets/tau/common/"` to the `tau-library` format entry.
- `check-card` reads the root from the index header (offset 40, string table) and compares it.

## Low

- **L1. Authenticity.** `SHA256SUMS.txt` comes from the same place as the zips, so it guards against corruption, not
  tampering.
  - Acceptable for now.
  - Policy: never replace an asset of a published release; fix with a new tag. Omega keys its cache on tag plus asset
    hash.
  - Signing (minisign or Sigstore) can come later.
- **L2. Card-check speed.** `check-card` walks all of `common/` for covers. Over the Pocket's USB mount this is slow
  (B-061, the fskit episode).
  - Cap the number of files sampled (already capped at 2000).
  - Omega should prefer an SD reader and show progress.
- **L3. Uninstall is not covered.** Obsolete removal applies only on install. Omega's remove path should delete exactly
  the `owned` entries of the installed release's layout, plus empty folders, and never `user` or `generated` files.
- **L4. Schema rules need to reach Omega.** Omega planned to read schema 1. Schema 2 is a strict superset, but its
  reader must accept the number 2. The "unknown schema: refuse; unknown key: ignore" rule should be written into
  Omega's `FIRMWARE_UPDATE_SPEC.md` (their repo, their session).
- **L5. GitHub rate limit.** One more request per release (the compat file). Fetch release lists once per session, as
  their spec already plans.
- **L6. Pocket JSON limits stay with `check_tau_package.py`, not the manifest.** Omega found that slot names over 15
  characters work, so it should not enforce that limit. Correct as is.

## What is sound (keep)

- Every field is derived and verified, never typed. Unclassified files or slots stop the release.
- An exact package match is required before an installer trusts a manifest. A non-matching explicit `--compat` stops
  before writing.
- "Refuse, don't guess" versioning matches the firmware's own library-index rule.
- The zips are unchanged, so Pocket Sync, Pocket Updater and manual installs are unaffected.
- Mutation tests on both the release and the card side.

## Status of step 1 (2026-10-08, same day)

Done on `release-system`. The schema stays 2: every change is an additive key. `make test-host` passes.

| Item | What was built | Test |
|---|---|---|
| H3 | `bitstream_core_version` evaluates `mp3_soc.v` with `git show <fit commit>`. It refuses `rtl_dirty` and a manifest without a commit. `selecting_macros()` finds the macros that choose CORE_VERSION (TAU_CLK66, TAU_RAM_192K), and `zero_defined()` refuses a fit bundle writing one of them as `=0` | dirty, no-commit and `=0` cases |
| M1 | Top-level `source {commit, dirty}` (dirty ignores `dist/` and `release/`). `make_release.py` refuses a dirty tree | field checked against git |
| M4 | `packages[].bitstream_features` from the fit manifest through `fit_manifest.FEATURES` (null when CORE_VERSION is given by hand). `build` refuses `rom_needs` the bitstream lacks | LPC-missing case |
| M6 | `build` refuses a zip that ships a user or generated file (`tau-assets.bin`, `tau-library.tdb`) | shipped-`tau-assets.bin` case |
| M8 | The `tau-library` format carries `root`. `check-card` parses the index and compares its root | foreign-root and damaged-index cases |
| M7 | A test that the schema uses only the keywords our checker implements; it also cross-checks with `jsonschema` when installed (not installed here, skipped with a note) | keyword test |
| H1 | The TAUA format carries `preserve_unknown_sections: true`. `docs/schemas/fixtures/tau-assets-roundtrip.bin` (THEM+METR+PRST, built by `tools/make_compat_fixtures.py`, `--check` in the test) is the shared fixture Omega's writer must round-trip | fixture check |

Also fixed: the compat test's synthetic zips used the current time, so their hashes changed when a rebuild crossed a
second boundary (a flaky "restored zip verifies" check). They now use a fixed timestamp.

Consequence: the published alpha.3 and alpha.4 zips still ship the inert `TAU.json`, which is now listed obsolete, so
`build` refuses them. A backfill of those releases needs that release's own tree and config (expected, not a defect).

Omega side, still open: the H1 round-trip fix in `assets.rs`.

## Status of step 3 (2026-10-08, same day)

Done on `release-system`. The schema stays 2 (additive keys only). `make test-host` passes. A real dev package was built
against fit `noeq-b670`.

| Item | What was built | Test |
|---|---|---|
| H2 | `tools/persist_registry.json`: name, firmware word, `meaning`, `since` and a sentence for every persisted id. `build` refuses an `interact.json` id that is missing from it or named differently, and a `since` later than the release or not released. `persist_ids_changed` adds every id whose `since` is later than the previous release. The registry is published as `persist_registry` | the shipped `interact.json` matches; rename, new-id and future-since cases |
| M2 | `previous_release` (new `--previous-release`, required with `--previous`). The previous zips must carry that release's version and CHANGELOG date. It must be older than the release. Union rule for Omega: an id changed since release R when its `since` is later than R | wrong-date, not-older and missing-tag cases; a card on alpha.3 must treat ids 10, 16, 28 as changed |
| M3 | Installer: the format of the card's own files and of `--assets` is checked **before** anything is written. Every file the copy overwrites is snapshotted. A copy mismatch or a failed card check restores the card exactly (verified) and leaves the catalog caches alone. Obsolete files are removed only after the card check passes | a bad `--assets` file and a TAUA v9 card file are refused untouched; fault injection after the copy and at the check gives a byte-identical card |
| H1 (installer side) | A `tau-assets.bin` found automatically (the sample next to the package, the carried-from core's copy) no longer overwrites the card's own file; an explicit `--assets` still does | sample-next-to-package case |
| M5 | `package_dev_build.py` writes a dev `tau-compat.json` and a deterministic `<core>_<version>_dev.zip` next to the package when the RBF has a fit manifest (release `v<X.Y.Z>-dev.<NN>`, which sorts before every preview). The installer uses it automatically | deterministic zip, package match, tag order |

Found while building the registry:
- **Persist id 27 was reused.** It was "Library off (restart)" until v0.5.0. It became "(internal) theme" at another
  address in v0.6.0-alpha.1 (B-346), against the rule in `tools/tau_data_slots.py`. That docstring is now corrected,
  and the registry records meaning 2.
- **Ids 20-23 are the Check summary since v0.6.0-alpha.1.** The legacy playlist words they held are gone.

Pending:
- Id 16's `since` is set to `v0.6.0-preview.1`, the expected next pre-release. If the next release gets another tag,
  `build` refuses until the registry is updated, which is intended.
- Omega: implement the union rule and read `persist_registry`.

## Status of H4 (2026-10-08, same day)

Built on `release-system`. **It changes the card layout, so it must pass the card probe (spec section 7, item 2)
before any release ships it.**

| What | Detail |
|---|---|
| Layout | `tau.rom`, `tau-cold.bin` and `tau-loading.bin` live in `Assets/<platform>/<core>/`. `data.json` slots 1, 4 and 6 set parameter bit 1 (slot 1 `0x108` -> `0x10A`, 4 and 6 `0x0` -> `0x2`). User and generated data stay in `common/` |
| One place | `tools/tau_layout.py`: the file list, the bit, and a finder that accepts the old `common/` location (old packages and cards) |
| Producers | `fw/build.sh` writes `dist/Assets/tau/alfatreze.TAU/` (dist files moved with `git mv`, bytes unchanged); `package.py`, `package_dev_build.py`, `make_release.py`, the release workflow, the splash tools and `.gitignore` follow |
| Checks | `check_tau_package.py` requires the bit and no build-bound file in `common/`. `tau_compat` refuses a release shipping one there or a build-bound slot without the bit. The pairing check finds the ROM in either place |
| Migration | The old `common/` copies are `obsolete` per core. The installer removes them after a passing card check, **only if no other core on the platform still reads them from `common/`**, so an older pinned build keeps working |
| Tests | Layout roles and slots; common-shipped and missing-bit refusals; the other-reader rule; an installer upgrade of an old-layout card (kept while an old core reads the file, removed once none does). A real dev package against `noeq-b670` |

Risk to confirm on hardware: that APF loads the required slot 1 (`tau.rom`) and the deferload slots 4 and 6 from the
core folder, as Analogue's data.json documentation says.

## Proposed order

1. **Now, small, Tau side (schema stays 2, all additive):** H3 (commit-pinned evaluation, `rtl_dirty`, `=0` guard),
   M1 `source`, M4 `bitstream_features`, M6 guard, M8 `root`, M7 schema-keyword test, H1 `preserve_unknown_sections`
   flag plus the fixture.
2. **Omega, urgent:** H1 round-trip of unknown sections (user data loss today).
3. **Next:** H2 persist registry; M2 `previous_release` and the union rule; M3 pre-write checks and auto-restore;
   M5 dev-package manifest.
4. **Before the channel scheme:** H4 (core-specific build files); section 7 probe; then H5's SemVer `core.json`.
5. **Contract text for Omega (their repo):** H5 identification by hash, M2 union, L3 uninstall, L4 schema rules.
