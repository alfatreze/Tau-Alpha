# Roadmap and status (public summary)

A short, public version of where Tau is and where it is going. The ordered internal list is [ROADMAP.md](ROADMAP.md) (draft; the project owner sets the order),
the one-page state of things is [CURRENT_STATUS.md](CURRENT_STATUS.md), and the evidence behind every claim is [AUDIT_TRAIL.md](AUDIT_TRAIL.md). Nothing below is a promise or a date;
"planned" means agreed as a direction, not scheduled.

## Released: v0.6.0-alpha.1 (30 September 2026)

- FLAC's LPC/FIXED reconstruction now runs in hardware, alongside v0.5.0's MP3 synthesis-window unit.
- On-chip RAM reorganised and shrunk (256 to 192 KB, freeing 64 blocks) and the system clock raised to
  66.667 MHz, both fit and shipped together.
- Theme and Dark/Light mode are remembered across a restart (the accent colour already was).
- The alpha blend closes timing and ships, used for a hardware cross-fade on Settings menu transitions.
- Several meter bugs fixed: fullscreen Winamp Bars no longer clip at 127 rows, Winamp Scope's fade trail no
  longer accumulates, the Chladni meter no longer goes blank after leaving Settings/fullscreen/Configure, and
  all four live-preview meters animate correctly on the Configure page. MASTER VU (listed but dead since
  v0.5.0) now works.

Full list: [CHANGELOG.md](../CHANGELOG.md). Known limits: `Track changes` still fails in the Diagnostic
Build's Check (playback unaffected); meter Configure settings are still session-only (theme/mode are not);
speeds above 1.20x are Diagnostic Build only.

## Released: v0.5.0 (27 September 2026)

- Themes (TAU and OCEAN, Dark and Light, a corrected 19-colour accent palette, optional `tau-assets.bin` themes).
- Hardware MP3 synthesis-window unit (bit-exact, with automatic software fallback) and more CPU headroom.
- Fast pre-scaled covers (`.timg`, about 90 ms), hardware level and spectrum measurement.
- Winamp Bars and Scope with a Configure page, the Chladni meter with fullscreen and presets, tear-free meter drawing, full-screen menus with an action bar.
- Hardware-assisted drawing (blit engine, rounded rectangles, palette blits) and 64 KB of on-chip memory freed for later use by moving code and data to PSRAM.
- Diagnostic Build additions: Meter Sweep, Info export by QR code, more Check tests.

## Next

| Item | What it means | State |
|---|---|---|
| **Fix the `Track changes` Check** | Find out why the Diagnostic Build's track-change test fails while real track changes work | Open, unexplained |
| **Remember meter Configure settings across a restart** | `interact.json`'s 16-entry display cap left no room this round; needs either freeing an existing entry or a different storage path | Open |

## Also planned or being considered

- **Batched drawing for the hardware scope path**, then removing the software paths it makes redundant (the 256-column scope drew about 21x a normal meter, so it is compiled out today).
- **Now-playing screen redesign** on the new theme roles and beam-aware drawing layer (Helios); full double buffering shipped in v0.6.0-alpha.1, the wider redesign is designed but not built ([HELIOS_SPEC.md](HELIOS_SPEC.md)).
- **More hardware for audio:** an MP3 IMDCT kernel is the next candidate after the FLAC LPC unit; not started.
- **Cover format freeze:** the `TIM1` container becomes the default once the owner freezes it ([IMAGE_FORMATS.md](IMAGE_FORMATS.md)).
- **Meter modules:** meters described by one manifest each, presets from `tau-assets.bin`, a preview lab; the runtime is built and host-verified, waiting for more hardware runs ([METER_MODULE_SPEC.md](METER_MODULE_SPEC.md)).
- **Library extras** that need free RAM first (see [MEDIA_LIBRARY_0.4_SPEC.md](MEDIA_LIBRARY_0.4_SPEC.md) sections 14-15).
- **Cymo, targeting v0.7.0**: a real-hardware 44.1 kHz audio quality issue, still unexplained. Tagged
  `v0.7.0-dev.1` as a checkpoint (not buildable yet). Two leading hypotheses have both been tested
  directly against real hardware and ruled out: jitter in the I2S master clock (a dedicated PLL was
  built and A/B tested -- SINAD/image levels matched the old generator within measurement noise, kept
  anyway for a separate 3.7 dB level anomaly it did fix) and the I2S clock-domain crossing itself (a
  JTAG-free diagnostic measured real update intervals matching theory almost exactly at both 44.1 kHz
  and 48 kHz). The remaining, still-untested candidate is something past the serializer -- the DAC or
  analog output stage, outside this core's RTL. Separately, a real resampler design was modelled
  (not built): a 32-tap Kaiser polyphase FIR predicts 77-86 dB SINAD for 8 M10K blocks, a large margin
  over the current hold's measured 10.8 dB, but nobody has done an actual listening test yet to
  confirm the defect is even audible on real music before spending RTL effort on it.
- **Companion app:** [Tau Omega](../../Tau%20Omega/) keeps its own roadmap.

## Parked on purpose

Tracker/MOD support, CJK and UTF-8 fonts, a public meter-preset gallery, the on-device Winamp configurator (may be replaced by authoring in Tau Omega), firmware modularization, and a
720 output resolution (deliberately last). See [ROADMAP.md](ROADMAP.md) section 3 and [DECISIONS.md](DECISIONS.md).
