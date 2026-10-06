# MP3 playback: added noise / clicks (suspected regression) - discovery plan

**Status: OPEN, plan only (2026-10-06). Nothing built or changed. Symptom details still to be pinned down (phase 0).**

## What the repo says today

- Last builds heard **clean** by the owner: alpha.2 ROM line, `DEV 88` (pipelined Subband handoff, B-588), `DEV 91` (burst push, B-593 + stress-pump fix), `alpha.3` smoke test (B-596), `DEV 93` (dB volume + ReplayGain: "sounds right, smooth, no issues", B-598/B-599).
- Reported problem since: tempo clicks at 1.75x on DEV 93 (B-600), but B-601 records the owner did not see them on DEV 93 earlier, so the report and the build are not yet tied. `DEV 94` (alpha.3 source + tempo) and `DEV 95` (main + ReplayGain range fix + tempo) are packaged as Diagnostic Builds and **not installed** (card write needs approval).
- No audit entry yet names a regression in plain MP3 decode (tempo off). Treat the symptom as unconfirmed until phase 0 is done.

## What changed in the MP3 audio path (alpha.2 to main), ranked by suspicion

| # | Change | Where | Why suspect / why not |
|---|--------|-------|-----------------------|
| 1 | **dB volume + 5 ms ramp, default 65 to 94, step 3** (B-598) | `fw/pcm_push.h` `pcm_gain_apply`, `pcm_vol_tab`; applies to every sample of every track | Only change that touches *every* output sample after alpha.3. Saved volumes now sound quieter (65 = -21 dB, was -3.7 dB), so the analog gain is turned up and the unchanged 1-LSB truncation floor (`>> 15`, no dither) becomes audible as noise. Ramp itself: 149/pair, should not click, but a ramp that re-triggers (RG update, snap, setting poll) would. **Highest.** |
| 2 | **ReplayGain** fold into the volume target (B-599, range fix B-600) | `rg_update()` on every track load, `vol_apply()` | A `vol_apply()` call mid-track starts a ramp; `rg_update` runs from `read_track_head` (hot path, per track). Mode Off = unity, so only matters if RG is on. Medium. |
| 3 | **Pipelined Subband handoff** (B-587) | `third_party/libhelix-mp3/real/subband.c`, `fw/mp3_poly_hw.*` | Changes decoder output order/timing (host-proven identical PCM, silence one slot on timeout). Heard clean on DEV 88/91/alpha.3. Low, but the only decode-core change. |
| 4 | **Burst push** (B-592) | `fw/pcm_push.h` `pcm_push_pairs`, one FIFO status read per burst | Host-proven stream-identical, clean by ear. Low. A burst that overruns the FIFO fill estimate would show as UNDERRUNS ALL. |
| 5 | Cymo resampler default ON (Diagnostic Build), 44.1 kHz guard | `cymo_guard_apply`, RTL `tau_cymo_*` | Hand-off fix hardware-confirmed (B-529). Rules out if noise exists with it OFF. |
| 6 | Code placement / cold image growth | I-cache, PSRAM fetch | Matters for tempo (stretcher in cold code) and any CPU-marginal case, not for 1.00x plain MP3. |
| 7 | Not audio but timing-adjacent: stress-pump range, TPG report code, Settings/meter UI | `fw/player.c` | A UI draw burst contending with audio shows as UNDERRUNS. Known issue 023 candidate. |

## Update 2026-10-06 (owner): seen on DEV 95, immediately apparent on the regular test files

Heard on `DEV 95` with the regular audio test files (immediate); the Nausicaa album (64 kbps) hid it. **Open question:** whether `DEV 94` was ever played with the regular files (it may only have been tried with Nausicaa), so 94-clean is not yet established. Code diff alpha.3 to main touches only the dB volume, ReplayGain, persist range and build flags (no decoder change), so if 94 is clean on regular files the suspects are #1/#2. **Host check done:** the dB gain path's own truncation noise is about -87 dB (vol 94) and -69 dB (vol 65) relative to the signal on a loud two-tone test, DC -0.5 LSB, versus -91/-87 dB for the old Q8 path: measurable but unlikely to be "immediately apparent" at vol 94, so the cause may be elsewhere in the volume path (ramp, state) or not the volume at all.
**Fastest decisive tests (no install):** on DEV 95 set volume to **100** (unity: the gain and ramp code are skipped entirely). Noise gone = volume path; noise still there = not the volume path (look at decode/Cymo/tempo build config). Also play a regular file on DEV 94 and on the alpha.3 release core.

## Phase 0 - pin the symptom (owner, no card write, 10 min)

Same MP3, same volume position, on the build where it is heard. Record: build name; MP3 or tempo; stereo/mono, kbps; speed; **volume position**; Cymo ON/OFF; REPLAYGAIN mode; where the noise is (constant hiss, or discrete clicks, on which events: track start, volume change, menu, meter). Read **Info: UNDERRUNS n ALL m, CYMO RESAMP S/D/Q, HEADROOM, METER YIELD**. Clicks at events = ramp/underrun class; constant hiss that scales with analog gain = truncation/low-volume class.

## Phase 1 - cheap A/B with builds that already exist (needs card-write approval)

1. Install `DEV 94` (alpha.3 source) and `DEV 95` (main). Same track, same volume, plain MP3 first (tempo OFF), then tempo 1.75x.
2. DEV 94 clean, DEV 95 noisy: suspect set = #1, #2 (dB volume, ReplayGain). DEV 94 also noisy: suspect set = #3, #4 or older (bisect with DEV 88, DEV 91, alpha.2 release ROMs, all reproducible from tags/commits).
3. Both noisy only with tempo: it is the tempo path (CPU/cache), not MP3 decode; use the existing 2.00x finding (idle 0%) as the baseline and go to the tempo plan, not this one.

## Phase 2 - isolate within the suspect (host first, then one build each)

- **Volume:** at the owner's volume, host-model the output: gain table, `>> 15` floor (rounds toward minus infinity), ramp re-triggers. Extend `sim/test_pcm_push.py` to measure noise floor vs the old Q8 path at equal loudness and to count ramp starts per track (should be 0 at steady state). Candidate fix: round-to-nearest or TPDF dither before the shift; restore the loudness mapping of saved volumes (migrate old saved position to the equivalent dB position) so users are not forced into a quiet, high-analog-gain setting.
- **Ramp/RG:** log `vol_apply()` call sites per track (a counter on Info); a nonzero count at steady state is a click source.
- **Subband/burst:** differential test of the final PCM stream between alpha.2, alpha.3 and main on 3 real MP3s (stereo, mono, VBR) through the existing rv32sim harness; any non-identical sample, or a changed count of silent slots, is a decode regression.

## Phase 3 - measure, do not guess (project rule)

Loopback record the 1 kHz tone and a silence file through `tools/lab/cymo_loopback.py track` on the A/B builds (Cymo OFF and ON, then volume 94 vs volume 100). Pass criterion: no block-SINAD drop and no flagged events beyond the capture chain's own start/stop glitches (B-529).

## Fix and exit

Fix only the proven cause, with a host test that fails red on the old code and passes green; hardware confirmation by ear plus loopback; log in `docs/AUDIT_TRAIL.md` (next free id B-602). Candidate release carrier: alpha.4.

Related: B-587, B-592, B-598, B-599, B-600, B-601; issue 023 (rare spike, shares the UNDERRUNS discriminators).

## Card state (2026-10-06)
Installed `alfatreze.TAU_DEV_96` (Diagnostic + tempo, built from commit `909651b` = alpha.3 + the dB volume only, no ReplayGain, no persist change; ROM `a597c24c...`; media and library carried from DEV 95; backup `work/card-backups/20261006-221252`). Card now holds DEV 94 (alpha.3), DEV 96 (+ dB volume), DEV 95 (+ ReplayGain/persist). Play the same regular file on all three: the first one that is noisy names the commit (`909651b` volume, `2132961` ReplayGain, `513e440` persist). Also try volume 100 on each.

## Update 2026-10-06 (owner): the MP3 noise was probably user error (tempo was on); the real issue is TEMPO clicks at 1.75x

Owner: no noise found any more (had tempo on without noticing while testing it). But tempo "seems to have regressed": DEV 79 played clean to 1.75x (B-559); now 1.75x always clicks on DEV 94, 95 and 96 (all Diagnostic + tempo). Tracked as a tempo regression between DEV 79 (`d3d1801`) and alpha.3. Candidates: pipelined Subband handoff (9038d9c; also changes the mono hardware path timing), burst push (non-tempo path, so unlikely), PSRAM/art/FLAC/CLUT-RTL changes (code layout in the I-cache, PSRAM contention), the CLUT-RTL bitstream (DEV 79 ran the cymo-feed-b527 bitstream, everything since runs clut-rtl), Diagnostic-vs-release build (DEV 93, release-style, also clicked, B-600).
**Bisect cores installed 2026-10-06** (release-style tempo, same bitstream as DEV 94-96, pairing PASS): `DEV 97` = `eacf774` (everything before the pipelined handoff), `DEV 98` = `9038d9c` (+ pipelined Subband), `DEV 99` = `b718b1e` (+ burst push). Same mono speech MP3 at TEMPO 1.75x on each; read Info > HEADROOM and UNDERRUNS. First click-free to first clicking names the commit; if 97 already clicks, the cause is older than pipelining (art/FLAC/CLUT/heap/layout or the bitstream). DEV 79 itself cannot be installed (its ROM predates the B-581 pairing marker; the install gate refuses it), so a true pre-everything control would need a rebuild of `d3d1801` with a marker or the bitstream swapped.

## Result 2026-10-06 (owner): DEV 97, 98 and 99 all click at TEMPO 1.75x
So the cause is older than the pipelined handoff and the burst push (or the test conditions changed). The RTL diff DEV 79 to DEV 97 is only the CLUT write-address registering (not audio); the firmware diff is FLAC Rice, art work buffers in PSRAM, CLUT start index, preview/splash fixes, heap floors. **Control installed: `DEV 100` = `d3d1801` (DEV 79's exact firmware, plus only the B-582 pairing marker so the install gate accepts it) with DEV 79's own bitstream (`08827e94...`, copied, not re-reversed).** Same mono speech MP3 at TEMPO 1.75x: if DEV 100 is clean, the regression is firmware between `d3d1801` and `eacf774` (next bisect: `386f4f9` art PSRAM buffers, `b5be9a6` FLAC Rice, `71c6a6a` CLUT) or the bitstream pairing; if DEV 100 also clicks, nothing regressed and the 1.75x result changed with the test conditions (file, volume, speed setting, Cymo state, card).

## Result 2026-10-06 (owner): DEV 100 (DEV 79 control) clean at 1.75x, but at the very limit; menus and returning to the main screen add audible clicks
Info screenshots from DEV 100: `HEADROOM 1.75x I0/0 O0 WS1.5X` (idle 0% latest and worst: the CPU is saturated at 1.75x, so the click/no-click verdict is decided by tiny margins and any UI work), `UNDERRUNS 1`, `LOAD MS 446/0/163/701`, `CYMO RESAMP OFF`, `I2S JITTER NO UNIT` (older bitstream), `DRAW STALL 29 MS`. This matches DEV 79's own B-559 reading (idle 0% already at 2.00x, stretcher 26-31% CPU). **Reading:** the 1.75x setting was never comfortable; DEV 97-99 click at it because the margin is thin and they carry slightly more per-frame work (clut/art/FLAC/Subband-era changes, a different bitstream), not because a single bug was introduced. A pinned HEADROOM idle of 0 cannot show the difference, so the comparison must be made where the CPU is not saturated.
**Next measurement (owner):** the same mono speech MP3 at **TEMPO 1.50x** (and 1.25x) on DEV 100 and DEV 99 (then 97/98 if they differ): read Info > HEADROOM `I<latest>/<worst>` and UNDERRUNS, with no menu open. The idle-percent gap between 100 and 99 at 1.50x is the extra per-frame cost in points of CPU; it also says which commit range owns it (97 vs 98 vs 99).
**Fix options (after the number; options 3 and 4 parked in docs/features/AUDIOBOOK_IDEAS_PARKED.md):** (1) cap the tempo list at 1.50x (or hide a step when idle is under ~10%) plus the planned headroom guard that steps tempo down on sustained low idle; (2) pause tempo work while a menu is open or ease the UI draw cost during tempo; (3) cut the stretcher cost (measured 26-31% against the 12% estimate; hot placement of the search loops, cheaper first search stage); (4) a hardware correlator, RTL, deferred.
