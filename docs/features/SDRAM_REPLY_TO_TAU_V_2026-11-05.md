# Tau Alpha to Tau V: `sched_v` v1.2-rc1 (2026-11-05)

DRAFT, not yet sent (the owner sends it). The tag `sdram-sched-v-v1.2-rc1` is pushed to `origin` (alfatreze/Tau-Alpha).

We built your suggested steps in your order. Step 2 ships as a candidate. Step 1 failed our replay and is not included.

**Candidate:** tag `sdram-sched-v-v1.2-rc1`, commit `f4124355479703f4a8810d0c614945efa5ce34db`.

**What changed:** one file, `src/fpga/core/tau_sdram_sched_v.sv`. The `o_row` entry is now written one cycle after the ACT, from a registered copy of the ACT's bank and row. The `sv_tm` output no longer feeds `o_row` in the same cycle. The quiet window after an ACT is 5 cycles instead of 4. The interface is unchanged, so it is a drop-in for v1.1.

**SHA-256 of the changed file:** `8c8a6a0714ed1b5c3d09deb1e7d6fa649c23783b4e363ae3cbac7c03782205ae`. The other 11 files in the manifest are byte-identical to v1.1 (`sdram-sched-v-v1.1`, commit 66687515), including `tau_sdram_sv_pq.sv`. The full table is in `docs/features/SDRAM_SCHED_V_V1_2_RC1_MANIFEST.md`.

**Evidence, all on our side:**
- **Simulation:** 78 tests and mutants pass. A new test, `stale_row`, and a new mutant, `MUT_OROW_NOWAIT`, cover the change. The mutant that keeps the old 4-cycle window survives every test. The window of 5 is a conservative choice that no test needs.
- **Replay of your six files, flat out:** all PASS. Busy % is equal to v1.1 on every file (for example clip 23.91 / 27.74 %, allbi 76.87 / 77.95 %). Scan-out underflow is 0 and the worst urgent wait is 65 to 79 cycles against the bound of 155. There are 0 data mismatches.
- **Quartus probes (standalone PHY + scheduler, two seeds):** core setup +0.189 and +0.897 ns at the slow 85C corner, Fmax 102 to 110 MHz, 3.6k ALMs, 0 DSP. v1.1's probes were +0.52 and +0.74 ns. The difference is inside the seed spread, so the probe shows no gain.

**What we cannot tell you:** the `sv_tm -> o_row` path only shows up in a whole-core fit, so your seed 6 and 8 re-fit decides whether this helps. We make no margin promise. It is not fitted in a whole core and not run on a Pocket.

**Step 1 (the `hi_c` adder out of the request path in `sv_pq`):**
- **What we did:** we built it. It passes the unit tests, and the queue matches v1.1 over 200,000 random cycles.
- **Why it is not shipped:** it fails the flat-out replay. Scan-out underflow is about 19,000 words and the worst urgent wait is 5,129 cycles against the bound of 155. We bisected it by running step 1 alone.
- **Likely cause (inference, not measured):** each new entry holds the conservative bound `1fff` for one cycle, and under continuous writes that holds scan-out reads back.
- **If you still need that path:** a tighter bound at push time for writes might work (a +1 incrementer instead of `1fff`). We have not tried it. Tell us if you want it.

Import by file with the SHA-256 check as before. Tell us your seed 6 and 8 slacks when you have them.
