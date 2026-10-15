# fw2: Player 2 firmware (work in progress)

Specification: `docs/features/PLAYER_V2_SPEC.md`. Nothing here boots yet. What exists is the **pure, host-tested logic** the spec rests on, in `cores/`:

| Core | Spec | Test |
|---|---|---|
| `cores/timeline_core.h` | sections 3 (P2, P3, P4) and 5: segments, the heard position, swap events, sample accounting | `sim/test_p2_timeline.py` (model + 9 mutants) |
| `cores/governor_core.h` | section 9.2: what the ring level buys | `sim/test_p2_governor.py` (twin + 3 mutants + a scheduling simulation) |
| `cores/counters_core.h` | principle P9, section 12.2: the counter registry that generates Info and the reports | `sim/test_p2_counters.py` (5 mutants) |

The module directories of spec section 4 (`app platform stream decode aout transport queue ui gfx settings diagnostics`) are created as each stage lands; The generic counter registry is `cores/counters_core.h`; the Player 2 table instance lives in `diagnostics/info_counters.c`. `tools/check_deps.py` enforces the dependency rule between them (`sim/test_check_deps.py`). The existing `fw/*_core.h` cores move here unchanged when a stage first needs them (spec section 4).
