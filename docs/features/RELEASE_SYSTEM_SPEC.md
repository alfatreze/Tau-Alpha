# Release system: naming, Pocket organisation, Tau Omega needs

Status: **investigation and proposal (2026-10-08), branch `release-system`.** Nothing built, nothing on a card.
Every claim below is marked **[DOC]** (Analogue developer docs), **[TAU]** (this repo, read today),
**[OMEGA]** (Tau Omega repo, read today) or **[TEST]** (not yet confirmed on a Pocket; section 7 lists the probe).

## 1. What the owner asked for

- Main releases: one place, several versions selectable, the Diagnostics build alongside them, no duplicated
  assets or media. Sketch: `Tau > v0.7, v0.7.1, v0.8, v0.7 Diagnostics`.
- Alpha/beta releases: separate from the main releases (risk of cold-image incompatibility to be checked).
- Dev builds: isolation and quick access first. Sketch: `Tau beta > 0.7.3`, `Tau DEV 385`.
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

With 1 fixed, alpha/beta builds can share the media library with stable without risk to stable's ROM. Whether they
**should** share the platform list is a UX choice (section 4), not a safety one.

## 4. Proposed scheme

Three channels = three platforms. Every build is its own core folder. Media lives once, in `/Assets/tau/common/`.

| Channel | Platform id / list name | Core folder (`author.shortname`) | Shown as | Media |
|---|---|---|---|---|
| Stable | `tau` / "TAU" | `alfatreze.TAU` (current stable, rolling) + optional pinned `alfatreze.TAU_0_7`, `alfatreze.TAU_0_7_1` | TAU 0.8 / TAU 0.7.1 / TAU 0.7 | own |
| Stable diagnostics | `tau` | `alfatreze.TAU_DIAG` (+ pinned `alfatreze.TAU_0_7_DIAG`) | TAU 0.8 Diagnostics | own |
| Pre-release | `tau_beta` / "TAU Beta" | `alfatreze.TAU_0_8_0_B_2`, `..._A_5`, `..._RC_1` | TAU 0.8.0-beta.2 | from `tau` via bits [25:24] |
| Dev | `tau_dev` / "TAU DEV" | `alfatreze.TAU_DEV_385` | TAU DEV 385 | from `tau` via bits [25:24] |

Notes:
- **Rolling stable folder**: `alfatreze.TAU` always holds the newest stable, so settings and the Pocket's
  recent-cores entry survive an update. A "keep this version" choice copies it to a pinned folder
  (`alfatreze.TAU_0_7`) with its own settings copy. Pinned folders are optional and Omega-managed.
- **Diagnostics moves onto platform `tau`** (today it is platform `tau_diagnostic` with duplicated media). With
  build-bound slots core-specific it can safely share media.
- **Pre-release and dev get one platform entry each** instead of one per build (today every DEV/alpha build creates
  its own platform, `tau_dev_42`, `tau_0_6_0_a_44`, ...). The list under "TAU DEV" is the dev picker; nothing new
  in the main TAU list. Shared media removes the per-install media copy in `install_dev_core.py` (the slowest step).
  Dev builds that change a shared format set `--private-library` and read core-specific copies.
- **Version strings** follow SemVer 2.0: `0.8.0`, `0.8.0-alpha.5`, `0.8.0-beta.2`, `0.8.0-rc.1`,
  dev `0.8.0-dev.385`. Written in full in `core.json` `version` (<= 31 chars **[DOC]**; B-142 showed the earlier
  disappearing-core problem was the 63-char description, not the version). This gives every build a unique,
  sortable id, removing Omega's "same date, different build" ambiguity **[OMEGA update.rs]**.
- **Dev numbers** stay monotonic and are never reused; three digits are fine (`TAU_DEV_385`, platform unchanged).
- **Zip names** keep Analogue's convention `<Author>.<Core>_<Version>_<Date>.zip`
  (`alfatreze.TAU_0.8.0_2026-11-02.zip`, `alfatreze.TAU_DIAG_0.8.0_...`, `alfatreze.TAU_BETA_0.8.0-beta.2_...`).
- **Git tags**: `v0.8.0`, `v0.8.0-beta.2`; GitHub `prerelease` true for anything with a suffix. Dev builds are never
  published to GitHub.

Owner decisions are listed in section 8.

## 5. What Tau Omega needs from a release (requirements)

From `FIRMWARE_UPDATE_SPEC.md` and `crates/tau-core/src/update.rs` **[OMEGA]**:

1. Find releases on GitHub, filter Stable/Alpha by `prerelease`, pick Normal vs Diagnostic asset by name pattern,
   verify against `SHA256SUMS.txt`, then install through its existing plan/confirm path.
2. Compare installed vs offered (it currently has to guess from `core.json` version + date).
3. Verify firmware/bitstream pairing. Finding: "a package cannot reveal its bitstream's CORE_VERSION", so it keeps
   a hand-seeded `KNOWN_BITSTREAMS` table.
4. Know which files an update replaces and which user files it must keep.

Proposed answer: a **release manifest** `tau-release.json`, inside each core folder and as a separate GitHub asset:

```json
{ "tau_release": 1,
  "channel": "stable|beta|dev", "version": "0.8.0-beta.2", "build": 412, "git": "f2d3373", "date": "2026-11-02",
  "core": "alfatreze.TAU_0_8_0_B_2", "platform": "tau_beta", "variant": "normal|diagnostic",
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

- SemVer 2.0 pre-release ordering (`alpha < beta < rc < release`), build number as a separate monotonic field
  rather than SemVer build metadata (`+385`), because Pocket/Omega compare strings and `+` is ignored in precedence.
- Release channels (stable / beta / dev) as separate install targets, as browsers and OS updaters do, so a tester can
  run stable and beta side by side.
- One immutable artefact per version with a checksum list and a machine-readable manifest next to it
  (GitHub releases + `SHA256SUMS.txt` already; manifest added).
- Analogue's own zip naming and folder layout, so manual installs and other updaters (Pocket Sync, Pocket Updater)
  keep working. Unknown files in a core folder are ignored by the Pocket **[TEST: confirm tau-release.json is
  harmless]**.

## 7. Probe before building anything (one card session, no Quartus)

Reuse the current alpha.4 bitstream and ROM; only JSON and folders change. Each item is one photo or one boot.

1. Two cores on platform `tau` (`alfatreze.TAU` and a copy `alfatreze.TAU_PROBE` with `version` `0.8.0-beta.2` and a
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

## 8. Decisions for the owner

1. Mechanism A (core per build, platform per channel) vs B (instance JSON). Recommended: A.
2. Rolling `alfatreze.TAU` for stable with optional pinned folders, vs every version its own folder. Recommended:
   rolling (settings survive updates).
3. Pre-release sharing the stable media library (via bits [25:24]) vs its own copy. Recommended: share, with the
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
