# Session handoff 2026-10-26: Tau Omega's requests, the persist migration test, the SOUND MAP twin

`main` at `960a8429` plus this handoff's commit; **origin/main is at `92a86a85`**, so the commits after it (the Info rows `SET MIGRATED`/`SET LAYOUT`, audit B-844/B-845, this handoff) are **not pushed**. Audit **B-835..B-845**. Read `docs/handoffs/SESSION_HANDOFF_2026-10-23_METERS_STATS_PERSISTENCE_PACKS.md` for everything before (stats block, SOUND MAP, persistence by area, Waterfall maps/flow, packs in the release).

## State of the card (`/Volumes/Pock`, ejected)
The stable `alfatreze.TAU` (v0.6.0) and **`TAU DEV 185`** (`0.6.1-dev.185+f60a3780`: packs from `tau-packs.bin`, Info rows `SET MIGRATED` and `SET LAYOUT`). `Settings/alfatreze.TAU DEV 185/Interact/_core/interact_persist.json` is a COPY of DEV 181's real legacy-layout file (SHA-256 `897677c7...bd79331`), put there before DEV 185's first boot on purpose: **the hardware test of the legacy -> new persist migration is staged and not yet run** (B-845). Expected on its first boot: Report `SET MIGRATED 1`, `SET LAYOUT 1`; settings = DEV 181's (volume 73, colour 7, meter 6, Space Grotesk, its presets); second boot `SET MIGRATED 0`. Then mount the card and copy `Settings/alfatreze.TAU DEV 185/Interact/_core/interact_persist.json` into `docs/research/persist_layout/captures/` as the AFTER (the BEFORE is `dev181_interact_persist.json`) plus the Report screenshot as a fixture, and tell Omega.
**Correction made this session:** the migration was only ever host-proven. Each core id has its own settings folder, so DEV 182 started from an empty file; what hardware proved is that the NEW layout saves and restores (KB-205).

## What was done since the 10-23 handoff
* v0.7.0, not v0.6.2, is the next release (B-841: registry `since`, docs). `main` pushed twice (B-840, 2026-10-26 `92a86a85`).
* Tau Omega's review (B-838): `replaces` for a Stable TAU, registry `since` of ids 31/32 corrected (they first ship in the next release), a `retired` block for ids 24-26, `docs/features/TAU_PACKS_FORMAT.md`, stale docs fixed; Report screenshots as fixtures (B-839, `docs/research/p2_reports/`).
* Omega's second round (B-842): `tools/persist_layout.py` (Python reference, proven equal to the C code by `sim/test_persist_layout.py`), golden vectors and real captures (`docs/research/persist_layout/`), `tools/tau_packs_info.py` + the real DEV 184 bundle (`docs/research/p2_packs/`).
* SOUND MAP preview twin (B-843): the pack's own C compiled to WebAssembly (`tools/meters/preview/wasm/`, `meters/sound_map.js`, `sound_map_wasm.js` committed), command-for-command identical to the native pack over 3,120 pictures; wasm toolchain = Homebrew `llvm` + `lld` (memory `reference-wasm-toolchain`).
* DEV 184 on a Pocket (B-837): `PACK SRC 2`, all seven packs run, no underruns.
* Skill: local KB entries KB-205..KB-209 added to `~/.claude/skills/analogue-pocket-dev` (per-core settings folders, packed persist words, resumable migration, core-specific optional data slot file, wasm preview).

## Pending, in order
1. **Run the migration test** (above) and file the capture; then push `main` (owner decides).
2. **Omega:** send the reply drafted in the conversation (corrections: migration not yet hardware-proven, captures are two layouts not a before/after; ask for the `min_omega` number). When Omega names it, set `tools/omega_compat.json` `min_omega`.
3. **v0.7.0:** the owner reviews pending items and picks features and rework first; then version bump, changelog, `make_release.py --player2 --release v0.7.0 --previous release/alfatreze.TAU_0.6.1_2026-10-21.zip --previous-release v0.6.1`, the owner's approval to publish, assets vs `SHA256SUMS.txt` (rule 8; pass `--repo alfatreze/Tau-Alpha`).
4. Open, none blocking: see the Proposed list at the end of `docs/ROADMAP.md`.
5. An unknown branch `claude/zealous-turing-qe0sij` appeared on origin (not made in this session; not looked at).

## Commands that bit
`install_dev_core.py ... --no-eject --yes` keeps the card mounted so a file can be staged before ejecting (`diskutil eject /Volumes/Pock`); `cp` on the card leaves `._*` AppleDouble files: remove them. A release core needs `--allow-release`, which the safety classifier blocks unless the owner says so in chat. `make test-host-fast` (2.5 min) is the check before any commit. The lab page concatenates its scripts in file order: a module may not read another module's data at load.
