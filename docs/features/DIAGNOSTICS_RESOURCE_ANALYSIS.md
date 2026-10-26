# Diagnostics: what they cost in on-chip RAM, and how to make them cheaper

Status: analysis, 2026-10-04 (B-564). Nothing in firmware changed. All sizes MEASURED from the linked ELFs of the three
builds on the 192 KB link (`RAM_192K=1 CLK66=1 SDRAM_BUSY=1`, main at `e4d68df`): `release`, `player-library-diagnostic`
("diag") and `player-library-diagnostic-profile` ("profile"). Method: `readelf -S` and `nm -S` per symbol, grouped by name
prefix (the grouping is approximate; the section totals and per-symbol deltas are exact).

## 1. What the "heap gap" is, and why it keeps biting

The gap is NOT a heap anything uses. `fw/alloc.c` replaces malloc with a 24,576 B static arena that lives in `.bss`
(the Helix decoder and the FLAC block buffer share it). `_sbrk` (`fw/sysio.c`) is a fallback nothing calls, `fl_buf` is
the only `malloc` in the firmware, and nothing links printf. So `_heap_end - _heap_start` is simply the RAM left over
between `.bss` and the DMA ring, and the floors in `fw/build.sh` (release 6,144 B, both diagnostic builds 4,096 B) are a
**policy margin**, not a functional requirement; the link's own hard minimum is `_min_heap` = 1,024 B (`fw/link.ld`).
The comment in `link.ld` that says Helix mallocs from this gap is stale (true before `alloc.c`). The real functional
limits are the arena (24,576 B) and the stack (6 KB on this link; measured peak 1,672 B plus the Layered Wave scratch).

Layout of the 192 KB link, release: image (text+rodata+data) 105,084 B, `.bss` 43,039 B (arena 24,576, pcm 4,608, ...),
gap 13,504 B, tag buffer about 4 KB, ring 24,576 B, stack 6,144 B.

| Build | Gap now | Floor | Spare above floor |
|---|---|---|---|
| release | 13,504 B | 6,144 B | 7,360 B |
| diag | 6,768 B | 4,096 B | 2,672 B |
| profile | 4,336 B | 4,096 B | 240 B |

## 2. What diagnostics actually cost

Sections per build (bytes):

| Section | release | diag | profile |
|---|---|---|---|
| `.text` (hot code) | 81,944 | +3.8 KB | +5,668 |
| `.rodata` | 21,368 | +0.5 KB | +2,784 |
| `.data` + `.bss` | 44,970 | +0.5 KB | +718 |
| **hot total vs release** | | **about +4.8 KB** (ROM +6.3 KB) | **about +9.2 KB** (ROM +8.5 KB) |
| `.cold_text` + `.cold_data` (PSRAM) | 101,368 | 148,820 (+47 KB) | 153,428 (+52 KB) |

Findings.
1. **Most of the diagnostics is already cold.** Check, Sweep, Blit Test, Meter Sweep, QR, soak, window tests: about 47-52 KB of
   code and data sit in the PSRAM cold image and cost no hot RAM. Hot RAM is only the hooks that must run in the main
   loop.
2. **Hot cost of diag (about 4.8 KB):** `stress_hud_draw` 916, `poll_input` +724, `stress_set_level` 424, `main` +412,
   `fb_copy` partial +248/-240, `dg_soak_start` 212, `cymo_guard_apply` +136, `wvcfg_input` +112, `settings_load`/`_store`
   +208, `set_spg_find` 104, `ui_mmss` 96, `dg_res` 110 (bss), stall counters 144, QR helpers about 136, small tables.
3. **Hot cost of the profile hooks over diag (about 2.4 KB):** `flac_decode_frame` +580, `MP3Decode` +548, `xmp3_IMDCT` +424,
   `main` +244, tables and counters. These are measurement probes inside the decode paths and cannot be moved cold.
4. **The cold image has a time cost, not a RAM cost:** Info `COLD IMAGE` loads in 186 ms (release-style) against 263-268 ms
   (diag) at boot. The PSRAM region reserved for it is 1 MiB; the diag image uses about 15%.
5. **Diagnostics are not where most hot RAM goes.** Release hot footprint by feature (approximate): Helix MP3 about 44 KB
   (code 33 KB, `xmp3_huffTable` 8.5 KB), `main` 8.4 KB, `poll_input` 2.7 KB, FLAC about 10 KB, settings/UI about 15.6 KB,
   meters about 8.4 KB, art/jpeg buffers about 5.7 KB, arena 24.5 KB, `pcm` 4.6 KB. Even moving ALL diagnostic hot code out
   would recover at most about 4.8 KB (diag) or 9 KB (profile).

## 3. Your ideas, assessed

**A. Modular diagnostics, a core set plus ad-hoc ones per build.** Little hot-RAM gain: the optional pages are already
cold. It does cut the cold image (boot load time, PSRAM, test surface) and lets one build carry only what its test needs.
Worth doing for the few hot hooks only if each page owns its hooks (see B/D1). Medium effort.

**B. Move everything possible to cold.** The real lever, already mostly applied. Remaining candidates (hot now, diag only):
`stress_hud_draw`, `stress_set_level`, `dg_soak_start`, stall counters, `set_spg_find`, `ui_mmss` about 2.2 KB; these are
UI and low frequency, and cold code costs a PSRAM instruction fetch per cache miss (the audio-adjacent stretcher ran about
3x slower from cold, B-559, so nothing on the decode path moves). The in-loop hooks (`poll_input` +724, `main` +412,
`settings_load`/`_store` +208) need a refactor into one cold `diag_tick()` called through the existing `COLD_READY()` gate,
roughly another 1.3 KB. Realistic total about 3.5 KB (diag), the profile hooks stay hot.

**C. Runtime toggles.** No RAM gain (code stays linked), as you suspected. A toggle does help CPU: counters and tick reads in
the audio path cost cycles even when nobody looks. Small and unmeasured; the profile hooks are tick reads per pass, not per
sample. Useful only as an observer-effect guard (headroom readings should come from the diag build, not the profile build).

## 4. Other ideas (D+), ranked by payoff per effort

1. **Make the floor honest (no code).** Since the gap is unused policy margin, set the diagnostic floors to 2,048 B (still
   twice the link minimum). That frees 2 KB in both diagnostic builds at once and unblocks the profile build (4,336 B now
   against 4,096 B). Keep release at 6,144 B (its comment about the previews is stale: they are cold data now; 4,096 B is
   defensible too). The risk is only that a later growth goes unnoticed until the margin is smaller; the Info FREE RAM row
   and the baseline check still show it.
2. **A RAM report tool.** Promote the scripts used for this analysis into `tools/ram_report.py`: per-section, per-feature
   table and a delta between two builds, run by `tools/check_heap_gap.py` whenever a target falls, so the cause (which
   symbols grew) is printed instead of rediscovered. Small effort, prevents the next surprise.
3. **Move load-time-only buffers off hot RAM.** Candidates in `.bss` (release): art decode buffers (`art_yslot` 1,024,
   `art_xmap` 1,024, `gMCUBuf*` 768, `gInBuf` 256, `gHuffVal*` 512, `art_*` counters about 1 KB) about 4.6 KB; Chladni
   (`chl_half` 1,600, `chl_cxn`/`chl_cxm` 640, ring 240) about 2.5 KB (PSRAM-resident meter state is already the framework
   strategy, D-M14); `wviz_scope_y` 512, `lw_row` 404, `th_file` 368. Potentially 5-8 KB for every build, release
   included, more than any diagnostics change. Needs a per-buffer audit (is it live during playback?) and, for art, whether
   it can share the arena while no decoder is initialised. Medium effort.
4. **Per-decoder profile builds.** The MP3 hooks (about 1.0 KB) and FLAC hooks (about 0.7 KB) are only needed when measuring
   that decoder; two flags instead of one `FLAC_PROFILE`/`MPROF` pair saves about 0.7-1.0 KB in the profile build. Small.
5. **Use the real measurements to size margins.** The Check reports heap peak (SR_T_HEAP) and stack peak; floors and the 6 KB
   stack could be derived from those numbers per build instead of fixed constants. Reporting only; no code change needed
   to start.
6. **Do not** put anything in the decode loop or the audio hand-off into cold code, do not widen the reservation for the
   cold image (it is not the constraint), and keep the profile variant for decoder work only.

## 5. Suggested order

1. Lower the diagnostic floors to 2,048 B (one line each in `fw/build.sh`, plus the baseline note): +2 KB now.
2. Build `tools/ram_report.py` and hook it into `check_heap_gap.py`.
3. Audit the load-time-only `.bss` buffers (idea 3) for a release-wide gain of several KB.
4. Cold-ify the diag-only hooks (`diag_tick()` refactor, about 3.5 KB), then per-decoder profile flags.

Open: whether any `.bss` buffer in idea 3 is live while a track plays (art decode runs inside the 3 s load); whether the
floors should be per-build constants or derived. Scratch scripts used here: `symdiff.py`, `hotdiff.py`, `groups.py`
(not committed).
