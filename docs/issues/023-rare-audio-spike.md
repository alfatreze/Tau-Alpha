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
