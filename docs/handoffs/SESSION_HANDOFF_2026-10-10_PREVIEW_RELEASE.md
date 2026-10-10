# Session handoff, 2026-10-09/10: first Preview release published and smoke-tested, branches merged (read this first)

Audit B-671..B-688 (`docs/AUDIT_TRAIL.md`). `main` pushed to origin through the release; the last log commits may be local (check `git status -sb`).

## Where things stand
- **GitHub releases:** `v0.6.0-preview.1` (pre-release, the first on the Stable / Preview / Dev scheme: `alfatreze.TAU_Preview_*.zip`, `alfatreze.TAU_Preview_Diagnostics_*.zip`, `tau-compat.json`, `SHA256SUMS.txt`), `v0.5.0 (alpha)`, `v0.3.0 (alpha)`. The 0.6.0 alpha.1 to alpha.3 releases were deleted at the owner's request (B-688); their **tags are kept**. `v0.6.0-alpha.4` was never published (its changelog section stays; everything in it is in preview.1).
- **What preview.1 is:** `main` at `3b64ef2`, bitstream `noeq-b670` seed 1 (CORE_VERSION `4D50331A`, features HALCYON LPC POLY SDRAM_BUSY, legacy EQ gone from the RTL), 192 KB / CLK66 link, ROMs stamped `0.6.0-preview.1+3b64ef2`. Halcyon is the only EQ; tempo is in the normal core; reports are the pixel grid only (QR encoder archived); softer gain dip on preset change. `tau-compat.json` names `v0.6.0-alpha.3` as `previous_release` (that release no longer exists on GitHub; the persist diff is inside the file).
- **Card (`Pock`):** `TAU` and `TAU_DIAGNOSTIC` (alpha.4, the old layout with files in `common/`), `TAU Preview` and `TAU Preview Diagnostics` (v0.6.0-preview.1, installed from the published zips; owner smoke test: everything as expected, B-686), `TAU DEV 111` (softer gain dip, Dev layout), `TAU_DEV_105` and `TAU_DEV_107` (kept for the recordings), the owner's `TAU_DEV_BARCODE_04`, `TAU_DEV_METER_14/18` (never touch).
- **Git:** `ram-diet` merged (B-674, B-676) and its branch and worktree removed; also removed: `clut-rtl`, the `ab96` worktree, `origin/ram-diet`, `origin/barcode-study`. Left: worktrees `tau-alpha-meter-builder` (owner's, unmerged) and `tau-alpha-test-720` (unmerged, 24 behind its remote). The old `.github/workflows/release.yml` is deleted (see traps).

## Done this session
- Fit `noeq-b670` collected (seed 1) and installed as `TAU_DEV_109` (aural: same as 108, no legacy EQ menu), then `TAU DEV 110` (merged main), then `TAU DEV 111` (B-681: the gain dip fades over about 16 ms, `R_GAIN_STEP` 43, restored to 149 by `gain_step_poll()`; owner: "pretty good"). Dev builds are `TAU DEV NN` on platform `tau_dev`, reading TAU's media in place (first real-card use of the Dev layout, B-680).
- `ram-diet` merged with review (audit-ID clash B-662 to B-674, stale `test-qr` target, heap baselines and `diag_cost.json` remeasured, `dist/` rebuilt with the shipped flags, B-675 chrome region cold-aware).
- Preview release built and published (B-684) after fixing a changelog-collision bug in `sim/test_tau_compat.py`; installed and smoke-tested (B-685, B-686); stray auto-attached zip removed (B-687).
- DEV 110 report codes read from the card (B-682): USER CHECK 7/7, FULL all pass except `Track changes`; CPU busy 43% at 1.00x; I2S JITTER min 1383 (pair fix holds).

## Open / next (in order)
1. **Owner:** recordings with a mono TS plug (`docs/features/CYMO_RECORDING_SCRIPT.md`; groups 0-4 and the ISP take are suspect, the chain was differential); a clean I2S JITTER read **within 60 s of playback after a boot** (the average wraps after about 64 s, KB-135); HW GAIN toggle below full volume.
2. **RTL, cosmetic:** I2S diag sum register is 32 bits (`i2s_diag_sum_r`): widen with the next fit; rename the leftover wire `eq_in_l` in `mp3_soc.v` at the same time.
3. **Not on hardware yet:** the tempo diet (`TEMPO_SLICE=1 TEMPO_RING=512`) and a lean diagnostic build (`DIAG_PRESET=slim`); `Track changes` Check failure still unexplained; tempo at 1.75x still clicks (parked).
4. **Release housekeeping:** the preview.1 release notes still say the Preview layout was not run on a Pocket (it has been: edit with `gh release edit` if wanted); a Stable `v0.6.0` release would use the same tool with a tag without a label (`make_release.py --release v0.6.0`, installs as `TAU` + `TAU Diagnostics`); heap-gap baseline in `tools/heap_gap_baseline.json` is for the shipped 192 KB link (release 8,768 B, diagnostic 1,376 B against its 1,024 B floor).
5. Gated by D-G01 (recordings): dither/quantiser scope, 16-bit default, limiter/clipper ceiling, hardware gain in releases.

## Traps found
- **A leftover GitHub workflow attaches a wrong zip to every release** (`.github/workflows/release.yml` zipped the Stable `dist/` onto the Preview tag, B-687). Removed; still: after any publish run `gh release view <tag> --json assets` and compare with `release/SHA256SUMS.txt`.
- `make_release.py` refuses a dirty tree outside `dist/` and `release/`: commit test or doc fixes before running it; it rewrites `dist/` (ROM, cold image, bitstream), so commit that afterwards and tag that commit.
- `gh release create` must be given `--repo alfatreze/Tau-Alpha` (the fork parent is the default target).
- `install_dev_core.py --compat release/tau-compat.json` works with the unpacked published zips; `--no-eject` for the first of two installs.
- The Pocket card volume is `Pock`; `CARDWRITE` is the Omega test card, never a Tau install target.
- Shell: a `cd` into `release/` persists for later commands; `sed -i` on macOS needs `-i ''`.
