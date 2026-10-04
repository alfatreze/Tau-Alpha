# Loadable meter packs: prototype (2026-10-03)

Goal: a large meter library lives in Tau Omega; the card carries only the meters the owner chose; a meter that is not on the card costs nothing resident
(`METER_COST_MEASUREMENT.md`, `METER_GAP_AUDIT.md`). This is the first working slice: **Layered Wave built as a pack, loaded through the firmware's portable loader
and run on the RISC-V simulator, drawing exactly what the built-in meter draws.** Nothing here is wired into the player yet and none of it has run on a Pocket.

## Design

- **A pack is freestanding code linked at a fixed address** (its slot in the PSRAM code window, the window the cold image already executes from). It carries its own compiler
  helpers (`-lgcc`, `memcpy`, `memset`) and **names no firmware symbol**. It reaches the firmware only through `mtr_host_api_t` (`fw/meter_pack.h`): the rectangle fill, the cycle
  counter, the PSRAM-ready check, the accent, the theme roles, its setting values, the repaint flag. Its input is the existing `mtr_in_t`.
- **Compatibility is an ABI number, not a ROM layout id.** Because the pack never refers to a ROM address, a pack works with any firmware that speaks the same ABI, however the ROM changes. That
  is the opposite of the cold image (bound to the ROM's exact layout) and is what makes a library shippable by Omega once rather than once per firmware release. `MTR_PACK_ABI` must be bumped when
  `mtr_in_t` or the host table changes; `sim/test_meter_pack.py` fails until the tracked fingerprint is updated, so it cannot be forgotten.
- **Where a pack's memory lives** (the on-chip scratch area, added in the second step). Code and read-only data stay in the pack's PSRAM slot. The meter's **working state** (`.data` and `.bss`, touched
  every frame) is linked at a small on-chip **meter scratch** area shared by whichever meter is active; the loader copies `.data` there and zeroes `.bss`. State that is only touched a word at a time
  (the history ring, declared `MTR_PSRAM`) stays in the slot (`.pstate`). So only the active meter's working state is resident in on-chip RAM, and switching meters reuses the same bytes.
- **File format** (`.tmpk`, `fw/meter_pack_core.h`): 48-byte header (magic, ABI, meter id, image size, slot origin, entry offset, CRC-32, `.data` offset and size, scratch origin, `.bss` size, pstate offset and
  size) then the image. The loader refuses, in this order: bad header (E21), wrong ABI (E22), wrong meter (E23), linked for another slot (E24), a wrong, truncated or over-long size or a region outside the slot (E25),
  CRC mismatch (E26), entry outside the image (E27), scratch origin the firmware does not use (E28), working state larger than the scratch area (E29); a missing file is E20. A refused pack is never executed and
  the player stays on its built-in meters.
- **Source stays the meter's own.** `fw/meter_pack_layered_wave.c` includes `fw/layered_wave.inc` unchanged and redirects its handful of firmware services to the host table with macros. Building a pack:
  `python3 tools/pack_meter.py layered_wave --org 0x24A00000 --out layered_wave.tmpk`.

## What is proven (host only)

| Check | Result |
|---|---|
| Pack builds with no undefined symbols; links at the simulator slot and at a PSRAM window address | 11,724 B image in the slot, 2,424 B history ring in the slot, **808 B working state in scratch**; 11,776 B file |
| Loaded by the firmware loader and run on `tools/rv32sim.py` over 3 scenarios (48 frames each, paused and silent stretches, forced repaints) | command counts and hashes identical to the in-tree meter |
| Refusal matrix (13 corruptions, including a wrong scratch origin and an oversize working state) | each refused with the expected code, nothing run |
| ABI fingerprint | tracked; changing `mtr_in_t` without a bump fails the test |

For comparison, the built-in Layered Wave is 10,608 B of cold image, 810 B of hot RAM and 2,424 B of PSRAM state (13.8 KB in total); the pack is 15.0 KB (11.7 + 2.4 + 0.8) because it carries its own helpers.
The 808 B of working state is the pack's entire on-chip RAM cost, and it is **released when another meter is selected**.

## Where the state lives, measured (simulator, exact access counts)

`tools/rv32sim.py --count=NAME:LO:HI` now counts data loads and stores to an address range. Over 3 scenarios of 48 frames, per frame (Layered Wave, three settings of layers, resolution, draw mode and split):

| | Per frame |
|---|---|
| Instructions | ~180,000 |
| Working-state accesses (in scratch) | ~4,900 reads, ~870 writes |
| Read-only table reads (in the slot, PSRAM) | ~2,300 |
| History-ring accesses (in the slot, PSRAM) | ~190 reads, ~20 writes |
| **Extra cycles from PSRAM accesses, state in scratch (this design)** | **~77,500** |
| **Extra cycles if the working state lived in the slot instead** | **~251,000 (3.2 times worse)** |

Priced with the measured uncached PSRAM window costs (about 32 cycles per read, 26 per write, KB-040) against 1 for on-chip RAM, so these are estimates built on exact access counts, not measured cycles. Two findings:
- **The scratch area is what makes a pack as cheap as the built-in meter.** The built-in Layered Wave keeps the same split (hot working state on-chip, history ring and tables in PSRAM), so scratch gives parity; leaving the state in the slot would cost 3.2 times the PSRAM time.
- **The remaining cost is the read-only tables:** about 2,300 reads a frame from PSRAM is roughly 71,000 cycles a frame, about 4% of the CPU at 38 frames a second, **in the built-in meter too** (its tables are in the cold data region
  behind the same window). Copying the hot tables into scratch at load would remove most of it, at the price of about 2.7 KB of scratch. Worth testing against the real `LW COST` row before deciding.

## Firmware integration (built, behind `PACKS=1`; test cores are named `TAU DEV METER NN` until the branch is merged)

- **Build switch.** `PACKS=1 RAM_192K=1 bash fw/build.sh <target>` (needs the 192 KB link). Unset, the firmware is **byte-identical** to a build without the feature (checked on the default 256 KB release, the 192 KB release
  and the 192 KB diagnostic profile). With it: **1 KB of heap gap is reserved as the meter scratch** at the fixed ABI address `0x27400` (`fw/link.ld`, which fails the link if the layout cannot hold it), plus about 224 B of
  hot code and state; the 192 KB release gap goes 14,464 to 13,216 B and the diagnostic profile 5,680 to 4,384 B (floor 4,096 B).
- **File and slot.** `tau-packs.bin` is data slot 9 (`tools/tau_data_slots.py add_packs_slot`, `package_dev_build.py --packs`), a `TPKB` bundle (`tools/pack_bundle.py`) of `.tmpk` packs. Optional like the assets file.
- **Boot.** `packs_boot_load()` runs right after `cold_boot_load()` and does nothing unless cold code (PSRAM instruction fetch) is proven. It checks the scratch symbol against the ABI, reads the bundle through the
  data-slot reader, probes the slot area for read-back, and installs every pack (`mpkb_install_all`): verify, copy into its fixed slot (slot 0 = Layered Wave, `0x24840000`, written through the `0xA4840000` data alias).
  Each pack's result is kept; one bad pack never affects another.
- **Drawing.** `helios_meter()` draws Layered Wave through the pack when a valid one is installed, otherwise through the built-in code (which stays in the firmware for now). The first tick after the meter becomes active
  calls `mpk_activate()`: the pack's `.data` is copied into the scratch, its `.bss` and its slot state are zeroed. The host table the pack receives points at the real `fb_rect`, the cycle counter, the accent, the theme roles,
  the repaint flag and Layered Wave's live setting values, so the Settings page, presets and the Configure editor keep working unchanged.
- **Info page.** Settings, Diagnostics, Info, last row, **METER PACKS**: `OFF E<n>` (feature off: 20 no file, 40 no PSRAM instruction fetch, 41 scratch not where the ABI says, 42 slot area failed read-back, 30 not a bundle) or
  `FILE <n> LW OK|NONE|E<n> <ms>MS`.
- **Tools.** `python3 tools/pack_meter.py layered_wave --out work/packs/layered_wave.tmpk` (defaults are the real slot and scratch), `python3 tools/pack_bundle.py work/packs/tau-packs.bin work/packs/layered_wave.tmpk`,
  `python3 tools/package_dev_build.py --meter NN --rbf R --rbf-sha256 H --build-flags RAM_192K=1,CLK66=1,SDRAM_BUSY=1,LPC_FW=1,PACKS=1 --packs work/packs/tau-packs.bin`.
  `python3 tools/check_packs_abi.py` builds the PACKS firmware and checks that the scratch symbol, the heap end and a freshly built pack all agree with `fw/meter_pack.h` (about a minute; not in `make test-host`).
- **Host-verified:** the loader (install, activate, bundle with skipped, corrupt, truncated, empty and duplicate cases), the pack drawing identically through the bundle path, the ABI numbers against a real firmware build, the data
  slot and packager (`check_tau_package` passes on the generated test core). **Not verified:** that any of it runs on silicon.
- **What a Pocket run should show:** with `tau-packs.bin` present, METER PACKS reads `FILE 1 LW OK <few>MS` and Layered Wave looks exactly as before; with the file removed it reads `OFF E20` and Layered Wave still draws
  (built-in); a corrupted copy reads `LW E26` and still draws. Any crash or blank meter on the first run with a good file is the pack path (the first execution of code the player loaded after boot, from the PSRAM window).

## First Pocket run (2026-10-03): the pack loaded, then froze the player; root cause and fix

`TAU_DEV_METER_01` booted: `METER PACKS FILE 1 LW OK 26MS` (load, CRC, ABI, scratch and slot checks all passed on silicon), but selecting Layered Wave froze the player. Cause (read from the RTL docs, not yet re-confirmed by a
clean run): the PSRAM **instruction alias** (`0x24xx_xxxx`) is fetch-only (`docs/MMIO_ALLOCATION.md`: "there is no cached alias"; a data load there is a bus error), but the pack's `.rodata` tables and its `.pstate` history ring were linked at that alias,
so the pack's first data read froze the CPU. The simulator has no PSRAM window, so no host test could see it. Fix (`TAU_DEV_METER_03` onward): `fw/meter_pack.ld` now links `.rodata` and `.pstate` at the **data alias**
(`0xA4xx_xxxx`, `DATA_ALIAS = 0x80000000`; the same bytes, reached pc-relative, which wraps correctly in 32 bits) while `.text` stays at the instruction alias; `tools/pack_meter.py` sets `DATA_ALIAS` for real slot addresses and 0 for host tests. The file
layout and sizes are unchanged. **A Pocket run of Layered Wave through the pack after the fix is still outstanding** (later screenshots showed `LW COST` at 0 commands, i.e. the meter was not selected).
Rule to keep: a loadable module's data must never be linked at an alias the data bus cannot decode.

## Second pack, ABI 3 and hardware results (2026-10-04)

- **Slots.** Slot 0 = Layered Wave (meter 16), slot 1 = Winamp Bars (meter 12) (`mtr_pack_slot_of`, `tools/pack_meter.py` derives each meter's slot origin). Winamp Bars lives in `fw/winamp_bars.inc`, included by the built-in meter and by `fw/meter_pack_winamp_bars.c` (1,868 B image, 240 B working state, no state in the slot).
- **ABI.** ABI 2 added `rect_clip`, `bar_clip` and `bar_ready` (the clip-aware fills the fullscreen figures use; named so the `fig_rect` macros in `fw/meter.h` cannot rename the members). ABI 3 added `clk_hz` (the CPU clock the cycle counter runs at) and `stats` (4 words a pack may publish: commands, CPU cycles, history-read cycles, stride; the host copies them into the built-in names so Info `LW COST` and `METER DRAW` work with a pack). The ABI is read from `fw/meter_pack.h` by the pack tool; the fingerprint in `sim/test_meter_pack.py` must change with any layout change.
- **Same feature macros as the firmware.** The firmware always compiles Layered Wave with `LW_STATS` (statistics and the time-based cost guard, about 9 ms of CPU); a pack built without it used the host build's command-count rule and drew 12.2 ms against 9.7 ms. The pack now defines it; host reference builds do too (`cycles()` is 0 on both sides).
- **Tests (host).** `sim/test_meter_pack.py`: each pack on rv32sim draws exactly what the built-in meter draws (`sim/lw_pack_native.c`, `sim/bars_pack_native.c`, `sim/bars_pack_trace.h`, `tools/host/pack_harness.c` with `PACK_BARS` / `PACK_SLOT`), a two-pack bundle installs both, pack order is irrelevant, bad packs are refused, the stats words equal the built-in's.
- **Hardware (Pocket).** `METER PACKS FILE 2 LW OK BR OK 35-37MS`. Layered Wave through the pack runs (the first-run freeze is fixed): `LW COST C437 CPU 7740 UNW 252`, draw 9.7 ms. Winamp Bars through the pack: 0.56-0.69 ms per draw (built-in 0.29 ms), clean fullscreen label. No audible impact (owner).
- **Open.** Packs for Winamp Scope, VU Master (clean its framework violations first) and Chladni (its 3,072 B state exceeds the 1 KB scratch: decide the scratch size or move the plane to PSRAM); a Settings meter list built from the pack directory; the fallback tests (file removed `OFF E20`, corrupt copy `LW E26`) are deliberately left to the end of the whole feature work (owner, 2026-10-04).

## Third pack, Winamp Scope, ABI 4 (2026-10-04, B-610)

Slot 2 = Winamp Scope (meter 13): `fw/winamp_scope.inc` is shared by the built-in meter and `fw/meter_pack_winamp_scope.c`. ABI 4 added the host table fields `grad`, `fullscreen`, `bg_restore`, `bg_blend` and `scope_note`, so the trail fade (blend, falling back to the gradient restore) and the per-column fullscreen erase behave exactly as built-in; the pack carries 136 B of working state. Host-verified equal to the built-in over four scenarios; **not yet run on a Pocket** (`TAU_DEV_METER_15`).

## Fourth pack, MASTER VU, ABI 5 (2026-10-04, B-611)

Slot 3 = MASTER VU (meter 15). Its framework violations were removed first: the time step comes from `in->dt_ms`, and the info overlay's figures come from the host as `mtr_info_t` (`in->info`), measured once a second only while the overlay is on. ABI 5 added `info` and the host text services. 3,076 B image, 28 B working state; host-verified equal to the built-in over four scenarios; **not yet run on a Pocket** (`TAU_DEV_METER_16`).

## Fifth pack, Chladni, 4 KB scratch, ABI 6 (2026-10-04, B-612)

Slot 4 = Chladni (meter 14). The scratch grew from 1 KB at 0x27400 to 4 KB at 0x26800 (owner decision; Chladni's working state is 2,744 B) and the slots from 4 to 5. ABI 6 added the mailbox, engine-copy, yield and toast services. Host-verified equal to the built-in over four scenarios; **not yet run on a Pocket** (`TAU_DEV_METER_17`). The diagnostic-profile PACKS build now has a 2,320 B heap gap (floor 2,048 B). **Possible later improvements:** see B-612 in `docs/AUDIT_TRAIL.md` (Chladni state in the PSRAM slot, shared transient buffers, per-pack scratch sizing).

## PACKS_ONLY: the list from the pack directory (2026-10-04, B-613)

`PACKS_ONLY=1` (with `PACKS=1`) leaves the five pack meters out of the firmware; Settings > Meter lists only meters whose pack is valid (plus the always-built-in ones). Frees about 5.8 KB of hot RAM (diagnostic profile gap 2,320 to 8,112 B) and 21 KB of cold image. Parameters and presets are still firmware tables; shipping them inside a pack (so a meter can be card-only) is the next step. Test core `TAU_DEV_METER_18`, not yet run on a Pocket.

## What is still not done, and the real risks

1. **A directory beyond Layered Wave.** Only meter id 16 has a slot and a pack source; the table in `fw/meter_pack.h` (`mtr_pack_slot_of`) and the pack TUs for the other modular meters are the next additions. The
   Settings meter list is still built from the built-in manifests: a meter that is *only* a pack (absent from the firmware) needs the list built from the directory, and its parameters and presets (today compiled into the
   firmware) shipped with the pack.
2. **The scratch is 1 KB.** Enough for Layered Wave (808 B). A larger pack is refused (E29); the size to reserve is the largest `hot_ram` budget of any pack in the library.
3. **Hardware proof.** Loading a second blob works on a Pocket (`LW OK 26MS`, first run); executing it with data at the data alias is built and unproven (see the first-run section above).
4. **Statistics the pack owns.** The `LW COST` Info row reads statics that live in the built-in code; with a pack drawing, they stay at zero. A pack would publish its own counters through the host table.
5. **A faulting pack.** A pack runs as ordinary code: a bug in it crashes the player like any other. The loader proves the file is intact, not that the code is correct; a watchdog or a "last pack faulted" boot flag is a later safeguard.
6. **Size per pack.** Each pack duplicates a little compiler support code (about 1 KB here); acceptable, and it is the price of ROM independence.
7. **Packaging and signing.** Omega must write the file where the firmware looks, and a bad file must never brick the player; the loader's refusal matrix covers corruption, not malice (a CRC is not a signature).

## Next steps

1. Hot read-only tables in scratch (the 71,000-cycle finding), measured on a Pocket first via the existing `LW COST` Info row.
2. A Pocket run: install the generated test core, check the METER PACKS row, compare Layered Wave with and without the file, and with a corrupted copy.
3. Pack sources for the other modular meters, and a Settings list built from the directory.
4. The Omega side: library view, cost ceiling meter (sum of the manifest `budget` fields), pack writer.
