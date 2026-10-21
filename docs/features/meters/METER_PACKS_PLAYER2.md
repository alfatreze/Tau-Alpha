# Loadable meters on Player 2 (packs)

Status 2026-10-21: built and host-proven for all five meters (ABI 3); **not run on a Pocket**. The design and the hardware proof of the mechanism come from the old player's `meter-builder` branch (`docs/features/meters/METER_PACKS.md` there: a pack loads, runs from the PSRAM code window and draws; owner-confirmed on `TAU_DEV_METER_18`). This is the Player 2 form; the ABI restarted at 1.

## What a pack is
One meter's drawing code, compiled freestanding for rv32im, linked at its slot in the PSRAM code window and carrying its own `memset`/`memcpy`. It reaches the firmware only through `mtr_host_api_t` (`fw2/ui/meter_pack.h`: `rect`, `text`, `rect_stash`, the `blend_*` calls, the mailbox (`mb_write/mb_read`), `fence`, `blit`, `blit_abs` and `sblit` = the `gfx_*` functions, the theme `role` table, the meter's live `params`, a small `stats` array). Registers are not in the table: a pack includes `platform/mmio.h` and reads them directly (hardware addresses, not firmware symbols); it never names a firmware symbol. Entry point: `uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)`, the same contract as a built-in `tick`.

**One source.** The pack source (`fw2/ui/meter_pack_<key>.c`) defines `P2_PACK`, redirects `gfx_*` and `th_role` to the table and includes the meter's own `meter_<key>.c`, which leaves out its firmware-only part (presets, the live parameter array) under `P2_PACK`. The built-in meter and the pack are compiled from the same lines.

## Addresses (fixed, part of the ABI; `fw2/cores/meter_pack_core.h`)
| What | Where |
|---|---|
| slots | `0x24900000` + slot x 64 KiB, 5 slots (instruction alias; written through `0xA4900000`; above the cold image's 1 MiB region at 8 MiB) |
| working state | a 4 KB scratch at `0x26C00` just under the tag zone, only in a `PACKS=1` build (`fw2/platform/link.ld` reserves it; the heap gap shrinks by 4 KB); only the active pack's `.data`/`.bss` are resident |
| delivery | the `MPKB` section of `tau-assets.bin` (`tools/tau_assets.py pack --packs tau-packs.bin`), read at boot like METR; the 4 KB scratch doubles as the loader's read window |

## Rules
- A refused or missing pack leaves the built-in meter in use (per-meter fallback; `meters_pack_state(m)`: 0 runs, 0xFF none, else the refusal code E21..E29).
- Refusal order: header, ABI, meter id, origin, sizes, CRC, entry, scratch origin, scratch capacity. Nothing is executed before the CRC matches.
- Any change to `mtr_in_t` or `mtr_host_api_t` bumps `MTR_PACK_ABI`; `sim/test_p2_meter_pack.py` fails until its fingerprint is updated.
- A pack grows the host table only when a meter that is a pack needs it. Compose (the off-screen draw of a whole-box meter) stays the host's: the host calls the pack between `gfx_compose_begin` and `present`, exactly as for a built-in meter.
- A pack's PSRAM history (Layered Wave's ring) is its `.pstate` region in its own slot, after the image: zeroed on activation, proven by the pack before first use.

## Tools and tests
`tools/player2/pack_meter.py <key> --out x.tmpk` (reads slot, id and addresses from the manifests and the core header), `tools/player2/pack_bundle.py`, `tools/player2/check_packs_abi.py` (firmware ELF vs the ABI), `sim/test_p2_meter_pack.py` (rv32sim: for each of the five packs (Chladni against a model of the mailbox words its probe uses, Layered Wave with its PSRAM window words answered by the same model), identical command hashes to the built-in meter over four scenarios, the Scope against a model of the capture block (`tools/p2_dev_model.py`, `rv32sim.py --dev`); 14 refusals; a bundle of all five; MPKB round trip; ABI fingerprint), build `PACKS=1 bash fw2/build.sh`.

## Measured
| Pack | image in the slot | working state in the scratch |
|---|---|---|
| Winamp Bars | 1,428 B | 164 B |
| Winamp Scope | 1,256 B | 152 B |
| Master VU | 2,384 B | 36 B |
| Chladni | 7,016 B | 2,744 B |
| Layered Wave | 15,700 B (+2,424 B ring state in the slot) | 1,072 B |
 A `PACKS=1` firmware is +116 B ROM and +24 B hot RAM over the default; the default build does not change.

## Versioned ABI (B-803)
The header's ABI field is `major << 8 | minor` (now 1.0 = `0x0100`). A **major** change breaks packs and old ones are refused (E22). A **minor** change only appends (a host table entry, an `mtr_in_t` field): firmware runs every pack whose minor is not above its own, and a pack declares the minor it NEEDS (`#define MTR_PACK_NEEDS_MINOR n` in its source; default 0; `pack_meter.py` writes it), not the firmware's, so packs built today keep running on firmware that is newer. `sim/test_p2_meter_pack.py` tracks the prefix fingerprint of both structs for every minor (`ABI_HISTORY`): changing an existing declaration fails it; appending needs a new history line and `MTR_PACK_ABI_MINOR`. Packs built before this (ABI 4 and 5 numbers) are major 0 and refused: rebuild once.

## ABI 5 (B-802, now the content of 1.0)
The host table gained `clut_load` (a palette load) and `cblit` (palette indices from the stash through the palette to the drawing buffer, 127 columns at most); `MTR_PACK_ABI` is 5, so every pack must be rebuilt and old ones are refused (E22). First user: the Waterfall's entering-row plane (stash rows 722..734). Other users of the palette (icons, previews, art) load their own before their blits, so a pack must do the same.

## Pack cost (B-800, `tools/player2/measure_pack_cost.py`)
The built-in Layered Wave is itself a cold object (`fw2/platform/link.ld`: `meter_layered_wave.o`), executed from PSRAM through the same instruction alias as a pack slot, so a pack pays no extra fetch price: both run from PSRAM. The difference is the host table: every draw call goes through a function pointer and a command counter. Under rv32sim over the same trace (320 pictures, 67,485 draw commands, `-DTR_NOHASH` so both sides pay the same for an engine call) the built-in takes 86,073,089 instructions and the pack 87,002,952: **the pack adds 1.08%** (13.8 instructions per draw command). The only Pocket figure so far is the pack's (DEV 173: slowest tick 7.66 M cycles); the matching built-in figure needs a build without `PACKS=1` on the card (`METER MAX LAYERED WAVE` on Info), which has not been run.

## Fallback and Info
Per-meter fallback, the dispatch and the Info rows are host-tested (`sim/test_p2_meter_pack_dispatch.py`: no bundle, all valid, one corrupt, not a bundle, another ABI, two mutants). Info rows (a `PACKS=1` build): `PACKS RUN` (bit per meter), `PACK ERR` (the first refusal code), `PACK FILE` (0 read, 255 no bundle, 30 not a bundle), `PACKS SEEN`. The test found a real bug: a slot's status was 0 (= running) until a bundle had been read, so a PACKS build without an `MPKB` section would have jumped to address 0; `meters_init` now marks every slot empty.

## Not done
`PACKS_ONLY` (leave the built-in code out); an Info row for the pack state; a Pocket run (needs a `PACKS=1` DEV package and a `tau-assets.bin` with an `MPKB` section; card write needs approval); pack fallback tests (last, as agreed).

`work/packs-p2/tau-assets.bin` (all five packs, 28 KB, so it is staged through the PSRAM like any file over 4 KB) is the test file for a `PACKS=1` DEV package; as it carries only `MPKB` it must be merged with the card's existing sections first.
