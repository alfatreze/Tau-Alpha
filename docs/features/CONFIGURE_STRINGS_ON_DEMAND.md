# Configure strings on demand (draft, 2026-11-06; nothing built)

**Status:** S1 built on branch `lw-perf` (2026-11-06, B-956), host-tested, not yet on a Pocket; S2 not started. Written after B-954. Not on `docs/ROADMAP.md` (the owner sets that order). Evidence labels: the sizes below are read from the code and the DEV 210 link, host-simulated or measured where stated; nothing here has run on a Pocket.

## 1. The problem

A pack-only meter (Layered Wave, Chladni, Waterfall Grid, Sound Map, ...) carries its Configure descriptors in its own PSRAM slot. The host (`fw2/ui/meters.c`, section "the pack-only rows") copies them into **hot RAM** while that meter is edited, so the Configure page never reads PSRAM per row:

| what | where | size now |
|---|---|---|
| numeric rows (`mpd_t`: type, min, max, step, nval, when, hidden, plus two pointers) | `dyn_pd[DYN_MAX_P = 35]`, 24 B each | 840 B |
| label and choice strings | `dyn_pool[DYN_POOL = 880]` | 880 B |
| pointers to the choice strings | `dyn_vp[DYN_MAX_V = 76]`, 4 B each | 304 B |
| live values per pack-only meter | `dyn_mv[6][35]` (u16) | 420 B |

Three hard limits follow (`dyn_register` refuses a pack that exceeds one, status 42, and the meter then **disappears from the list**): 35 parameters, 880 string bytes, 76 choice values. Layered Wave uses 34 / 861 / 73. On DEV 209 it was 73 of 72 and the meter vanished (B-954), and raising the limit by 16 B failed the Player 2 link: the image is within 16 B of `_min_free` (1,024 B; the floor is never lowered, rule 5). So every future pack-only meter with a long Configure page, and every new choice value in Layered Wave, is blocked by RAM that holds text nobody reads except while one page is drawn.

## 2. The finding that makes it small

**Only one place reads the strings:** `fw2/ui/configure_view.c`, the row drawing loop (lines 85 and 88): `label = p->label` and `p->vals[val]`, and it already copies the value into a 20-byte local buffer. Everything else that touches `pd` (`cf_build`, `cf_input`, the persistence shapes in `meters.c` `shapes()`, `tm_max`, the preset reset) uses the **numeric** fields only. The descriptor in the slot already has an O(1) offset table for the choices (`md_val_off`), and `md_string` reads a string by offset, so reading one label and one choice name costs a few words of PSRAM (about 30 cycles each, a few hundred cycles a row, a few thousand cycles for a page drawn when a key is pressed; Configure is not real time).

## 3. Proposal

**S1 (the change): strings are read when a row is drawn, not cached.**
* `meters.c`: two accessors, `meters_param_label(id, char *buf, n)` and `meters_param_choice(id, val, char *buf, n)`. For a built-in meter they return the static string (no copy); for a pack-only meter they read it from the slot with `md_string`/`md_val_off` into the caller's buffer (the same `desc_rd` that `dyn_register` already uses).
* `configure_view.c`: the drawing loop calls them with two small stack buffers (24 B each, the value buffer is already there). `mpd_t.label` / `vals` stay for built-ins and are 0 for dyn rows.
* `meters.c`: delete `dyn_pool`, `dyn_vp`, the string loops in `dyn_cache_build` and in `dyn_register` (the pool/choice-count check), and the limits `DYN_POOL` and `DYN_MAX_V`. `dyn_register` keeps the parameter-count check.
* `sim/test_p2_meter_desc.py` (my B-954 check) drops the two limits it no longer needs and keeps `DYN_MAX_P`.

**Memory:** frees `dyn_pool` + `dyn_vp` = **1,184 B** of hot RAM; adds two small stack buffers (48 B, stack not heap) and roughly 100-200 B of code (the accessors; the image grows by that, which also counts against the floor). **Net about +1 KB of headroom against `_min_free`**, and the 73-of-72 class of failure goes away: a meter can have any number of choices and any label length the descriptor format allows (`MD_MAX_STR` 23, `MD_MAX_LEN` 4,096 B per descriptor). With the room, `DYN_MAX_P` can go back from 35 to 36 (+24 B), or higher if a meter needs it.

**S2 (optional, later): compact the numeric rows.** `dyn_pd` carries two pointers per row (8 B of 24) that S1 leaves unused for dyn rows: a 16 B packed row would free another 280 B. Needs `mpd_t` to change for built-ins too (it is `meter.h`, used by the generated tables), so it is a separate step, only if the headroom is still wanted.

## 4. Tests (red first, per the project's rule)

* `sim/test_p2_configure_view.py` (`sim/p2_configure_view_harness.c`) draws the Configure page of a fake meter and checks the rows' text after key presses; it does not use a real pack-only meter. **New check, red first:** a real pack-only meter (every one in `meters/`, through the real loader as `sim/test_p2_pack_only.py` does) is drawn with its Configure page and every row's label and chosen-value text is compared with the manifest, for every page position and every choice value; on the old code the check passes (the strings come from the cache), and it must keep passing after S1: the page does not change.
* `sim/test_p2_pack_only.py` / `sim/p2_dyn_register.h` (a host copy of the dyn code): follow the change; add a case with a descriptor over the old limits (more than 76 choice values, more than 880 string bytes): listed and drawn correctly now (it must FAIL on the current code).
* Link: `bash fw2/build.sh` with `PACKS=1` and `make heapcheck`-style budget: `.bss` must fall by about 1,184 B and the floor assertion stay satisfied.
* A mutant: the accessor returns the wrong choice (index off by one) or an empty label: the draw hash must change.

## 5. Risks and decisions for the owner

* **Latency:** reading PSRAM while drawing. Same read path and window as the boot-time check; a page is a few thousand cycles. A watchdog is not involved (the Configure page is the host's, not a pack's).
* **Window availability:** the PSRAM window must be up when Configure opens; it is (the pack's own tick already depends on it) and the old code also read the descriptor at that moment (`dyn_cache_build`).
* **Order:** `meters.c` is also touched by branch `lw-perf` (DYN_MAX_V/P, B-954). Do this after `lw-perf` is merged or branch from it.
* **Effort:** about half a day, host-only (no VM fit, no bitstream change), then one DEV install to look at the Configure page of Layered Wave, Chladni and Waterfall Grid.
* **Not decided:** whether to do S2, and whether to raise `DYN_MAX_P` or keep the spare RAM.
