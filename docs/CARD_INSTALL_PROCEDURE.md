# SD card install procedure

The concrete checklist behind `CLAUDE.md`'s standing rule ("Ask before any SD-card write and follow the
backup / clear-catalog / verify-SHA-256 / eject procedure"). Written up 2026-09-23 (B-143) after this exact
procedure was followed for backup/verify/eject but the **clear-catalog** step was skipped for an entire
session's worth of installs (B-129 through B-142), producing a core that silently never appeared in the
Pocket's menu and two rounds of plausible-but-wrong root-causing (B-141, B-142) before the real cause was
found. Follow this list top to bottom; don't skip a step because a fix earlier in the session seems to have
already explained the symptom.

## Use the script (owner rule, 2026-09-25)

**Do not hand-type this procedure. Run `tools/install_dev_core.py`** -- it does steps 1 to 6 below and the eject,
stops on the first failed check, and refuses to touch the release cores unless told to:

```
python3 tools/install_dev_core.py work/diagnostics/tau-0_5_0_a_14/pocket \
    --carry-from alfatreze.TAU_0_5_0_A_13 --remove alfatreze.TAU_0_5_0_A_13          # dry run: prints the plan
python3 tools/install_dev_core.py <same arguments> --yes                             # writes
```

Still needs explicit approval for the write (section 0). If any step of this document changes, change the script
(and `sim/test_install_dev_core.py`, part of `make test-host`) in the same commit; the list below is the spec the
script implements, kept for reference and for the rare manual case.

## 0. Before touching the card

- Get explicit approval for the write (per-action, not assumed from an earlier approval).
- Know exactly what you're installing: the local package directory (`work/diagnostics/...` or `dist/`), its
  bitstream/ROM SHA-256 hashes, and whether it needs media/a library index.

## 1. Back up anything you're about to replace or remove

- Copy the existing `Cores/<author>.<shortname>`, `Assets/<platform>`, and `Platforms/<platform>.json` +
  `Platforms/_images/<platform>.bin` to the session scratchpad (not `/tmp`).
- Verify the backup byte-identical (`diff -rq`) **before** deleting anything from the card. A backup that
  wasn't verified isn't a backup.
- **Media is left out of a backup only when it is provably not lost** (B-542; backups had grown to 34 GB, 99% duplicate media): the core whose media
  is carried to the new core (`--carry-from`, copied and SHA-256 verified before anything is removed) and a core replaced in place (`--replace`, its
  `Assets` folder is not touched) are backed up without their audio/image files (`.mp3 .flac .wav .ogg .m4a .jpg .jpeg .png .timg` under `common/`).
  A core removed WITHOUT being carried from keeps a full backup, media included, and `--backup-media` forces a full backup. `tools/install_dev_core.py`
  does this; if you back up by hand, do the same or keep the media.

## 2. Copy the new files, verify by hash

- `Cores/<author>.<shortname>/` (bitstream.rbf_r, core.json, and the rest of the seven required JSON files),
  `Assets/<platform>/common/` (tau.rom, tau-cold.bin if present), `Assets/<platform>/<author>.<shortname>/`,
  `Platforms/<platform>.json`, `Platforms/_images/<platform>.bin`.
- `shasum -a 256` the bitstream and ROM (and cold image, if present) on the card against the local package
  copy. Every file must match exactly, not just "look about the right size."

## 3. Library index: rebuild it for THIS core, never copy an old one across (B-136)

If a new test/dev core reuses another core's media (the common "clone the media, save a re-sync" shortcut),
its `tau-library.tdb` has the **destination core's own platform folder baked into its `root` field**. Copying
an old index verbatim silently points every track open at the wrong folder -- browsing still works (names
come from strings inside the index) but nothing actually plays, with no error shown.

- Always run `tools/sync_media.py --from-core <SOURCE> --core <DEST> --library --card /Volumes/Pock` in one
  step (copies whatever media differs, then rebuilds the index against `<DEST>`'s own path).
- Optional: add `--art-variants` to the same command to also write the pre-scaled cover sidecars (`<album>/tau-art/cover_128.pal256.timg`; `docs/IMAGE_FORMATS.md`). The firmware ignores them today; they carry across `--from-core` clones unchanged. Needs Pillow and numpy.
- Verify: `tools/tau_library.py verify <path>/tau-library.tdb --root <path>` must print `OK`, and
  `tools/tau_library.py report` should show `root /Assets/<DEST platform>/common/`, not some other core's.

## 4. Respect core.json's documented field limits (B-141, B-142)

Per the `analogue-pocket-dev` skill's `references/json-files.md`: `shortname<=31` (must equal the folder's
own `author.shortname`), `description<=63`, `author<=31`, `url<=63`, `version<=31` (SemVer, pre-release
suffixes like `-alpha.1` are the documented, intended use -- **not** a source of trouble by itself).
Per `references/sd-packaging-assets.md`: **"cores also disappear from the openFPGA menu if json is
invalid"** -- a `description` over 63 characters is exactly this class of defect, and the failure mode is
silent (no error, the core just isn't in the list). `tools/package_dev_build.py` now enforces this
(truncates over-length descriptions, writes the full text to `info.txt`, the documented About-screen field)
-- if hand-editing a `core.json` instead, check these limits yourself.

## 5. Clear the Pocket's catalog cache (B-143 -- do not skip this)

The Pocket caches its core/platform list in five files under `System/` on the card:

- `core_viewby_platform.bin`
- `corelist_cache.bin`
- `cores_cache.bin`
- `platform_viewby_category.bin`
- `platforms_cache.bin`

**A new or renamed core will not appear in the menu until these are rebuilt.** Back them up (verify
byte-identical, same rule as step 1), then delete them so the Pocket regenerates them from scratch on its
next scan. This is not optional and does not get inferred from "the install looked clean" -- it is a
separate, silent failure mode from everything else on this list, and the whole reason this document exists
is that it was skipped for an entire session despite being named in `CLAUDE.md`'s own rule.

## 6. Clean up, verify, eject

- Remove AppleDouble junk (`._*`) and `.DS_Store` files `cp -a` leaves behind on the exFAT card, scoped to
  the paths you just touched -- not a broad `find` over the whole card (see the note below).
- Re-run `tools/check_tau_package.py` against the source package directory if you haven't already.
- `diskutil eject` (or the platform equivalent) before removing the card or considering the write done.

## Reading results back off the card (screenshots, persist files)

`interact_persist.json` is documented elsewhere as written only on Quit. **A screenshot
(`Memories/Screenshots/*.png`) can show the same symptom** -- confirmed 2026-09-25 (B-197): a
freshly-taken screenshot did not appear in a directory listing of the already-mounted card, nor
after a plain `diskutil eject` + physical reinsert; it only showed up after the owner properly
Quit the core back to the Pocket menu. Not confirmed whether this is the Pocket buffering the
write until Quit (same as the persist file) or a stale macOS exFAT directory-entry cache on the
already-mounted volume -- either way, if a file the owner says they just created isn't showing up,
ask them to Quit the core (not just eject/reinsert the card) before concluding anything is wrong.

## A note on card operation weight

A session in 2026-09-23 (B-141..B-143) triggered a macOS filesystem-extension (`fskit`) hang from repeated
broad `find`/`diff` traffic against the mounted card, disruptive enough that the user had to intervene in
Activity Monitor. Prefer targeted, scoped commands (a known path, a specific file) over recursive scans of
the whole card or a wide `find` when a narrower check will do.

## Order of operations, summarized

1. Approval.
2. Back up + verify what's being replaced.
3. Copy new files + verify by hash.
4. Rebuild the library index for this core specifically (if media is being reused from elsewhere).
5. Confirm `core.json`'s field limits are respected.
6. Release installs: remove the files the release manifest marks obsolete and check the card against it (step 3c below).
7. **Back up + clear the five catalog caches.**
8. Clean junk, verify, eject.

## Stale media on --replace (B-332)
`--replace` keeps the core's media, so an index that no longer matches its files (folders moved or renamed) used to survive an install: the library then showed albums twice and could not open tracks. `tools/install_dev_core.py` now verifies the existing `tau-library.tdb` against the files at step 3a (when `--carry-from` is not used, which rebuilds it anyway), rebuilds it if anything is missing, and notes album folders without a `tau-art` cover file (slow embedded-JPEG covers; `sync_media.py --art-variants` writes them). `tau-assets.bin` is placed at step 3b (`--assets`, else next to the package, else copied from the carried-from core).

## Firmware / bitstream pairing checks (B-581, B-653)

`tools/install_dev_core.py` (and `tools/package_dev_build.py`, and `make_release.py`) refuse a package whose firmware the bitstream would not run:
- **Version** (B-581): the ROM's `TAUFWPAIR` list must contain the bitstream's CORE_VERSION (a ROM built without `RAM_192K=1 CLK66=1` gives a black screen on the 192 KB bitstream).
- **Features** (B-653): the ROM's `TAUFWNEED` list (HALCYON, LPC, POLY, SDRAM_BUSY) must be among the macros the bitstream was built with. The macros come from `<rbf>.json`, written by `tools/vm_fit.py collect` (for a fit launched earlier: `collect NAME --seed N --append BUNDLE`); the packager copies it into the package as `bitstream-manifest.json`. Without a manifest the feature check is skipped with a note. A deliberate fail-safe pairing: `check_fw_bitstream_pair.py --bitstream-manifest M --allow-missing HALCYON`.

## Release manifest card check (2026-10-08, RELEASE_SYSTEM_SPEC section 11)

When a release manifest `tau-compat.json` (schema 2) describes **exactly** the package being installed, `tools/install_dev_core.py` uses it at step 3c:
- **Which manifest:** `--compat FILE`, else `<package>/../tau-compat.json`, else `release/tau-compat.json`. "Exactly" means every owned and shared file in the manifest is in the package with the same hash, and the core's own folders hold nothing else. An explicit `--compat` that does not match stops the install **before anything is written**. An auto-found one that does not match is ignored, so dev packages, which have no manifest, install as before with "card check skipped".
- **Obsolete files:** files the manifest marks `obsolete` for this core are removed. They are already in the backup when the core was replaced. Example: the inert `Assets/tau/alfatreze.TAU/TAU.json` the packagers wrote until 2026-10-08.
- **Card check** (`tau_compat.check_card`):
  - owned files must be present with their hashes;
  - shared files must be present (a different hash is a warning);
  - the library index, `tau-assets.bin`, TIM1 covers and the settings file must be in a format the release reads;
  - the core's folders must hold no stray files.
- **On an error:** the script stops with the core installed but the catalog caches **not** deleted. The Pocket keeps showing the old catalog until the problem is fixed and the script is re-run.
- **By hand:** `python3 tools/tau_compat.py check-card release/tau-compat.json /Volumes/Pock --core alfatreze.TAU`.

### Before writing, restore on failure, and tau-assets.bin (2026-10-08, review M3/H1)

- **Before anything is written**, the installer checks that the release can read the card's own files and the `--assets` file:
  - with a matching manifest: `tau-assets.bin` (unless `--assets` replaces it), TIM1 covers, and the settings file;
  - the library index is skipped here, because steps 3 and 3a rebuild it.

  A problem stops the install with the card untouched (no backup folder is even created).
- **Restore:** every file the copy will overwrite is snapshotted into `<backup>/_overwritten/`. If the copy does not
  verify, or the card check fails, the card is put back exactly (core folder from the backup, overwritten files from the
  snapshot, new files deleted), the restore is verified, and the script stops with the catalog caches untouched. Media
  carried in step 3 and a rebuilt index stay; both are additive.
- **Obsolete files** are removed only after the card check passed.
- **`tau-assets.bin` is user data.** A file found automatically (the sample next to the package, or the carried-from
  core's copy) is placed only when the card has none. `--assets FILE` replaces the card's file on purpose (it is in the
  backup when the core was replaced).
- **Dev packages** built with an RBF that has a fit manifest carry their own `tau-compat.json` next to the package, so
  the card check runs for them too.

### Core-specific build files (H4, 2026-10-08, not yet hardware-confirmed)

`tau.rom`, `tau-cold.bin` and `tau-loading.bin` now go in `Assets/<platform>/<core>/`; data slots 1, 4 and 6 are core-specific.
`common/` holds only user and generated data (media, library index, `tau-assets.bin`, covers). When a release core replaces an
old-layout one, the installer removes the stale `common/` copies after the card check passes, unless another core on the platform
still reads them from `common/`. Confirm on the first install of this layout that the core boots, the splash shows, and Info shows
the cold image loaded (spec section 7 probe, item 2).

### Removing cores, and platforms shared by several cores (B-672, 2026-10-08)

- **Remove only:** `python3 tools/install_dev_core.py --remove CORE_ID [--remove ...]` with no package. It runs a dry run unless
  `--yes` is given, then makes a verified backup, removes the cores, clears the caches and ejects. Release cores stay protected unless
  `--allow-release` is given.
- **Shared platforms:** when another core lists the same platform (several Tau builds under TAU), removing or replacing a core
  touches only `Cores/<core>` and `Assets/<platform>/<core>`. The platform's `common/` media, library index, `tau-assets.bin` and
  `Platforms/<platform>.*` are kept.
- **Exclusive platforms** (every TAU DEV build today) are still removed whole, with a full backup.
- The old behaviour would have deleted TAU's whole music folder when a second TAU core was removed.

### Dev builds share TAU's media (B-673 option a, 2026-10-08)

New dev builds (`alfatreze.TAU DEV NN`, platforms `tau_dev` + `tau`) read TAU's library index, `tau-assets.bin`, music and covers in
place: no `--carry-from`, which the installer now refuses for them. They appear under **TAU Dev** and in TAU's Select Core list. The
`TAU Dev` platform stays until the last dev build is removed. Core ids contain spaces, so quote them:
`--remove "alfatreze.TAU DEV 110"`.
