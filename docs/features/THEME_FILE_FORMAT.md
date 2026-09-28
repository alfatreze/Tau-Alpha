# tau-assets.bin: the theme file (step 0d) and the meter preset file (M4), as built

Status: **built and host-tested 2026-09-26 (B-313); not yet read by a Pocket, so not frozen.** This is the as-built form of
`docs/features/THEME_SPEC.md` section 5 inside the container chosen in `docs/DECISIONS.md` D-M01. Where they differ, this file is right for the code.

## Where it lives
Data slot **8**, file `tau-assets.bin` in `Assets/<platform>/common/`. Optional and never shipped with a core: a missing file (or a core
that does not declare slot 8) simply means built-in themes. Written by `tools/tau_assets.py` or by Tau Omega. Read once at boot by
`fw/assets.inc` (cold code, after the library), Info > THEME FILE shows `NONE`, `n LOADED` or `E<code>`.

## Container `TAUA` (little endian)
```
header : "TAUA" | version u16 = 1 | section_count u16 (<= 8) | crc32 of the section table u32          (12 bytes)
table  : section_count x { tag[4] | offset u32 | length u32 | crc32 of the section u32 }                 (16 bytes each)
data   : the sections; offsets are from the start of the file
```
Sections today: `THEM` (themes) and `METR` (per-meter presets). Planned: `ICON`, `FONT`. The two fail independently. Unknown tags are ignored. The firmware caps the file at 1 KiB today.

## Section `THEM`
```
"TTHM" | version u16 = 1 | role_count u8 | theme_count u8 | crc32 of everything after this 12-byte header u32
theme  : name[16] (uppercase A-Z 0-9 space _ -, NUL padded, 1..15 chars) | bg_luma_dark u8 | bg_luma_light u8 | pad u16 = 0
         | dark[role_count] u16 RGB565 | light[role_count] u16 RGB565
```
- **Role numbers are the `TR_*` enum in `fw/theme.h`** (0 bg/top, 1 bg/bottom, 2 surface, 3 surface-track, 4 text/primary, 5 text/secondary,
  6 accent, 7 on-accent, 8 accent-2, 9 ok, 10 warn, 11 danger, then the extension roles 12..20). Append-only. Roles 0, 6 and 8 are not
  stored in a theme (bg/top is derived from the accent and `bg_luma`, accent is the user's palette pick, accent-2 is unused): their slots are ignored.
- A file with fewer roles than the firmware knows (`role_count` smaller) keeps built-in theme 0's values for the rest; more roles than the firmware
  knows: the tail is ignored. Up to 4 themes are used; extras are ignored. They appear after the built-in themes in Appearance > THEME.
- `bg_luma_*`: the luma the tinted background ramp is normalised to (built-in dark 45, light about 205). Clamped 20..235 by the firmware.
- Every theme must define both polarities. The writer refuses a theme whose text or accent contrast fails the rules in `tools/gen_themes.py`.

## Firmware rules (same precedents as the library and the cold image)
Version and every CRC are checked before any byte is used; any failure leaves the built-in themes and sets a code: 30 read failed, 31 bad magic,
32 bad version, 33 bad size, 34 CRC mismatch. A container without `THEM` is fine. Nothing here is persisted: theme and mode are session-only
until persist is widened (`docs/ROADMAP.md` item 8).

## Tools
```
python3 tools/tau_assets.py pack themes/user_examples/sunset.json -o tau-assets.bin
python3 tools/tau_assets.py dump tau-assets.bin
python3 tools/tau_assets.py install tau-assets.bin --core alfatreze.TAU_0_5_0_A_32      # copies to the card and verifies
python3 sim/test_tau_assets.py       # the real fw/assets_core.h against the reference reader; every byte flip and truncation is refused
```
Theme source JSON has the shape of `themes/*.json` (a `name`, `dark` and `light` objects with the 18 role keys and `bg_luma`, colours as `#RRGGBB`
or `0xRGB565`). `themes/user_examples/sunset.json` is an example.

## For Tau Omega
The exporter belongs in Omega: read the source of the colours (Figma variables per `docs/features/THEME_SPEC.md` section 3), snap to RGB565, run the
same contrast and both-polarities checks, write this file. **Not built here**: there is no real Figma `Tau Theme` collection yet to check an
importer against, and the rule in `docs/features/CROSS_PROJECT_INTERFACE.md` is to verify against a real captured artefact, not an invented one. What Omega
can rely on today: the byte layout above, `tools/tau_assets.py` as a reference writer/reader, and `sim/test_tau_assets.py` as the definition of
"refused". After a card has read a file, capture that file for Omega's fixtures.

## Section `METR` (M4): per-meter preset sets
```
"TMTR" | version u16 = 1 | 2 pad | crc32 of everything after this 12-byte header u32 | count u16
entry  : len u16 | id u8 | schema u8 | flags u8 | order u8 | npre u8 | nparams u8 | npre x { name[16] | nparams values } | default_preset u8
value  : one byte per u8, bool and enum parameter, two (LE) per u16 parameter, in the meter's manifest order
```
`id`, parameter order and widths come from `tools/meters_schema.json` (generated from `meters/*/meter.json`); the explicit `len` per entry (not in the
first design sketch) lets an old firmware skip a meter it does not know. What a file can do: **replace a meter's presets** (1 to 8, names of at most 15
characters) and **pick the boot preset**. What it cannot do: add a meter (meters are code) or change a parameter's range (the compiled table is the only
authority). Firmware rules: version and CRCs first; a meter the firmware does not know, a newer `schema`, a different `nparams`, a preset count outside
1..8, a size that does not add up or a default preset out of range skips **that meter only**; every value is clamped to the compiled range; names are
reduced to upper-case printable ASCII; a meter's presets are committed only when all of them parsed. `flags` (selectable override) and `order` (list
position) are carried and validated by the tool but **not applied yet**: reordering or hiding meters needs the meter list to become runtime data, which
touches how saved meter indexes resolve, so it is a separate step. Info > METER FILE shows `NONE`, `n LOADED` or `E<code>`.

```
python3 tools/tau_assets.py pack themes/user_examples/sunset.json --meters themes/user_examples/meters_example.json -o tau-assets.bin
```
The writer validates values against the registry (complete, in range) and refuses what the firmware would clamp. `sim/test_tau_assets.py` compiles the real
firmware reader with the generated meter tables: valid files apply exactly what the Python reader shows, every byte flip is refused, a corrupted METR
does not affect THEM, unknown/mismatched/oversized entries are skipped, out-of-range values are clamped.
