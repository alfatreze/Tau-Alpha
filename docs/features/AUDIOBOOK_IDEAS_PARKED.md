# Audiobook features: parked ideas (for later exploration)

**Status: PARKED (owner, 2026-10-06). Ideas and measured facts only; nothing here is specified or built.** Context: audiobook tempo (Cymo C7, `CYMO_TEMPO_INTEGRATION.md`) works to about 1.75x on mono speech but the CPU is saturated there (DEV 100/DEV 79: idle 0%, stretcher 26-31% of the CPU against a 12% estimate; menus and screen changes add audible clicks). See `docs/issues/024-mp3-noise-clicks-regression.md` for the measurements and the next one owed (idle at 1.50x on DEV 100 vs 99).

## A. Tempo cost and headroom (the two parked options from issue 024, plus the cheap ones)

| # | Idea | Notes |
|---|------|-------|
| A3 | **Cut the stretcher's CPU cost** (owner: option 3) | Measured 26-31% vs 12% [EST]; cold-code fetch through the instruction cache is the suspect, unmeasured. Try: hot placement of the search loops (small, RAM cost), a cheaper first search stage or wider decimation, fewer candidates when the signal is steady, mono-only fast path, cost counters per stage (like MP3's H/I/S) before changing anything. Gate: a host-proven identical output stream where the algorithm is unchanged. |
| A4 | **Hardware correlator for the WSOLA search** (owner: option 4) | RTL, built only if A3 cannot reach comfortable headroom (`CYMO_AUDIO_ENGINE.md` section 7). Same discipline as the MP3 window and FLAC LPC units: host symmetry check, golden model, RTL plus mutants, firmware probe with software fallback, fit, hardware A/B. Needs one DSP-slice MAC plus MLAB history; budget check against the 98% ALM level first. |
| A1 | Headroom guard | Step the tempo down (or pause the stretcher) when idle stays under a threshold for a few seconds; show a toast. Already on the follow-up list (ROADMAP row 12). |
| A2 | Quiet the UI while tempo plays | Skip meter feed and heavy redraws, ease menu draw cost while a menu is open over tempo; menus and screen changes currently add clicks at the limit. |
| A5 | Cap the list (1.50x or 1.75x) per measured headroom | Free; depends on the owed 1.50x reading. |
| A6 | Stereo audiobooks | Stereo MP3 128k is about 83% busy at 1.50x (B-553). Option: downmix to mono for tempo (halves decode-adjacent work and stretcher cost) when the user accepts it. |

## B. Other audiobook-related features worth exploring

| Idea | Why it matters for audiobooks | Dependencies / notes |
|------|------------------------------|----------------------|
| Per-book resume position | The player already derives position from file position; a saved position per file (and per library item) survives power-off | persist words are scarce: needs a data-slot file or the widened persist file; design the format once, share with Tau Omega |
| Bookmarks | Several saved points per book | same storage question |
| Skip back 15 / 30 s and skip forward | The most-used audiobook control | Left/Right already mean back/forward in menus; need a gesture that does not clash |
| Rewind a few seconds on resume | Listeners lose the thread after a pause | trivial once resume is a code path; a soft reset on resume is already a listed follow-up |
| Sleep timer | Falling asleep to a book is a main use | needs a clock (cycle counter is enough for minutes) and a fade-out (the volume ramp exists) |
| Per-book speed memory | Each narrator wants a different speed | storage as above |
| Chapter navigation | Long single-file books | ID3 CHAP/CTOC frames for MP3; m4b/M4A is not supported (no AAC decoder), so many commercial books cannot be played at all |
| Voice-boost EQ preset | Speech clarity at speed | the EQ exists (biquads); a preset is data only |
| Loudness evening between books | ReplayGain exists (Track/Album); books carry few tags | optional per-book gain stored with the position |
| Gapless between chapter files | Chapters split into files should not stutter at boundaries | Cymo gapless is already on the roadmap (C-series), shared with music |
| Pause shortening presets | Spec done and approved by ear (Small/Medium/High, B-536/B-537) | not built; depends on the tempo path being stable |
| Remaining-time display that accounts for tempo | The clock drops faster than real time with tempo and shortened pauses | label or smooth it (`CYMO_AUDIO_ENGINE.md` section 7) |
| Library view for books | Books are albums of one or many files with no art conventions | Tau Omega sync can write a book index/cover; see `CROSS_PROJECT_INTERFACE.md` |
| Lower sample rates (22.05/24 kHz MPEG-2 books) | Many books use them; they are cheap to decode | tempo already supports 22.05-48 kHz; the resampler guard applies only at 44.1 kHz |

## C. Order when picked up (suggestion)

1. Measure: idle at 1.50x and 1.25x on DEV 100 vs 99 (owner), per-stage stretcher cost counters (firmware).
2. A5/A1/A2 (free or small, solve the practical clicks).
3. A3 only if A1 to A2 leave too little range; A4 only if A3 does.
4. Then the B items that need storage (resume, bookmarks, per-book speed) as one design with Tau Omega.
