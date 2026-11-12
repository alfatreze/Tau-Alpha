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
  "id": "9f3c1e7a52b84d06a1c9e34b7d20f518",   128 random bits as 32 lower-case hex digits, made once by the SDK (`meter new`) and never changed: the identity of the meter and of everything that follows it (owner, 2026-11-12: a large random id, no registry, no naming rules)
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
 0 u32 id_hash       the first 4 bytes of the id (its first 8 hex digits read as a number): the key of the settings file and the preset folder name
 4 char id[40]       the 32 hex digits, NUL padded (always terminated)    44 char name[24]  NUL padded
68 u16 ver_major  70 u16 ver_minor  72 u16 ver_patch  74 u16 abi (major<<8|minor)  76 u16 tau_min (major<<8|minor, 0 = any)
78 u16 flags  (bit0 visible, bit1 built-in fallback (Winamp Bars), bit2 binary-only)   80 u16 params_rev   82 u16 nparams  84 u16 cmds
86 u16 reserved   88 u32 max_cycles   92 u32 pack_off   96 u32 pack_size   100 u32 pack_crc   104 u32 pstate_bytes   108 u32 bss_bytes
112 u32 preview_off  116 u32 preview_len  120..255 reserved (zero)
```
- **The firmware reads the header and the TOC at boot into PSRAM** (up to 16 KB for 64 meters, 136 of each 256 bytes reserved: kept at 256 for growth, R-021/8; hot RAM is untouched, it is 16 bytes from its floor in the Diagnostic Build). The Settings list reads the rows it shows and their previews from that PSRAM copy.
- A bin is refused whole when the magic, version, `total size` or the end CRC is wrong, with the Info row naming which. Order of preference at boot: `tau-meters.bin` (valid) then `tau-packs.bin`; neither valid means Winamp Bars only, the player as it is without packs today.
- `prev_off/len`: the previous good TOC is kept inside the file by Omega's atomic write; the firmware uses it only to fall back when a pack in the new TOC fails its CRC (it then lists that meter as damaged, never crashes).
- **Version 1 bins stay readable:** a v1 bundle is mapped to a TOC built from each pack's descriptor and header, so the first v2 firmware runs old cards. An old firmware refuses a v2 bin (it checks `version`), which is why Omega's TAU-update guard (section 6) exists.

**Independent by default, a catalogue on top (owner, 2026-11-12: meters "fully independent, users can just create, load and share them"; this keeps D-M20, "anyone may make a pack"; it replaces the pull-request-only draft).**
1. **Anyone can make, share and load a meter with nobody's approval.** An author builds a `.tmeter` with the SDK and hosts it anywhere (a file, a GitHub release, a web page). A user installs it from a file, or from a URL or GitHub repository they give Omega. No key, no review, no catalogue entry is needed.
2. **The user sees what they are loading:** Omega shows author, source (or "binary only"), licence, the hashes and the line "a meter runs with full access to the player" before every install and every update, and labels it **independent: not reviewed**. A meter installed from a URL or repository the user gave can get update notices from that same source (the user opted in; every update is shown and confirmed, never silent).
3. **The catalogue is an optional verified tier, not the gate.** `tau-alpha-meters` lists meters that were submitted as **source** (a pull request), built by CI from that source, run through the conformance suite and reviewed; its `index.json` is signed by the project key and Omega verifies it. Omega labels those **verified** and offers updates for them. Opening submissions waits for the SDK, template and conformance suite (plan P5); it starts with our own seven meters. A `revoked` list in the index lets Omega name a meter the project withdrew (an independent meter is never revoked by us: the user decides).
4. **What the player guarantees, for any meter (D-M20, D-T15, and new requirements for P2):** CRC and ABI refused before running; a cycle limit with strikes; the hardware watchdog cuts a hang; a refused or struck meter falls back to the built-in Winamp Bars; a newly installed meter is **never selected automatically**; and **safe boot**: if the core resets or the watchdog fires while a meter was active, the next boot starts on Winamp Bars and marks that meter "crashed last time" until the user selects it again. The selected meter is only remembered after it has run cleanly for a few seconds.
5. **Honest limit:** a meter is native code with no memory protection. These guards make "does not work" and "is slow" safe. They do not make a *hostile* meter safe: it could corrupt the player's memory (a crash, caught by safe boot) and, **unverified and to be checked in P2 with a hostile test pack on the host simulator**, might reach the card through the bridge. That is why Omega warns, why hashes are shown, and why the verified tier exists for people who want it.
6. **Author-level signatures stay reserved** (`signature` in `meter.json`); when they exist, an independent meter can show "signed by <author>" without anyone reviewing it.

**Identity (owner, 2026-11-12; R-021/2):** the id is **128 random bits** (32 hex digits), generated by the author's SDK once per meter, so nobody needs a registry or a naming convention and two authors cannot clash by accident. `id_hash` is simply its first 32 bits (no hash function). The odds that two of 64 installed meters share it are about 5 in 10 million; Omega still **refuses** to install a meter whose `id_hash` equals another installed meter's (different id), and the firmware may assume no two rows share one. **A random id is not an identity proof:** anyone can copy an id into their own package. So Omega treats an update as an update only when the source (the file's author field, or the URL or repository the user gave) is the same as the installed meter's; the same id from a different source is shown as **"same id, different source"** and installed only after an explicit, separate confirmation. A fork must make a new id (`meter new --fork`). Our own seven shipped meters get fixed ids made once and listed in the collection repository. The id is exactly 32 hex digits, so it always fits with a NUL (R-021/4). The **user bin is shared** by every Tau core on the platform (TAU, TAU Diagnostics, and TAU Preview, which reads TAU's media in place) (R-021/10).

## 3. The pack v2: `TMPK`
Same 48-byte header as v1 (`TAU_PACKS_FORMAT.md`) with three changes:
- `abi` keeps its meaning (needed, not built-with). **`meter id` (u16) becomes the id hash's low 16 bits, informational only**; identity is the TOC id (32 hex digits).
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

## 6a. The signed catalogue index (text only: nothing is built until the owner keeps the branch)
- **What is signed:** the bytes of `index.json` exactly as published (UTF-8, no re-serialising), so there is no canonical form to agree on. Anyone who changes a byte invalidates the signature.
- **Signature and key:** ed25519 (standard, RFC 8032). The detached signature is the file `index.json.sig` next to the index: the 64 raw signature bytes as one line of base64. The public key is 32 raw bytes, compiled into Omega (written as 64 hex digits in docs). Any standard ed25519 signing tool works; Omega verifies with `ring` (Omega D-024, O023).
- **Key id and rotation:** `index.json` has a top-level `key_id` (the first 8 hex digits of the SHA-256 of the 32-byte public key). Omega carries a short list of known keys: the current one and the next one, so a rotation ships in a normal Omega release **before** the index switches key. An unknown `key_id` is shown as "signed with a key this Omega does not know: update Omega"; those catalogue meters are labelled **unverifiable** (not blocked, and an installed meter keeps working). A lost or leaked key is handled by a new Omega release with a new key; until then the verified labels fall back to "independent: not reviewed".
- **"Verified" means exactly** "the signed index lists this exact package hash" (and the version), nothing more; the screens never say "safe".
- **Revoked** (`revoked`: id, version, reason) applies to verified meters only; Omega shows them as revoked and offers to hide or remove them, never removes one by itself.

## 7. Ids, names, limits (fixed numbers)
**The largest bin the firmware accepts: 4,456,448 bytes (4.25 MiB)** = 64 packs of at most the 64 KiB window (4 MiB) plus the 32-byte header, 64 x 256-byte TOC entries and the preview blocks (at most 64 x 1 KiB), with room to spare. The player reads a pack from the card only when its meter is selected, so this is a file-size limit, not a RAM one; a bin above it is refused whole (`MTOC ERR`), and Omega refuses to compose one and names the meter to remove. 64 meters; id exactly 32 hex digits (128 random bits); name 1..23 chars; 48 parameters; one pack at most the window (64 KiB incl. pstate, to confirm in P2 against the largest, Layered Wave at 15.7 KB + 2.4 KB); previews 56 x 32.

## 7a. Presets and the id (R-021/6)
Today the preset folder is the decimal manifest index (`tau-presets/12/`) and the files carry that number. With the random id: the folder is the **first 8 hex digits of the id (the `id_hash`)**; the `meter id` u16 inside `index.tpi` and `.tpr` is the hash's low 16 bits, informational (P2 decides whether the firmware checks it, and whether it keeps reading the old decimal folder for the seven shipped meters when the hash folder is absent; Omega moves them when it composes the user bin). The preset format itself does not change.

## 8. What P2 must decide (not guessed here)
The settings data slot id and flags; whether the TOC copy lives in the existing PSRAM state region or a new one; the Info/Report rows (`MTOC`, `MWIN`, `MSET`: registered as diagnostic features, `fw2/diag_features.json`); the list scrolling for up to 64 rows; the cost of reading previews through the PSRAM copy.
