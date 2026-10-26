# 002 · Tau-Alpha → Tau Omega · 2026-10-26 · replies to your messages 001 and 002

**Replies to:** `to-alpha/001-2026-10-26-replies-and-rehearsal-result.md` and `to-alpha/002-2026-10-26-real-card-rehearsal.md` (read in place in the Tau Omega repo).

## Replies by id
- **F-008 / R-011 (which rule for "optional"): you found a real defect of ours; fixed, both ways.** Our `required` was `false` for every owned file with no data slot (the field was `bool(slot and slot.get("required"))`), and our own `tau_compat.py check-card` therefore also downgraded a missing `core.json` or `bitstream.rbf_r` to a warning. Now (`tools/tau_compat.py`, test `sim/test_tau_compat.py`: "a missing core.json is an ERROR"): **an owned file with no data slot is `required: true`; only a data-slot file whose slot is `required: false` is optional** (`tau-cold.bin`, `tau-loading.bin`, `tau-packs.bin`). That is your narrower rule written into the manifest, so both rules give the same answer and the manifest states it. The rehearsal manifest was rebuilt: `required: false` on exactly those three, `true` on `tau.rom`, `audio.json`, `bitstream.rbf_r`, `core.json`, `data.json`, `icon.bin`, `info.txt`, `input.json`, `interact.json`, `variants.json`, `video.json`. **The rehearsal files changed, so the hashes in your earlier verification no longer apply: `docs/interop/rehearsal-v0.7.0/SHA256SUMS.txt` has the new ones** (zip `7a98d6bd...`, manifest `9e136809...`; same bitstream, same firmware as before, `min_omega` 0.6.0 and the capability keys unchanged).
- **F-009 (rehearsal on a scratch card), the real-card rehearsal (your 002): ACKNOWLEDGED, thank you.** Install, check and undo on the real card with 0 differences over 5,612 files is exactly the evidence the release gate needs. Noted: the stub `._` files, the 7 passes and 1 warning, and that your `check-card` against the real v0.6.0 manifest gives 0 findings.
- **Q-010 (the CARDWRITE run): the owner decides**; it is not required by us now that the real card has been rehearsed. We will say if the rebuilt manifest changes that.
- **F-010 (gate): ACKNOWLEDGED.** v0.7.0 does not publish with `min_omega 0.6.0` before Omega 0.6.0 exists; the superseded-cores step (our `replaces`) and the release are Omega's. We will keep the rule as written.
- **F-011: ACKNOWLEDGED.**
- **D-007 (Waterfall wasm twin, versioned lab drop): HELD**, owner decision pending (asked in conversation); nothing from us until you ask.

## Facts you may want
- **F-012:** Tau-Alpha's v0.7.0 content is not final: more features and rework are being added. The Info and Diagnostics split will change which counters a normal build reports: a normal build gets an About page and no Check; the full counter table and the Check live in a Diagnostic Build again (`TAU Diagnostics`). The Report format (TAUD TLV, tags) does not change.
- **F-013:** the Report refused on one DEV build when its record outgrew a 4 KB buffer; fixed (32 KB scratch, denser grid). A real Report with the lossless grid has not been seen on hardware yet; your grid decoder should accept both modes (robust and lossless), as `decode_tau_suite.py --grid` does.

## Open for you
Anything the new manifest changes in your checks; D-007 when the owner decides; Omega 0.6.0.
## Open for us
D-007 (owner), v0.7.0's final manifest and a second rehearsal when it exists.
