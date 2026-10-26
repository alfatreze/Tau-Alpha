# Session handoff 2026-10-08: RAM diet (branch `ram-diet`, pushed, not merged)

Entry point for the next session on this work. Audit entry **B-674** (ids B-653..B-661 were claimed on `main` meanwhile). Worktree `../tau-alpha-ram-diet` (the toolchain is an ignored symlink to the main checkout's `toolchain/`). The main checkout's uncommitted Halcyon work was never touched.

## The problem and what was found
Free hot RAM on the shipped link (192 KB, CLK66, SDRAM_BUSY) was small and the guard measured the wrong link. Real figures, from linked ELFs:
| Build | Free before | Free now |
|---|---|---|
| release (tempo now in it) | 15.5 KB without tempo, 8,960 B with | 8,960 B (floor 6,144) |
| release + `TEMPO_SLICE=1 TEMPO_RING=512` | | 11,776 B |
| Diagnostic Build | 1,328 B (1,600 B after the QR removal) | 1,600 B default, 4,416 B with the tempo diet, 7,568 B `DIAG_PRESET=slim`, 8,944 B with every feature dropped |
| profile build | did not link (-1,056 B; first reported as -3,376 B, a stale stack constant in `fw/build.sh` overstated it by 2 KB, fixed) | links with the tempo diet (1,760 B) or `DIAG_DROP=stress` (1,872 B) |

## Done (all host-verified; `make test-host` exit 0 on the final commit `8054894`)
1. `tools/check_heap_gap.py` builds the shipped flags and records a non-linking build as a negative gap; baselines and snapshots in `tools/`.
2. Tempo ships in the normal release (`fw/build.sh` default; `TEMPO=0` removes it). Only the 1.75x+ click fix is parked, not tempo.
3. Tempo state diet behind switches for a Pocket A/B: `TEMPO_SLICE=1` (output hop in 64-sample slices, -1,792 B) and `TEMPO_RING=512` (-1,024 B; the largest span ever needed is 320 entries). `sim/test_tempo_slice.py`, `sim/test_tempo_funnel.py -DTEMPO_SLICE=1` (11 mutants killed).
4. QR encoder archived (`archive/qr_encoder/`, tag `archive/qr-encoder`, pushed). Pixel grid is the only report view. Tau Omega decodes QR only and cannot read reports from these builds yet (noted in `docs/features/CROSS_PROJECT_INTERFACE.md`; owner: no users yet). Report-page identifiers renamed `*qr*` to `*rep*`/`*_CODE`.
5. Diagnostic-Build features switchable at build time: `fw/diag_features.json` (17 features, `dx` diagnostics / `fx` incubating features, class, "influences", "safe to drop", dependencies, presets), `DIAG_DROP=<ids>`, `DIAG_PRESET=perf|slim|release-like`, `FEATURES_ON=<id>`; `tools/diag_cost.py` measures every row (about 12 min) into `tools/diag_cost.json`. Guide: `docs/features/DIAG_FEATURES.md`. Default builds were hash-identical (`tau.rom` + `tau-cold.bin`, five configs) after every conversion; after the rename exactly 4 bytes (the layout id) differ in both files.
6. Guards for agents: `sim/test_diag_features.py` (no new bare `#if TAU_DIAGNOSTIC`, every firmware macro registered, measurements cover every feature), `fw/build.sh` prints the cheapest features to drop under a floor, `CLAUDE.md` section 3b.
7. Mode-overlay region: proposal + host prototype only (`docs/features/MODE_OVERLAY_PROPOSAL.md`, `sim/overlay_proto/`, `sim/test_overlay_proto.py`); recommended against (2-3 KB for the bug class this project has met before). Not built.

## Owner decisions in force (do not re-litigate)
Tempo in release; ring stays 24 KB; QR archived; `PolyphaseStereo` stays hot (playback must work without the cold image); no PSRAM for tempo state (+8-12 % CPU); three-region 224 KB RAM is a real RTL project, not proposed; `cymo_toggle` is the only feature that changes the default audio path.

## Not done / next
1. **Pocket tests (nothing here has run on a Pocket):** A/B of the tempo diet (HEADROOM, UNDERRUNS at 1.00-1.75x on a mono speech MP3 and a stereo MP3) and a lean Diagnostic Build (`DIAG_PRESET=perf` or `slim`). No package was made: the Halcyon bitstream fit (`halcyon-pair-b650`) was pending and firmware must pair with its bitstream. Command shape in `docs/features/RAM_DIET_PLAN.md`; use `tools/package_dev_build.py --build-flags ...` and `tools/install_dev_core.py`.
2. **Merge to `main`:** conflicts expected on the tails of `CLAUDE.md` and `docs/AUDIT_TRAIL.md` (both append): interleave. Claim audit ids on main first. Re-run `python3 tools/check_heap_gap.py --update` and `python3 tools/diag_cost.py` after the merge (Halcyon changed the sizes).
3. Decide whether to make the tempo diet the default after the Pocket A/B, and whether the profile build joins the guard's required set.
4. `dist/` still holds a 256 KB build; the Info rows of dropped features stay as placeholders ("-"/"OFF") on purpose.

## Traps found this session
- A guard that builds a different link than ships is green and wrong (KB-130).
- `SW_REP` is the Repeat persist id: a new enum with that name collided; only the profile build compiles the decode sweep, so build every configuration after a rename.
- `make test-host` takes about 4-5 minutes; a shell `grep -c` that finds nothing exits 1 and makes a monitor wrapper report "failed" although the tests passed.
- The stale `*qr*` names are gone, but archived QR tools (`tools/decode_tau_suite.py --qr`, `tools/lab/barcode_lab.py`) still exist on purpose.
