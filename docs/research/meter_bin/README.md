# Golden vectors for the meter bin v2 (plan P2.1, for Tau Omega: O021/1)

Made by `tools/meter_bin_vectors.py` (which also writes this file), read by the independent Python reference `tools/meter_bin.py` and by the firmware's reader `fw2/cores/meterbin_core.h`; `sim/test_p2_meterbin.py` proves both give the results in `expected.json` (and each other's, on about 800 random bins). Format: `docs/features/meters/EXTERNAL_METERS_SPEC.md` sections 2, 3, 7. **HOST-ONLY: nothing here has run on a Pocket.**

## What is real and what is not
* The seven packs are the CURRENT firmware's packs (ABI 1.2), built once from the current source, linked for the window `0x24970000`, with the low 16 bits of the id hash as their header `meter id`. Their ids and names are the PERMANENT ones of `meters/<key>/meter.json` (B-1047).
* **The only synthetic ids** are the three rows of `v2_tuning_collision.bin` (last 8 digits `5e7e71c0`), built from the Winamp Bars pack.
* The v1 files (`v1_*.bin`) are for the **Python tool only** (inspecting old bundles): the firmware reads v2 alone and its reader answers `3 VERSION` to them. `v1_seven.bin` is `tau-packs.bin` as the firmware ships it today (packs linked for slots 0..6).

## Reading a bin (what the vectors pin)
Header **40 B** `{'TPKB', u16 2, u16 count, u32 toc_off = 40, toc_len, pv_off, pv_len, packs_off, total, toc_crc, 0}`, TOC rows of 256 B in display order, preview blocks (64-byte palette + run-length stream, padded to 4), packs (`TMPK`, padded to 4), u32 CRC-32 of bytes 40.. at the very end. `toc_crc` = CRC-32 of bytes `toc_off .. packs_off` (the TOC and the previews). `id_hash` = the id's first 8 hex digits read as a number, stored little endian. `pack_off`/`pack_size` = the whole `TMPK` (header included), `pack_crc` = the pack header's image CRC, `preview_off`/`preview_len` = absolute file offsets.
**Two checks, two codes.** The firmware at BOOT reads bytes `0 .. packs_off` only (header, TOC, previews) and checks `toc_crc` and the rows: `boot_code` in `expected.json`. The FULL check (tools, Omega after a write) adds the whole-file CRC and every pack's header against its row: `code`. A pack problem the boot path cannot see is found when the meter is SELECTED (the pack header against its row, then the image against `pack_crc`).
A refused bin gets ONE code (the first failing check) plus the row it was about:
`1 SIZE  2 MAGIC  3 VERSION  4 CRC (toc_crc)  5 COUNT  6 LAYOUT  7 ID  8 HASH  9 DUP  10 NAME  11 PACK  12 WINDOW  13 PREVIEW  14 RESERVED  15 ORIGIN  16 PARAMS  17 READ  19 FILECRC` (18 was the v1 descriptor, gone).
A sound bin may still have UNAVAILABLE rows: ABI major other than the firmware's or minor above it, or `tau_min` above the TAU version (`v2_unavailable_rows.bin` with a firmware at ABI 1.2, TAU 0.8: rows 1 and 2). And **tuning collisions**: rows sharing the low 16 bits of `id_hash` (`v2_tuning_collision.bin`: rows 0 and 1) get no tuning in the firmware.

## Files
`expected.json` lists every file with its size, SHA-256, `code`, `boot_code`, row count and which readers read it. Not filed: the 4,456,449-byte oversize bin (code 1 SIZE; 4,456,448 is the largest accepted): the test generates both.

| file | bytes | SHA-256 | full check | boot | what |
|---|---|---|---|---|---|
| `v2_seven.bin` | 63604 | `dc6f78ef1ed5059b1fc017c3c6d698c032027c4794f9fc188e0b84433e5f5fd4` | OK, 7 rows | OK | the seven shipped meters in display order, Winamp Bars flagged as the fallback |
| `v2_reordered_hidden.bin` | 63604 | `27f6549d8faaff4e5992265e24a4d3abce72d79a7b4d05bbd9b865210112960d` | OK, 7 rows | OK | the same seven in reverse order, VU Master hidden (visible flag clear): the TOC is the order |
| `v2_unavailable_rows.bin` | 14228 | `e32be301026d15be57568929eed6302c4dc187db32815359396af6f1ffa7ef1e` | OK, 3 rows | OK | three meters: Winamp Bars, one needing ABI 1.3, one needing TAU 0.9 (a firmware at ABI 1.2, TAU 0.8 lists the last two as unavailable) |
| `v2_empty.bin` | 44 | `df65fa6afb26564eea54f4f159d8d054ddb2c840027b1bbd30af60096f24631d` | OK, 0 rows | OK | a sound bin with no meters (the player keeps Winamp Bars) |
| `v1_seven.bin` | 58468 | `5e4d112cff110b45c837341d43a42b28826660f4c80020cab50732a0ff7ec69d` | OK, 7 rows | - | the v1 bundle (tau-packs.bin as the firmware ships it today, packs linked for their slots) of the same seven meters |
| `bad_magic.bin` | 63604 | `1fcb001dc001b147af801798f5be48559b4ea681f167a2cd42d90a50b118200d` | 2 MAGIC | 2 MAGIC | the magic is not TPKB |
| `bad_version.bin` | 63604 | `62a8226bad2e4a6f65c2548401535f8dc75acdf7ce369586e9a6749d3c1ade51` | 3 VERSION | 3 VERSION | version 3 (this reader knows 1 and 2) |
| `truncated.bin` | 63603 | `4c3caaae0c1d79d0bf053e07582065d7112fa854ceb3db64ca3d9d08350d9c70` | 1 SIZE | 1 SIZE | one byte cut from the end |
| `trailing_byte.bin` | 63605 | `3f982e5fe103485badb70738ffeeb7bd944f1d20754874ea924322d43081d757` | 1 SIZE | 1 SIZE | one byte after the end (total size says otherwise) |
| `bad_toc_crc.bin` | 63604 | `d36313b64fe195c2c8113b9ef80f2088067a25215d71d14280eac31b6ebc796e` | 4 CRC | 4 CRC | toc_crc (the CRC of the TOC and the previews) is wrong |
| `flipped_toc_byte.bin` | 63604 | `a9e9e0c590ac6c80f3ea5c4a04125b6174ca930404d83d89aa7f000e8e20fbbb` | 4 CRC | 4 CRC | one byte of row 3 changed, toc_crc no longer matches |
| `bad_file_crc.bin` | 63604 | `2c649cf00ddcb6448b65d10642a7cd3a481bd196f99cf95aeea6f227f45df3b8` | 19 FILECRC | OK | the whole-file CRC at the end is wrong (the firmware never reads it: boot code 0) |
| `flipped_pack_byte.bin` | 63604 | `f86c6bdbcc735c73a392ffe2db2a02dc5eb32fe8ea5d73737f4a04c4e4e9f8b2` | 19 FILECRC | OK | one byte of a pack's image flipped: the whole-file CRC no longer matches (boot code 0; the loader's pack_crc check catches it at selection) |
| `header_reserved.bin` | 63604 | `0cb84d69e63133ff6c583cfbf1dadc1d90389a6f1673ddc7cbacbaf72cf19119` | 14 RESERVED | 14 RESERVED | the reserved u32 at offset 36 is not zero |
| `count_65.bin` | 63604 | `5251b630b9730b83cc345fe9c95a98dc51a10b2192510ab4113aa9b779267be8` | 5 COUNT | 5 COUNT | the header says 65 meters (the limit is 64) |
| `layout_toc_moved.bin` | 63604 | `ba93ec333c61e226262ed1624afea4e7501115a40ecf9a31e16a4309b603ea8e` | 6 LAYOUT | 6 LAYOUT | toc_off is not 32 (CRC and total fixed up) |
| `duplicate_hash.bin` | 63604 | `f70076b34c955d020dde1cf4be99c55f61fdf22f544a42f744ecceb86c2cb67e` | 9 DUP (row 1) | 9 DUP | row 1 has the id_hash and first 8 digits of row 0 |
| `bad_id_uppercase.bin` | 63604 | `b8a71a2d24c239c5e13d22947437a3a5ad3ae683a2f877f4f58ba5f86c52a9ac` | 7 ID (row 2) | 7 ID | an upper-case hex digit in row 2's id |
| `bad_hash.bin` | 63604 | `3f65b32327c3fdb54a871f738582aba15321192e093d1fad1f12789314f246ee` | 8 HASH (row 1) | 8 HASH | row 1's id_hash is not its id's first 8 digits |
| `bad_name_empty.bin` | 63604 | `5f5c6c4088727928c2ad671e1f8ab091f9b651dce778150f2ee97efd7b3040c2` | 10 NAME (row 0) | 10 NAME | row 0's name is empty |
| `bad_name_24.bin` | 63604 | `9a58ad16e34641eff5426af3be08325cb4fc382848b78540a57d4345f8729dd4` | 10 NAME (row 0) | 10 NAME | row 0's name has 24 characters and no NUL |
| `reserved_byte.bin` | 63604 | `d039fbad7496e25594f74bcb9c6a8f4430be9de0f7b8ded98c76e1d1abb30c4c` | 14 RESERVED (row 1) | 14 RESERVED | a reserved byte of row 1 is not zero |
| `params_49.bin` | 63604 | `0a41add2e4141248e260856c3b1814168bb0eda6c3cdef9b402733531aab460d` | 16 PARAMS (row 1) | 16 PARAMS | row 1 claims 49 parameters (the limit is 48) |
| `pack_out_of_range.bin` | 63604 | `b56ca85eb9a1d60a77f310d6c52382a98b34ba5283edbb9c20aed6f2ca9ae331` | 11 PACK (row 1) | 11 PACK | row 1's pack_size runs past the end of the file |
| `pack_crc_mismatch.bin` | 63604 | `de4647b3a34e682e671096486b117c33fe2495528e9d6b78e8327e4a9a26abc9` | 11 PACK (row 1) | OK | row 1's pack_crc is not the pack header's (boot code 0: found when the meter is selected) |
| `pack_window.bin` | 63604 | `3bddcee6167d73d908b801887962183aed5ee92c8f2344df2ac27bd63cb81b67` | 12 WINDOW (row 1) | 12 WINDOW | row 1's pstate (70,000 bytes, TOC and pack header agree) does not fit the 64 KiB window |
| `pack_origin_slot.bin` | 2284 | `e9f54beaa10429e52927c704f80d74f203560dc725b526de615dacb06db0b675` | 15 ORIGIN (row 0) | OK | a pack linked for a slot, not for the window (boot code 0: found when the meter is selected) |
| `preview_not_56x32.bin` | 63604 | `b62a796562b21eed04e4abb44b37c222f8c1cd2041ac394ffec604a0bdc4ec5c` | 13 PREVIEW (row 1) | 13 PREVIEW | row 1's run-length stream no longer covers 56 x 32 pixels |
| `v2_tuning_collision.bin` | 8804 | `97c8a36fa28fe4b6812db4343da977081fa37ac2aab725121b39c71f6d3884db` | OK, 3 rows | OK | a SOUND bin whose rows 0 and 1 share the low 16 bits of id_hash (1234): the firmware refuses tuning for those two rows (flags 1 1 0), nothing else changes |
| `v1_zeroed_descriptor.bin` | 58468 | `2d91c17bbb77f2c88eca3dfb7b9a7d83907fbf6b89a67df20b0541984f4732f0` | 18 DESC (row 1) | - | a v1 bundle whose second pack's image (descriptor too) is zero |
| `v1_duplicate_meter.bin` | 4448 | `f48a117924c9c8d29a252b0d4e1cac52a8dade0900aa1e454a4241c87df79b36` | 9 DUP (row 1) | - | a v1 bundle with the first pack twice (one meter id twice) |
| `v1_truncated.bin` | 58465 | `7e25235eeba18753b07d66483db8cd51e589d85a4dd504dccfe367541ee9722c` | 1 SIZE (row 6) | - | a v1 bundle cut short |

### The packages the v2 bin was composed from (`meter.json` + `meter.bin`, in display order)
| package | id | SHA-256 of `meter.bin` |
|---|---|---|
| `packages/winamp_bars/` | `7dddfe63121e904843088c075cc28ce9` | `452903da1da6079fa9b12521d88b3a3aa5fc3ef20bf4e03e3e32a24bbe52590c` |
| `packages/winamp_scope/` | `143acb80cbcd1b08b84c2661eeab3414` | `1f2db1b0e4098553ca66f70f0c829c39f5d3392f6c3e23993a5e348ce0fb7d1f` |
| `packages/chladni/` | `7471cc214987407c0b03917f27923179` | `4a3a78bf3f75f8b8cfc47d18213bced26a2967aee465c9d122cfa0e10c647896` |
| `packages/layered_wave/` | `5812803445b81ffae735527099ef6793` | `5e623a6341644b9789dff983af5622c4de2b186e01bec12058c943cdcbe93f61` |
| `packages/vu_master/` | `0d8c03d3a2770c1aac2dc6abbc222afd` | `f80f7b85440d519159d64da2f6c4db38c836cd13b07665acf61038c298d52be2` |
| `packages/waterfall_grid/` | `17e607764b83b9372452eda124ae763f` | `8c96e985986bc53393c4cfa323c530e9e5d02f47d7468a4435403c8e720892a5` |
| `packages/sound_map/` | `0c82d9f07f73f25d90ca54c8d39173ce` | `fe8b03dd11176e4bcd9c944c2af4a164b6b67e843b98c936850ff53ac78c3124` |

`python3 tools/meter_bin.py compose OUT.bin packages/<each, in this order> --fallback <the winamp_bars id>` reproduces `v2_seven.bin` byte for byte (the test checks it). The packages have only `meter.json` and `meter.bin`: `README.md` and `LICENSE` of the full `.tmeter` come with `tools/meter_package.py` (P2.6). The previews in the bins are the ones the packs' descriptors carry (the package has no authoritative `preview.png`). Binary-only (flag bit 2) is set when a package has no `source/` folder or `source` link: all seven here.

## Regenerating
`python3 tools/meter_bin_vectors.py` rebuilds the packs from the current source (needs the RISC-V toolchain) and rewrites everything including the SHA-256s; the damaged files are also reproducible without the toolchain from `v2_seven.bin` and `v1_seven.bin` (the test does it).
