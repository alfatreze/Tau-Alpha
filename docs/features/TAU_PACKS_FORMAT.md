# `tau-packs.bin`: the meter bin a Player 2 core ships (v2 since P2.2; the v1 bundle below is history that the tools still read)

> **v2 note (external meters P2.2, 2026-11-12).** Since TAU DEV 220 the firmware reads **v2 only**: `tau-packs.bin` (data slot 9, the core's own) is a **v2 meter bin**, exactly the format of the user's `tau-meters.bin` (data slot 10, `Assets/<platform>/common/`, optional, never shipped, written by Tau Omega) and specified in `docs/features/meters/EXTERNAL_METERS_SPEC.md` section 2 (header 40 B, TOC of 256-byte rows in display order, previews, packs, whole-file CRC for tools). `tools/player2/build_packs.py` composes the shipped one with `tools/meter_bin.py` from the meters' own manifests: every pack linked for the ONE 64 KiB execution window (`0x24970000`) with its permanent 128-bit id (`meters/<key>/meter.json`), the seven shipped meters in the current order, Winamp Bars first and flagged as the built-in fallback. At boot the firmware reads the user's bin, and only if it is not sound the core's own (`MTOC SRC` 1 or 2); **neither valid means Winamp Bars alone**. It reads the header, the TOC and the previews into PSRAM (`0x24900000`, `0x24910000`), checks `toc_crc` and every row there, and **reads no pack**; a pack is read when its meter is selected (header against its TOC row, `pack_crc`, ABI, `tau_min`, window bounds, then written through the uncached alias, `fence.i`, pstate zeroed). A bin without a row flagged as the built-in fallback is refused whole (`MTOC ERR 20`: a firmware rule on top of the reader codes). The `MPKB` section of `tau-assets.bin` is no longer read by the firmware. The text below describes the v1 bundle (header 8 B, packs back to back, slot-linked packs), which `tools/tau_packs_info.py` and `tools/meter_bin.py` still parse for inspection and the P0 history.


**Who writes it:** the build (`tools/player2/build_packs.py`, called by `tools/package_dev_build.py` for every `PACKS=1` Player 2 package and so by `tools/make_release.py --player2`). **Who reads it:** the firmware (data slot 9, `fw2/stream/packfile.c`, `fw2/cores/meter_pack_core.h`). **Tau Omega reads it, never writes it.** It is core-specific (parameter bit 1 of the slot: `Assets/<platform>/<core id>/tau-packs.bin`), optional (a card without it runs: the firmware falls back to the `MPKB` section of `tau-assets.bin` if there is one, else shows Winamp Bars alone), and always wins over an `MPKB` section (hardware-confirmed on TAU DEV 184, B-837: `PACK SRC 2` with a stale section on the card). In `tau-compat.json` it is a `layout` entry `role: owned`, `slot: 9`, `required: false`.

## Bytes (all little endian)
```
bundle  0  'TPKB'                      magic
        4  u16 version = 1             a change of this format raises it; the firmware refuses another value
        6  u16 count                   the packs that follow, back to back (the file ends with the last one; there is no outer checksum)
pack    0  'TMPK'                      magic of each pack, header 48 bytes, then `image size` bytes of image
        4  u16 abi                     major << 8 | minor of the host table the pack was built against and NEEDS (ABI 1.2 = 0x0102 at most); another major, or a minor above the firmware's, is refused (E22)
        6  u16 meter id                the meter's index in the registry (meters/<key>/meter.json p2.slot = the manifest's order); the slot a pack belongs to is the same number
        8  u32 image size              code + read-only data + the initial values of .data + the meter descriptor trailer
        12 u32 origin                  the PSRAM address the pack is linked at (0x24900000 + slot * 0x10000); a pack for another slot is refused (E24)
        16 u32 entry offset            inside the image
        20 u32 image crc32             zlib CRC-32 of the image
        24 u32 .data offset in the image    28 u32 .data size
        32 u32 scratch origin          where .data and .bss run (the 4 KB on-chip meter scratch, 0x26C00)    36 u32 .bss size
        40 u32 pstate offset           44 u32 pstate size    (state kept in the slot's PSRAM beyond the image, zeroed, never in the file)
image   the last bytes are the meter's descriptor (section .mtr_desc: name, preview thumbnail, parameters, presets), found from the END of the image: tools/player2/meter_desc.py writes it, fw2/cores/meter_desc_core.h reads it
```
A reader needs nothing else to list what a core can show: walk the headers (`size` gives the next offset), read each pack's `meter id`, `abi` and descriptor trailer. The file's length is the sum of the headers and images; a file cut short is refused whole by the firmware (`mpkb_length`). The independent Python reader is `tools/tau_assets.py parse_packs` (checks the CRCs); the layout is pinned by `sim/test_p2_meter_pack.py` (the ABI fingerprints) and `sim/test_p2_meter_pack_dispatch.py` (the file reader).

## What changes it, and how Omega finds out
A new meter or a rebuilt pack changes the file's hash with every build: it belongs to the release like `tau.rom`, so Omega copies it with the core's other build-bound files and never merges it into `tau-assets.bin`. A new host-table entry or `mtr_in_t` field appends and raises the ABI minor (packs that do not need it keep running); a breaking change raises the major and `version`. `tau-compat.json` does not carry the pack list; the file's own headers do.
