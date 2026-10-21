# Loadable meters on Player 2 (packs)

Status 2026-10-21: built and host-proven for Winamp Bars; **not run on a Pocket**. The design and the hardware proof of the mechanism come from the old player's `meter-builder` branch (`docs/features/meters/METER_PACKS.md` there: a pack loads, runs from the PSRAM code window and draws; owner-confirmed on `TAU_DEV_METER_18`). This is the Player 2 form; the ABI restarted at 1.

## What a pack is
One meter's drawing code, compiled freestanding for rv32im, linked at its slot in the PSRAM code window and carrying its own `memset`/`memcpy`. It reaches the firmware only through `mtr_host_api_t` (`fw2/ui/meter_pack.h`: `rect` = `gfx_rect`, the theme `role` table, the meter's live `params`, a small `stats` array); it never names a firmware symbol. Entry point: `uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)`, the same contract as a built-in `tick`.

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
- A pack grows the host table only when a meter that is a pack needs it (today: `rect`). Chladni, Layered Wave, Scope and VU Master need blit, mailbox, text and blend entries first.

## Tools and tests
`tools/player2/pack_meter.py <key> --out x.tmpk` (reads slot, id and addresses from the manifests and the core header), `tools/player2/pack_bundle.py`, `tools/player2/check_packs_abi.py` (firmware ELF vs the ABI), `sim/test_p2_meter_pack.py` (rv32sim: identical command hashes to the built-in meter over four scenarios; 14 refusals; bundles; MPKB round trip; ABI fingerprint), build `PACKS=1 bash fw2/build.sh`.

## Measured (Winamp Bars)
Image 1,428 B in the slot, 164 B working state in the scratch, 1,476 B file. A `PACKS=1` firmware is +116 B ROM and +24 B hot RAM over the default; the default build does not change.

## Not done
The other four meters as packs; `PACKS_ONLY` (leave the built-in code out); an Info row for the pack state; a Pocket run (needs a `PACKS=1` DEV package and a `tau-assets.bin` with an `MPKB` section; card write needs approval); pack fallback tests (last, as agreed).
