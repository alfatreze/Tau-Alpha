# Roadmap and status (public summary)

A short, public version of where Tau is and where it is going. The ordered internal list is [ROADMAP.md](ROADMAP.md) (draft; the project owner sets the order),
the one-page state of things is [CURRENT_STATUS.md](CURRENT_STATUS.md), and the evidence behind every claim is [AUDIT_TRAIL.md](AUDIT_TRAIL.md). Nothing below is a promise or a date;
"planned" means agreed as a direction, not scheduled.

## Released: v0.5.0 (27 September 2026)

- Themes (TAU and OCEAN, Dark and Light, a corrected 19-colour accent palette, optional `tau-assets.bin` themes).
- Hardware MP3 synthesis-window unit (bit-exact, with automatic software fallback) and more CPU headroom.
- Fast pre-scaled covers (`.timg`, about 90 ms), hardware level and spectrum measurement.
- Winamp Bars and Scope with a Configure page, the Chladni meter with fullscreen and presets, tear-free meter drawing, full-screen menus with an action bar.
- Hardware-assisted drawing (blit engine, rounded rectangles, palette blits) and 64 KB of on-chip memory freed for later use by moving code and data to PSRAM.
- Diagnostic Build additions: Meter Sweep, Info export by QR code, more Check tests.

Full list: [CHANGELOG.md](../CHANGELOG.md). Known limits: `Track changes` fails in the Diagnostic Build's Check (playback unaffected); the release and diagnostic builds may restore the last track differently after
a restart; speeds above 1.20x are Diagnostic Build only.

## Next: the 0.6 items (decided by the project owner on 2026-09-27)

| Item | What it means | State |
|---|---|---|
| **Remember theme, mode and meter settings across a restart** | Widen the hardware settings-persist channel (a hardware change and a fit), then save the Configure values | Designed; needs a bitstream fit ([THEME_SPEC.md](THEME_SPEC.md), [METER_CONFIG_SPEC.md](METER_CONFIG_SPEC.md)) |
| **Use the alpha blend in the interface** | Translucent panels and fades using the pipelined blend, which closed timing on 2026-09-27 | Hardware built and fit-proven; not in a shipped bitstream; no firmware use yet ([ALPHA_BLEND_ANALYSIS.md](ALPHA_BLEND_ANALYSIS.md)) |
| **Fix the `Track changes` Check** | Find out why the Diagnostic Build's track-change test fails while real track changes work | Open, unexplained |
| **RAM shrink (256 to 192 KB)** | Free 64 on-chip memory blocks | Hardware fit-proven; the firmware still needs trimming to link ([RAM_SHRINK_192K_PLAN.md](RAM_SHRINK_192K_PLAN.md)) |

## Also planned or being considered

- **Batched drawing for the hardware scope path**, then removing the software paths it makes redundant (the 256-column scope drew about 21x a normal meter, so it is compiled out today).
- **Now-playing screen redesign** on the new theme roles and beam-aware drawing layer (Helios); full double buffering is designed but not built ([HELIOS_SPEC.md](HELIOS_SPEC.md)).
- **More hardware for audio:** FLAC bit-reader acceleration is the next candidate after the MP3 window unit; not started.
- **Cover format freeze:** the `TIM1` container becomes the default once the owner freezes it ([IMAGE_FORMATS.md](IMAGE_FORMATS.md)).
- **Meter modules:** meters described by one manifest each, presets from `tau-assets.bin`, a preview lab; the runtime is built and host-verified, waiting for more hardware runs ([METER_MODULE_SPEC.md](METER_MODULE_SPEC.md)).
- **Library extras** that need free RAM first (see [MEDIA_LIBRARY_0.4_SPEC.md](MEDIA_LIBRARY_0.4_SPEC.md) sections 14-15).
- **Companion app:** [Tau Omega](../../Tau%20Omega/) keeps its own roadmap.

## Parked on purpose

Tracker/MOD support, CJK and UTF-8 fonts, a public meter-preset gallery, the on-device Winamp configurator (may be replaced by authoring in Tau Omega), firmware modularization, and a
720 output resolution (deliberately last). See [ROADMAP.md](ROADMAP.md) section 3 and [DECISIONS.md](DECISIONS.md).
