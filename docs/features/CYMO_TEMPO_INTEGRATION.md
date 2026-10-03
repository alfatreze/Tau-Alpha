# Cymo C7: putting tempo (pitch-preserving speed) into the player

Design, 2026-10-03 (B-551). Nothing here is built. It turns the finished, tested core (`fw/wsola_core.h`, B-549/B-550) and the pause-shortening presets (B-536/B-537) into a player feature, using only things measured in this repository. Where a number is an estimate it says so. Parent plan: `docs/features/CYMO_AUDIO_ENGINE.md` section 7 (audiobook tempo, owner scope B-425).

## 1. What the owner asked for, and what the example shows

- Audiobook speed that sounds like the same voice (WSOLA), plus optional pause shortening: **Off (default), Small, Medium, High**.
- The one example audiobook (Test Album track 11, Twain/LibriVox) is **mono, 44.1 kHz, 128 kbps MP3**. Every other test track is stereo (44.1/48/96 kHz). Stereo audiobooks will exist, so the design supports both from the start and says what stereo costs.
- Firmware only. No RTL, no bitstream change, no new fit: the feature probes nothing and runs on every bitstream that plays audio today.

## 2. The idea in one picture

```
                         today (varispeed)                              new (tempo)
 decoder --PCM--> cymo_push --> FIFO drains at N x rate         decoder --PCM--> staging --> WSOLA --hops--> cymo_push --> FIFO drains at 1 x rate
```

Varispeed changes the rate the FIFO drains, so the decoder must deliver N x, in frame-sized bursts, into a FIFO that holds only 23 ms at 2 x: B-547/B-548 measured 516 empty-FIFO events at 2.00 x and zero at 1.00 x. Tempo keeps the FIFO at the **native** rate (the same condition as normal 1.00 x playback, which measures zero stalls), and the stretcher consumes input N x faster than it produces output. The decoder still runs N x faster, but its bursts are absorbed by a staging buffer instead of the 23 ms FIFO. Volume, fade-in and the audio write stay in the one funnel, `cymo_push()` (B-533).

## 3. Where it hooks in (read from `fw/player.c`)

| Existing | Tempo change |
|---|---|
| `flac_emit()` pushes each decoded pair through `cymo_push()` (called about every 64 samples while the second channel decodes, so decoding and pushing interleave) | when tempo is on, append the pair to the staging buffer and run as many stretcher hops as the staging allows; each hop's output pairs go to `cymo_push()` (which still blocks on a full FIFO and services input and I/O) |
| MP3 loop pushes `pcm[]` pairs after each Helix frame | same funnel: append, then drain through the stretcher |
| `pcm_rate_apply()` scales the FIFO drain by `speed_idx` | in tempo mode the drain stays at the file rate; the old speed list stays as varispeed, mutually exclusive with tempo |
| `meters_feed()` is fed at the decode rate | in tempo mode feed it from the stretcher's OUTPUT (every 1,152 pairs, as now), so the meters move at real time, not N x |
| `pcm_flush()` at track start, seek, resume, stop | also resets the stretcher (see section 6) |
| `ui_sec` advances from decoded `frames` | unchanged: the clock shows position in the FILE, which at tempo N moves N x faster than real time; that is correct for a book (it is where you are in it). The audible position lags by the staged samples, below 0.1 s |
| `cymo_guard_apply()` engages the resampler only at 1.00 x | in tempo mode the FIFO runs at the native rate, so the resampler can stay engaged at any tempo; the guard becomes "varispeed active", not "speed != 1.00 x" |

Mono: the funnel always hands over a left/right pair (a mono file has L = R); the stretcher is told the file's real channel count (1 or 2) and processes one channel when it is 1.

## 4. Components

1. **Staging ring** (PSRAM window, proposed offset 0x500000, 256 KB, clear of the library image at +0x10000 (4 MiB cap) and the cold image at 8 MiB; to be confirmed against the PSRAM map before use): the decoder writes decoded samples sequentially; the stretcher reads windows from it. 256 KB is 1.5 s of stereo or 3 s of mono at 44.1 kHz, enough for the stretcher window (about 4,100 samples at 3 x) and the pause-shortening lookahead (section 7).
2. **Stretcher** (`fw/wsola_core.h`, unchanged API: `ws_need()` says which absolute sample range the next step reads, `ws_step()` produces one 512-sample hop).
3. **Funnel** (`tempo_push(l, r)`): the new function the two decode paths call instead of `cymo_push()` when tempo is on; it owns the staging write pointer, runs the hops, and calls `cymo_push()` for every output pair.
4. **Settings and persistence** (section 8) and the **guard** (section 9).

## 5. The memory problem, and the choice it forces

The core as built reads a contiguous on-chip window. The window `ws_need()` asks for is about 2,400 samples at 2 x and 3,700 at 3 x per channel (measured from the geometry: the previous grain lands up to a search radius off its nominal place). Putting that on chip costs 5-8 KB at 2 x (mono) and 8-17 KB stereo.

| Build | Free on chip at run time | Fits? |
|---|---|---|
| release | 52,720 B heap gap (B-538) | yes (also holds the 2.1 KB step scratch and the 2 KB overlap state) |
| Diagnostic / profile | 5,088 B (floor 4,096) | **no** |

Reading the window straight from the PSRAM window instead costs 32 cycles per halfword load (B-022/B-054): the core touches about 4,400 samples per grain (three decimation passes re-read the same region), about 140,000 cycles per grain against a grain period of 774,000 cycles: 18% of the CPU, and it triples the stretcher's cost. Not acceptable.

**Recommended:** change the core, with the same golden-test discipline, so that (a) the input is decimated ONCE, as it enters the stretcher, into a small on-chip ring at the stage-2 rate (about 480 entries, 1 KB; each input sample is touched once instead of about four times, which also cuts the stretcher's own instruction count), and (b) the full-rate samples it still needs are fetched in blocks from the PSRAM staging ring: the 128-sample reference and the candidate window of the fine stage (about 300 samples) and the 1,024-sample grain (two 512-sample chunks into a 1 KB on-chip buffer). PSRAM block reads of about 650 words per grain are about 21,000 cycles, 2.7% of the CPU. On-chip total about 5 KB for mono (2.1 KB step scratch, 1 KB decimated ring, 1 KB chunk, the overlap tail 1 KB held on chip) and about 8 KB for stereo; with the overlap tail also in PSRAM about 4 KB and 6 KB. That still does not fit the Diagnostic build's 5 KB gap for stereo, so:

**Owner decision 1.** Build and test tempo first in the **release-style build** (52 KB free; the Info page and its HEADROOM row already exist there), and bring it to the Diagnostic Build only after a stack-and-heap pass (B-230's stack peak is 1,672 B; the Diagnostic stack is 6 KB with 2.4 KB of Layered Wave scratch already counted, so 2.1 KB more inside `flac_emit` is a stack-overflow risk that needs its own check, section 12 risk 1).

Code size: the core is 5.4 KB. It runs from the cold-code PSRAM alias if the instruction cache holds its small loops (the dot product is about 20 instructions); that must be MEASURED (cycles per grain in cold code against hot) before choosing hot or cold.

## 6. Behaviour the player already has, and what tempo does to it

| Event | Behaviour |
|---|---|
| Track start, seek, stop, track change | hard reset: `pcm_flush()` also empties the staging ring and restarts the stretcher; the existing fade-in covers the first hop |
| Pause then resume | `pcm_flush()` is called on resume (it must be, for the FIFO glide); a hard reset would throw away up to about 0.15 s of staged audio, so tempo uses a **soft** reset: keep the staged input, restart only the overlap (the first hop after it is raw, under the fade-in). Nothing is skipped |
| Underrun | unchanged (fade-in, counters). The Info `UNDERRUNS n ALL m` row now counts every stall (B-546), so the first tempo runs can be judged |
| Position and bookmarks | derived from the decoded frame count as now; error under 0.1 s |
| Speed list (0.85x-1.20x) | stays as varispeed. Tempo has its own setting; choosing one clears the other. Varispeed above 1.2 x remains hidden (it stalls, B-547) |
| End of file | the stretcher cannot read past the last decoded sample; the final up to about 0.1 s is output raw (the last grain is padded by repeating the tail) and the track ends as it does today |

## 7. Pause shortening

Streaming, causal at the decision point, with lookahead. The host model (`tools/lab/cymo_tempo_model.py`) used a window-percentile noise floor and a whole-file view; the Pocket version needs:

- a 10 ms energy envelope of the decoded mono mix (cheap: one multiply-accumulate per sample, folded into the decimation pass);
- a causal noise floor: an asymmetric tracker (falls fast to a quiet envelope, rises very slowly), which settles at the pause level like the model's 5th percentile; its constants are to be tuned against the model on the Twain clip, with the model's result (Small/Medium/High = 3.0/6.2/9.8% saved on 331 s) as the target;
- hysteresis and the three presets' four constants (minimum pause, kept fraction, floor, maximum cut) plus the detector margin;
- a **delay line of about 1.2 s** (the longest pause considered plus its edges) in the staging ring: the envelope runs on the newest samples, and a cut is decided when a pause has ended (or reached the maximum), applied to the oldest samples that have not yet reached the stretcher. A cut removes the middle of the pause, never the 60 ms at either edge, with a 10 ms crossfade. 1.2 s costs 106 KB mono, 212 KB stereo of the 256 KB ring; stereo with a long delay line may need a 512 KB ring (open).

It is a separate stage between the staging ring and the stretcher and can ship after plain tempo (phase T3). It only runs in tempo mode. The remaining-time display drops faster than real time while pauses are skipped; label or smooth it (UI decision).

## 8. Settings, persistence, UI

- Playback > **TEMPO**: Off (default), 1.10x, 1.25x, 1.50x, 1.75x, 2.00x; the available top end is capped by the guard (section 9). Finer steps (the model allows 0.05) can follow.
- Playback > **PAUSES**: Off (default), Small, Medium, High (section 7). Hidden or inert when tempo is Off.
- Persistence: one persist word holds both (the persist file is 32 words since B-346; the exact free word is checked before use). Not persisted in the first test builds, like the other experimental toggles.
- Info page: HEADROOM (B-538/B-541/B-544), UNDERRUNS `n ALL m` (B-546) and one new row for the tempo state (active speed, hops, grains, bytes staged, stretcher cost) are the evidence the first runs need.
- The settings-menu hold-to-repeat (B-534) already makes stepping through the list comfortable.

## 9. Fail-safe: tempo must never make playback worse than 1.00 x

1. **Eligibility:** file rate 22.05-48 kHz and 1 or 2 channels; anything else plays at 1.00 x with a short message (96 kHz FLAC is already refused for speed reasons, B-356).
2. **Headroom guard:** the worst second of plain playback (`hr_t.min_idle`, B-539) and the stall counter (`ur_all`, B-546) are already measured. If a stall occurs or the worst idle falls under a threshold (initially 8%) for a few seconds, tempo steps down one setting and says so ("TEMPO 1.50x"); it never steps back up on its own within a track. This turns the unmeasured stereo cost into a runtime decision.
3. **Pure firmware:** no bitstream dependency, so no probe or version interlock is needed; the failure mode of a bug is "no tempo", reachable by the Off setting at any time.

## 10. CPU budget (all [EST] except where marked)

| Part | Mono 44.1 kHz | Stereo 44.1 kHz | Source |
|---|---|---|---|
| Stretcher core (current form) | 8% at one instruction per cycle, about 10-12% real | about 25% more (the mono mix reads both channels; the grain and overlap are doubled) | **measured instruction count** B-550; real CPI not measured |
| Same with incremental decimation (section 5) | about 5-6% | about 8% | [EST] from removing about 3 of the 4 decimation passes |
| Staging writes (decoded samples into PSRAM) | about 1% | about 2% | 26 cycles per word written, B-022 |
| PSRAM block reads for the fine stage and grain | 1.6-2.7% (1.5x-2.0x) | twice that | 32 cycles per word read, B-054 |
| Decode work | FLAC mono 40% busy at 1.00 x, 51% at 1.50 x, 67% at 2.00 x; MP3 mono 128k 44% at 1.00 x | **not measured** | B-545, B-539 |

Mono total at 2.00 x: about 67% + 12% = 79% (about 20 points free); at 1.50 x about 51% + 11% = 62%. **Stereo audiobooks are the open question**: the decode work is roughly double for stereo MP3/FLAC, so tempo above about 1.25x may not fit; that needs a measurement before any promise (section 11, step 0).

Burst check (why the FIFO is no longer the limit): at 1.00 x playback the longest decode burst between pushes is about 40 ms against 46 ms of FIFO, and it measures zero stalls (B-548); with tempo the FIFO drains at the same native rate, and the decoder's frame still decodes while the FIFO is full. Tempo does not add a longer burst; the stretcher's own hop (about 0.35 ms of CPU per 512 output samples at 1.00 x equivalent) runs between pushes where the CPU is otherwise waiting.

## 11. Build and verification order, each phase gated

| Phase | Work | Gate |
|---|---|---|
| T0 measure | HEADROOM and UNDERRUNS ALL on a **stereo 128 kbps MP3** (Test Album track 02 or 07) and a stereo FLAC at 1.00x/1.50x; cold-code versus hot-code cycles per grain on the rv32 build | numbers in the audit trail decide the stereo ceiling and whether the core runs from the cold alias |
| T1 core v2 | the stretcher with incremental decimation and block reads from a caller-supplied reader (`ws_read(ch, abs, n, dst)`); integer twin updated first, bit-exact test, mutants, instruction count | C == twin on every case, same quality (periodicity within 0.01 of B-550), instruction count at or below B-550's |
| T2 player path | `tempo_push()`, staging ring, hooks in `flac_emit` and the MP3 loop, resets, soft reset on resume, eligibility, guard; release-style build, setting not persisted | host: an rv32sim harness driving the real `flac.c` plus the core over a speech FLAC, sample-exact against the integer twin fed the same decoded PCM; Pocket: speech FLAC and MP3 at 1.25/1.50/2.00x, no stalls (`ALL` 0), HEADROOM readings, owner listening pass |
| T3 pause shortening | the delay-line stage and the three presets | seconds saved within 1 point of the model on the Twain clip, zero clipped onsets by construction, owner listening pass |
| T4 UI and persistence | the settings rows, the persist word, the Info row, tempo in the Diagnostic Build (after the stack/heap pass) | settings survive a restart, `make test-host` fixtures, hardware check |
| T5 | stereo ceiling confirmed or raised, optional finer steps | as measured |

## 12. Risks and open questions

1. **Stack:** 2.1 KB of step scratch inside the decode callback on a 6 KB stack (192 KB link) that already carries a 2.4 KB meter scratch in Diagnostic builds. Mitigate by making the scratch static (counted against the heap) or splitting it; the Check's stack-peak reading (B-230) is the guard.
2. **Cold-code speed** of the core is unmeasured (T0).
3. **Stereo cost** is unmeasured (T0); the guard makes it safe either way.
4. **Staging ring placement** in the PSRAM map needs confirming (`docs/features/MEDIA_LIBRARY_0.4_SPEC.md` has the map; the proposed 0x500000 is clear of everything named in `fw/link.ld`).
5. **A PSRAM that is slow at the wrong moment:** the worst single PSRAM access is 380 cycles (B-054); block reads are short and between pushes, but a worst case burst should be bounded in the T2 harness.
6. **Other rates:** the core supports 22.05-48 kHz with the two grain sizes; 32 kHz uses the 1,024 grain (about 32 ms); only 44.1 kHz has been listened to.
7. **MP3 gaps and frame boundaries:** the decoder hands over whole frames; the staging ring makes them invisible to the stretcher, but a decode error that drops a frame creates a splice the stretcher will treat as signal (it is repaired by the cross-fade at the next grain).

## 13. Decisions for the owner

1. Build and test tempo in the release-style build first (section 5)?
2. Measure stereo first (T0) before committing to a stereo ceiling, or ship mono-first and let the guard cap stereo?
3. Tempo step list (section 8) and the default top speed.
4. Whether pause shortening ships with the first tempo release or after it (T3).
