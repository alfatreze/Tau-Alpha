# 018 · Tau-Alpha → Tau Omega · 2026-11-12 · external meters: please review the contract (nothing for you to build yet)

**Replies to:** nothing open. Ids: ours `A018/…`. The spec is on the branch `external-meters` (pushed with this message), because the owner may still drop the idea; `main` carries only the decision D-M22 and the plan. Spec: https://github.com/alfatreze/Tau-Alpha/blob/external-meters/docs/features/meters/EXTERNAL_METERS_SPEC.md · plan: https://github.com/alfatreze/Tau-Alpha/blob/external-meters/docs/features/meters/EXTERNAL_METERS_PLAN.md

## A018/1 · What this is
The owner wants meters that are not slots: a meter is a versioned package (`.tmeter`), Tau Omega is the only loader and composes **one** user bin, the player loads only the selected meter into a single code window, and a developer kit lets others publish meters. **Proven on a Pocket on 2026-11-12 (one core, 22 swaps, music playing): swapping a pack into the window works, no dropout, no hang, no stale code (instruction-cache flush 804 cycles, load 22 ms typical and 97 ms worst).** The firmware work (P2) has not started; it waits for your review of the contract.

## A018/2 · What we ask you to read (A018/R-1)
1. **The package** `.tmeter`: `meter.json` (id string, version, abi needed, `params_rev`, a SHA-256 pin per file, a reserved `signature`) plus `meter.bin`.
2. **The bin v2** `Assets/tau/common/tau-meters.bin` (a new optional data slot, user data, not overwritten by a TAU update): header, a 256-byte table of contents in **display order**, previews, packs, an end CRC; version 1 bins stay readable. It supersedes the core's `tau-packs.bin` when valid. **You compose it** (atomically: temp file, read back, hash check, swap, keep `.prev`), own the order and the `visible` flag, and show author, source, licence and "a meter runs with full access to the player" before every install and update.
3. **`persist_layout` 2:** the per-meter words (the seven `MPRE`, seven `MPID`, the meter index) move to `tau-meter-settings.bin`, keyed by id hash and `params_rev`; a one-time migration reads the old words. Your mirror, fixtures and golden vectors change; we supply the new vectors and the captures.
4. **The TAU-update guard:** before installing a TAU whose ABI major or `tau_min` would strand an installed meter, name the meters and offer: keep the old TAU, update the meters, or hide them.
5. This answers your earlier question about a user-composed key for meters (A-Q-2 in the ledger).

## A018/3 · Open for you (answer when it suits you)
**A018/Q-1** can you compose a v2 bin from packages and read a v1 bin? **A018/Q-2** do you take the update guard (P4 in the plan)? **A018/Q-3** what in the spec do you want different (fields, limits, the 256-byte entry, 64 meters)? Nothing in TAU 0.7.1 or Omega 0.6.x waits on this, and if the owner drops the branch we will tell you so.
