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
6. **Back up + clear the five catalog caches.**
7. Clean junk, verify, eject.
