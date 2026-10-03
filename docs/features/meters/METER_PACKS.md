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
- **File format** (`.tmpk`, `fw/meter_pack_core.h`): 32-byte header (magic, ABI, meter id, load size, bss size, origin, entry offset, CRC-32 of the body, bss offset) then the image. The loader
  refuses, in this order: bad header (E21), wrong ABI (E22), wrong meter (E23), linked for another slot (E24), wrong or truncated or over-long size or a body/bss that does not fit the slot (E25), CRC
  mismatch (E26), entry outside the body (E27); a missing file is E20. A refused pack is never executed and the player stays on its built-in meters.
- **Source stays the meter's own.** `fw/meter_pack_layered_wave.c` includes `fw/layered_wave.inc` unchanged and redirects its handful of firmware services to the host table with macros. Building a pack:
  `python3 tools/pack_meter.py layered_wave --org 0x24A00000 --out layered_wave.tmpk`.

## What is proven (host only)

| Check | Result |
|---|---|
| Pack builds with no undefined symbols; links at the simulator slot and at a PSRAM window address | 11,724 B image + 3,232 B state; 11,756 B file |
| Loaded by the firmware loader and run on `tools/rv32sim.py` over 3 scenarios (48 frames each, paused and silent stretches, forced repaints) | command counts and hashes identical to the in-tree meter |
| Refusal matrix (11 corruptions) | each refused with the expected code, nothing run |
| ABI fingerprint | tracked; changing `mtr_in_t` without a bump fails the test |

For comparison, the built-in Layered Wave is 10,608 B of cold image, 810 B of hot RAM and 2,424 B of PSRAM state (13.8 KB in total); the pack is 15.0 KB because it carries its own helpers.

## What is not done, and the real risks

1. **Firmware integration.** Nothing calls a pack yet. Needed: a slot allocator in the 1 MB code window beyond the cold image (about 100 KB used today), a directory (which packs are present, their
   meter ids and cost fields) read from `tau-assets.bin` or a new data slot, dispatch from `helios_meter()` through the entry point, the Settings meter list built from the directory, and the fallback
   to the built-in meter on any refusal.
2. **State in uncached PSRAM.** A pack's `.data` and `.bss` live inside its slot, and PSRAM data accesses cost about 32 cycles (KB-040), against a few for on-chip RAM. Layered Wave touches its
   hot state (810 B) every frame, so this may cost real CPU. Mitigation, not built: link the pack's `.data/.bss` at a small reserved **meter scratch** area in on-chip RAM (about 1 KB shared by whichever
   meter is active, state lost on a meter switch, which is harmless) and let the loader copy `.data` there. That also delivers the "only the active meter's state is resident" saving.
3. **Hardware proof.** Instruction fetch from the PSRAM window is hardware-proven for the cold image; loading a second blob and branching into it has not been tried on a Pocket.
4. **Settings and the Info row.** The pack reads its setting values through the table; the Configure page and the QR export still read the built-in manifest data, and the `LW COST` Info row reads
   statics that live inside the pack. Both need the directory work above.
5. **Size per pack.** Each pack duplicates a little compiler support code (about 1 KB here); acceptable, and it is the price of ROM independence.
6. **Packaging and signing.** Omega must write the file where the firmware looks, and a bad file must never brick the player; the loader's refusal matrix covers corruption, not malice (a CRC is not a signature).

## Next steps

1. Meter scratch overlay (risk 2), measured on the simulator for CPU cost before any hardware.
2. Directory and dispatch in the firmware, behind a build switch, with the built-in meters untouched.
3. A Pocket run: load the pack from the card, draw, fall back on a corrupted copy.
4. The Omega side: library view, cost ceiling meter (sum of the manifest `budget` fields), pack writer.
