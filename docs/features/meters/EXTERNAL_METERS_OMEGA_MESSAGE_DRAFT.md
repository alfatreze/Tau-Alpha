# DRAFT, not sent: Tau-Alpha -> Tau Omega, external meters (to become `docs/interop/to-omega/018-...` on main if the owner keeps this branch)

**Status:** the branch `external-meters` is an experiment the owner may drop. Send this only after the owner decides to continue; until then Omega needs to do nothing.

## What we ask Omega to review (A018)
1. **A018/1 The contract** in `docs/features/meters/EXTERNAL_METERS_SPEC.md`: the `.tmeter` package (`meter.json`, `meter.bin`), the bin v2 (header, 256-byte TOC in display order, previews, packs, end CRC, previous TOC kept), the user bin `Assets/tau/common/tau-meters.bin` (data slot 10) that supersedes the core's `tau-packs.bin`.
2. **A018/2 Omega composes** the user bin (atomically: temp file, read back, hash check, swap, `.prev`), owns the order and the `visible` flag, pins every package file by SHA-256 and shows author, source, licence and the access wording before every install and update.
3. **A018/3 `persist_layout` 2** (per-meter words move to `tau-meter-settings.bin`, keyed by id hash and `params_rev`): fixtures and golden vectors change; we will supply them.
4. **A018/4 The TAU-update guard:** before installing a TAU whose ABI major or `tau_min` would strand an installed meter, Omega names the meters and offers to keep the old TAU, update the meters or hide them.
5. **A018/5 Q-2 is answered by this:** the user-composed key replaces the question of one shared key for meters.

## Open for Omega
A018/Q-1 can Omega compose a v2 bin from packages (and read a v1 bin)? A018/Q-2 does it take the guard? A018/Q-3 what changes in its schedule? Nothing in TAU 0.7.1 or Omega waits on this.
