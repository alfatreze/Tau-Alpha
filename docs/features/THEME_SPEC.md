# Theme spec (design only, nothing built)

Status: decided with the owner 2026-09-25. Extends `docs/features/HELIOS_SPEC.md` section 7.4 (colour/Figma). No firmware, RTL or tool changes yet.

## 1. Decisions

- **12 colour roles** per theme (table below).
- **Polarity is global.** One Settings switch, Dark or Light, applies to every theme. Every theme must therefore define both polarities.
- **Status colours are themeable** (`ok`/`warn`/`danger`, the VU ladder's green/amber/red). They are roles like any other, so a theme may keep the classic traffic-light values or replace them.
- Solid colours only. No per-pixel alpha exists in the hardware (7.4).

## 2. The 12 roles

| # | Role (Figma variable) | Used for | Today's constant |
|---|---|---|---|
| 0 | `bg/top` | Top of the background gradient | derived from `ui_accent` (`ui_grad_set`) |
| 1 | `bg/bottom` | Bottom of the gradient | fixed black |
| 2 | `surface` | Panels, cards | `UI_PANEL` |
| 3 | `surface-track` | Unfilled meter/progress track | `UI_TRACK` |
| 4 | `text/primary` | Titles | white |
| 5 | `text/secondary` | Artist, secondary lines | `UI_DIM` |
| 6 | `accent` | Selection, progress, meter fill | `ui_accent` |
| 7 | `on-accent` | Text on accent fills | black/white by luma |
| 8 | `accent-2` | Peak caps, secondary meter colour | none yet |
| 9 | `ok` | VU ladder low | fixed green |
| 10 | `warn` | VU ladder middle | fixed amber |
| 11 | `danger` | VU ladder top | fixed red |

`UI_FAINT` (filename line) is not a role: the firmware derives it as `ui_mix(text/secondary, bg/top)`. If a theme needs it free, that costs a 13th role.

## 3. Figma structure

- One variable collection `Tau Theme`, colour variables named exactly as the role column (slash groups are fine).
- Modes are `<Theme>/Dark` and `<Theme>/Light`, for example `Ocean/Dark`. Every theme has both. A theme missing a polarity fails export.
- Description field of each variable carries the contrast requirement, for example `text/primary on surface >= 4.5:1`.
- Values should be pre-snapped to RGB565. The export tool snaps anyway and reports the delta per role.
- Icon and meter components fill layers with these variables only, never raw hex. Layers that must not follow the theme are tagged `fixed` in the layer name.

## 4. How the device applies it

- **Procedural drawing** (bars, wave, LED, VU ladder, panels): replace the hard-coded constants and `ui_palette[]` with a role table indexed by role number. `ui_mix()` calls keep working on role values. Ladder code reads `ok/warn/danger` instead of its fixed palette.
- **Palette-indexed bitmaps** (meter thumbnails, future icons): stored as 8-bit indices in greyscale steps. At draw time the CLUT (B8) is loaded from roles: step 0 maps to `surface`, step 7 to `accent`, intermediate steps to `ui_mix` of the pair. Switching theme or polarity only reloads the CLUT; the bitmap is untouched. B9 (re-index) allows several theme variants from one base bitmap where a straight ramp is wrong.
- **Fixed layers** (tagged `fixed`) keep their own colours.
- **Text AA gamma table** is fitted to palette and background. A new theme set needs `tools/gen_text_gamma.py --check`, or regeneration. Dark and Light both need checking.

## 5. Theme file (proposed binary, data slot not assigned)

Little-endian. Data slot 5 is the library and slot 6 the cold image, so a new slot number is needed and is deliberately not chosen here.

```
header  : magic "TTHM" (4) | version u16 = 1 | theme_count u16 | crc32 of everything after the header u32
theme[] : name[16] ASCII, zero padded
          dark[12]  u16 RGB565, role order 0..11
          light[12] u16 RGB565, role order 0..11        (= 16 + 48 = 64 bytes per theme)
```

Rules: append-only role order; unknown higher roles ignored by old firmware; bad CRC or version falls back to the built-in default theme (never blank screen). The persisted settings gain "theme index" and "polarity"; persist words are full (see METER_CONFIG_SPEC), so those two values need the same solution as the meter settings (RTL persist widening, or session-only until then). Open item.

## 6. Tooling (Tau Omega)

Export path: Figma variables JSON -> Omega reads the modes -> snaps to RGB565, reports the per-role delta, checks contrast, checks both polarities exist -> writes the file above and a preview. Registered as a new surface in `docs/features/CROSS_PROJECT_INTERFACE.md`. Omega should validate against a real captured file, not an invented fixture.

## 7. Build order (not started)

1. Role table in firmware with the current look as the built-in default theme (no visible change; proves the plumbing).
2. Polarity switch and a second built-in theme, on device, with the gamma check.
3. Convert VU ladder, meters and thumbnails to roles and CLUT-from-roles.
4. Theme file loader plus Omega exporter.

## 8. Open items

- Persistence of theme index and polarity (see section 5).
- Whether `text/faint` earns a 13th role after seeing real themes.
- Whether the Light polarity needs different `bg` handling: the background ramp is normalised to a fixed dark luma today, and a light ramp inverts that assumption.
