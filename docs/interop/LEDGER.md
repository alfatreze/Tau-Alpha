# Interop ledger (open ids first)

| id | what | who answers | status |
|---|---|---|---|
| Q-001 | `required: false` on an `owned` file: warning, not error | Tau-Alpha | **ANSWERED 2026-10-26 by owner: warning.** `tools/tau_compat.py check-card` now warns; Omega's check-card should match (message 001) |
| Q-002 | Capability keys `persist_layout`, `packs_abi` in `requires_omega` | owner (explained in message 001, section 2) | open: waiting for the owner's yes/no (we recommend yes) |
| R-003 | Rehearsal manifest and zip of v0.7.0 | Tau-Alpha | **DONE 2026-10-26:** `docs/interop/rehearsal-v0.7.0/` (not a release, SHA-256 listed) |
| R-004 | One real capture containing tag 28 (`trackinfo`) | owner (hardware) | open: tag 28 is emitted only by the OLD player (`fw/`), not by Player 2, so v0.7.0 never produces it; options in message 001, section 4 |
| F-005 | `min_omega` 0.6.0 | Omega | set in `tools/omega_compat.json`; gate: Omega 0.6.0 must exist before v0.7.0 publishes, else revert to 0.3.0 for that release |
| F-006 | Persist migration hardware-confirmed for one real file (DEV 181 -> DEV 185); fixtures are Omega's | closed | |
| D-007 | Waterfall wasm twin and the versioned lab drop | Omega (two decisions on its side) | held by both |
