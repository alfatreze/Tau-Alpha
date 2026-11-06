# Layered Wave instruction-cache analysis, 2026-11-06 (scratch tools and results)

Backs `docs/features/LAYERED_WAVE_PERF_PLAN.md`. These are the scripts as they were run on 2026-11-06, kept so the result can be repeated; they are not part of the build or the test suite and contain absolute paths of that machine (the repo root and the session scratch directory: change `R`, `S` and the paths in `runpk.sh` / `runvar.sh` before reusing them).

* `profsim2.py` - a copy of `tools/rv32sim.py` (exec'd with one line patched) that counts instructions per program counter and models the CPU's instruction cache (4 KiB, direct-mapped, 32-byte lines, only addresses at or above 0x400000 = the pack); prints instructions, misses, per-function shares, the most-missing lines and the cache indices shared by two hot lines. Usage: `python3 profsim2.py harness.elf pack.tmpk pack.elf`.
* `lwpack.c` - `tools/host/p2_pack_harness.c` with its trace replaced by one preset (`-DLW_P=n`), box (`-DLW_FS=0|1`) and frame count (`-DNF=n`); the per-tick cost is the difference between NF=13 and NF=3, divided by 10.
* `mkvariant.py` - builds a pack with another link order or another `layered_wave.inc` from a symlinked copy of the tree (`mkvariant.py REPO SCRATCH NAME TEXT_LINE [INC]`); `inc_split.inc` is the 3-line variant (`noinline` on `lw_history`, `lw_spectrum`, `lw_curve`).
* `runpk.sh`, `runvar.sh`, `table.py` - the sweep over the 16 presets and two boxes, and the table generator; `combined.md` is the result table of the plan.
Model constants: 253 cycles per line fill (`docs/PHASE_G_SPEC.md`, B-047/B-054), 1.65 cycles per instruction otherwise (`docs/HOW_IT_WORKS.md`). Not validated on a Pocket for this code: plan step P0.
