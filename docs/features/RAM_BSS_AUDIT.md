# `.bss` audit: which static buffers are only live part of the time

Status: audit, 2026-10-04 (B-566), step 3 of `docs/features/DIAGNOSTICS_RESOURCE_ANALYSIS.md`. Nothing in firmware changed.
Sizes MEASURED from the release ELF on the 192 KB link (`tools/ram_report.py`); liveness READ from `fw/player.c`, `fw/art.inc`,
`fw/chladni.inc` (the firmware is single-threaded with no interrupts, so two buffers can share memory exactly when no code path
uses both between one write and the last read of the other, i.e. program order decides).

## 1. The numbers

Release `.bss` + `.data` is 44,793 B in 526 symbols; 41 KB of it is in the 38 symbols of 96 B or more:

| Bytes | Symbol(s) | Live when |
|---|---|---|
| 24,576 | `arena` | decoder open (Helix or the FLAC block buffer, swapped); needed |
| 4,608 | `pcm` | MP3 decode loop; FLAC meter staging (`fl_meter_n`); tempo input |
| 2,816 | `art_yslot` 1,024, `art_xmap` 1,024, `art_rowcnt`/`art_colcnt`/`art_line` 256 each | only inside `art_decode()` |
| 2,272 | picojpeg statics (`gMCUBuf*` 768, `gInBuf` 256, `gHuffVal*` 544, `gHuffTab*` 320, `gQuant*` 256, `gCoeffBuf` 128, small ones) | only inside `art_decode()` |
| about 2,700 | Chladni: `chl_half` 1,600, `chl_cxm` 320, `chl_cxn` 320, `chl_ring` 240, `chl_st` 164 | Chladni selected and drawing |
| 760 | `fl` (`flac_t`, incl. the 512 B input buffer) | FLAC track open |
| about 1,100 | `wviz_scope_y` 512, `scope_y`/`scope_x` 384, `lw_row` 404, `lw_band` 192 | that meter selected |
| 368 / 192 / 100+100 / small | `th_file`, `helios_region`, `ui_mq_*`, about 490 small symbols (about 3.8 KB) | always |

## 2. Findings

**F1. The art decode working set (5,088 B) and `pcm` (4,608 B) are never live together: about 4.6 KB free for every build.**
In the track-load sequence `pcm_flush()` empties the FIFO and the decoder is re-initialised (`fw/player.c` about lines 8610-8614,
the comment at 8826 says so), then `art_decode()` runs (8881), and only afterwards does the first `MP3Decode(..., pcm, 0)` (8970) or
the FLAC meter staging touch `pcm`. Nothing reads `pcm` at draw time (the comment at line 1659: points are captured during decode).
The TIM1 cover path (`timg_cover`) uses neither. So a shared region of max(4,608, 5,088) = 5,088 B replaces 9,696 B.
Constraints found: (a) `fl` is live during `art_decode()` for a FLAC track (`flac_open` runs first), so it cannot share this
region; (b) picojpeg's statics are `static` inside the vendored `third_party/picojpeg/picojpeg.c` (public domain, Rich Geldreich),
so sharing them means either a small mechanical edit (one state struct placed by the caller) or a linker overlay; (c) any future
"audio-first load" or background cover decode would break the exclusivity and must re-check this. Risk: low, contained, host-testable.

**F2. Meter scratch: about 2 KB, with a real caveat.** Chladni (2.7 KB), the Winamp scope arrays and the Layered Wave rows are
mutually exclusive meters, so one shared scratch of max(...) = 2.7 KB would replace about 4.7 KB. But each module currently
assumes its state survives meter switches (`wviz_scope_init`, Chladni's ring), so every module would need an "I now own the
scratch, re-initialise" hook, and a meter switch during Configure or fullscreen must call it. Medium risk; the framework's own
direction is PSRAM-resident state (D-M14), which is the alternative, but `chl_half` is touched every render and a PSRAM window
access costs about 30 cycles, so Chladni would need a measured CPU comparison first.

**F3. `fl` (760 B) and `pcm` could share a region** (FLAC and MP3 playback are exclusive, and `fl` is live during art decode),
but only if the art working set stays out of it; it competes with F1 for the same bytes, so F1 first. Gain 760 B at best.

**F4. Not worth touching:** `arena` (the measured Helix peak is 23,824 B of 24,576 B), the small symbols (3.8 KB across 490, each
a counter or flag), `th_file`, `helios_region`.

**F5. Outside `.bss`, for context:** the MP3 ring is 24,576 B and the tag buffer about 4 KB, reserved for APF DMA; together 14% of
the 192 KB. Not part of this audit, but the largest block that is neither code nor decoder state.

## 3. Ranking

| # | Change | Saves (every build) | Risk | Evidence needed |
|---|---|---|---|---|
| 1 | Art working set shares the `pcm` region (F1) | **4,608 B** (release gap 13,504 to 18,112 B) | low | host art tests, one Pocket track change with MP3 and FLAC covers |
| 2 | Meter scratch union with re-init hooks (F2) | about 2 KB | medium | golden-frame tests per meter, switching meters in Configure and fullscreen |
| 3 | `fl` shares with `pcm` (F3) | 760 B | low-medium | after 1 |

## 4. Proposed implementation of #1 (not started)

One 5,088 B `static` scratch in `fw/player.c`; `pcm` becomes a `short *` into it; the five `art_*` arrays get their offsets from the
same scratch (they are in `art.inc`, included into `player.c`); picojpeg's statics move into one `pjpeg_state_t` that
`pjpeg_decode_init` receives from the caller (about 35 declarations, mechanical). Guard: `art_decode()` begins with a check that the
decoder loop is not running (an assert in host builds) and marks the scratch "art" so the first `MP3Decode` after it can never
see stale art bytes as PCM (it never reads before writing, but the flag makes the rule explicit). Verify with `sim/test_art*`
(if any exist: `tools/check_art_load_order.py` is the only art test found today, so a host decode test comes first), then the
heap-gap and RAM snapshot check (`tools/check_heap_gap.py --update`), then a Pocket run: MP3 then FLAC track change with
covers, same-album reuse, a JPEG-fallback cover, and a long soak with Chladni.

## 6. Added 2026-10-04 (B-568): the ring, the tag buffer and the meter scratch, parked for later

Read from `fw/player.c` and `fw/link.ld`; nothing measured on a Pocket, CPU figures are estimates. Not scheduled.

### 6.1 MP3 ring (24,576 B): about half of it is slack, 4 to 7 KB can be freed with no change in buffering

- The ring is LINEAR, not circular (Helix needs contiguous bytes). `refill_pump` (about lines 7899-7926) refills only when
  `ring_fill - ring_rd < RING_SIZE / 2` (12 KB trigger), then appends one `REFILL_CHUNK` (4 KB); when `ring_fill + 4096 > RING_SIZE` it
  compacts (copies the unread bytes down to offset 0, `w[i] = w[src + i]` through the uncached alias, about 7160-7190 and 7910-7926).
  So the buffered amount sits between about 8 and 16 KB; the rest of the 24 KB is room for appends and compaction.
- Buffering depth is set by the 12 KB trigger, not by the ring size: 12 KB is about 0.3 s at 320 kbps, about 0.14 s at 700 kbps FLAC.
  Minimum ring for an unchanged trigger = trigger + one chunk + a little (16,384 + about 512 B); 20 KB leaves a second chunk of slack.
- Options (estimates): 20 KB saves 4 KB, compaction every second refill; about 17 KB saves about 7 KB, compaction at nearly every
  refill, copying about 11 KB (about 2,800 uncached word moves, roughly 0.4 ms) per refill, so about 0.4% CPU at 320 kbps MP3, about 1%
  on 700 kbps FLAC, about 2% at 2.00x MP3.
- Hazard: `RING_SIZE` is used in 8 places and several scale with it: `/ 2` (trigger, 7899), `/ 4 * 3` (size-probe gate, 9506, which at
  18 KB is almost never reachable in steady state), `want` clamps in `prefill` (7939) and the ID3 skip fill (8474), the compaction tests
  (7171, 7910). Shrinking the ring without first turning these into explicit byte constants would silently shrink the trigger and change
  buffering. Stale comment: "The ring is 32 KB" near line 8470.
- Plan: (1) a ring-level min/max tracker on the Info page (Diagnostic Build) and one long soak to confirm the 8-16 KB steady state;
  (2) explicit constants (`RING_REFILL_AT` 12,288 B, the probe gate and clamps in absolute bytes), build identical to today; (3) resize
  `_ring_size` in `fw/link.ld` to 20 KB, then to about 17 KB if the CPU cost is acceptable; (4) Pocket: HEADROOM file waits (`O`),
  `UNDERRUNS n ALL m`, idle, on the hard FLAC (415K 48K) and a 320 kbps MP3 at 2.00x (fastest drain), plus seek, pause/resume and a
  track change. Worktree, host-testable pieces first (the thresholds as a pure function with a test).

### 6.2 Tag buffer (4,096 B): leave it

It is a DMA landing zone with many users, several live during playback: size-probe pump (512 B reads at far offsets), FLAC seek-table and
probe reads, ID3 and tail probes, the I/O benchmark (4 KB reads), the art window (4 KB per read). Sharing it with `pcm` is unsafe
(FLAC's meter staging keeps data in `pcm` across calls, `fl_meter_n`); shrinking the art window to 1 KB multiplies SD commands for big covers.

### 6.3 Meter scratch (about 2 KB): last

Overlay of Chladni (2.7 KB), scope arrays and Layered Wave rows needs a re-init-on-activate hook in each meter and has a bug class that only
shows when switching meters. PSRAM instead: `chl_half` (1.6 KB) would cost about 4% CPU (about 60 K accesses per second at about 48 cycles,
estimate); `chl_cxm`/`chl_cxn` are inner-loop data and must stay hot. Only worth doing if the ring and the art change are not enough.

### 6.4 Where the other RAM work stands (2026-10-04)

Branch `art-overlay` (`386f4f9`, B-567): JPEG fallback work buffers in PSRAM, +3,968 B heap in every build, host test in place; NOT merged;
`TAU_DEV_82` packaged, not installed (dry run clean; compare `LOAD MS` against DEV 80 on a no-`tau-art` album such as Daft Punk).
