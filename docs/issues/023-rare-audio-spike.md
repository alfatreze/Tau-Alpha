# Rare, random audio spike (not reproducible, not attributed)

**Status: OPEN, logged for later analysis (2026-10-03). Not investigated. Not shown to be Cymo.**

## Observed

Owner, on `alfatreze.TAU_DEV_69` (Cymo resampler available, 44.1 kHz guard, B-527 feed queue), while listening with Cymo on: "sometimes I can hear a weird spike, but very rare." The owner has heard the same thing **before, while recording** (the loopback captures), and usually just records again to get a clean file. Not easy to reproduce; appears random.

## What is and is not known

- Not recorded: whether it happens with Cymo OFF, which track/format (MP3 or FLAC), the speed, whether it follows a menu/meter/screen action, how long into a track, and whether `S`/`D` (Info > CYMO RESAMP) or UNDERRUNS changed at that moment.
- Cymo as the cause is **not supported by anything measured so far**: in the B-529 loopback the Cymo-ON tone was stable (SINAD 54.5 dB, same pitch as OFF, no added noise on silence); S stays at 1 across a run (the one count is the first consume after a track restart, B-531) and D is 0.
- Seen "before when recording" implies it may predate Cymo, so the base audio path or the analog/capture chain are equally open.
- The capture-chain events in the B-529 recordings (single glitches at about 10.5 s and 30.5 s in every file, including the 48 kHz control) are start/stop artefacts of the chain, not this issue, but a rare spike could also be a capture-side event.

## Candidate causes (hypotheses, not findings)

1. A decode underrun (late refill): would show on the UNDERRUNS Info row.
2. A Cymo hand-off slip: would show as `S` or `D` increasing without a track change.
3. A card read or decoder hiccup on a particular frame.
4. The analog output or the capture interface (USB/ADC).
5. A UI draw burst contending with audio (the Layered Wave distortion re-check is also still open, UNDERRUNS row).

## Discriminators (when picked up)

1. When a spike is heard, read **S, D and UNDERRUNS** immediately and note whether any changed.
2. Record a long (5-10 min) 1 kHz 44.1 kHz tone through the loopback chain with Cymo **OFF** and again **ON**, then run `python3 tools/lab/cymo_loopback.py track <files> --freq 1000`: it flags clicks, dropouts and level steps with timestamps. A spike present with OFF is not Cymo.
3. If it appears in both, repeat once with the 48 kHz tone (no resampling in any path) to separate the decoder/DAC/capture chain from the resampler.
4. Only then consider instrumentation (a counter, or a JTAG probe), per the project rule to measure rather than guess.

Related: `docs/AUDIT_TRAIL.md` B-527, B-529, B-531; `docs/features/CYMO_AUDIO_ENGINE.md` section 6.8.

## Second observation (2026-10-03, TAU_DEV_71): noise profile changes after toggling the resampler ON then OFF

Owner: after switching Cymo ON and then OFF again, the OFF sound seems worse than the OFF sound heard from a cold boot, and the noise profile changes. Not measured; not attributed. Related to this issue only in that both are unexplained audio-quality observations around the resampler. Plan (later): boot with Cymo OFF (the default is now ON, so switch it OFF first thing, before playing anything), record the 1 kHz 44.1 kHz tone and the silence file; then switch ON, record; then switch OFF again, record; compare tone SINAD/spurs (`cymo_loopback.py track`/`analyze`) and the silence noise floor between the first and last OFF recordings. If they differ, suspects are state the toggle leaves behind (the EQ/output stage state, the FIFO's fill level or rate accumulator phase, the queue/resampler state, a pcm_rate change) -- not yet examined.

## Update 2026-10-03 (TAU_DEV_73): no spikes detected in this session
Owner, after testing DEV 73 (volume hold, default-on resampler, HEADROOM): "no spikes detected". One session, not proof it is gone; the issue stays open (it is rare and random). The same session's Info page read `UNDERRUNS 1` and `CYMO RESAMP LIVE POP S1 D2 Q3` (two queue-full drops since the last engage, after ALL SPEEDS and track changes); neither was tied to an audible event, so they are noted, not attributed. If D keeps rising during steady 1.00x playback, model it.

## 2026-10-08 (B-663): the first glitch captured on a recording, with nothing pressed

`105`-session take `tone_1k_48khz_85_test.wav` (TAU_DEV_105, 16-BIT ON, HW GAIN ON, core volume 85 and system volume maximum, Info page confirmed T11627 as expected, 1 kHz tone playing steadily at -12.0 dBFS for 13 s). At 14.985 s, with **no input from the owner** ("this is the rare glitch I mentioned"), the recording shows a burst of **95 ms**: a ramp of about 10 ms (-8.7 dBFS in the first 5 ms windows), then a plateau peaking at **-6.2 dBFS (6 dB above the tone)**, then an **abrupt return** to -12.0 in one 5 ms window. Inside the burst the waveform is not the tone: it is irregular and noise-like (THD against its own fundamental -24 dB against -42 dB in the steady part; one cycle of 48 samples shows jagged, non-sinusoidal values). Its size, 6.0 dB, equals the step of the 16-bit slot or a doubled gain, which may be a coincidence with a noisy burst. This is the first time a glitch is on a recording with the device state known, so the state at that moment is recorded: Cymo off, Halcyon engine off (DEV 105 has none), HW GAIN on, 16-bit on, FLAC tone file, screen on, default meter, no menu. Next: any glitch seen later in a soak should be matched against this signature (about 100 ms, a hard end, noise-like content, +6 dB peak) with `cymo_loopback.py track` events.

## 2026-10-08, second capture (B-664): `tone_1k_48khz_level-map.wav`, 12.27 to 12.37 s, owner pressed nothing, 16-BIT ON, HW GAIN ON, volume position 82 (plateau -28.75 dBFS)

A **100 ms** rise to -22.6 dBFS: **+6.16 dB** over the plateau, with a ramp of about 15 ms on each side (-24.3, -24.1, -23.2, -22.6 ... -23.2, -24.4, -26.2, back), and 82 % of the energy still in the 1 kHz fundamental (the first capture, at the volume position that behaved as 100, was 95 ms, +5.8 to +6.2 dB, noise-like, abrupt end). **Two captures, two different volume positions, both +6 dB and about 100 ms**: an exact doubling that does not depend on the volume, so it is not the gain stage (capped at unity). The 16-bit slot register is written only by the Diagnostics toggle (`fw/settingsui.inc`) and the hardware flag is sticky, so a flip of the 15-bit/16-bit mapping would have to come from a reset, which would not return after 100 ms. Not explained. Candidates to look at next: the I2S writer's read of its clock-domain FIFO (a repeated or merged word), and anything with a 100 ms period in the firmware (UI tick, meter, settings poll); the signature is now two clean events to test them against.
