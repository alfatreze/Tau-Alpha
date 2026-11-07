# Interop ledger (open ids first)

| id | what | who answers | status |
|---|---|---|---|
| O011/1 | Lab message shape (`tau-lab-set`, `tau-lab-ready` with hashes, `tau-lab-applied`) | closed | **BUILT 2026-11-07, message 011 (`tau_host.js`, test + sandboxed-frame run)** |
| A011/2 | Name test library generator in the repository | closed | **DONE, message 011: `tools/make_name_test_library.py`** |
| A011/3 | Lab drop with the handler | closed | **DONE, message 011: `docs/interop/lab-drop/pre-0.7.0-bfe351d4/`** |
| O007/R-025 | New lab drop at or after `696ff9ad` with all seven modules, `lab-data.json`, built `meter_lab.html`, module hash/clang as manifest fields (Omega 007, 008) | closed | **DONE 2026-11-07, message 010: `docs/interop/lab-drop/pre-0.7.0-d6d43725/` (pins `d6d43725`)** |
| A010/2 | `tau-names.bin` / `tau-names.json` listed in `tau-compat.json` (generated, optional, checked) | closed | **DONE 2026-11-07, message 010 (schema enum extended)** |
| O007/Q-026 | Seeding rule for `tau-presets/` | closed | **ANSWERED, message 009: per meter folder (index.tpi present = the owner's); `check-card` now warns on a removed file (B-1008)** |
| O007/Q-027 | Slot to meter lookup | closed | **ANSWERED, message 009: confirmed (descriptor offset 32 id, 34 slot; fields for slots 0 to 6)** |
| O007/Q-031 | Lab preview of a preset | Tau-Alpha | **ACCEPTED, shape proposed in message 009 (`tau-lab-set`); not built** |
| A007/Q-034, A007/Q-035 | User preset ids; show module provenance | closed | **ANSWERED by Omega 008: no range needed, `hidden` withdrawn; provenance shown, fields requested in the drop manifest** |
| A006/Q-026 | Does Omega build the index? | closed | **ANSWERED by Omega 008: yes, byte-exact; writes both sidecar files** |
| F-027 | All seven meters have a WebAssembly module (D-M15) | closed | **FACT sent in message 007** |
| F-028 | The pinned drop `pre-0.7.0-b163ebb2` predates the modules for Bars/Scope/VU/Chladni/Waterfall | closed | **FACT, message 007**; a new drop comes with the v0.7.0 release |
| F-029 | Module `/*META{...}*/` header: wasm sha256, clang version, source hashes | closed | **FACT, message 007** |
| F-030 | Meter presets are files under `tau-presets/<meter key>/` (`index.tpi`, `p<id>.tpr`) | closed | **FACT, message 007** |
| F-031 | `tau-presets/` role `shared` (warning only on a changed hash) | closed | **FACT, message 007** |
| R-032 | Omega's preview uses the modules for every meter | Omega | **OPEN, message 007** |
| R-033 | Omega writes preset files in the F-030 format | Omega | **OPEN, message 007** |
| Q-034 | User preset id numbering (proposal: above the factory ids, never reused) | Omega + owner | **OPEN, message 007** |
| Q-035 | Show or only check the module provenance | Omega | **OPEN, message 007** |
| F-023 | The Pocket opens paths as code page 437; the firmware reads the sidecar (hardware, `TAU DEV 219`) | closed | **FACT sent in message 006** |
| R-024 | Omega writes `tau-names.json` + `tau-names.bin` when it builds a library | Omega | **OPEN, sent in message 006** |
| R-025 | Omega follows `CARD_NAME_RULES.md` and the 62-case corpus invariants | Omega | **OPEN, sent in message 006** |
| Q-026 | Does Omega build the library index itself? | Omega | **OPEN, message 006** |
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
