# External meters: packages, one bin, lazy loading, no slots (plan)

Status 2026-11-11: **plan only, nothing built.** Owner decisions of this date are in section 1 and recorded as **D-M22** (it supersedes the cap part of D-M21). Written after a read of the pack framework (`METER_PACKS_PLAYER2.md`, `TAU_PACKS_FORMAT.md`, `fw2/ui/meters.c`, `fw2/generated/meter_registry.h`, `tools/player2/*`) and a review against the Analogue Pocket framework references (`json-files.md`, `host-target-commands.md`, KB-001, KB-074, KB-079, KB-238, KB-240). Evidence labels: **code** (read in this tree), **docs** (Analogue docs), **lead** (community-reported, to be tested), **to measure**.

**Branch (2026-11-11, owner: "do this in a worktree so I can decide whether to keep this approach"):** this work lives on branch `external-meters`, worktree `../tau-alpha-external-meters` (cut from `main` `e3293098`, which already holds the first version of this plan and D-M22). `main` does not carry any of the branch's changes. Keep = merge the branch (the audit entries of the branch are listed below because `docs/AUDIT_TRAIL.md` is not touched here, to keep the merge clean); drop = `git worktree remove ../tau-alpha-external-meters` and delete the branch (D-M22 would then be marked withdrawn on `main`).

Branch log: (2) 2026-11-11 owner decisions recorded in section 6 (user bin in `common` as `tau-meters.bin`, 64 meters, SHA-256 pins plus confirmation with signing reserved, repository `tau-alpha-meters`, Winamp Bars stays built in). (1) 2026-11-11 worktree and branch created; the instruction-cache finding and the P0 probe design added to this plan (no firmware, tool or test changes yet).

## 1. What the owner asked for (2026-11-11)
1. Load an external meter from a **zip file**, **ingest from GitHub**, and **publish the current meters separately**.
2. A **well documented framework** other developers can use to write meters.
3. **Meters need versioning** for updates; list whatever else is missing.
4. **One `tau-packs.bin`**, built and managed by **Tau Omega** (visibility, loading, updates); the packs are only ever loaded by Omega.
5. **Meters are never tied to a slot**, and a user can add a **reasonably large number**.
6. The **order in the Now Playing menu is not aligned with the order in Settings**: review.
7. Decision: **lazy loading** (only the active meter is resident; the others are read from the bin when selected).

## 2. What is true today (code)
| Fact | Where |
|---|---|
| A pack is a freestanding rv32im blob with a 48-byte header (`TMPK`: ABI, meter id, size, **origin**, CRC, entry, scratch, pstate) and a descriptor trailer (`MDSC` v3: name, parameters, presets, preview, cycle limit). | `TAU_PACKS_FORMAT.md`, `tools/player2/meter_desc.py` |
| The **origin is the slot**: `0x24900000 + slot x 64 KiB`; a pack linked for another slot is refused (E24). So a pack cannot move. | `meter_pack_core.h`, `pack_meter.py` |
| **Slot = row = identity = saved position.** "The saved meter is the table index: slots never move, new meters append." Seven rows, all used: Winamp Bars (built in, also a pack) and six pack-only meters. | `fw2/generated/meter_registry.h` |
| Settings per meter (preset position and preset id) are **persisted by row** in the 16 `interact.json` persist variables; `SF_METERS_MAX` 7, all 16 words used. The Pocket takes at most 16 shown entries (KB-079, hardware-validated). | `fw2/ui/appearance.c`, `settings_core.h`, D-M21 |
| The Settings meter list shows 7 rows without scrolling (`MLIST_VIS` 7, a static assert). | `fw2/ui/settings_view.c` |
| `tau-packs.bin` is data slot 9, **already `deferload`** (not auto-loaded), core-specific, optional; the firmware reads it by target commands at boot and copies every pack into its slot. | `tools/tau_data_slots.py`, `fw2/stream/packfile.c` |
| The bin has a small header (`TPKB`, version, count) and the packs back to back; **no outer checksum**; each pack has its own CRC, refusal code and fallback. | `TAU_PACKS_FORMAT.md` |
| Safety: CRC, per-meter cycle limit (three strikes), hardware watchdog (4 x limit). **A pack has full access** (D-M20); not a security boundary. | D-M20, D-T15 |
| ABI is versioned (major.minor, now 1.2); a pack declares the minor it needs. **No per-meter version, id string, author, licence or source** exists anywhere. | `METER_PACKS_PLAYER2.md` |
| Presets are files on the card (`Assets/tau/common/tau-presets/<meter id>/`), edited by Omega. | `METER_PRESETS_EXTERNAL.md` |
| Every meter has a WebAssembly twin (D-M15); the lab and Omega run it. | `METER_WASM_MODULES.md` |
| A developer needs the **whole repository**, the RISC-V toolchain, Python generators and the WebAssembly toolchain. `NEW_METER.md` is 27 lines; `METER_PACKS_PLAYER2.md` is partly stale (five slots, "not run on a Pocket"). | docs, `new_meter.py` |
| **Order:** Now Playing X (`meters_next`) and the Settings list (`meters_list_row`) both walk the rows in slot order, so the code I read agrees with itself. The manifests also carry a leftover **`sel_index`** from the old player (1, 0, 2, 12, 11, 0, 0) that does not match slot order. | `fw2/ui/meters.c`, `meters/*/meter.json` |

## 3. Target design

### 3.1 The meter package (`.tmeter`, a zip) : the unit of distribution
```
meter.json        id ("author.key", lowercase, the identity), key, name, version (semver), abi_needs ("1.2"), min_core, order_hint,
                  license (SPDX), authors, source (URL), cost_class, max_cmds, max_cycles, pstate_bytes, params[] with STABLE keys + params_rev,
                  files{ name: sha256 }
pack.tmpk         the pack, linked for the ONE execution window (3.3), with its descriptor trailer
thumb.json        the 56 x 32 preview (the descriptor carries it too; the TOC copy lets the player list without loading)
presets.json      factory presets (params by key)
wasm/<id>.wasm    the lab / Omega twin (D-M15)
README.md LICENSE (MIT by default, D-M11)   tests/ (golden command hashes, 3.10)
```
The schema is `tools/meters_schema.json` extended; one validator (`tools/meter_package.py`, new) is used by the build, by Omega (Rust twin in `tau-core`) and by CI. **Identity is the `id` string**, never a number; a 32-bit hash of it is the key inside the firmware.

### 3.2 The bin (v2), still one file, composed by Omega
```
header   'TPKB'  version 2  count  toc_offset  outer sha256 (or crc) of everything after the header
TOC      per meter: id hash, id string (<= 40), name, version, abi_needs, order, flags (visible), preview (palette + RLE),
         cost class, cycles, cmds, params_rev, pack offset, pack size, pack crc, pstate bytes
packs    each pack as today (TMPK header + image + descriptor)
```
- The player reads the **TOC only** at boot (a few KB for 64 meters) and lists meters from it; a pack is read when it is selected.
- **Version 1 bins stay readable** (a v1 bin is mapped to a TOC built from the descriptors at boot) so the first v2 firmware runs old cards.
- Omega writes it **atomically** (temp file, read back, hash check, swap, the previous bin kept as `.prev`), like every other card write.

### 3.3 Lazy loading: one execution window, no slots
- All packs are linked for **one fixed window** in the PSRAM code window (`0x24900000`, 64 KiB, unchanged address); only the **active** meter is resident. A pack no longer has a slot, so **no relocation is needed**: the "origin" field is the window for every pack.
- On selection (Now Playing X, Settings list, Configure, restore at boot) the firmware: reads the pack from the bin by target read (data slot 9 is already `deferload`; reads of the size a pack has, KB-238), checks CRC and ABI, writes it through the uncached alias, **flushes the instruction cache** (KB-240 is the lead: a soft-CPU cache can serve stale bytes of a bridge-written buffer), zeroes the pstate region, runs it. A refusal falls back (the built-in Winamp Bars stays as the always-present fallback).
- The 4 KB scratch and the pstate region are the window's, sized by the TOC (`pstate_bytes`); Layered Wave (15.7 KB + 2.4 KB) fits.
- **Instruction cache (code, RTL read 2026-11-11):** pack code runs from PSRAM through a **4 KiB direct-mapped instruction cache** (128 lines of 32 bytes; `tools/player2/icache_sweep.py`, `docs/PHASE_G_SPEC.md`). **The firmware has no flush today** (nothing ever rewrote a window; `PHASE_G_SPEC.md` listed "needs a `fence.i`" as an open risk). The CPU does decode a flush: `src/fpga/rtl/VexRiscv_Full.v` drives `IBusCachedPlugin_cache_io_flush` from `decode_FLUSH_ALL`, which VexRiscv sets for `fence.i` (**lead: confirm on a Pocket**; emit it as `.word 0x0000100f` or build with `zifencei`). Fallback if it does not flush: alternate between two windows and never reuse an address that is still cached, or sweep 4 KiB of dummy code.
- **To measure before building on it:** the read latency of a pack at selection, SD contention while a track streams (does a selection cause a dropout?), I-cache coherence after a swap, and the time for the first drawn tick.

### 3.4 Settings keyed by meter id, out of the persist words
- Per-meter settings (selected preset position / preset file id, and the live parameter values the user changed) move to a **nonvolatile save slot** (a small file in `Saves`, written when the core quits, as the persist words are today). Records: id hash, preset, params, `params_rev`. KB-074: a nonvolatile slot is only written back if it was loaded, so the **core creates the first file itself**; KB-001: size from the size table.
- The 16 persist words keep what is not per-meter (colour, theme, playback, last played track); `persist_layout` goes to **2** (Omega's mirror, fixtures and golden vectors change: an interop message first). A **one-time migration** reads the old per-meter words into the file.
- A parameter set change is declared by `params_rev`: same revision keeps the user's values, a different one resets that meter's values (never a wrong value in a wrong parameter), and Omega says so before an update.

### 3.5 Order and visibility: one source
- The TOC carries `order` (set by Omega, the user's choice; the package's `order_hint` is only the first guess) and `visible`. **One function** (`meters_order()`) feeds Now Playing X, the Settings list, Configure and the persisted "current meter". The leftover `sel_index` is removed from the manifests and the generators.
- The Settings meter list **scrolls** (`MLIST_VIS` stays a viewport, the static assert goes); Now Playing cycling steps through visible meters only. At least one meter (the built-in fallback) is always visible.

### 3.6 Versioning and updates
- **Per meter:** semver in the package and the TOC; Omega compares installed against available (3.8).
- **ABI:** `abi_needs` in the package; a TAU update that raises the ABI **major** makes older packs refuse (E22). Omega therefore **checks every installed meter against the new core's `packs_abi` before updating TAU** and either updates the meters, holds back the TAU update, or tells the user which meters will be off. (Today a TAU update would also overwrite `tau-packs.bin` and drop external meters: the manifest says the file is **user-composed**, to be rebuilt from Omega's library, never copied over; Omega's Q-2 becomes this key.)
- **Rollback:** the previous bin and the previous package versions are kept.
- **Pocket Sync / pupdate** replace core files from a release zip: they would overwrite the bin. Mitigation to decide (section 6): the composed bin lives beside the user's data and the core's own bin is only the default.

### 3.7 Trust (D-M20 stays true)
A pack runs with full access. The package flow therefore **shows author, source and licence, asks to confirm, and pins SHA-256** (the `files` map plus a catalogue hash for GitHub). No silent auto-update. Signing (ed25519 over `meter.json`, verified in Omega, not in the firmware) is a later option, not v1. The watchdog and strikes catch mistakes, not malice; the docs say so.

### 3.8 Ingest and publishing
- **Zip:** Omega opens a `.tmeter`, validates it (schema, hashes, ABI, licence present, preview present), shows it, adds it to its library, and recomposes the bin.
- **GitHub:** a repository whose release carries `<id>.tmeter` (release title = version, the same convention as the Pocket plugin); Omega reads the release, downloads, verifies, installs. Optional `meters-index.json` catalogue (list of ids, repositories, hashes). The unauthenticated API rate limit is planned for (a cached catalogue, a token option).
- **The shipped meters, separately:** each of the seven becomes a package built from its `meters/<key>/` folder; the release's default bin is **composed from those packages**, so the shipped set uses the same path (no second code path). They are published in one collection repository (`tau-meters`, MIT) with per-meter versions.
- **The Pocket plugin** (`tau-alpha-pocket-sync`) could install a package on a card without Omega through the same `tau-core` code (it has network access if `allowed_hosts` is set). Parked: one manager first (Omega, D-O01).

### 3.9 Developer kit and documentation
- **`tau-meter-template`** repository: a working meter, the build script, CI that builds the pack and the WebAssembly module, runs the conformance tests and attaches the `.tmeter` to a release (the plugin's release workflow is the model).
- **`tau-meter-sdk`**: the host-table and `meter.h`/`theme.h`/`gfx` headers vendored, the pack build (`pack_meter.py`), the validator and the WebAssembly build, plus a pinned toolchain script or container, so a developer does **not** need this repository or the FPGA tree.
- **Documents** (one set, replacing the overlapping ones, which are archived): *Write a meter* (tutorial from the scaffold to a Pocket), *Host API reference* (generated from `fw2/ui/meter_pack.h`), *Manifest and package reference* (generated from the schema), *ABI and versioning policy* (what bumps major and minor, what a pack must declare), *Cost and limits* (draw commands, cycles, pstate, scratch, the watchdog), *Testing* (the lab, golden vectors, rv32sim), *Packaging and publishing*, *Safety and licence*. `METER_PACKS_PLAYER2.md` is brought up to date and stays the internal design note.
- Review gate: a developer who has not seen the project builds a meter from the template using only these documents.

### 3.10 Conformance tests for third parties
The pack test (`sim/test_p2_meter_pack.py`) becomes a reusable runner: golden command hashes for a meter against its source on four scenarios (every preset, loud and quiet, pause, degrade levels, fullscreen box), the budget (commands, cycles), the refusal cases. The package carries its golden file; CI and Omega run it.

## 4. Work breakdown and gates
| Phase | What | Exit |
|---|---|---|
| **P0 spike** | A registered diagnostic feature (`P2_DIAG`, `fw2/diag_features.json`, modelled on `preset_probe.c`, cold code, counters on the Info page): on each new track it loads one window-linked pack from the bin into the free PSRAM slot 7 (`mpk_install` takes any slot address), flushes the I-cache (`fence.i`), runs one tick against stub host calls and checks the call counts against the first run, then forces the live meter to re-activate. The probe bin = the normal seven packs + seven copies linked for slot 7 with ids `0x100 + k` (the normal installer skips ids it has no slot for). Also the first write of a nonvolatile settings file (a second probe). Hot RAM is 16 bytes from its floor in the Diagnostic Build: the probe must be cold code with no buffer. | **Pocket (owner):** latency, no dropout while a track streams, a meter swap 200 times without a hang, the settings file written and read back after Quit. Kill or go. |
| **P1 spec** | Package schema, bin v2, TOC, settings file format, ABI policy; D-M22 text; interop message to Omega (TOC and package contract, `persist_layout` 2, the user-composed key replacing Q-2). | Written, reviewed by Omega. |
| **P2 firmware** | TOC reader, window loader, id-keyed settings, migration, `meters_order()`, scrolling list, order fix; host tests (rv32sim) for every refusal and the migration. | Host green, DEV core on the card (owner approval, `install_dev_core.py`). |
| **P3 tools** | `meter_package.py`, package the seven shipped meters, compose bin v2, golden tests, byte parity with Omega's composer. | The release builds from packages. |
| **P4 Omega** (their repo, their schedule) | Library, ingest (zip, GitHub), compose, update checks, visibility and order, the TAU-update guard. | Their gates. |
| **P5 SDK and docs** | Template, SDK, the document set, the cold-start review. | Reviewer builds a meter from nothing. |
| **P6 publish** | `tau-meters` collection, catalogue, a release. | Assets checked (rule 8). |

## 5. Risks (to measure, not assume)
Load latency and SD contention at selection; I-cache coherence after a swap (KB-240, lead); the window and pstate size for the largest pack; the settings write happens only on Quit, as now (pulling the card loses it, the same as the persist words); migration of old settings; Omega's golden vectors for the persist change; the ABI major policy (a break strands third-party meters); trust (D-M20); list scrolling on a 7-row design; Pocket Sync or pupdate overwriting the bin.

## 6. Decisions
**Decided by the owner, 2026-11-11:**
1. **Where the composed bin lives: `common`.** The user's bin is `Assets/tau/common/tau-meters.bin` (a new optional `deferload` data slot, id 10, no core-specific bit, so it sits beside `tau-library.tdb` and a TAU update by Omega, Pocket Sync or pupdate never overwrites it). The core's own `tau-packs.bin` stays as the shipped default. Boot order: the user's bin when present and valid (it supersedes the default entirely: Omega composes it with the shipped meters inside), else the core's. The name differs from `tau-packs.bin` on purpose (two files with one name in two folders would be a support problem). Omega marks it as a user file (the generated-file list of `tau-compat.json`).
2. **Design number: 64 meters.** The TOC is fixed-size entries (about 256 bytes each, 16 KB for 64) kept **in PSRAM, not in hot RAM** (hot RAM is 16 bytes from its floor in the Diagnostic Build); the Settings list reads only the rows it shows (names and previews are read from the PSRAM copy a window at a time); the settings file is about 6 KB at 64 meters.
3. **Trust (my decision, as asked for "a reasonable approach"):** v1 = **SHA-256 pins plus explicit confirmation**, all verified in Omega (the firmware keeps integrity checks only): the package's `files` map pins every file; a GitHub catalogue (`index.json` in `tau-alpha-meters`) pins each release's package hash; Omega shows author, source, licence and the permissions line "a meter runs with full access to the player" before every install **and every update** (no silent updates); an optional "trusted authors" list in Omega's settings only skips the wording, never the hash check. **Signing is designed for and not built:** `meter.json` reserves an optional `signature` (ed25519 over the canonical `meter.json`) so v1 packages stay valid when signing arrives; the catalogue can then mark signed releases.
4. **Collection repository: `tau-alpha-meters`** (MIT, D-M11): the seven shipped meters as packages, the catalogue `index.json`, and the shared conformance suite. The template and SDK get their own repositories (`tau-alpha-meter-template`, `tau-alpha-meter-sdk`) so a developer can start without the collection.
5. **Winamp Bars stays built in** as the always-present fallback (it can never be hidden or removed; a bin with no valid pack leaves the player exactly as it is today).

**Still open:** the exact **Now Playing against Settings order** mismatch (the code agrees: both walk the rows in slot order; I need the order you see on each screen or a screenshot). The plan removes the double ordering and the leftover `sel_index` either way.

## 7. Not in this plan
More than one meter drawing at once; a pack writing to the card; signing in the firmware; the Pocket plugin installing meters (parked); changing the draw engine or the ABI content.
