# Hardware helpers for meters: ideas saved for later review (2026-09-26)

Status: **IDEAS, none started** except the two already built (B-263 spectrum bank, B-283 level/scope block, which is prepared but not fitted).
Rule of thumb from the project's history: small streaming blocks fed from the PCM sample strobe (one comparison per clock, no long chains,
registers or a single M10K) close timing easily; anything with a wide multiply pipeline needs the retiming discipline of B-111/B-114/B-231.

| # | Idea | Helps | Cost (estimate) | Notes |
|---|---|---|---|---|
| 1 | Beat / onset detector: bass energy against a running mean, flag on a hit | Chladni mode switching, pulse effects, Copper bars | small logic, no RAM | Chladni does this in software from the spectrum today; cheap either way |
| 2 | Zero-crossing rate and spectral centroid ("brightness") | colour or motion driven by timbre | very small | |
| 3 | L/R correlation: running sums of L*R, L*L, R*R | Phase Scope, a stereo-width indicator | 1 to 3 multipliers, no RAM | multiplier count to be checked against the 66 DSP blocks (11 used) |
| 4 | Ballistics inside the spectrum bank (gain, log, attack/release per band) | frees a little CPU | small | firmware loops over 16 bands today; low value |
| 5 | Chladni field evaluator: stream the cos-product field cell colours straight into the plane | Chladni at full frame rate with CPU headroom, a larger tile | a few DSP blocks and a pipeline | the expensive part of Chladni is one multiply-add per cell per mode (CPU 100% in alpha.18). The biggest single win; a real project, timing discipline required |
| 6 | Scope decimator for the classic Oscilloscope and Phase Scope (reuse the B-283 block) | Oscilloscope resolution and CPU | none new | B-283 already produces the envelope; only firmware |

Decision pending on: which to build, and whether MMIO must be widened first (0x00 to 0xFF is now fully allocated, see the MMIO note in
`docs/MMIO_ALLOCATION.md` and the widening answer in AUDIT_TRAIL B-283).
