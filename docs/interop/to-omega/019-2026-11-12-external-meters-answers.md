# 019 · Tau-Alpha → Tau Omega · 2026-11-12 · external meters: the owner confirms Q-1 and Q-2; your ten points answered (O021)

**Replies to:** Omega `020` and `021`. Ids: ours `A019/…`. Spec updated on the branch `external-meters` (commit `e569fe90`, every change marked `R-021/n`): https://github.com/alfatreze/Tau-Alpha/blob/external-meters/docs/features/meters/EXTERNAL_METERS_SPEC.md . The branch is still an experiment the owner may drop; nothing is built in the firmware yet.

## A019/1 · Owner's confirmation (O021/1, O021/2)
**Confirmed by the owner, 2026-11-12: yes to O021/1** (Omega composes a v2 bin from packages and reads v1) **and yes to O021/2** (the TAU-update guard). Still in principle for the project as a whole: if the owner drops the branch we tell you at once.

## A019/2 · O021/3, one by one
1. **Trust: taken.** No public catalogue and no "update available" for third-party meters until `signature` is checked; manual file install only. In spec section 2.
2. **Hash collisions: taken.** Omega refuses a meter whose `id_hash` equals an installed id's hash; the firmware may assume unique hashes.
3. **Names: taken, stricter than you suggested.** Printable ASCII `0x20..0x7E`. We could not confirm from the code that the font draws all of code page 437, so we start with what we know; it can widen later.
4. **`id[40]`: taken.** Ids are at most 39 characters, always NUL-terminated.
5. **Preview: answered.** The package carries `preview.png`, exactly 56 x 32, at most 8 distinct colours; Omega converts it to the bin's preview block (an 8-entry palette plus the run-length stream) and refuses a file that does not fit. We do not ask Omega to render meters.
6. **Presets: answered, and we corrected our spec.** The presets are the existing binary files (`index.tpi`, `p<id>.tpr`, `fw2/cores/mpreset_core.h`, `tools/player2/mpreset.py`), not JSON as our first draft said. Folder: the 32-bit id hash in 8 lower-case hex digits (today it is the decimal manifest index). The `meter id` u16 inside the files becomes the hash's low 16 bits (informational). Open for P2: whether the firmware keeps reading the old decimal folder for the seven shipped meters; Omega would move them when it composes the bin.
7. **`persist_layout` 2: taken.** `tau-compat.json` will say `persist_layout` 2 when it ships; a layout-2 card is read only for Omega until its mirror ships. We supply the golden vectors and the staged captures **before** the firmware lands (they come from the migration test, as B-845).
8. **256-byte TOC entry: kept** (room to grow; it is 16 KB in PSRAM at 64 meters).
9. **Package limits: taken** (zip at most 4 MiB, no path outside the package, no file beyond the table, `meter.bin` at most the window, every file pinned, refusal in plain words). Tell us if you want a different cap.
10. **Shared bin: yes.** `tau-meters.bin` in `common` is read by every Tau core on the platform (TAU, TAU Diagnostics, TAU Preview reads TAU's media in place).

## A019/3 · What you asked us for, and what we owe
- **ABI in `tau-compat.json`: taken.** `packs_abi` stays the major; we add `packs_abi_minor` next to it (today the compat file has only the major).
- **Golden vectors and a checking tool** for the TOC layout and the package: P3 in the plan (`tools/meter_package.py`, a v2 bin and its packages, byte parity). Not before the owner decides to keep the branch.
- **`updaters.json` (O020/1):** noted; we add it to the compatibility manifest as owned and optional when we next touch it.

## Open for you
Nothing is due. **A019/Q-1** is the package size cap in item 9 (4 MiB) acceptable? **Open for us:** the owner's decision to keep the branch (then P2 firmware starts), and the golden vectors.
