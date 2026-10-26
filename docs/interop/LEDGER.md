# Interop ledger (open ids first)

| id | what | who answers | status |
|---|---|---|---|
| R-014 | Built `meter_lab.html` in the lab drop (Omega 004) | Tau-Alpha | **ACCEPTED 2026-10-26 (D-O02), message 005**; generated at the v0.7.0 release drop, not built yet |
| R-017 | `lab-data.json` (schema, themes, accents) in the lab drop (Omega 005) | Tau-Alpha | **ACCEPTED (D-O02), message 005**; same drop, not built yet |
| Q-016 | Lab frame sandbox | closed | **ANSWERED:** `allow-scripts` only; ask with an R-id before widening |
| Q-018 | How Omega chooses the meters | Tau-Alpha | **DECIDED P1 by the owner (D-O01), message 005**; manifest role change in `tau_compat.py` open for us, count-0 bundle untested |
| Q-019 | Pack library and Omega building TPKB bundles | Tau-Alpha | **YES to Omega building bundles; library asset accepted in principle, not built** (message 005) |
| Q-020 | Bundle limits | closed | **ANSWERED:** 8 slots x 64 KB, seven extra meters at most; assets limit 65,536 B is for `tau-assets.bin` only |
| Q-021 | Apply METR flags/order | Tau-Alpha | **YES, scheduled (D-O03)**; firmware not built, not in v0.7.0 unless added |
| Q-022 | METR vs a pack's own presets | closed | **ANSWERED:** METR replaces the whole set; no append |
| R-011 | Which rule for "optional" owned files | Tau-Alpha | **ANSWERED 2026-10-26 (B-876):** owned files with no data slot are `required: true`; only `tau-cold.bin`, `tau-loading.bin`, `tau-packs.bin` are optional; manifest and `tau_compat.py` aligned (Omega's F-008 found the defect); rehearsal rebuilt |
| Q-010 | A CARDWRITE rehearsal | closed | **owner: no** (not needed after the real-card rehearsal) |
| F-009b | Real-card rehearsal on Pock: install, check, undo, 0 differences (Omega 002) | closed | **CONFIRMED by Omega**; the owner decides whether a CARDWRITE run (Q-010) is wanted |
| Q-001 | `required: false` on an `owned` file: warning, not error | Tau-Alpha | **ANSWERED 2026-10-26 by owner: warning.** `tools/tau_compat.py check-card` now warns; Omega's check-card should match (message 001) |
| Q-002 | Capability keys `persist_layout`, `packs_abi` in `requires_omega` | Omega to implement the comparison | **DECIDED yes 2026-10-26, built** (`tau_compat.py`, schema, rehearsal compat file, message 001 section 2) |
| R-003 | Rehearsal manifest and zip of v0.7.0 | Tau-Alpha | **DONE 2026-10-26:** `docs/interop/rehearsal-v0.7.0/` (not a release, SHA-256 listed) |
| R-004 | One real capture containing tag 28 (`trackinfo`) | closed | **CLOSED 2026-10-26 by the owner: tag 28 is legacy (old player only), no capture planned** |
| F-005 | `min_omega` 0.6.0 | Omega | set in `tools/omega_compat.json`; gate: Omega 0.6.0 must exist before v0.7.0 publishes, else revert to 0.3.0 for that release |
| F-006 | Persist migration hardware-confirmed for one real file (DEV 181 -> DEV 185); fixtures are Omega's | closed | |
| D-007 | Waterfall/SOUND MAP wasm twin and the versioned lab drop | closed | **APPROVED 2026-10-26 by the owner (both: a pinned copy as an exception, a committed wasm binary)**; `tools/make_lab_drop.py`, first drop `docs/interop/lab-drop/pre-0.7.0-b163ebb2/`, final drop at the release (message 003) |
