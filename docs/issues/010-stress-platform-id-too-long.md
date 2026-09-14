# 010 — Stress core platform ID exceeds Pocket limit

**Status:** Pocket catalog caches regenerated with corrected platform ID; ordinary-browser confirmation pending
**Date:** 2026-09-14
**Evidence level:** **code-review | host | Pocket pending**

## Symptom

The installed `TAU SDRAM Stress` core did not appear anywhere in the Pocket
core browser. Normal TAU and TAU SDRAM Diagnostic remained discoverable.

## Cause

The stress package used `tau_sdram_stress` as its `metadata.platform_ids`
entry and as the platform JSON/assets path. This identifier is 16 characters.
Analogue's [Platform Metadata documentation](https://www.analogue.co/developer/docs/platform-metadata)
limits platform shortnames to 15 characters. The core shortname itself,
`TAU_SDRAM_STRESS`, is 16 characters and valid under the separate 31-character
limit in [core.json documentation](https://www.analogue.co/developer/docs/core-definition-files/core-json).

## Correction

Use the 14-character internal platform ID `tau_sdram_strs`, preserving the
user-visible platform name **TAU SDRAM Stress** and the valid core folder and
shortname identity. Regenerate all references together: `core.json`,
`Platforms/tau_sdram_strs.json`, `Platforms/_images/tau_sdram_strs.bin`, and
`Assets/tau_sdram_strs/...`. Both Tau diagnostic packagers now validate the
documented 15-character rule before producing a package.

The obsolete `tau_sdram_stress` asset and platform files were preserved in
`work/diagnostics/sdram-stress/obsolete-platform-id/` before removal from the
card. The existing stress core folder now contains the corrected metadata and
remains side-by-side with the normal player and SDRAM diagnostic. Card hashes:

- `bitstream.rbf_r`: `49c1b6000fb88152ef6ee8aad4d3c4198039699f7ddb9bc0b68d3d35a2cd6340`
- `tau.rom`: `2e896ae1b647477b07446a1e554949dd8f557c2eda3d92cf1ce7a5e8f295b3de`

## Verification and next gate

Host package checks confirm the platform ID is lowercase/underscore-safe,
14 characters, and resolves to the platform metadata, image, common firmware,
and instance JSON paths. The corrected package was installed on `/Volumes/Pock`;
core JSON and firmware compare byte-for-byte with staging, and the bitstream
hash was verified. The user then performed a cold boot and still could not find
the core. Therefore the overlength platform ID was a real metadata defect but
not a sufficient explanation for the discovery failure. Do not repeat the
cold boot or rebuild the bitstream as the next diagnostic step.

**Pocket cache evidence (2026-09-14):** The mounted card contains the corrected
`core.json` and valid `Platforms/tau_sdram_strs.json`. The Pocket's
`System/cores_cache.bin` contains `TAU_SDRAM_STRESS`, and
`System/corelist_cache.bin` contains `tau_sdram_strs`, but
`System/platforms_cache.bin` still contains the obsolete `tau_sdram_stress`
entry and no `tau_sdram_strs` entry. Cache mtimes are 12:34 for core/core-list
and 12:23 for platform cache, while corrected platform JSON and artwork were
copied at 13:07. This is direct evidence of stale/inconsistent Pocket catalog
state, rather than evidence that the current package files are absent. The
user's cold boot did not refresh the platform catalog.

The user confirms `TAU_SDRAM_STRESS` is visible in **Tools > Developer >
Builds** but absent from the ordinary openFPGA browser, and subsequently
launched it successfully from Builds. A playlist and MP3 are now present in
the stress platform assets for workload testing.

**Cache recovery action (2026-09-14):** Backed up and byte-verified the five
Pocket catalog/index files in
`work/diagnostics/sdram-stress/pocket-cache-backup-2026-09-14/System/`, then
removed only their originals from `/System` so Pocket can regenerate the
catalog. No core, asset, music, save, or other card files were touched.
After the user exited the core and remounted the card, all five cache/index
files were regenerated. `platforms_cache.bin`, `corelist_cache.bin`,
`core_viewby_platform.bin`, and `platform_viewby_category.bin` now contain
`tau_sdram_strs` and no longer contain the obsolete ID. `cores_cache.bin`
still contains `TAU_SDRAM_STRESS`. The playlist and all 26 MP3s remain present
under `Assets/tau_sdram_strs/common`.

**Next gate:** Confirm TAU SDRAM Stress now appears in the ordinary openFPGA
Media Players browser. The catalog rebuild succeeded at the file/cache level;
if it remains absent, restore the archived caches only after inspection. Then
continue the workload per
[SDRAM contention diagnostic](../SDRAM_CONTENTION_DIAGNOSTIC.md). Do not change
manifest fields or rebuild the bitstream without new evidence.
