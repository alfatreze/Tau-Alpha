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

## What is not done, and the real risks

1. **Firmware integration.** Nothing calls a pack yet. Needed: a slot allocator in the 1 MB code window beyond the cold image (about 100 KB used today), a directory (which packs are present, their
   meter ids and cost fields) read from `tau-assets.bin` or a new data slot, dispatch from `helios_meter()` through the entry point, the Settings meter list built from the directory, and the fallback
   to the built-in meter on any refusal.
2. **The scratch area itself.** The loader and the link script support it and the simulator shows it works, but the firmware has no scratch region yet: the base link needs a reserved block (1 KB covers
   Layered Wave; the size is the largest working state any pack declares, which the manifest budget `hot_ram` already bounds) taken from the heap gap, and `mpk_load` is called with its address.
3. **Hardware proof.** Instruction fetch from the PSRAM window is hardware-proven for the cold image; loading a second blob and branching into it has not been tried on a Pocket.
4. **Settings and the Info row.** The pack reads its setting values through the table; the Configure page and the QR export still read the built-in manifest data, and the `LW COST` Info row reads
   statics that live inside the pack. Both need the directory work above.
5. **Size per pack.** Each pack duplicates a little compiler support code (about 1 KB here); acceptable, and it is the price of ROM independence.
6. **Packaging and signing.** Omega must write the file where the firmware looks, and a bad file must never brick the player; the loader's refusal matrix covers corruption, not malice (a CRC is not a signature).

## Next steps

1. Hot read-only tables in scratch (the 71,000-cycle finding), measured on a Pocket first via the existing `LW COST` Info row.
2. Directory and dispatch in the firmware, behind a build switch, with the built-in meters untouched.
3. A Pocket run: load the pack from the card, draw, fall back on a corrupted copy.
4. The Omega side: library view, cost ceiling meter (sum of the manifest `budget` fields), pack writer.
