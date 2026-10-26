# Release system: naming, Pocket organisation, Tau Omega needs

Status: **investigation and proposal (2026-10-08), branch `release-system`.** Nothing built, nothing on a card.
Every claim below is marked **[DOC]** (Analogue developer docs), **[TAU]** (this repo, read today),
**[OMEGA]** (Tau Omega repo, read today) or **[TEST]** (not yet confirmed on a Pocket; section 7 lists the probe).

## 1. What the owner asked for

- Main releases: one place, several versions selectable, the Diagnostics build alongside them, no duplicated
  assets or media. Sketch: `Tau > v0.7, v0.7.1, v0.8, v0.7 Diagnostics`.
- Alpha/beta releases: separate from the main releases (risk of cold-image incompatibility to be checked).
- Dev builds: isolation and quick access first. Sketch: `Tau beta > 0.7.3`, `Tau DEV 385`.
- Channel names decided 2026-10-08: **Stable / Preview / Dev** ("beta" clashes with the project name Tau Alpha; section 4a).
- Easy download and install by Tau Omega; an easy workflow for development.

## 2. What the Pocket offers (the two real mechanisms)

The Pocket has no "versions" concept. There are exactly two ways to offer a choice:

### Mechanism A: several cores on one platform (Pocket's own core list)

- "Multiple cores can support a common platform" **[DOC overview]**. A core is a folder
  `/Cores/<author>.<shortname>/` (shortname <= 31 chars) whose `core.json` lists `platform_ids` (<= 4).
  When several cores share a platform the openFPGA menu lists the platform once and then the cores under it **[TEST: exact
  text shown per core, see section 7]**.
- Platform ids: `[a-z0-9_]`, <= 15 chars, own `/Platforms/<id>.json` (name <= 31) and image **[DOC platform-metadata]**.
- Each core folder is fully separate: own bitstream(s), own `data.json`/`interact.json`, own settings file
  `/Settings/<core>/Interact/interact_persist.json` **[DOC interact-json]**.
- Shared media comes for free: platform-common assets live in `/Assets/<platform>/common/` **[DOC data-json bit 1]**.
- A data slot can read from **another** platform's folder: parameter bits [25:24] index into `platform_ids`
  **[DOC data-json]**. So a core on platform `tau_dev` can still read `/Assets/tau/common/...` if `tau` is its second
  platform **[TEST]**.

### Mechanism B: one core, several bitstreams, chosen by an instance JSON

- `core.json` `cores[]`: up to **8 bitstreams**, `filename` <= 15 chars, 16-bit `id` **[DOC core-json]**.
- The choice is made by an `<instance>.json` loaded into the **first** data slot (parameter bit 4 "Instance JSON",
  must also be core-specific, extension `json`). It sets `core_select {id, select}`, overrides data-slot filenames
  (relative to the instance file's folder, <= 255 chars) and up to 32 memory writes **[DOC instance-json]**.
  Chip32 `CORE Rx` is the other way to pick a bitstream **[DOC chip32]**.
- The "menu" is the asset browser that opens at launch when slot 0 is required and has no filename. This is what
  arcade/multi-system cores use (approaches.md item 6 in the skill). Bit 9 "persist browsed filename" skips it next
  time; bit 0 + bit 8 make it reloadable from the core menu with a full bitstream reload **[DOC data-json]**,
  **[TEST: whether reloadable slots count against interact.json's 16-entry cap, KB-079]**.
- One folder means **shared** `data.json`, `interact.json`, `video.json`, `input.json` and (by default) one shared
  `interact_persist.json` for every version. A per-asset interact menu and persist file exist at
  `/Presets/<core>/Interact/<slot0 path>.json` **[DOC interact-json]**, which could give each version its own menu
  and settings **[TEST]**.

### Comparison for Tau

| Need | A: core per build on a platform | B: instance JSON per version |
|---|---|---|
| Native "pick a version" list | Pocket core list (text per row unknown) | Asset browser of `.json` files, at every launch or via reload |
| Old ROM/cold image always paired with its own bitstream | Yes, by folder | Yes, if every instance names ROM + cold + bitstream |
| Version-to-version coupling | None (separate JSONs and settings) | `data.json`/`interact.json` shared by all versions: a slot or setting removed in v0.8 breaks v0.7 |
| Settings across an update | Lost unless copied (new folder) | Kept, but a persist-layout change silently misreads old values |
| Limits | None in practice | 8 bitstreams; instance bit only on the first slot |
| Diagnostics beside normal | Separate core, same platform | Extra instance JSON, same bitstream, different ROM (needs its own interact menu) |
| Tau Omega install/remove | One zip = one folder, add/delete atomically | Must edit shared `core.json` and add files into a folder other versions use |
| Boot friction | None | A browser (or one remembered choice) before every start |

**Recommendation: Mechanism A everywhere**, with the platform as the "channel" and the core folder as the build.
B's only advantage (one folder, settings kept) is outweighed by the shared-`data.json` coupling, which is exactly the
cold-image/format risk the owner is worried about, and Omega would have to rewrite a shared `core.json` on each
install. B stays documented here as the fallback if the section 7 probe shows the core list is unusable.

## 3. The real incompatibility risk (answer to "cold file incompatibility")

What actually breaks when versions share files today **[TAU]**:

1. **ROM and cold image live in the platform-common folder** (`data.json` slots 1 and 6 have bit 1 clear:
   `/Assets/tau/common/tau.rom`, `tau-cold.bin`). Any second core on platform `tau` would load the **same** ROM and
   cold image. This is the real cold-file hazard, and it is a packaging choice, not a Pocket limit.
   Fix: set bit 1 (core-specific) on every **build-bound** slot so each core reads `/Assets/tau/<core>/...`.
   Build-bound = ROM (1), cold image (6), loading artwork (4, ships with the build). The firmware/bitstream pair is
   then fixed per folder by construction; the `TAUFWPAIR`/`TAUFWNEED` gate (B-582, B-653) stays as the backstop.
2. **Shared, user-owned data in common/**: `tau-library.tdb` (5), `tau-assets.bin` (8), covers (7), media.
   These are meant to be shared. They are safe across versions only if every format carries a version that older
   firmware refuses cleanly (the library index has a header version check; TAUA has its own) **[TAU]**.
   Rule: a build that changes one of these formats must bump its format version and, until released, read a
   core-specific copy instead of the common one.
3. **Settings ids**: persist ids are interpreted by each ROM. Keep them append-only; never reuse an id
   (precedent: ids 20-23 are read as both legacy playlist state and the Check report, see the 2026-09-22 log).
4. **Firmware does not hard-code a platform path** **[TAU: only comments mention /Assets/tau/common]**; track paths
   come from the library index root. So moving builds between platforms is safe as long as media stays under
   `/Assets/tau/common/`.

With 1 fixed, Preview and Dev builds can share the media library with stable without risk to stable's ROM. Whether they
**should** share the platform list is a UX choice (section 4), not a safety one.

## 4. Proposed scheme

Three channels = three platforms. Every build is its own core folder. Media lives once, in `/Assets/tau/common/`.

| Channel | Platform id / list name | Core folder (`author.shortname`) | Shown as | Media |
|---|---|---|---|---|
| Stable | `tau` / "TAU" | `alfatreze.TAU` (current stable, rolling) + optional pinned `alfatreze.TAU_0_7`, `alfatreze.TAU_0_7_1` | TAU 0.8 / TAU 0.7.1 / TAU 0.7 | own |
| Stable diagnostics | `tau` | `alfatreze.TAU_DIAG` (+ pinned `alfatreze.TAU_0_7_DIAG`) | TAU 0.8 Diagnostics | own |
| Preview | `tau_preview` / "TAU Preview" | `alfatreze.TAU_PREVIEW_0_8_0_2`, later `..._RC_1` | TAU Preview 0.8.0-preview.2 | from `tau` via bits [25:24] |
| Dev | `tau_dev` / "TAU Dev" | `alfatreze.TAU_DEV_385` | TAU Dev 385 | from `tau` via bits [25:24] |

Notes:
- **Rolling stable folder**: `alfatreze.TAU` always holds the newest stable, so settings and the Pocket's
  recent-cores entry survive an update. A "keep this version" choice copies it to a pinned folder
  (`alfatreze.TAU_0_7`) with its own settings copy. Pinned folders are optional and Omega-managed.
- **Diagnostics moves onto platform `tau`** (today it is platform `tau_diagnostic` with duplicated media). With
  build-bound slots core-specific it can safely share media.
- **Preview and Dev get one platform entry each** instead of one per build (today every DEV/alpha build creates
  its own platform, `tau_dev_42`, `tau_0_6_0_a_44`, ...). The list under "TAU Dev" is the dev picker; nothing new
  in the main TAU list. Shared media removes the per-install media copy in `install_dev_core.py` (the slowest step).
  Dev builds that change a shared format set `--private-library` and read core-specific copies.
- **Version strings** follow SemVer 2.0: Stable `0.8.0`, Preview `0.8.0-preview.2` (optionally `0.8.0-rc.1` just before a
  release), Dev `0.8.0-dev.385`. Written in full in `core.json` `version` (<= 31 chars **[DOC]**; B-142 showed the earlier
  disappearing-core problem was the 63-char description, not the version). This gives every build a unique,
  sortable id, removing Omega's "same date, different build" ambiguity **[OMEGA update.rs]**.
- **Dev numbers** stay monotonic and are never reused; three digits are fine (`TAU_DEV_385`, platform unchanged).
- **Zip names** keep Analogue's convention `<Author>.<Core>_<Version>_<Date>.zip`
  (`alfatreze.TAU_0.8.0_2026-11-02.zip`, `alfatreze.TAU_DIAG_0.8.0_...`, `alfatreze.TAU_PREVIEW_0.8.0-preview.2_...`).
- **Git tags**: `v0.8.0`, `v0.8.0-preview.2`; GitHub `prerelease` true for anything with a suffix. Dev builds are never
  published to GitHub.

Owner decisions are listed in section 8.

### 4a. Channel names (decided 2026-10-08)

One word per channel, used identically in the Pocket list, platform id, version label, zip name and GitHub flag.

| Channel | Pocket list | Platform id | Version | Zip | GitHub |
|---|---|---|---|---|---|
| Stable | TAU | `tau` | `v0.8.0` | `alfatreze.TAU_0.8.0_<date>.zip` | release |
| Preview | TAU Preview | `tau_preview` | `v0.8.0-preview.2` | `alfatreze.TAU_PREVIEW_0.8.0-preview.2_<date>.zip` | pre-release |
| Dev | TAU Dev | `tau_dev` | `0.8.0-dev.385` | not published | none |

- "Beta" was rejected because it clashes with the project name Tau Alpha.
- "Nightly" implies an automatic daily build, which this project does not have.
- "Insider" implies a membership programme.
- "Tester" names a person, not a build. "Testing" (Debian's word) was the second choice.
- SemVer orders pre-release labels alphabetically, so `dev < preview < rc < release` with no special cases in Omega's
  comparator. `-rc.N` stays available inside the Preview channel.
- Existing published tags (`v0.6.0-alpha.N`) are history and are not renamed. The first Preview is the next pre-release.
- "Tau Alpha" remains the repository name only. Users see TAU, TAU Preview and TAU Dev.

## 5. What Tau Omega needs from a release (requirements)

From `FIRMWARE_UPDATE_SPEC.md` and `crates/tau-core/src/update.rs` **[OMEGA]**:

1. Find releases on GitHub, filter Stable/Preview by `prerelease`, pick Normal vs Diagnostic asset by name pattern,
   verify against `SHA256SUMS.txt`, then install through its existing plan/confirm path.
2. Compare installed vs offered (it currently has to guess from `core.json` version + date).
3. Verify firmware/bitstream pairing. Finding: "a package cannot reveal its bitstream's CORE_VERSION", so it keeps
   a hand-seeded `KNOWN_BITSTREAMS` table.
4. Know which files an update replaces and which user files it must keep.

Proposed answer: a **release manifest** `tau-release.json`, inside each core folder and as a separate GitHub asset:

```json
{ "tau_release": 1,
  "channel": "stable|preview|dev", "version": "0.8.0-preview.2", "build": 412, "git": "f2d3373", "date": "2026-11-02",
  "core": "alfatreze.TAU_PREVIEW_0_8_0_2", "platform": "tau_preview", "variant": "normal|diagnostic",
  "bitstream": {"file": "bitstream.rbf_r", "sha256": "...", "core_version": "0x4D50331A", "fit": "noeq-b670 s1"},
  "firmware": {"rom_sha256": "...", "cold_sha256": "...", "needs": ["HALCYON", "..."]},
  "formats": {"persist_layout": 3, "library_index": 1, "assets": 1, "data_slots": 2},
  "owned_files": ["Cores/...", "Assets/tau/<core>/..."], "shared_files": ["Assets/tau/common/tau-library.tdb"],
  "min_omega": "0.4.0" }
```

`owned_files` vs `shared_files` answers item 4 directly; `formats` lets Omega warn before installing a build whose
library or settings format differs; `bitstream.core_version` removes the need for `KNOWN_BITSTREAMS`.
The manifest is generated by the release/package tools from values they already check, never hand-written.

**The specific feature request from Tau Omega mentioned in the brief was not included in the message; this section
uses Omega's own committed spec. Paste the request and it will be folded in here.**

## 6. Standard practice it follows

- SemVer 2.0 pre-release ordering, which sorts labels alphabetically: `dev < preview < rc < release`, build number as a separate monotonic field
  rather than SemVer build metadata (`+385`), because Pocket/Omega compare strings and `+` is ignored in precedence.
- Release channels (Stable / Preview / Dev) as separate install targets, as browsers and OS updaters do, so a tester can
  run Stable and Preview side by side.
- One immutable artefact per version with a checksum list and a machine-readable manifest next to it
  (GitHub releases + `SHA256SUMS.txt` already; manifest added).
- Analogue's own zip naming and folder layout, so manual installs and other updaters (Pocket Sync, Pocket Updater)
  keep working. Unknown files in a core folder are ignored by the Pocket **[TEST: confirm tau-release.json is
  harmless]**.

## 7. Probe before building anything (one card session, no Quartus)

Reuse the current alpha.4 bitstream and ROM; only JSON and folders change. Each item is one photo or one boot.

1. Two cores on platform `tau` (`alfatreze.TAU` and a copy `alfatreze.TAU_PROBE` with `version` `0.8.0-preview.2` and a
   different description): what does the platform list show, what text identifies each core (shortname, version,
   description)? Does a 0.x semver with suffix in `version` show and sort sanely?
2. Build-bound slots with bit 1 set (ROM, cold, loading art) on the probe core: boots from
   `/Assets/tau/alfatreze.TAU_PROBE/`.
3. Dev-style core on platform list `["tau_dev", "tau"]`, library slot with bits [25:24] = 1: library loads and tracks
   play from `/Assets/tau/common/` with no media copied.
4. `tau-release.json` in the core folder: no effect on load.
5. Fallback B only if 1 is unusable: instance-JSON slot 0, two bitstream entries, check the browser and per-asset
   interact/persist.

Results go to AUDIT_TRAIL as a new B entry; then the tools change.

### 7a. Probe results (2026-10-08, B-672)

| Question | Answer on hardware |
|---|---|
| Two cores on one platform | One platform entry with a count badge; opening it shows **Select Core**, with one core marked **Default** |
| Text per core | The core's **shortname** (underscores shown); the selected row adds Version and Author. Not the description |
| SemVer pre-release in `version` | Shown as written (`0.8.0-preview.2`) |
| Core-specific build files (H4) | **Work**: boot, splash, cold image, covers, library, theme file |
| Data slot bits [25:24] (read another platform's file) | **Work**: library and theme file read from `tau` |
| A core with two `platform_ids` | **Listed under both platforms**, so a dev core sharing `tau` shows up in the TAU list |
| Unknown file in the core folder | Ignored |

**Probe 2 (B-673):**
- spaces, dots and hyphens are legal in shortnames and show as written (`TAU 0.7`, `TAU Preview`, `TAU-0.7`);
- a core cannot open files outside the platforms it declares. A dev core with only its own platform reads a copied index but plays
  nothing.

**Design consequences** (they revise section 4; owner decisions in section 8):
- **Shortnames are display text, and may contain spaces and dots (B-673).** Stable `TAU` (rolling), pinned `TAU 0.7`, `TAU 0.7.1`,
  Diagnostics `TAU Diagnostics`. Renaming the existing `TAU_DIAGNOSTIC` folder would lose its settings, so it stays until a release
  decides otherwise.
- **Preview** cores: platform `tau_preview` (own list entry "TAU Preview"), shortnames such as `TAU_PREVIEW`. One rolling folder
  for the current preview keeps the list short, with the exact version shown on the row.
- **Dev** cores: B-673 rules out the index-copy idea, because a core cannot open files outside its declared platforms. Two options
  remain, an owner decision:
  - (a) declare `["tau_dev", "tau"]`: no music copied, the dev build also appears in TAU's Select Core list (shortname such as
    `TAU DEV 385` plus its version);
  - (b) `tau_dev` only, with a media copy (today's `--carry-from`, slow).
  - Recommendation: (a) on the owner's own card, where quick access matters more than a tidy TAU list; (b) for anything handed to
    testers.
- **Installer safety:** removing or replacing a core on a shared platform must never touch the platform's media or platform files.
  Fixed in `install_dev_core.py` (B-672).

## 8. Decisions for the owner

**Decided 2026-10-08:** dev builds use option (a), built the same day:
- **Naming:** `package_dev_build.py --number NN` and `--barcode NN` make `alfatreze.TAU DEV NN` / `alfatreze.TAU DEV BARCODE NN`.
- **Platforms:** `["tau_dev", "tau"]`. One **TAU Dev** platform for every dev build, replacing a platform per build.
- **Slots:** data slots 5 and 8 read TAU's library index and `tau-assets.bin` from `Assets/tau/common/` (parameter bits [25:24] = 1).
  Music and covers open from there; build files stay in `Assets/tau_dev/<core>/`.
- **Visible consequence:** each dev build is also listed in TAU's Select Core list.
- **Installer:** finds `tau-assets.bin` and the library where the core reads them, refuses `--carry-from` (no music copy), and
  keeps the TAU Dev platform until the last dev build is removed.
- **Cards today:** existing `TAU_DEV_NN` cores keep working on their own platforms. Remove them as they are superseded (their
  backups include their media copies).
- `--semver` builds keep the old scheme until the Preview channel is implemented.

1. Mechanism A (core per build, platform per channel) vs B (instance JSON). Recommended: A.
2. Rolling `alfatreze.TAU` for stable with optional pinned folders, vs every version its own folder. Recommended:
   rolling (settings survive updates).
3. Preview sharing the stable media library (via bits [25:24]) vs its own copy. Recommended: share, with the
   format-version rule in section 3.2.
4. Diagnostics on platform `tau` beside normal, vs its own platform as today. Recommended: beside normal.
5. Core folder rename for diagnostics (`TAU_DIAGNOSTIC` -> `TAU_DIAG`)? Cosmetic; renaming loses existing settings
   on cards. Recommended: keep `TAU_DIAGNOSTIC`.

## 9. Implementation outline (after the probe)

1. `data.json`: bit 1 on slots 1, 4, 6; packagers place those files under `Assets/<platform>/<core>/`.
2. One naming module (`tools/tau_release_naming.py`) used by `make_release.py`, `package_dev_build.py`,
   `install_dev_core.py`: channel, version, core id, platform id, display strings, limits checked in one place,
   with tests. Replaces the three ad-hoc naming branches (`--number`, `--semver`, `--barcode`, `--meter`).
3. `tau-release.json` writer + checker (reuse `check_fw_bitstream_pair.py` and the fit manifest from B-653).
4. `install_dev_core.py`: shared-media mode (no copy), pinned-stable copy, migration of existing per-build
   platforms (`tau_dev_NN`, `tau_0_6_0_a_NN`, `tau_diagnostic`) on the card.
5. Remove the inert `Assets/tau/alfatreze.TAU/TAU.json` the packagers write today: no slot has the instance bit, and
   it uses `variant_select`, a key the instance schema does not have **[TAU, DOC]**.
6. Omega side (its repo, its session): read `tau-release.json`, channel = platform, keep `SHA256SUMS.txt` check.
   Recorded in `CROSS_PROJECT_INTERFACE.md` as a new interface surface.

## 10. Tau Omega request: `tau-compat.json` (analysis, 2026-10-08; built the same day)

**Built** on branch `release-system`: `tools/tau_compat.py` (build/verify), `tools/omega_compat.json`, hook in
`tools/make_release.py` (new required `--release` and `--previous`/`--no-previous`; the pairing gate now gets the
manifest-derived CORE_VERSION), `sim/test_tau_compat.py` in `make test-host` (four CORE_VERSION contracts, hashes
recomputed from the zips, nine mutations refused). Smoke run on the published alpha.4 zips: builds and verifies
(`rom_needs` empty because alpha.4 predates `TAUFWNEED`). Not yet run inside a real `make_release.py` (next release).
CHANGELOG convention: lines `- Omega: ...` in a release's section become `notes`.

Omega asks for one extra release asset, `tau-compat.json` (schema 1), written by `tools/make_release.py`, listed in
`SHA256SUMS.txt` and attached to the GitHub release. Zips stay unchanged. Verdict: **accept, with the source
corrections below.** It fits section 5: it is the external half of the `tau-release.json` idea and needs no Pocket
change, so it can ship before the section 7 probe. The in-zip manifest is deferred, because the zips must not change.

Where each field comes from, and what needs fixing first:

| Field | Source | Issue |
|---|---|---|
| `release`, `prerelease` | Not in the zips (`core.json` says `0.6.0` for every alpha) | New `--release vX.Y.Z[-preview.N]` argument (published history uses `-alpha.N`), refused unless the `CHANGELOG.md` heading and README "Current version" match. `prerelease` = the tag has a suffix |
| `date_release`, `zip`, `zip_sha256`, `core_id`, hashes | The zips | Straightforward. Assert both packages carry the same bitstream hash |
| `rom_accepts`, `rom_needs` | `TAUFWPAIR`/`TAUFWNEED` in each zipped `tau.rom` (reuse `check_fw_bitstream_pair.py`) | Straightforward |
| `bitstream_core_version` | Asked to come from "the source the pairing gate uses" | **Weak source.** `tree_core_version()` takes the first `CORE_VERSION` literal in `mp3_soc.v`, but there are four, selected by `TAU_RAM_192K`/`TAU_CLK66`. The first one happens to be the shipped one. Correct source: the macros in the RBF's fit manifest (`<rbf>.json`, written by `vm_fit.py collect` since B-653) mapped through the same ifdef table. Refuse a release whose RBF has no manifest unless `--bitstream-version` is given explicitly. Fix the pairing gate the same way, so both read one function |
| `library_index_version` | `tools/tau_library.py VERSION` cross-checked with `fw/library_core.h` | Straightforward |
| `assets_sections` | Sections the firmware reads (`as_find(..., "THEM"/"METR")` in `fw/assets.inc`, `PRST` via `hal_prst_load`) | Derive by scanning the firmware source, or the ROM strings, not from `tau_assets.py`. That tool's docstring still calls METR "planned" |
| `assets_read_limit_bytes` | The firmware constant (D-A01, 64 KiB) | Read the constant from the firmware source |
| `report_tags_max` | Last `SR_T_*` in `fw/suite_core.h` | Parse the enum. Next free is 28, so 27 today |
| `persist_ids_changed` | Cannot come from one release's files | Diff the zipped `interact.json` variables (id to name and range) against the previous release's zip. Example: id 16 changed from preset EQ to the Halcyon preset. Needs `--previous ZIP` (or the previous tag's asset). Exact definition: ids whose name, range or default changed, plus ids removed |
| `min_omega` | Judgement | Keep it in a small checked-in file (`tools/omega_compat.json`) so it is reviewed, not typed at release time |
| `notes` | Judgement | Take it from an `Omega:` subsection of the release's CHANGELOG entry. It is still hand-written, but versioned with the changelog |

Other points:
- **Test:** building a real release inside `make test-host` is too slow (two full firmware builds). Proposal: the
  generator is a separate module, `tools/tau_compat.py build|verify`, tested on small synthetic zips with real marker
  strings. Mutations: one ROM byte, one zip byte, a `CORE_VERSION` missing from `rom_accepts`, a removed persist id.
  `verify` also runs at the end of every real `make_release.py`. "Done" is then one real release run plus the test.
- **GitHub step:** `make_release.py` does not publish today. Publishing stays a manual step with the owner's
  approval: it prints the exact `gh release create ... --repo alfatreze/Tau-Alpha` command including `tau-compat.json`.
- **Backfill:** `verify`/`build` work from published zips, so v0.6.0-alpha.3 and alpha.4 can get a compat file
  added to their existing releases. That is an upload to a public release, so it needs approval.
- **Future channels:** schema 1 works with section 4 unchanged (one `packages` entry per zip, `core_id` per
  folder). Dev builds are never published, so they get no compat file.

## 11. Card layout contract: `tau-compat.json` schema 2 (2026-10-08, built)

**Problem.** Omega keeps its own copy of the rules for what belongs on a card: which files a core owns, which are shared,
which it must build, which it must never touch, which formats a release reads. Each Tau change can make that copy wrong
without anything failing. Schema 2 moves the rules into the release itself, generated from the same files the release
is built from, and gives both projects a way to test their reading of it.

**What schema 2 adds.** Schema 2 contains every schema 1 field unchanged, plus `packages[].layout`: one entry per file
the package expects on a card.

| Role | Meaning for an installer | Today's entries (normal core) |
|---|---|---|
| `owned` | Install and replace exactly; the hash must match | `Cores/alfatreze.TAU/*`, `tau.rom` (slot 1, required), `tau-cold.bin` (6), `tau-loading.bin` (4) |
| `shared` | Install if missing; other cores may use it, so a different hash is only a warning | `Platforms/tau.json`, `Platforms/_images/tau.bin` |
| `generated` | Never in the zip; the companion builds it in `format` | `tau-library.tdb` (tau-library v1), `**/tau-art/cover_128.pal256.timg` (TIM1) |
| `user` | Never overwrite; check its `format` | `tau-assets.bin` (TAUA v1, sections, 64 KiB), media `**/*.{flac,mp3}`, `Settings/<core>/Interact/interact_persist.json` |
| `obsolete` | Remove on update | From `tools/omega_compat.json` `obsolete`, per core: `Assets/tau/alfatreze.TAU/TAU.json` (and the Diagnostic Build's copy) |

Entry fields: `path` (card-relative; `pattern: true` paths use `**` and `*.{a,b}`), `role`, `sha256` (owned/shared),
`slot` (the data slot it feeds, or null), `required` (the core's `data.json` marks the slot required), `format`
(`name`, `version`, plus `sections`/`max_bytes` for TAUA).

**How it is generated (no hand-written rules).**
- Every file in the zip is classified as follows. The core's own folders are `owned`. A file a `data.json` slot names
  in the platform `common/` folder is `owned`. `Platforms/` is `shared`. **Anything else stops the release**.
- Every slot filename that is not shipped must be listed in `tau_compat.NOT_SHIPPED` (generated or user), and every
  slot opened by name (no filename) must match `BY_EXTENSION`. A new slot nobody classified stops the release.
- Format versions come from the firmware's own checks: the library reader version in `fw/library_core.h`, the TAUA
  version in `fw/assets_core.h`, sections and the 64 KiB limit from schema 1, and the cover file name from `fw/timg.inc`.
- An `obsolete` path that the release still ships or uses stops the release.

**How both sides test their reading.**
1. **Published JSON Schema**: `docs/schemas/tau-compat.schema.json`. `tau_compat.py` validates every file it writes or
   verifies against it (a small built-in checker, no new dependency). Omega validates before acting.
2. **Versioning rule**: new keys inside a schema number are additive and must be ignored by readers. A change of
   meaning bumps `schema`, and a reader refuses a schema it does not know ("refuse, don't guess", as the firmware does
   for the library index). Schema 2 is a strict superset of 1, so Omega's schema-1 code reads it unchanged once it
   accepts the number 2.
3. **Expected card state**: `tau_compat.py check-card tau-compat.json CARD_DIR [--core ID]`. It checks that owned
   files are present with the right hash, shared files are present (a different hash is a warning), generated and user
   files are in a format this release reads (index reader version, TAUA version and size, TIM1 magic, settings JSON),
   obsolete files are gone, and the core's folders have no stray files. Omega implements the same check after install.
   The shared fixture is the test itself: an unpacked zip must check clean, and every listed mutation must be caught.
4. **Test** (`sim/test_tau_compat.py`, in `make test-host`):
   - The owned and shared entries are exactly the zip's files, with their hashes.
   - Roles and formats are checked per file.
   - The unpacked install checks clean.
   - These card mutations are caught: changed or missing ROM, stray file, index needing reader v2, TAUA v2, TAUA
     over 64 KiB, non-TIM1 cover, corrupt settings, obsolete file still present.
   - These release mutations are refused: an unclassified zip file, an unclassified data slot, an obsolete path that
     is still shipped, a schema-invalid file.

**Smoke run on the published alpha.4 zips.** The run builds, verifies, and checks an unpacked install clean, with 0
errors and 0 warnings.

**Not covered (by design).** The manifest says which format version a file must have; the formats themselves (index,
TIM1, TAUA) still rely on their reference readers and real card captures (D-I05 and the TAUA freeze note in
`CROSS_PROJECT_INTERFACE.md`). The in-zip copy (`tau-release.json`, section 5) waits for Omega's agreement because it
changes the zips.

**Follow-ups.**
- **Done 2026-10-08:** the inert `TAU.json` is no longer shipped. It was removed from `dist/` and
  `package_dev_build.py`. Both cores' copies are listed `obsolete` per core in `tools/omega_compat.json`
  (`{core_id, path}` entries).
- **Done 2026-10-08:** `tools/install_dev_core.py` uses a matching manifest. It removes obsolete files and runs the card
  check (step 3c, `docs/CARD_INSTALL_PROCEDURE.md`). A non-matching explicit `--compat` stops before writing.
- Still open on our side: `tools/package_psram_diagnostic.py` (old P2/P4 probe packager, not a release path) still
  writes an instance file.
- Omega: accept schema 2 and implement `check-card`.

**Additions after the review (2026-10-08).** These are additive keys, so the schema stays 2; details in
`RELEASE_SYSTEM_REVIEW_2026-10-08.md`:
- top-level `source {commit, dirty}`;
- `packages[].bitstream_features`;
- `format.root` (tau-library);
- `format.preserve_unknown_sections` (TAUA).

The CORE_VERSION is now evaluated at the fit's commit. Shipping a user or generated file stops the release. The shared
TAUA round-trip fixture lives in `docs/schemas/fixtures/`.

## 12. One version everywhere (owner, 2026-10-08, built)

The Pocket's Select Core row (`core.json` `version`), the splash and Info > FIRMWARE always show the exact build:
- **ROM field:** the firmware reserves a 48-byte `TAUVER:` field (`fw/player.c` `tau_ver_field`), built as the plain `APP_VER`.
  The splash (both version lines) and Info > FIRMWARE read it at run time.
- **Stamping:** packagers stamp `<SemVer>+<commit>[.dirty]` into the packaged ROM (`tools/tau_version.py`) and write the SemVer into
  `core.json`:
  - dev builds: `0.6.0-dev.385+f2d3373`;
  - releases: the tag (`make_release.py`), from a committed tree;
  - `--semver` builds: their version.
- **Check:** `tau_compat` refuses a package whose ROM stamp and `core.json` disagree, and publishes `rom_version`.
- **Splash text:** it no longer says "TAU ALPHA": that is the repository name, which read like a channel.
- **Cost:** 32 B in the 192 KB Diagnostic Build. That build has only about 150 B above its heap floor; a two-string splash line
  (about 190 B) did not fit.
- **Later:** `APP_VER` digit parsing (`APP_VER[0]`, `[2]`, `[4]` in the Check/Info export) assumes single-digit version parts and
  breaks at 0.10.
