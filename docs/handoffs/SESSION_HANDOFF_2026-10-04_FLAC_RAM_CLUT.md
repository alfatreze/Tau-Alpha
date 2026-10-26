# Session handoff, 2026-10-04: FLAC Rice fast path, RAM headroom work, three hardware-confirmed visual fixes, CLUT RTL fix with a fit running

Read this first, then `docs/CURRENT_STATUS.md` and `docs/ROADMAP.md`. Audit ids B-559 to B-577 in `docs/AUDIT_TRAIL.md`. **`main` is pushed to origin (`21f69ae`).** The CLUT RTL fix is on the unmerged branch `clut-rtl` (`../tau-alpha-clut-rtl`, `cccc57a`) with a Quartus fit running on the VM.

## State in one paragraph

Tempo (audiobook WSOLA) was tested on a Pocket and parked (works to 1.75x, 2.00x clicks, stretcher costs 26-31% CPU, B-559). The FLAC Rice fast path (C1, 32-bit window) is merged and hardware-confirmed: on the hard 48 kHz stereo file idle went 0% to 45%, FIFO stalls 12 to 0 (B-560..562). Diagnostics RAM was analysed (the "heap gap" is unused policy margin, diagnostics cost 4.8 KB diag / 9.2 KB profile of hot RAM, most is already cold), diagnostic floors lowered to 2 KiB, `tools/ram_report.py` added and wired into `check_heap_gap.py` (B-564/565), the `.bss` audit done (B-566) and the JPEG-fallback work buffers moved to PSRAM (+3,968 B in every build, B-567: merged on the owner's order WITHOUT a Pocket test of the fallback, B-575). Three real bugs found from the owner's screenshots and fixed, all hardware-confirmed on `TAU_DEV_85`: the hardware CLUT was written one slot high (covers and previews had shifted colours; firmware start index 255, B-569/570), four CLUT-blit meter previews vanished depending on the double-buffer parity (B-571/572, host model of the engine first), and the boot splash looked bad because of a 16-colour palette (TAU2 256-colour format, B-573/574). The proper RTL fix for the CLUT (`tau_clut_wr.sv`, registered write address, firmware probes which behaviour it has) is built and host-tested on `clut-rtl` and its fit is running.

## Do next (in this order)

1. **Fit `clut-rtl-b576`** (2 seeds, launched 10:08 on the VM's clock, expected about 11:50): `python3 tools/vm_fit.py status clut-rtl-b576`, then `collect clut-rtl-b576 --seed N` for the better seed (all four corners positive; the fit adds one small module). The local `date` and the VM clock differed by hours this session: trust `vm_fit.py status`.
2. **Package and test on a Pocket**: `tools/package_dev_build.py --number 86 --rbf <collected raw rbf> --rbf-sha256 <hash> --build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1` from the `clut-rtl` worktree (the firmware has the probe), install with `tools/install_dev_core.py ... --carry-from alfatreze.TAU_DEV_85` (dry run first). Expect: covers and meter previews still right; the probe now chooses start 0 (there is no Info row for it yet: add one, or judge by the images: if the probe picked wrongly they would be shifted). Also run an OLD bitstream core (DEV 85) once more with the new firmware if convenient: it must still choose 255.
3. **Merge `clut-rtl` into `main`** only after step 2 (owner decision), then push and remove the worktree and branch.
4. **JPEG fallback on a Pocket** (owed since B-575): an album with no `tau-art` sidecar (stage by removing the folder on a test core's media copy, a card write: ask), read `LOAD MS` and the cover.
5. **Release**: the zips in `release/` (v0.6.0-alpha.2, B-563) predate the CLUT, preview, splash and art changes and were never smoke-tested; the published GitHub alpha.1 normal core does not boot (its ROM accepts only core rev 23). Rebuild with `RAM_192K=1 CLK66=1 SDRAM_BUSY=1 python3 tools/make_release.py --rbf <raw rbf> --rbf-sha256 <hash> --test` (now ships the freshly built 192 KB ROM, B-563), smoke-test BOTH cores on the card, then tag/publish with `gh ... --repo alfatreze/Tau-Alpha`. Nothing was tagged or published this session.

## On the card (Pocket, volume `Pock`)

`TAU`, `TAU_DIAGNOSTIC` (old alpha.1 release cores), `TAU_DEV_80` (FLAC Rice, old CLUT start), `TAU_DEV_83` (CLUT fix), `TAU_DEV_84` (+ preview fix), `TAU_DEV_85` (+ TAU2 splash; the newest, bitstream cymo-feed-b527 seed 1, built from `splash-tau2` BEFORE the art merge), and the owner's `TAU_DEV_METER_01/03/08` (never touch). Superseded test cores 80/83/84 can be removed with `install_dev_core.py --replace --remove` on the next install.

## Decisions and parked items

- Tempo and audiobook features: nice-to-have, parked (follow-ups in ROADMAP row 12). FLAC speed changes stay off.
- RAM headroom parked: MP3 ring (24 KB, half is slack: 4-7 KB can be freed once `RING_SIZE` is split into explicit byte constants and a ring-level diagnostic exists), tag buffer (leave), meter scratch (about 2 KB, last). All in `docs/features/RAM_BSS_AUDIT.md` section 6.
- Open issues: 023 (rare audio spike), `Track changes` Check failure, accented names.

## Traps (learned this session)

- `tools/check_heap_gap.py` rebuilds with default flags and clobbers `dist/`: `git checkout -- dist` afterwards.
- `--build-flags` takes COMMAS (`RAM_192K=1,CLK66=1,...`), never spaces.
- A `RAM_192K=1` firmware build writes to `work/ram192k/`, not `dist/` (B-333); `make_release.py` now copies it in and asserts (B-563).
- Host tests that model hardware as documented hide real bugs: the CLUT bug passed for months because the model forced the index to 0 (skill KB-093). New host models: `sim/thumb_dbuf_harness.c` (double-buffered engine with a command queue), `sim/splash_harness.c`, `tools/host/art_decode_harness.c`.
- Work in worktrees, not branch switches, in this shared checkout (`git worktree add ../tau-alpha-<name> -b <name>`); remove them after merging. `meter-builder` and `test/720` worktrees are the owner's.
- Confirm the screenshot's core before comparing: Info FREE RAM and the persist-file times under `Settings/<core>/Interact/_core/` identify it.
- A card write needs the owner's OK; installs go through `tools/install_dev_core.py` (dry run, then `--yes`).

## Files that matter

`src/fpga/core/tau_clut_wr.sv`, `sim/tb_tau_clut_wr.v` (branch `clut-rtl`); `fw/blit_probe.inc` (`clut_probe`), `fw/player.c` (`clut_start_idx_v`); `fw/settingsui.inc` (preview fix); `fw/flac.c` (`rice_run`, `rice_chunk`); `tools/ram_report.py`, `tools/ram_snapshots/`; `tools/gen_splash_asset.py`; `docs/features/DIAGNOSTICS_RESOURCE_ANALYSIS.md`, `docs/features/RAM_BSS_AUDIT.md`. Tests added: `sim/test_flac_rice_fast.py`, `sim/test_ram_report.py`, `sim/test_art_decode.py`, `sim/test_clut_contract.py`, `sim/test_thumb_dbuf.py`, `sim/test_splash_asset.py`.
