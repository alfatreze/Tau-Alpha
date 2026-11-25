# Session handoff, 2026-09-26 (late): alpha.22 on the card, two hardware blocks waiting for a fit (read this first)

Supersedes `docs/SESSION_HANDOFF_2026-09-26_ALPHA17.md` for everything below (that file is still right for the earlier decisions and traps). Detail per step:
`docs/AUDIT_TRAIL.md` B-282 .. B-292 (B-284 is the image-format session's, uncommitted; the MMIO widening is B-287).

## 1. On the Pocket card now
`TAU`, `TAU_DIAGNOSTIC` (both v0.4.0 release, untouched) and **`TAU_0_5_0_A_22`**: bitstream `beam-b267` seed 1 (rbf_r `c5e3264a...`), firmware ROM `cfa53f46...`, built with
`ART_TIMG=1` (TIM1 cover reader on, data slot 7 declared). Three albums carry `tau-art/cover_128.pal256.timg` (Nausicaa x2, Test Album). It has NOT been run yet after install.
Not on the card and packaged: nothing else (alpha.20/21 removed). Backups: `work/card-backups/`.

## 2. What is unverified on hardware (the first run of alpha.22 decides these)
| Item | Look at | If it fails |
|---|---|---|
| **TIM1 cover reader** (B-285) | Info > last row `TIM1 COVER`: `n LOADED ms E0`; cover appears instantly on those albums | `E4` = opening an absolute path in a subfolder into slot 7 does not work (the main unverified assumption); 5/6 read/mailbox. Falls back to the embedded JPEG (now decoded at 128 px) |
| **128 px layout** (B-286) | now-playing screen vs the 4x design: title/artist/album baselines, meter 122 px, toast between meter and progress, Chladni in the taller box | constants in `fw/player.c` (`ART_*`, `UI_*_Y`) |
| **Fullscreen frame rate** (B-288) | Winamp Scope/Bars and Chladni fullscreen: I removed the beam gate that capped it at about 7 figures/s (analysis, not measured) | tearing is the trade; scope erases per column |
| **Meter Configure page** | preview animates, no vanishing bars, no MODE row, scroll track | `wvcfg_*` in `fw/settingsui.inc` |
| Waterfall / Peak Dots | reverted to the original loudness-history versions | |

## 3. Pending work, in priority order
1. **Quartus fit `wave-b283`** (2 seeds, launched 2026-09-26 ~02:35, still running at the last check): hardware level + scope block (`tau_wave_meter.sv`, MMIO 0xEC-0xFC, firmware already uses it when present with a software fallback) plus the 128-slot MMIO decode. Check with `python3 tools/vm_fit.py status wave-b283` then `collect`. Expect M10K 300/308 and all four corners positive. It does NOT contain the MP3 window unit.
2. **MP3 window unit** (`tau_mp3_poly.sv`, `TAU_POLY`, MMIO 0x100-0x110, B-290..B-292): design, golden model (bit-exact vs Helix on 170 slots) and RTL (bit-exact in sim, 4 mutants killed, 4,691 clocks per stereo slot) are done and committed. **Next: firmware** (`hw_poly` probe, redirect FDCT32's 32 unique output writes to `POLY_PUSH` skipping the duplicate sample-16 write, read 32 PCM words, per-slot software fallback, a Check test), then a fit with `tools/blit_g3_poly_qsf_append.txt` (wave + wider MMIO + poly), then a soak and the real CPU-saving profile (estimate 17-19%, not measured). Expect about 4 M10K, 1-2 DSP. Stereo only.
3. After the wave block is proven on the Pocket: delete the software level/scope paths in `fw/player.c` (`meters_feed` peak scan, software scope), as was done for the spectrum cascade.
4. Cover reader follow-ups: playlist-mode tracks (only library tracks handled), add slot 7 to `docs/CROSS_PROJECT_INTERFACE.md` for Tau Omega, the darker transport strip from the design, regenerate `tools/ui_snapshot_renderer.py` fixtures to the new layout.
5. Ideas saved, not started: `docs/HARDWARE_METER_IDEAS.md` (beat detector, L/R correlation, a Chladni field evaluator = the biggest CPU relief for Chladni, which reads 100% CPU); B19 flip flags and B5 blend deliberately out (timing history); the spectrogram Waterfall and spectrum Peak Dots as extra presets.
6. Old items: 192 KB RAM shrink deferred (`docs/RAM_SHRINK_192K_PLAN.md`), firmware modularization parked, Winamp Configure page parked (may move to Tau Omega), `Track changes` Check failure, boot-restore mismatch (issue 021).

## 4. Git state
Local `main` is 3 commits ahead of `origin/main` (`a32d564` audit B-288/B-289, `623b924` MP3 window unit, `e9538f7` audit B-290..B-292); the earlier ones are pushed. **Not mine, uncommitted, do not overwrite or commit:** `tools/tau_image.py`, `tools/sync_media.py` edits, `docs/IMAGE_FORMATS.md`, `sim/test_tau_image.py`, `tools/lab/`, `docs/CHLADNI_METER_SPEC.md`, `docs/METER_MODULE_SPEC.md`, `docs/THEME_SPEC.md`, `docs/DECISIONS.md`, one-line edits in `CARD_INSTALL_PROCEDURE.md`, `CROSS_PROJECT_INTERFACE.md`, `CURRENT_STATUS.md`, `MEDIA_LIBRARY_0.4_SPEC.md`, and that session's B-284 entries in `AUDIT_TRAIL.md`/`CLAUDE.md`. To commit only my hunks of a shared file, stage a filtered blob (`git hash-object -w` then `git update-index --cacheinfo`), as done for the audit trail and CLAUDE.md.

## 5. Things learned this stretch (traps)
- **`vm_fit.py launch` stages the WORKING TREE** (tracked plus untracked src/fpga), not HEAD; a fit contains whatever RTL exists at launch. `status`/`collect` take the NAME without the seed suffix.
- **Beam gating a whole-figure draw starves the frame rate** when the caller runs once per decoded frame (26 ms): the gate was open ~19% of the time (B-288). Gate small regions, or do not gate.
- **`fb_sblit(w, h)` takes SOURCE cells** (the engine scales them, clamped at 127); passing the output size read past the plane and drew noise (B-288/B-285).
- **The Quartus/`include`:** `.svh` include files are found through `SEARCH_PATH core`; do not list them as `SYSTEMVERILOG_FILE`.
- Helix's polyphase window reads only 263 distinct words per call, but every one of a slot's 32 words is read at some age; the history is 16 slots x 32 words per channel (host-proven, `sim/test_mp3_poly_probe.py`).
- RAM read before write: the window unit's history must be zeroed on a new track (Helix's vbuf starts at 0); uninitialised M10K contents propagated x in simulation.
- Audit ids: the image session used B-284; do not reuse. Next free: B-293.
- Card installs: `tools/install_dev_core.py <pkg> --carry-from OLD --remove OLD --yes` (dry run first); it does not generate cover files: `tools/sync_media.py --core CORE --art-variants --library <album folders>` does, into the new core.

## 6. Tools
| Need | Command |
|---|---|
| Build | `bash fw/build.sh release`; diagnostic: `ART_TIMG=1 SDRAM_BUSY=1 bash fw/build.sh player-library-diagnostic-profile` |
| Package | `python3 tools/package_dev_build.py --semver 0.5.0-alpha.N --cover-slot --rbf work/diagnostics/beam-b267/ap_core_s1.rbf --rbf-sha256 092331f1...9e389` |
| Tests | `make test-host`, `make test-rtl` (includes `test-rtl-mp3-poly` and its mutation run, `test-rtl-wave-meter`) |
| Fit | `python3 tools/vm_fit.py launch NAME --append tools/blit_g3_poly_qsf_append.txt --seed 1 --seed 2` then `status NAME`, `collect NAME --seed 1` |
| Regenerate the MP3 ROM include | `python3 tools/gen_mp3_poly_rom.py --sv src/fpga/core/tau_mp3_poly_rom.svh` (a test fails if it drifts) |
