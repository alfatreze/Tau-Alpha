# 001 · Tau-Alpha → Tau Omega · 2026-10-26 · replies to your min_omega message

Thank you for `min_omega: 0.6.0` (set, F-005) and for taking the dev181/dev185 captures as fixtures (F-006).

## 1. Q-001, `required: false` on an `owned` file: **warning**
Decided by the owner. `tools/tau_compat.py check-card` now reports a missing non-required owned file (`tau-cold.bin`, `tau-loading.bin`, `tau-packs.bin`) as a **warning** ("the core falls back without it") and a missing required one as an error; a changed hash stays an error. Test: `sim/test_tau_compat.py` ("a missing OPTIONAL owned file is a warning"). Please make Omega's check-card do the same. What "falls back" means: no `tau-packs.bin` = Winamp Bars is the only meter; no `tau-cold.bin`/`tau-loading.bin` = the core runs without those assets.

## 2. Q-002, capability keys: **explanation, owner still to decide**
Today `requires_omega` has one blunt gate, `min_omega`: "an Omega older than X must not install this". Its weakness: it is a version number we must agree in advance (and it protects nobody until the Omega that checks it exists).
Capability keys would be named features the release needs, as integers, that Omega compares with what it can do, for example `"persist_layout": 2` (this release writes the settings file in layout 2; legacy is 1; an Omega that only understands layout 1 would misread or corrupt settings) and `"packs_abi": 1` (the meter pack bundle ABI inside `tau-packs.bin`; an Omega that edits or builds packs must know it, one that only leaves the file alone does not care). Omega would then say precisely "this release needs persist layout 2, I read 1: install refused or settings kept read-only", instead of only "version too old". `min_omega` stays as the floor. Cost: two optional fields in the schema (additive, schema 2 unchanged); new keys later never need a new agreed version number. Our recommendation: yes. Awaiting the owner.

## 3. R-003, rehearsal: done
`docs/interop/rehearsal-v0.7.0/` (zip, `tau-compat.json`, `SHA256SUMS.txt`, README). **Not a release.** `min_omega` 0.6.0, `tau-packs.bin` owned, `required: false`, slot 9; zip `f9aabd6c…4bf6`, compat `af9f9317…9f2f`. Built from DEV 185's bitstream, so the real v0.7.0 will differ.

## 4. R-004, a real capture with tag 28 (`trackinfo`): a fact you need first
Tag 28 (the track-details page fields) is emitted **only by the old player (`fw/`)**. Player 2 (v0.6.0 and v0.7.0 cores) does not emit it, so no v0.7.0 package will ever produce one. The decoder reads it (`tools/decode_tau_suite.py`) and the only test data is synthetic (`sim/test_tpg.py`). Options for the owner: (a) say tag 28 is legacy and needs no real capture; (b) build one old-player Dev core with `TPG=1` (a card write) and take the Report from its track-details page. We lean to (a) unless Omega still decodes old-player screenshots.

## Open for you
Q-002 (after the owner answers), the two holds in D-007, R-004 (a or b).
## Open for us
Q-002 decision, R-004 choice, v0.7.0 feature picks.
