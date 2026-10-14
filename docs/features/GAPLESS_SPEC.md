# Gapless playback: spec and sizing (B-723, 2026-10-14)

**Status: DESIGN + HOST MODEL + one measurement. Nothing built.** Parent plans: `CYMO_HEADPHONE_PLAN.md` section 8 (gapless re-scoped), `CYMO_AUDIO_ENGINE.md` F5/C4/C6, `AUDIO_FIRST_TRACK_LOAD_SPEC.md` (the load sequence). Model: `tools/lab/gapless_model.py`. Evidence labels: [READ] source, [HW] measured on a Pocket, [HOST] measured on the host files, [EST] estimate.

## 1. What gapless needs, in one line
At a natural track end the next track's first sample must reach the DAC before the last buffered sample of the old track has played: **buffered audio at the moment the old decoder reaches end of file > the load of the next track**, and the boundary must neither flush the buffer nor fade, and must cut the encoder's delay and padding.

## 2. Facts
- **Buffer today [READ]:** the hardware FIFO is 2,048 stereo pairs (46 ms at 44.1 kHz). `load_track()` begins with `pcm_flush()` (empties it and arms the fade-in) and runs synchronously: nothing is pushed until the next track's first frames are prefilled.
- **Load time [HW]:** a typical album track is **263 to 292 ms** (LOAD MS total, B-562: head 117 + size 18 + cover 12 to 115 + prefill about 120 to 170), a FLAC with a large metadata block **1,170 ms** (1.1 s head read, B-544). The audio-first spec's 3.7 s load with cover decode is history (TIM1 covers take about 90 ms, B-330). A manual change measured **1.48 s** press to sound (B-717, includes the press, the flush and the 44 ms fade-in); that is not a gapless case, a manual skip keeps its flush and fade.
- **Trim rule [HOST, proven here]:** `playable = frames x 1152 - delay - padding`, from the Xing/Info frame count and the LAME delay/padding (`fw/lame_tag.h`: skip `delay + 529` at the start, drop `padding - 529` at the end). On all **six** Test Album MP3 files (MacCunn and Clementi, 128 and 320 kbps, 44.1 and 48 kHz; delay 576, padding 832 to 1,668) the result equals the **FLAC sample count of the same recording exactly** (7,708,092, 7,345,884, 8,389,760, 7,995,520: difference 0). FLAC needs no trim (STREAMINFO has the exact count). Files without a LAME tag (other encoders) are played as today, gap and all.
- **Output chain [READ]:** gain stage fades only on flush or `FADE_NOW`; the Cymo resampler and the feed queue run on (no clear) when the rate is unchanged; the Cymo guard (44.1 kHz at 1.00x) follows `pcm_rate_apply()` and is unchanged across a same-rate boundary.

## 3. The sizing result (the reason the plan is what it is)
`python3 tools/lab/gapless_model.py` (gap = max(0, load - buffer depth), 44.1 kHz):

| Buffer | M10K | gap for 263 ms | 292 ms | 1,170 ms |
|---|---|---|---|---|
| today's FIFO, 2,048 pairs | 7 | 217 ms | 246 | 1,124 |
| RTL FIFO 8,192 pairs (186 ms) | 26 | **77 ms** | 106 | 984 |
| RTL FIFO 16,384 pairs (372 ms) | 52 | 0 | 0 | 798 |
| PSRAM ring 0.5 s (86 KiB) | 0 | 0 | 0 | 670 |
| PSRAM ring 1.5 s (258 KiB) | 0 | 0 | 0 | **0** |
| PSRAM ring 2 to 3 s (345 to 517 KiB) | 0 | 0 | 0 | 0 |

**The deeper RTL FIFO that the earlier plan proposed (8,192 pairs) does not make a typical track gapless (77 ms gap left) and 16,384 pairs takes 52 of the 68 free M10K for a result that still gaps on the slow FLAC.** A ring of decoded PCM in PSRAM, 1.5 to 2 s deep, covers every measured load with margin, costs no block RAM and no RTL.

## 4. Recommended architecture: a decoded-PCM ring in PSRAM (firmware only)
- **Producer / consumer split.** The decoder writes PCM into a PSRAM ring (`0xA4...`, a new region next to the tempo staging ring at `0xA4600000`; 2 s at 44.1 kHz stereo is 345 KiB, 48 kHz 375 KiB); a drain step moves pairs from the ring into the hardware FIFO whenever it has room. This is the structure the tempo funnel already uses (`fw/tempo_core.h`: planar PSRAM staging ring, drain through the push path, host-tested), so the mechanism is proven and the same drain serves MP3 and FLAC.
- **Boundary = a marker in the ring.** At end of file the decoder opens the next track WITHOUT `pcm_flush()` and without the loader UI (`load_track_gapless()`), applies the trim, and keeps writing into the same ring behind a boundary marker carrying the next track's identity. The drain pops the marker when it reaches it and only then switches the UI, the ReplayGain target (the stage's 5 ms ramp) and the position clock: the screen follows what is heard, not what is decoded.
- **Rules at a boundary:** same sample rate only (the FIFO drain rate is global; a rate change keeps today's flush path); no `FADE_NOW`, no `pcm_flush`; the Cymo guard and the resampler state untouched; a manual Next/Previous/seek/stop flushes the ring (the hardware FIFO too) and fades in as today.
- **Cost [EST]:** one extra PSRAM write and read per pair, measured access cost 26 and 32 cycles (B-022): about 58 cycles of about 1,512 per pair at 44.1 kHz and 66.667 MHz, **about 4% of the CPU**; the hard stereo FLAC (idle 45% after the Rice fast path, B-562) can afford it. Decode-ahead must be paced (for example 1.25x real time) so the first 2 s of a track do not saturate the CPU and starve the UI and the meters.
- **Side benefit, independent of gapless:** a 1.5 to 2 s buffer absorbs SD stalls and UI bursts, the biggest lever on non-decode underruns (`CYMO_AUDIO_ENGINE.md` F5); it would also hide the slow-head FLAC loads at a manual change if the user accepted the position lag (not planned).
- **Not recommended:** a second decoder (RAM), a deeper RTL FIFO as the main fix (section 3), cross-rate gapless (needs a drain-rate change; a 44.1 to 48 kHz boundary stays a short gap).

## 5. Open design points (decide before building)
1. **Playback position and seek.** The decode position runs up to 2 s ahead of the heard position; the clock, the progress bar and seeking must use the heard position (decode position minus the ring fill). Seek flushes the ring.
2. **Pause.** The ring keeps its content; resume plays it (no re-decode). Stop and track-end-with-repeat-one behave as today.
3. **Which track is next.** Library queue order, shuffle and repeat decide it (`lib_q`); the next track is chosen when the old file ends. A user skip before the boundary flushes everything (normal manual path).
4. **ReplayGain across the boundary.** The next track's tags are read at open, the target changes at the marker (ramp 5 ms), so two tracks with different gains meet without a step; with positive gain the cap of B-720 is per track.
5. **Cover and tags.** Decoded at open (about 90 ms with TIM1, up to a few hundred ms otherwise) while the ring plays; the display switches at the marker. A track with no tau-art cover and a big embedded JPEG (seconds) is the worst case and still fits a 3 s ring; it is the reason the ring is sized by the slowest measured load, not the average.
6. **Tempo.** The tempo funnel (MP3, speed above 1.00x) has its own staging ring; gapless with tempo is a later step and should reuse one ring.
7. **FLAC hi-res and refused files.** A refused file (96 kHz) ends the gapless chain: the old track plays out, the refusal screen appears after it.

## 6. Build order and gates
| Step | Work | Type | Gate |
|---|---|---|---|
| **G0** | Measure: LOAD MS and the time from the old file's EOF to the next first pushed pair on real library tracks (MP3 and FLAC, with and without cover), natural end and manual change; LAME delay/padding of every library MP3 (host, done for the Test Album: section 2) | firmware instrumentation (Diagnostic Build) + host | numbers in the audit trail; the 2 s ring size confirmed |
| **G1** | The ring and the drain, no boundary behaviour: same audio, 1.5 to 2 s deep, paced decode-ahead; position/seek/pause/stop on the heard position | firmware (cold where possible), host model first | host test: stream pushed equals the stream decoded, sample for sample, through seeks, pauses and flushes (as `sim/test_pcm_burst.py`); Pocket: CPU busy within 5 points, no new underruns, hard FLAC clean |
| **G2** | The gapless boundary: `load_track_gapless()`, trim, marker, UI and ReplayGain at the marker, same-rate rule | firmware | host: two-file boundary reproduces the exact concatenated sample stream (trimmed MP3 equals the FLAC of the same recording); Pocket: loopback recording of the boundary shows no silence (a continuous tone split in two files keeps its pitch track and level, `cymo_loopback.py track`) |
| **G3** | Cross-rate boundary policy, shuffle/repeat corner cases, refused-file ends | firmware | owner listening on a real live/classical album |
| **G4 (optional)** | Crossfade (needs the ring and a mix stage) | firmware or RTL | only if asked |

**Test material to make:** a continuous 1 kHz tone and a piece of music split into two FLACs at an arbitrary sample, and the same split MP3s encoded with LAME; the Test Album already provides the FLAC/MP3 length pairs (section 2).

## 7. Risks
- The ring and the drain touch every audio push site (MP3, FLAC, tempo) and the track-load path: the largest firmware change in the audio loop since the dB volume, so G1 ships alone and is heard before G2.
- PSRAM contention with instruction fetch for cold code and the art decode (the ring adds about 350 KB/s: small, but the Check soak must run with it).
- Hot RAM: the drain and the marker handling must live in cold code or fit the 1,120 B Diagnostic gap (it is tight, B-720 trim note); budget the first G1 build against `check_heap_gap.py` and a `DIAG_DROP` if needed.
- A wrong trim is audible as a click at every boundary; the six-file exact match (section 2) is the guard, the host stream test the gate.
