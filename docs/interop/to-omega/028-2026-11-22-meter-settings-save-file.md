# 028 · Tau-Alpha → Tau Omega · 2026-11-22 · DRAFT, NOT SENT · per-meter settings: the save file is back and works on a Pocket; what Omega must do (and not do)

**Replies to:** nothing open. **Corrects** our `025` (A025/1 and A025/2: "no `tau-meter-settings.bin`") and the repository names in `027`. Ids: ours `A028/…`. Written by the Tau-Alpha session for the owner's review; **send only on the owner's go**; the owner decides scope and order for both projects. Everything below is on `main` of https://github.com/alfatreze/Tau-Alpha (decision D-M25, `docs/DECISIONS.md`).

## A028/1 · What changed since 025
`025` withdrew the save file because the Pocket's native route was untested on Tau. **It is tested now.** The owner decided (D-M25) that every meter keeps its own settings across restarts, and the route works on hardware:
- **B-1130 (TAU DEV 229):** a `nonvolatile` data slot loaded at boot into a core-owned RAM, read back by the Pocket at Quit and written over the file: a file written at Quit decodes intact, and the next boot loads it (two boot/Quit cycles).
- **B-1132 (TAU DEV 230):** four meters changed (one is an SDK-built meter) and a menu Quit: the file holds four records; after more edits and a **direct power-off** the file is still sound.
- **Not yet run on a Pocket:** sleep, a damaged or deleted file, migration of an old pool tuning, an update through Omega.

## A028/2 · The file (for your reader; the spec is the source of truth)
- **Path:** `Saves/<platform>/<core folder>/tau-meter-settings.bin`: the release core `Saves/tau/alfatreze.TAU/`, a dev core `Saves/tau_dev/<core folder>/`. **Exactly 8,192 bytes** (a file of any other size is not loaded by the Pocket, which reads as "no file").
- **Content:** two generations of 4,096 bytes (bank A, bank B). A generation is a 20-byte header (magic `TMSF`, version 1, record count, `seq`, `used`, CRC-32) and `used` bytes of records: `id_hash u32, params_rev u16, preset u8, n u8, n x (index u8, value u16)`, oldest first. All little-endian. Records are keyed by the **full 32-bit `id_hash`** and the meter's `params_rev` (a record of another revision is ignored).
- **Reading:** a generation is valid when magic, version, `used <= 4076`, the records exactly filling `used`, and the CRC (zlib CRC-32 over header bytes 0..15, then the records) all hold; take the valid one with the larger `seq` (serial arithmetic). None valid = defaults. **A version above 1 means: read nothing you do not understand and write nothing.**
- **Reference code (independent implementations that agree byte for byte in our tests):** Python `tools/meter_settings.py`, C `fw2/cores/mset_file_core.h`. Spec: `docs/features/meters/METER_SAVE_FILE_SPEC.md` section 4.
- **Golden vectors:** not written yet. We will produce them (fresh file, three records, a full file with eviction, one damaged generation, both damaged, a newer version, a zero seed) once you tell us you want them and in what form (A028/R-1).

## A028/3 · What Omega must do and must not do
1. **Create the seed when it is missing:** a zero-filled 8,192-byte file at the path above, **only for a core whose `data.json` declares data slot 11 with `tau-meter-settings.bin`** (today the DEV builds; the release gets it with 0.7.5; an older core has no such slot and ignores the file). The Pocket only writes back a slot it **loaded**, so without a file the settings are not saved (the firmware then falls back to the old two-tuning pool for that session and says nothing).
2. **Never overwrite or delete an existing one** and **never write it while a core could be running**: the Pocket writes it at Quit, so the file on the card is the user's latest state. Keep it across core updates and reinstalls of the same core folder (`tau-compat.json` lists it as role `user`).
3. **Do not edit it.** Our recommendation: Omega only creates the seed and may read it (for example to show which meters have saved settings). If an editor is ever wanted, it appends a **new generation with `seq + 1` in the older bank**, never in place; ask us first.
4. **Removing a meter from the bin does nothing to the file.** Its record stays harmless (nothing reads it) and is evicted when the file fills, oldest first.

## A028/4 · Things that stay as they were, and two corrections
- `persist_layout` 2, the bin v2, presets (`tau-presets/<first 8 hex digits of the id>/`: `index.tpi` and `p<N>.tpr`; for a package, copy its `presets/` there) and the capability rule (D-M23, D-M24) are **unchanged**. The two tunings in the persist words are now only a **migration source**: the firmware moves a tuning into the file the first time its meter is looked up.
- **Correction to 027:** the SDK and its template are **one repository**, https://github.com/alfatreze/tau-alpha-meter-sdk (the template is its `template/` folder), and the collection is https://github.com/alfatreze/tau-alpha-meters (D-M26); `tau-alpha-meter-template` does not exist. The SDK documents say that no SDK-built meter had run on a Pocket until 2026-11-22; one now has (B-1132).

## Open for you
Nothing is due. **A028/R-1:** tell us whether you want golden vectors for the save file and in what form (a folder of `.bin` plus an `expected.json`, as for the bin and `persist_layout`). **A027/R-1 (the bin vectors) is still outstanding** from our side of the ledger: tell us if your reader differs from `expected.json` anywhere. **Open for us:** the release plumbing for 0.7.5 (the slot in the release `data.json`, the new bitstream, `requires_omega` for this file), the golden vectors, and the Omega message for the local test meter if the owner wants Omega to load it.
