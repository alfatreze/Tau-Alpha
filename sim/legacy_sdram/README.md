# sim/legacy_sdram: archived old SDRAM path (not part of any build)

`tau_sdram_ctrl.sv` (the controller inherited from the HarpMudd / agg23 lineage, MIT, Copyright (c) 2023 Adam Gastineau; licence text in `sdram_LICENSE` and in the file header),
`tau_sdram_arbiter.sv` and `tau_sdram_cpu_bridge.sv` were removed from `src/fpga` by decision D-T13 (2026-10-31), after the shared PHY + `sched_v` path ran on a Pocket
(TAU DEV 201, audit B-933). The files are unchanged copies of the last tree that built them; the full history is in git (`git log --follow` on the old paths).

Nothing here is read by `ap_core.qsf`, `core_game.vh` or any host-test target. They are kept only so two benches stay reproducible:

- `sim/sdram_baseline/` (baseline findings, `docs/features/SDRAM_BASELINE_FINDINGS.md`): the old controller through the chip model.
- `sim/sdram_sched_a/run_gfx.sh`, stack 1: the old-versus-new comparison of the graphics path.

Provenance: `docs/PROVENANCE.md` (the inherited controller is no longer in the build; the historical record stays). Do not copy these files back into `src/fpga`.
