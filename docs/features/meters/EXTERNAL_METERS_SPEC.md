# External meters: the specification (phase P1)

Status 2026-11-12 (later): **reviewed by Omega (message 021); the answers are folded in below (marked R-021/n); nothing built from it yet.** It turns section 3 of `EXTERNAL_METERS_PLAN.md` into exact formats. Gate P0 passed on a Pocket (see the plan's branch log), so the load-into-one-window design stands. Evidence labels: **code** (read in this tree), **Pocket** (measured by the owner on DEV 920), **to fix in P2** (a number or flag the firmware work decides against the real code, never guessed here).

All integers are little endian. "Window" is the 64 KiB PSRAM code window at `0x24900000` (today's pack slot 7, which P0 proved can be rewritten at run time).

## 1. The package: `<id>.tmeter` (a zip)
| File | Required | What it is |
|---|---|---|
| `meter.json` | yes | the manifest below |
| `meter.bin` | yes | the pack: a `TMPK` v2 image linked for the window (section 3) |
| `presets/index.tpi`, `presets/p<id>.tpr` | no | factory presets in the binary format the firmware already reads (`fw2/cores/mpreset_core.h`, written by `tools/player2/mpreset.py`); Omega copies them to `tau-presets/<id hash, 8 lower-case hex>/` (R-021/6) |
| `preview.png` | yes | exactly 56 x 32, at most 8 distinct colours; Omega turns it into the preview block of the bin (palette plus run-length stream), refusing a file that does not fit (R-021/5) |
| `README.md`, `LICENSE` | yes (licence) | shown by Omega before install |
| `source/` or a `source` link | recommended | the meter's source; a meter without it is marked "binary only" |

`meter.json`:
```
{ "schema": 1,
  "id": "alfatreze.winamp-scope",            lower-case, [a-z0-9.-], 3..39 chars (R-021/4), "<author>.<name>"; the identity, never a number
  "name": "Winamp Scope",                    1..23 chars of printable ASCII 0x20..0x7E (R-021/3; widened later only after the font is checked)
  "version": "1.2.0",                        semantic version of the meter itself
  "author": "...", "licence": "MIT", "homepage": "https://...",
  "abi": "1.2",                              the host-table version the pack NEEDS (not the one it was built with)
  "tau_min": "0.8.0",                        optional: oldest TAU that can run it
  "params_rev": 1,                           raised whenever the parameter set changes (section 5)
  "files": { "meter.bin": "<sha256>", "presets/x.json": "<sha256>" },     pins every file
  "signature": null }                        reserved: ed25519 over the canonical manifest; v1 never checks it
```
**Limits Omega enforces on a package (R-021/9):** a zip size cap (4 MiB), no path outside the package (no `..`, no absolute path), no file beyond this table, `meter.bin` at most the window, every file pinned in `files`; a failure is refused with the reason in plain words. Omega verifies the pins, shows author, source, licence and "a meter runs with full access to the player" before every install and every update (D-M22 trust). The firmware keeps its own integrity checks only (CRC, ABI, origin, watchdog; D-M20).

## 2. The bin v2: `Assets/tau/common/tau-meters.bin` (user bin, data slot 10) and the shipped `tau-packs.bin`
```
header (32 B)  0 'TPKB'  4 u16 version = 2  6 u16 count (<= 64)  8 u32 toc_off  12 u32 toc_len  16 u32 prev_off  20 u32 prev_len
               24 u32 packs_off  28 u32 total size.  The file's CRC-32 (of every byte after the header) is the u32 at the very end.
TOC            count x 256 B, in DISPLAY ORDER (section 4)
previews       one block per meter: 8 x 8 B palette spec + the 56 x 32 run-length stream (the descriptor's own format), <= 1 KiB each
packs          each pack as `TMPK` v2 (section 3), 4-byte aligned
```
TOC entry (256 B):
```
 0 u32 id_hash       FNV-1a 32 of the id string (the key of the settings file)
 4 char id[40]       NUL padded                      44 char name[24]  NUL padded
68 u16 ver_major  70 u16 ver_minor  72 u16 ver_patch  74 u16 abi (major<<8|minor)  76 u16 tau_min (major<<8|minor, 0 = any)
78 u16 flags  (bit0 visible, bit1 built-in fallback (Winamp Bars), bit2 binary-only)   80 u16 params_rev   82 u16 nparams  84 u16 cmds
86 u16 reserved   88 u32 max_cycles   92 u32 pack_off   96 u32 pack_size   100 u32 pack_crc   104 u32 pstate_bytes   108 u32 bss_bytes
112 u32 preview_off  116 u32 preview_len  120..255 reserved (zero)
```
- **The firmware reads the header and the TOC at boot into PSRAM** (up to 16 KB for 64 meters, 136 of each 256 bytes reserved: kept at 256 for growth, R-021/8; hot RAM is untouched, it is 16 bytes from its floor in the Diagnostic Build). The Settings list reads the rows it shows and their previews from that PSRAM copy.
- A bin is refused whole when the magic, version, `total size` or the end CRC is wrong, with the Info row naming which. Order of preference at boot: `tau-meters.bin` (valid) then `tau-packs.bin`; neither valid means Winamp Bars only, the player as it is without packs today.
- `prev_off/len`: the previous good TOC is kept inside the file by Omega's atomic write; the firmware uses it only to fall back when a pack in the new TOC fails its CRC (it then lists that meter as damaged, never crashes).
- **Version 1 bins stay readable:** a v1 bundle is mapped to a TOC built from each pack's descriptor and header, so the first v2 firmware runs old cards. An old firmware refuses a v2 bin (it checks `version`), which is why Omega's TAU-update guard (section 6) exists.

**Trust and the public catalogue (owner, 2026-11-12: "host these online and have people submit new meters"; refines R-021/1).** A meter is native code with full access to the player, so a hash pin proves only "this is the file that was listed". The catalogue is therefore built so that **what is listed has been built and reviewed by us, and Omega can prove it**:
1. **Submissions are source, never binaries.** A developer opens a pull request to `tau-alpha-meters` with the meter's source and `meter.json` (made from the template/SDK). CI builds `meter.bin` from that source with the pinned toolchain, runs the conformance suite (section 3.10 of the plan: ABI, window size, watchdog, determinism, no forbidden calls) and produces the `.tmeter`. Reviewers read source, and the binary that ships is the one CI built, not one the author uploaded.
2. **A maintainer merges after review;** only then does the entry reach `index.json` (package hash, version, author, source link, licence, ABI, `tau_min`, review date).
3. **The index is signed by the project's key** (ed25519, the catalogue-level form of the reserved `signature`): Omega ships the public key and treats an entry as a catalogue meter only if the index signature verifies. Omega still shows author, source, licence, binary-only and hashes before every install **and** every update (no silent updates); updates of catalogue meters are offered because the signature proves we reviewed that version.
4. **A meter installed from a file the user chose stays possible** and is labelled "not from the catalogue: not reviewed" with the same hash display; it never gets update notices.
5. **Staging:** the catalogue starts with the seven meters we ship (our own). Opening submissions waits for the SDK, the template and the conformance suite (plan P5), so a submission has something to build against. Author-level signatures stay reserved.
6. **Revocation:** `index.json` has a `revoked` list (id, version, reason); Omega names a revoked meter on the card and offers to hide or remove it. The firmware cannot revoke by itself.
**Identity (R-021/2):** `id_hash` must be unique among the TOC rows. Omega **refuses** to install a meter whose hash equals another installed id's hash, and the firmware may assume no two rows share one. `id` is at most 39 characters so it always has a NUL (R-021/4). The **user bin is shared** by every Tau core on the platform (TAU, TAU Diagnostics, and TAU Preview, which reads TAU's media in place) (R-021/10).

## 3. The pack v2: `TMPK`
Same 48-byte header as v1 (`TAU_PACKS_FORMAT.md`) with three changes:
- `abi` keeps its meaning (needed, not built-with). **`meter id` (u16) becomes the id hash's low 16 bits, informational only**; identity is the TOC id string.
- **`origin` is always the window address.** There is no slot, so a pack never relocates and E24 (origin mismatch) now means "not linked for the window".
- `pstate size` and `.bss size` are copied into the TOC so the loader can refuse a pack too big for the window before reading it.
The loader path is exactly what P0 proved on a Pocket: read the pack, check CRC and ABI, write it through the uncached alias, `fence.i`, zero `pstate`, run. Measured (Pocket, DEV 920, 22 swaps): flush 804 cycles, load 1.48 M cycles (22 ms) typical and 6.49 M (97 ms) worst, no dropout, no hang, no wrong code.

## 4. Order and visibility: one source
The TOC is **the** order. Now Playing X, the Settings meter list and Configure all walk TOC rows with `visible` set, in TOC order; nothing else sorts, and the `sel_index` leftover of the manifests is deleted. Omega writes the order the user chose (drag in its Library). Winamp Bars (flag bit1) cannot be hidden or removed and is the fallback when a pack is refused or a bin is missing. The meter chosen at boot is stored by id hash (section 5), so reordering or installing a meter never changes which meter is selected.

## 5. Per-meter settings: `tau-meter-settings.bin`
A nonvolatile data slot, the core creates the first file itself (KB-074: written only if loaded; KB-001: the size comes from the size table). **Data slot id and the exact flags: to fix in P2 against `json-files.md`** (core-specific or common, and whether it needs `instance.json`); the 16-entry cap of `interact.json` (KB-079) is why per-meter values leave the persist words.
```
header (16 B)  'TMST'  u16 version = 1  u16 count  u32 selected_hash (the meter showing)  u32 crc32 of the records
record (104 B) u32 id_hash  u16 params_rev  u8 preset  u8 nparams (<= 48)  u16 values[48]
```
64 records plus the header is 6,672 B. Rules: a record is used only if its `params_rev` equals the TOC's; a different revision resets that meter to its defaults (never a value in the wrong parameter), and Omega says so before an update. Unknown hashes (meters since removed) are kept untouched, so reinstalling a meter finds its settings. A bad file CRC means defaults, never a refusal to boot. The write happens at quit like the persist words today (pulling the card loses it).
`persist_layout` goes to **2**: the per-meter words (seven `MPRE`, seven `MPID`, the meter index) leave the persist words; a **one-time migration** reads them into the new file (host-proven first; hardware needs a staged legacy file, as B-845 did). Omega's mirror (`tools/persist_layout.py` twin, fixtures, golden vectors) changes: that is the interop message.

## 6. Versioning policy
- **ABI** (`major.minor`, now 1.2): a firmware supports exactly one major. Minor additions (a new host-table entry, a new `mtr_in_t` field) append, and old packs keep running. A pack declares the ABI it needs; the TOC carries it, so the list can show "needs newer TAU" greyed without loading it.
- **A major break strands third-party meters**, so it is a TAU decision with notice: Omega's **TAU-update guard** reads each installed meter's `abi` and `tau_min` from the bin, and before installing a TAU that would strand any, names them and offers to keep the old TAU, update the meters first, or hide them. This is the Omega work of P4.
- **Meter version** is semver, shown in Info and the list; Omega compares it with the catalogue. `params_rev` is separate (settings compatibility, section 5).
- **`tau-compat.json` carries the firmware's ABI** as `packs_abi` (the major, as today) **and a new `packs_abi_minor`** (R-021/2), next to `persist_layout`, which becomes 2 when this ships (R-021/7): Omega treats a card with layout 2 as read only until its mirror ships.
- A pack never needs a newer firmware silently: unmet `abi` or `tau_min` marks the row unavailable.

## 7. Ids, names, limits (fixed numbers)
64 meters; id 3..40 chars; name 1..23 chars; 48 parameters; one pack at most the window (64 KiB incl. pstate, to confirm in P2 against the largest, Layered Wave at 15.7 KB + 2.4 KB); previews 56 x 32.

## 7a. Presets and the id (R-021/6)
Today the preset folder is the decimal manifest index (`tau-presets/12/`) and the files carry that number. With string ids: the folder is the **32-bit id hash in 8 lower-case hex digits**; the `meter id` u16 inside `index.tpi` and `.tpr` is the hash's low 16 bits, informational (P2 decides whether the firmware checks it, and whether it keeps reading the old decimal folder for the seven shipped meters when the hash folder is absent; Omega moves them when it composes the user bin). The preset format itself does not change.

## 8. What P2 must decide (not guessed here)
The settings data slot id and flags; whether the TOC copy lives in the existing PSRAM state region or a new one; the Info/Report rows (`MTOC`, `MWIN`, `MSET`: registered as diagnostic features, `fw2/diag_features.json`); the list scrolling for up to 64 rows; the cost of reading previews through the PSRAM copy.
