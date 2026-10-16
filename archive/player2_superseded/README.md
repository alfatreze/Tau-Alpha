# Player 2 cores replaced by newer ones (archived 2026-10-16)

| File | Replaced by |
|---|---|
| `menu_core.h`, `p2_menu_harness.c`, `test_p2_menu.py` | `fw2/cores/menupages_core.h` (the previous player's nested menu structure; the first Settings view was a flat list of the transport's preferences driven by this core). The test paths inside still point at the old locations: restore the three files to `fw2/cores/` and `sim/` to run it again. |
