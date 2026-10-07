# Recording plan for TAU DEV 105 and the measurement gates (2026-10-07)

Status: **PLAN SAVED, recordings not yet made.** Core under test: `alfatreze.TAU_DEV_105` (B-615 hardware gain stage on the `gain-b615` seed 1 bitstream, diagnostic firmware, 16-bit I2S switch, installed 2026-10-07). Owner's capture chain: Volt 1 interface, the Pocket's headphone jack to its input, WAV 48 kHz / 24-bit. Analysis tool: `tools/lab/cymo_loopback.py` (`analyze`, `track`). Background: `CYMO_HEADPHONE_PLAN.md` sections 11-12 (the existing recordings show an analogue noise floor of -76.4 dBFS, idle spurs at 6.18 / 12.30 / 18.48 kHz and signal-dependent sidebands).

## 1. Questions
Q1 Is the floor analogue (Pocket or interface) or digital? Q2 Does the hardware gain stage equal the firmware gain? Q3 What does the 16-bit I2S slot change? Q4 Where do the idle spurs come from?

## 2. Core and settings (set once, then leave)
- Core `TAU DEV 105`. Settings > Audio: EQ FLAT, REPLAYGAIN Off, volume as listed per run. Settings > Diagnostics: HW GAIN ON (NO UNIT means the bitstream is wrong), CYMO RESAMPLER OFF (48 kHz files do not use it), 16-BIT OUTPUT OFF except session 3, ACCEPT ALL RATES OFF.
- Pocket: note its own volume position and do not change it; battery, charger unplugged (except run 4d); screen on with the default meter (except where stated); nothing plugged into the Pocket's jack but the capture cable.
- Interface: same input and cable as the earlier takes; set the gain once so `tone_1k_48000` at core volume 94 reads about -12 dBFS peak, then never touch it (photograph the knob). Direct monitoring does not affect the recording.
- Files (folder `cymo_loopback` on the card): `tone_1k_48000.flac`, `silence_44100.flac`, optional `levels_44100.flac`. If missing on the core, they must be synced (card write: ask).
- Per recording: start recording, wait 3 s, start playback, record 12 s of steady tone or silence. Name `105_<test>.wav`, put in `test music/Audio Lab/`.

## 3. Runs
Session 0 (floors): `105_if_short` (interface alone, shorted 3.5 mm plug or the cable with the Pocket off, 15 s); `105_idle` (core loaded, nothing playing, 15 s).
Session 1 (volume, 15-bit; positions are 0.6 dB each: 100 = 0 dB, 70 = -18, 40 = -36, 10 = -54): `105_sil_v100/v70/v40/v10` (silence file) and `105_tone_v100/v70/v40/v10` (tone file).
Session 2 (HW GAIN A/B; start recording before pressing play so the 43 ms fade-in is captured): `105_hw_on_v94` / `105_hw_off_v94`, `105_hw_on_v70` / `105_hw_off_v70`, `105_hw_on_step` / `105_hw_off_step` (volume 94, then 70, then 94 during the tone).
Session 3 (16-bit; lower the volume before switching it ON, it gives about +6 dB): `105_a16_off_v94`; `105_a16_on_v84` (level-matched, 10 positions = 6.0 dB); `105_a16_on_v94` (same volume; lower the interface gain by about 6 dB first and note it); `105_sil_a16_on_v94` (silence).
Session 4 (spur source, silence 15 s): `105_spur_screen_on`, `105_spur_screen_blank` (Settings > Appearance > SCREEN BLANK), `105_spur_menu` (a menu open, static), `105_spur_charger` (charger connected, screen on).
Optional (needs a 32 ohm resistor across the jack): `105_load_v94`, `105_load_v100` (tone, 15-bit): distortion under load.
About 27 recordings, 25 minutes.

## 4. Predictions (written before the data)
- Floor at -76 +-1 dB across all volumes: analogue and fixed. A floor that falls with volume: digital.
- Fixed floor means tone SINAD falls about 1 dB per dB of attenuation (about 55 dB at volume 100, about 37 dB at volume 70).
- HW GAIN ON vs OFF: level within 0.05 dB, SINAD and THD within about 1 dB, same fade shape.
- 16-bit level-matched: SINAD the same as 15-bit if the analogue chain dominates; 16-bit at the same volume: about +6 dB SINAD if the noise sits after the DAC.
- Spurs: unchanged across all runs means analogue/supply; a change with screen blank or charger names the source.

## 5. Gates: developments that must wait for this plan (decision D-G01)
| Gate | Opens when | Developments held until then |
|---|---|---|
| **G-FLOOR** (Q1, sessions 0 and 1) | The floor is classified analogue-fixed or digital | Scope and priority of the final quantiser's dither, noise shaping, the 24-bit gain-stage output, and any work whose value depends on the digital noise floor; the order of volume mapping, start-volume, maximum-volume and peak-aware positive ReplayGain work (they move up if the floor is analogue) |
| **G-HWGAIN** (Q2, session 2) | HW GAIN ON equals OFF within the predictions | Putting `TAU_GAIN` in any release or alpha bitstream; making hardware gain the shipped default; the gain stage's extensions (per-channel balance targets, positive ReplayGain, 24-bit output); folding the EQ preamp into the gain target |
| **G-A16** (Q3, session 3) | The level-matched and same-volume results are read | Adopting or defaulting the 16-bit I2S slot; the loudness-neutral toggle policy for it; the clipper ceiling that depends on the extra +6 dB |
| **G-SPUR** (Q4, session 4) | The spur source is named | Any mitigation of the idle spurs (screen blank policy, power advice); the idle-noise claims in the user documentation |
| **G-LOAD** (optional run plus an output-impedance measurement) | THD under load and the amp's behaviour into 32 ohm are known | Headphone correction presets (the Tau Omega profile import and the engine's correction stage budget), the maximum-volume cap value, and the soft-clipper ceiling below -0.5 dBFS |
| **G-ISP** (a new inter-sample-peak test file, not yet built) | The codec's behaviour above 0 dBFS inter-sample is known | The final soft-clipper ceiling and any true-peak limiter |

**Not gated** (correct whatever the data say): rounding instead of flooring the 24-to-16 FLAC reduction, loudness-neutral ramping of the existing toggles, the resampler output widening, the infrasonic stage change (15-20 Hz), moving EQ state and the coefficient store to MLAB/M10K, 24-bit coefficient and EQ-input RTL, crossfeed and mono model and listening work, gapless load-latency measurement, Tau Omega parsers and data formats.
