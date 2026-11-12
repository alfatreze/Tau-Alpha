# Golden vectors for the meter bin v2 (plan P2.1, for Tau Omega: O021/1)

Made by `tools/meter_bin_vectors.py`, read by the independent Python reference `tools/meter_bin.py` and by the firmware's reader `fw2/cores/meterbin_core.h`; `sim/test_p2_meterbin.py` proves both give the results in `expected.json` (and each other's, on 750 random bins). Format: `docs/features/meters/EXTERNAL_METERS_SPEC.md` sections 2, 3, 7; v1: `docs/features/TAU_PACKS_FORMAT.md`. **HOST-ONLY: nothing here has run on a Pocket.**

## The ids are SYNTHETIC
The ids of the seven shipped meters are not decided yet (plan P2.0). Every id below ends in `5e7e71c0` (the first 24 digits come from the SHA-256 of the meter's key, so the id hashes differ) and every `meter.json` says `synthetic_id`. **Do not ship them, do not build fixtures that treat them as the real ids**; when P2.0 fixes the real ids these files are regenerated and the SHA-256s change.
The packs are the CURRENT firmware's seven packs (ABI 1.2), linked for the window `0x24970000` (pack slot 7, the window P0 ran on a Pocket; the spec's text also says `0x24900000`: see the P2.1 report), with the low 16 bits of the id hash as their header `meter id`. `v1_seven.bin` is the v1 bundle the firmware ships today (packs linked for slots 0..6).

## Reading a bin (what the vectors pin)
Header 32 B, TOC rows of 256 B in display order, preview blocks (64-byte palette + run-length stream, padded to 4), packs (`TMPK`, padded to 4), u32 CRC-32 of bytes 32.. at the very end. `id_hash` = the id's first 8 hex digits read as a number (big-endian digit order), stored little endian. `pack_off`/`pack_size` = the whole `TMPK` (header included), `pack_crc` = the pack header's image CRC, `preview_off`/`preview_len` = absolute file offsets. A v1 bundle maps to rows too: `id_hash = 0x56310000 | meter id`, empty id, flag bit 3 set (legacy row), name and preview read from the pack's descriptor.
A refused bin gets ONE code (the first failing check, in the order of `fw2/cores/meterbin_core.h`), plus the row it was about:
`1 SIZE  2 MAGIC  3 VERSION  4 CRC  5 COUNT  6 LAYOUT  7 ID  8 HASH  9 DUP  10 NAME  11 PACK  12 WINDOW  13 PREVIEW  14 RESERVED  15 ORIGIN  16 PARAMS  17 READ  18 DESC`.
A sound bin may still have UNAVAILABLE rows: ABI major other than the firmware's or minor above it, or `tau_min` above the TAU version (`v2_unavailable_rows.bin` with a firmware at ABI 1.2, TAU 0.8: rows 1 and 2).

## Files
`expected.json` lists every file below with its size, SHA-256, code and row count. Not filed: the 4,456,449-byte oversize bin (code 1 SIZE; 4,456,448 is the largest accepted) because of its size: the test generates both.

| file | bytes | SHA-256 | expected | what |
|---|---|---|---|---|
| `v2_seven.bin` | 63596 | `40b909ef84dfae5a252ce2c7fefcbfd31263e3b1dbf7f750a8923e39d22a3948` | OK, 7 rows | the seven shipped meters in display order, Winamp Bars flagged as the fallback |
| `v2_reordered_hidden.bin` | 63596 | `1bf5b859a3bb82a73857ec7ae29d4fdccd8febfe997dd9ad696087ec8dbb4dbb` | OK, 7 rows | the same seven in reverse order, VU Master hidden (visible flag clear): the TOC is the order |
| `v2_unavailable_rows.bin` | 14220 | `d8111b26895122d668d39e4abb40bf3f692651cce41bad9c17104ad92a1dc868` | OK, 3 rows | three meters: Winamp Bars, one needing ABI 1.3, one needing TAU 0.9 (a firmware at ABI 1.2, TAU 0.8 lists the last two as unavailable) |
| `v2_empty.bin` | 36 | `b11b7fe0a6231c2056d7d5eb375037578b32a8c1d4e86ba491266d5fb929e97e` | OK, 0 rows | a sound bin with no meters (the player keeps Winamp Bars) |
| `v1_seven.bin` | 58468 | `5e4d112cff110b45c837341d43a42b28826660f4c80020cab50732a0ff7ec69d` | OK, 7 rows | the v1 bundle (tau-packs.bin as the firmware ships it today, packs linked for their slots) of the same seven meters |
| `bad_magic.bin` | 63596 | `f45c74373c30a4f588950a707204dcbd8b5699ef4160cdf66d6a119fad9d75ff` | refused: 2 MAGIC | the magic is not TPKB |
| `bad_version.bin` | 63596 | `f5c74c68514c6b888d30a660c0a6efec6c6b95bb70d029c539f9f797cbd87047` | refused: 3 VERSION | version 3 (this reader knows 1 and 2) |
| `truncated.bin` | 63595 | `2ab0c20941b56323e0066ad0f31260b3ca17f6d46ad50c8eba9aaebdad786ac5` | refused: 1 SIZE | one byte cut from the end |
| `trailing_byte.bin` | 63597 | `6b320e0b6bd9a4d6d3fc42be025a7b4493cc20834c2118337034068a82ecd011` | refused: 1 SIZE | one byte after the end (total size says otherwise) |
| `bad_crc.bin` | 63596 | `48a19d801f4f6dde4ba640b9ca73f1243990c13cbc22daf7b190dad5a8cfd96d` | refused: 4 CRC | the end CRC-32 is wrong |
| `flipped_pack_byte.bin` | 63596 | `32b9ba1040bb7372b43b20849a5e5436f1370cb0c45234fea735cef528d6b71e` | refused: 4 CRC | one byte of a pack's image flipped, the end CRC no longer matches |
| `count_65.bin` | 63596 | `a6e76ef7ec3ff0d45bc5d834214259b830cc7ae0307bab3e83436556d1710101` | refused: 5 COUNT | the header says 65 meters (the limit is 64) |
| `layout_toc_moved.bin` | 63596 | `b98082111b454bf75ab4326e1cc2bf3a28993c74dee2a1abdb826d5041acedab` | refused: 6 LAYOUT | toc_off is not 32 (CRC and total fixed up) |
| `duplicate_hash.bin` | 63596 | `7724118c6593bcd5dcceeacac40673e93e8d9705f1e90a19c333bf02d95dc025` | refused: 9 DUP (row 1) | row 1 has the id_hash and first 8 digits of row 0 |
| `bad_id_uppercase.bin` | 63596 | `fcfe57efddd042ddb4227dde027ac06f40df9e4489ca5d5aaa101b6cba33720f` | refused: 7 ID (row 2) | an upper-case hex digit in row 2's id |
| `bad_hash.bin` | 63596 | `2e310d778fc24bfc3a999361bf6c553d5172214a456b4f0ef5e98fe354a4d74e` | refused: 8 HASH (row 1) | row 1's id_hash is not its id's first 8 digits |
| `bad_name_empty.bin` | 63596 | `53f9f742fe424570b43c5637ec2f446462ec5c9c5b129c0f506bee4b45deddf4` | refused: 10 NAME (row 0) | row 0's name is empty |
| `bad_name_24.bin` | 63596 | `c215e2b5799a86562373330c305e7ee19cdd7f91a478296cabd63ea8bcee41ea` | refused: 10 NAME (row 0) | row 0's name has 24 characters and no NUL |
| `reserved_byte.bin` | 63596 | `1017e27f89e66814ff44b6ce79e1d639c0a387dd5feaa17bd36e90002ecd1765` | refused: 14 RESERVED (row 1) | a reserved byte of row 1 is not zero |
| `params_49.bin` | 63596 | `61f693172ebc688932ba1fae9ccfe2ed54625eda916966ec9b50e44090173eb6` | refused: 16 PARAMS (row 1) | row 1 claims 49 parameters (the limit is 48) |
| `pack_out_of_range.bin` | 63596 | `f9bc4fd0b70456470be98e6ed257498f927a85dfe58a6d36fd7410b742c4636b` | refused: 11 PACK (row 1) | row 1's pack_size runs past the end of the file |
| `pack_crc_mismatch.bin` | 63596 | `42b0878c63418fda9a6adfda228a1d3efb28861d049e3d3b362dbcefa129fd88` | refused: 11 PACK (row 1) | row 1's pack_crc is not the pack header's |
| `pack_window.bin` | 63596 | `096a5f984fd9108118b93af96d400451ca00d9489015a76136660c34b6652c23` | refused: 12 WINDOW (row 1) | row 1's pstate (70,000 bytes, TOC and pack header agree) does not fit the 64 KiB window |
| `pack_origin_slot.bin` | 2276 | `2912f60561608adfd5f3c82646336cb0bd0e7187f67ff3ff061b4fcb20b47fc9` | refused: 15 ORIGIN (row 0) | a pack linked for a slot, not for the window |
| `preview_not_56x32.bin` | 63596 | `ea14cf28238dd3e3b39637b41ee8c547641a8685d8c1c600cd42a7fc8e8ef7c7` | refused: 13 PREVIEW (row 1) | row 1's run-length stream no longer covers 56 x 32 pixels |
| `v1_zeroed_descriptor.bin` | 58468 | `2d91c17bbb77f2c88eca3dfb7b9a7d83907fbf6b89a67df20b0541984f4732f0` | refused: 18 DESC (row 1) | a v1 bundle whose second pack's image (descriptor too) is zero |
| `v1_duplicate_meter.bin` | 4448 | `f48a117924c9c8d29a252b0d4e1cac52a8dade0900aa1e454a4241c87df79b36` | refused: 9 DUP (row 1) | a v1 bundle with the first pack twice (one meter id twice) |
| `v1_truncated.bin` | 58465 | `7e25235eeba18753b07d66483db8cd51e589d85a4dd504dccfe367541ee9722c` | refused: 1 SIZE (row 6) | a v1 bundle cut short |

### The packages the v2 bin was composed from (`meter.json` + `meter.bin`, in display order)
| package | id (SYNTHETIC) | SHA-256 of `meter.bin` |
|---|---|---|
| `packages/winamp_bars/` | `102d4d21726303d2f1c571f55e7e71c0` | `06b8d85cfcf5b9a1699e2290c7e382c63665babea9a00c2c8a205a8706de9acc` |
| `packages/winamp_scope/` | `8adf2f573d87264ea1d2214d5e7e71c0` | `afea2d5e065d6ba64617a387bf3a22d6832896ac9860073316fd51b5d5f1bca9` |
| `packages/chladni/` | `d844eb1b9036fceccf790b565e7e71c0` | `deb0b9105798c1f245b822704ca50c1bd88c3c2fbb7e37306600a1719a9a3d74` |
| `packages/layered_wave/` | `4dab9aa0fd90083726ad39335e7e71c0` | `73bede0ec41fa59911c88df2040bb1427f059674c069747b6066edc2c8ef653a` |
| `packages/vu_master/` | `15413d591e83c64b1972d5225e7e71c0` | `d08bcf12d76984afa41dd8ba1c4cb50f5e7385d9fb6b46bbff62445c66f46865` |
| `packages/waterfall_grid/` | `463e43baa4f12b14c6326fa35e7e71c0` | `dff348b5614699b786ffff9f7ceb16a1e915e30669ae64b92fd275dc0afa8866` |
| `packages/sound_map/` | `3d2b056ab8f9a6d7400e5e4d5e7e71c0` | `ef820fbbb399f3439411ea9ae26bbe661f0d55338850eed17a185bbf4e3f5270` |

`python3 tools/meter_bin.py compose OUT.bin packages/<each, in this order> --fallback <winamp_bars id>` reproduces `v2_seven.bin` byte for byte (the test checks it). The packages have only `meter.json` and `meter.bin`: `preview.png`, `README.md` and `LICENSE` of the full `.tmeter` (spec section 1) come with `tools/meter_package.py` (P2.6); the bin's previews are the ones the packs' descriptors carry. Binary-only (flag bit 2) is set when a package has no `source/` folder or `source` link: all seven here, as they carry none.

## Regenerating
`python3 tools/meter_bin_vectors.py` rebuilds the packs from the current source (needs the RISC-V toolchain) and rewrites everything; the damaged files are also reproducible without the toolchain from `v2_seven.bin` and `v1_seven.bin`.
