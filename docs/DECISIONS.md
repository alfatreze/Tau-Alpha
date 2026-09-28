# Decisions register

Numbered decisions with reasons, so an assessed option is never re-litigated. Rejected or parked options are recorded
too, with why. Newest last. Detail lives in the spec each entry points at.

## Meter module structure (2026-09-25, B-274) -- `docs/features/meters/METER_MODULE_SPEC.md` section 25

| # | Decision | Status |
|---|---|---|
| D-M01 | One `tau-assets.bin` container (themes, meters, later icons and fonts) instead of one slot per asset type | Decided |
| D-M02 | Generic on-device Configure page kept, Diagnostic-Build-only | Decided |
| D-M03 | Omega consumes a versioned preview bundle from a Tau-Alpha release and never forks it | Decided |
| D-M04 | Lab maths: JS port + golden vectors by default; WASM per meter only if a `wasm32` toolchain is proven | Decided |
| D-M05 | `role[]` theme roles are part of the meter contract from day one | Decided |
| D-M06 | A function enters the shared meter core when a second meter needs it | Decided |
| D-M07 | Legacy meters stay exactly as they are; any look change is opt-in per meter with owner approval | Decided |
| D-M08 | Cost-guard fallback is per-meter declared, defaulting to the `VIZ_BARS`-equivalent | Decided |
| D-M09 | Preset sharing formats: `.tmeter` JSON, `.tmeterpack` zip, `tau-meter:` text; binary only on device | Decided |
| D-M10 | Templates live in the firmware registry only; user variety comes from presets | Decided |
| D-M11 | Licence for shared presets and packs is MIT | Decided (owner) |
| D-M12 | Public preset gallery | **Parked** (owner) |
| D-M13 | Build order M0 to M6 as in the spec | Decided |

### D-M12 detail: public preset gallery (parked)

What it is: a hosted, browsable collection of user-submitted `.tmeter` / `.tmeterpack` presets that Omega could search
and install from.

Why parked: it needs hosting, identity, moderation and takedown handling, none of which improve the device, and the
formats already support file and text-string exchange without any of it. Nothing built today depends on it.

Residual value kept so a later gallery is cheap: the content-addressed preset `id` (dedupe), the `licence` field
(MIT default), the optional `preview` block (trace + frame hash), and the compatibility fields (`meter`, `schema`,
`requires`, `min_firmware`). Revisit after M4 has real users; if revisited, start as a static, curated index in a git
repository (reviewed pull requests) rather than a service.

## Cover / thumbnail image format (2026-09-26, B-284) -- `docs/features/IMAGE_FORMATS.md`

| # | Decision | Status |
|---|---|---|
| D-I01 | Cover art variant is palette 256 (CLUT + 8-bit indices, `TIM1` container) at 128 px on the long side | Decided (owner) |
| D-I02 | Non-square covers scale proportionally, no crop and no letterbox; the file carries the real width and height | Decided (owner) |
| D-I03 | Better on-device codecs (WebP, AVIF, JPEG XL, HEIF) rejected: entropy decoding, loop filters and RAM compete with audio on a 60 MHz core without an FPU | Rejected |
| D-I04 | BC1 and pre-scaled JPEG kept as named variants, not defaults: on the owner's nine real covers palette beat BC1 on every one and pre-scaled JPEG is smudged at small sizes and 5x slower to show | Parked |
| D-I05 | Container `TIM1` and the `tau-art/` sidecar path stay unfrozen until a firmware reader exists | Open |

