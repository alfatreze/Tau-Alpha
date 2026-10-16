# Player 2: a new player built around the Cymo audio architecture

**Status: SPECIFICATION (2026-10-15, B-753). Nothing is built.** Owner request: rebuild the player aligned with the architectural restructure already defined, drop the inherited HarpMudd application code, and optimise the new player for Cymo. This document is the build specification. It supersedes nothing by itself: it **implements** [`FIRMWARE_MODULARIZATION_PLAN.md`](FIRMWARE_MODULARIZATION_PLAN.md) (module map and phases), [`../research/HARPMUDD_COMPARISON_AND_REWRITE_2026-10-12.md`](../research/HARPMUDD_COMPARISON_AND_REWRITE_2026-10-12.md) (what is inherited, stages R0 to R7), [`HELIOS_SPEC.md`](HELIOS_SPEC.md) section 11 (views), [`GAPLESS_SPEC.md`](GAPLESS_SPEC.md) and [`PCM_RING_HW_DESIGN.md`](PCM_RING_HW_DESIGN.md) (the ring and the boundary), [`CYMO_AUDIO_ENGINE.md`](CYMO_AUDIO_ENGINE.md) and [`CYMO_GAIN_STAGE_DESIGN.md`](CYMO_GAIN_STAGE_DESIGN.md) (the output chain), and [`RAM_DIET_PLAN.md`](RAM_DIET_PLAN.md) / [`DIAG_FEATURES.md`](DIAG_FEATURES.md) (memory and diagnostics rules). Where this document and an older one disagree, this one is the proposal and the older one is the evidence; the owner decides (section 17).

Evidence labels: **[HW]** measured on a Pocket, **[READ]** read from source or docs, **[HOST]** measured on the host, **[EST]** estimate, **[OPEN]** unknown, test named.

## 1. Why a new player, and what it must be

The present player grew by adding features around the inherited design: one `main()` loop (about 1,180 lines) that decodes, polls the card, draws and reads buttons, writing the *display* variables live while it loads. A month of audio hardware was then added around it (hardware gain, resampler, EQ, polyphase and LPC units, a PSRAM ring with a hardware reader). Each was wired in as a hook. The result is correct only as long as every hook is remembered, and the bugs of the last week are all of one kind:

| Observed [HW], 2026-10-14/15 | Class of mistake | Rule that prevents it (section 3) |
|---|---|---|
| Old track's clock reads 10 to 11 s of 12 s at a gapless swap; wrong by the frames decoded between two UI ticks | a counter advanced per UI tick instead of per frame; a second writer for the same quantity | P2: one writer per quantity, deltas from a monotone count |
| Stop makes the time "go crazy" after the fix for the above | a delta taken across a reset | P2: counters never reset, only re-based |
| MP3 to MP3 boundary: about 41 ms of silence; FLAC to FLAC: a 199 degree phase jump (about 2,053 samples) | pairs lost or padded at a boundary nobody counts | P3: sample-accounting invariant, checked on the device |
| One-pair FLAC put went through cold hooks: about 300 cycles a pair, FLAC idle 45% to 0% (DEV 123) | an indirect call into code fetched from PSRAM in the hottest loop | P5: hot paths contain no indirect calls into cold code |
| `UNDERRUNS` shows at most one event per flush | a sticky one-shot hardware flag read as a count | P6: free-running counters, read as deltas |
| `GP SWAP` shows the old clock at 0 s; `load_track()` rewrote the screen's variables while the old track still played | the loader writes what the screen reads | P4: an immutable track record; the screen follows what is heard |
| The default Diagnostic Build fell 16 B under its heap floor from two Info rows | rows, labels and counts maintained by hand in three places | P9: Info and reports generated from a counter registry |

Player 2 therefore is not a restyle. It is a different *structure*: audio is hardware-paced and the firmware is a producer; every hand-off between modules has a counter on both sides; the screen is a function of what is heard.

**Goals** (each has an acceptance test in section 15):
1. **Audio never depends on the CPU's other work.** No firmware stall shorter than the ring's lead (about 1 s) can underrun the output. Natural track ends are gapless for same-rate, same-codec pairs with **zero** lost or inserted pairs.
2. **The clock, progress bar, meters and screen show what is heard**, exactly, through play, pause, seek, boundary, tempo and flush.
3. **Less CPU per second of audio than today** (MP3 stereo 128 kbps at 1.00x: busy at most 44% today [HW]; target at most 35% [EST]; FLAC hard file: idle at least 20% where today it is 8%).
4. **Less hot RAM**: release heap gap at least 12 KB on the 192 KB link (8.4 to 8.8 KB today [READ]); the default Diagnostic Build at least 4 KB (1.3 KB today).
5. **No inherited HarpMudd application code** in the product build (section 14), without losing any shipped behaviour.
6. **Everything that is shown is counted, and everything counted is exportable** without a hand-edited row list.

**Non-goals.** A new visual design (Figma owns it; the view layer takes it later); a new bitstream beyond the one already fitted and hardware-tested (`pcmring-b745`, the PCM ring with the hold bit, on `TAU DEV 123` to `128`), plus one optional read-only register (section 13); replacing the CPU, Helix, the APF shell or the vendor IP; video (Tau ν is a separate fork).

## 2. Facts that bound the design

| Fact | Value | Source |
|---|---|---|
| CPU | VexRiscv RV32IM, 66.667 MHz, **no interrupt controller**, cooperative only | [READ] HELIOS_SPEC section 8 |
| On-chip RAM (shipped link) | 192 KB total; image about 107 KB release; DMA/ring/decoder reservations protected; heap gap 8.4 to 8.8 KB release, 1.3 KB default Diagnostic | [READ] build, 2026-10-15 |
| PSRAM CPU window | 0xA400_0000, uncached: **32 cycles per read, 26 per write** | [HW] KB-040, B-022 |
| Instruction fetch from PSRAM (cold code) | about 31 cycles per word | [HW] B-054 |
| SDRAM CPU window | 48 to 50 cycles per access | [HW] A-094 |
| Audio FIFO | 2,048 stereo pairs, 46 ms at 44.1 kHz; drops pushes silently when full | [READ] |
| PCM ring (hardware reader) | 52,000 pairs allocated, 3,328 margin, **48,672 usable = 1.10 s at 44.1 kHz, 1.01 s at 48 kHz**; reader tops the FIFO up below 1,536 pairs; flush = read index jumps to write index; hold bit stops the reader (pause) | [READ] `tau_pcm_ring.sv`, fit B-751 |
| Output chain | FIFO output, then hardware **gain stage** (volume x ReplayGain x fade, 5 ms ramp, wide word), then the **Cymo resampler** (160:147 polyphase FIR, only for 44.1 kHz at 1.00x), then **Halcyon** (24-bit coefficients, 17 stages), then I2S 48 kHz | [HW] B-529, B-615, B-640..B-650 |
| Hardware taps (heard-aligned) | spectrum bank, wave meter, both read the FIFO output | [READ] |
| Decode cost, stereo MP3 128 kbps, 1.00x | busy 44% = decode 30 + meter feed 2 + push 7 + other 5 | [HW] B-593 |
| Decode cost, FLAC | idle 8% (a 44.1 kHz test file) to 60%; **channel 1 costs about twice channel 0** (63 to 66% vs 33 to 36%) | [HW] B-363, DEV 124 |
| Card read | about 736 KB/s measured | [HW] measured value quoted in the `flac_pull` comment in `fw/player.c` |
| Track load | typical 263 to 292 ms; a FLAC with a large metadata block 1.17 s; a natural end today 456 to 540 ms, made of about a dozen stalls each longer than the FIFO | [HW] B-562, B-544, B-740 |
| Longest atomic CPU step in the decode loop today | a whole FLAC frame (up to about 85 ms for a hard file) or an MP3 frame (about 12 ms) | [READ]/[HW] |
| Single media slot | one APF slot (id 2) holds the file being read; opening another file replaces it; **one target command in flight** | [READ] |
| Host command `0x008A` (data slot update) | exists in APF; **not used by Tau today**; whether the Pocket sends it to this core on a menu reload is unverified | [READ] skill docs; [OPEN] probe in S1 |

The conclusion the numbers force: the ring (1.1 s) is bigger than every stall the CPU can produce except a seek, so **the firmware's job is to keep the ring above a low-water mark and to finish whatever else it does in slices small enough that the mark is never crossed**. That is a scheduling problem, and the present loop solves it with `poll_input()` calls scattered through every wait, which is exactly what the audit of 2026-10-14 showed cannot be maintained (B-740).

## 3. Design principles (each one is a test, not a wish)

**P1. Audio is hardware-paced; the firmware is a producer with a fill target.** The hardware ring reader, FIFO, gain stage, resampler and EQ run on their own. The firmware decodes while `ring_fill < HI` and does everything else in the time that leaves. There is no per-sample CPU work in the output path.

**P2. One writer per quantity, and counters are monotone.** Every number the system shows or decides on (position, fill, counts, durations) has exactly one module that writes it; others read through an accessor. Counters are free-running `uint32_t`; consumers compute deltas with unsigned subtraction and *re-base* instead of reset. (The clock bug of 10-15 was a violation of both halves.)

**P3. Sample accounting.** Every pair that enters the system is counted at every hand-off: decoded, trimmed, put into the ring, pushed by the reader, heard. For every segment, `put == expected` where *expected* is derived independently (FLAC STREAMINFO samples; MP3 `frames x spf - delay - padding`). The check runs on the device on every track end and the result is a Diagnostics counter and a report field. A mismatch is a **defect counter**, never silently tolerated. (This is what would have found the boundary loss on the first track change.)

**P4. A track is an immutable record; the screen follows the heard segment.** Loading a track builds a new `track_t` and appends a *segment* to the timeline (section 5). It never writes a variable the screen reads. The screen renders the track of the segment the reader is currently inside. A gapless swap is therefore just "the heard segment changed", with no save/restore of display variables (the `gp_save/gp_load` of G2b disappears).

**P5. Hot paths have no indirect calls into cold code.** Hot code is annotated `HOT`; it may call only other `HOT` functions, MMIO, or inlines. `tools/check_cold_calls.py` is extended from "no hot-to-cold call without a gate" to "no function pointer in a `HOT` function". The ring put is a block store, not a call per pair.

**P6. Hardware status is read as counters.** Where hardware offers only a sticky flag (the FIFO underrun flag), the RTL gains a free-running event counter (a few flip-flops) and the firmware reads deltas. Until that exists, the firmware counts *edges it observed* and says so in the label.

**P7. One owner per resource.** Target I/O (one request in flight), the draw engine, the media slot, each PSRAM region, each MMIO group, the decoder arena. Ownership is written in a table (section 12) and enforced by the module's header being the only way to reach it.

**P8. Fixed capacity.** No `malloc` after boot except the decoder arena, which is allocated once per codec session from a static region. Every queue has a compile-time size and a counter for "full".

**P9. Everything counted is exported.** A counter is *registered* once (name, unit, scope, owner); the Info page, the Check/QR/pixel-grid report and the stress logs are generated from the registry. Adding a counter adds it everywhere; no row index, label array or `SET_INFO_ROWS` to maintain (the cause of the 16-byte floor miss and of the truncated `RING` row).

**P10. Capabilities are probed once into a typed struct.** At boot a single `caps_t` is filled (ring, gain stage, polyphase, LPC, resampler, Halcyon, blit/rrect/blend, double buffer, vblank, TIM1). Modules read `caps`; nothing re-probes, and no feature has two probe mechanisms. Player 2 **requires** the ring, the gain stage and the cold image (section 13); builds for older bitstreams are not supported, which deletes the software gain, the direct-FIFO push paths and `cymo_push` from the product.

**P11. Time is measured.** Every budget in this document has a counter on the device: worst atomic step per module, ring minimum fill, UI slice overruns, I/O wait. HEADROOM and the Check report them.

**P12. Fail safe, never fail loud.** An accelerator that fails its boot proof is switched off and the player carries on with the software path that exists *in hot memory* (MP3 polyphase, FLAC LPC). Missing cold code means a defined reduced player (play, pause, skip, volume; no art, no library UI), not a crash.

## 4. Architecture

```
  ┌────────────────────────────── app ──────────────────────────────┐
  │  boot · command bus · scheduler (section 9) · capabilities      │
  └──────┬───────────────┬──────────────┬───────────────┬───────────┘
         │               │              │               │
   ┌─────▼─────┐  ┌──────▼──────┐ ┌─────▼─────┐  ┌──────▼──────┐
   │ transport │  │   queue     │ │    ui     │  │  settings   │
   │ (section 6)│ │ (library,   │ │ views,    │  │ typed values│
   │ segments, │  │  playlist,  │ │ Helios,   │  │ persistence │
   │ timeline  │  │  shuffle)   │ │ meters    │  │ migration   │
   └──┬──────┬─┘  └──────┬──────┘ └─────┬─────┘  └─────────────┘
      │      │           │              │ reads heard segment + counters
 ┌────▼───┐ ┌▼─────────┐ │         ┌────▼─────┐
 │ decode │ │  stream  │◄┘         │ counters │◄── every module registers here (P9)
 │ MP3,   │ │ (media   │           └──────────┘
 │ FLAC,  │ │  slot,   │
 │ tempo  │ │  readahead)
 └────┬───┘ └────┬─────┘
      │ pcm blocks│ target commands (one in flight)
 ┌────▼───────────▼──────────────────────────────────────────────┐
 │ aout: put · watermarks · hold · flush · heard count · gain    │   platform: mmio, caps,
 │       ring (PSRAM) → FIFO → gain → Cymo → Halcyon → I2S       │   memmap, cold, timers
 └───────────────────────────────────────────────────────────────┘
```

**Dependency rule** (enforced by `tools/check_deps.py`, a new include-graph check run in `make test-host`): `app` may call everything; `transport` calls `decode`, `stream`, `aout`, `queue`; `ui` calls `counters`, `transport` (read-only), `settings`, `gfx`; `decode` calls only `stream` and `platform`; `aout` calls only `platform`; `counters` and `platform` call nothing. Nothing calls `app`. Diagnostics are a leaf that reads the counter registry and registers its own counters.

**Directory layout** (the target of the rewrite report, section 7, adopted unchanged):

```
fw2/  app/ platform/ stream/ decode/ aout/ transport/ queue/ ui/ gfx/ settings/ diagnostics/ generated/
      cores/        pure logic, no MMIO, host-tested (*_core.h): the existing library_core, replaygain,
                    mp3_trim, lame_tag, pcm_ring_hw, gain_handover, wsola_core, tempo_core, key_repeat,
                    start_gesture, meter_policy, headroom, suite_core, tpg, chladni_core move here as they are
third_party/    libhelix-mp3, picojpeg, font    (kept, vendored, attributed)
```

Most of the pure logic the product depends on is already Tau-written, host-tested and independent of `player.c`; Player 2 reuses those cores unchanged and replaces the *application* that drives them. That is why the rewrite is feasible: the inherited code is concentrated in the glue.

## 5. Data model

```c
/* Immutable once built. Built by queue/stream/decode from the library index details (32 B) when present,
   otherwise from the file head. Lives in a small pool (4 entries: playing, next, previous, spare). */
typedef struct track {
    uint32_t  gen;               /* unique, never reused: stale completions are rejected by comparing gen */
    uint16_t  lib_id;            /* library track id, 0xFFFF = not from the library */
    uint8_t   codec;             /* CODEC_MP3, CODEC_FLAC */
    uint8_t   channels, bits;
    uint32_t  rate;              /* source Hz */
    uint32_t  pairs_total;       /* decoded pairs the track holds after trim (the P3 expectation) */
    uint32_t  trim_start, trim_end;
    int16_t   rg_track_db10, rg_album_db10; uint16_t rg_peak_track, rg_peak_album;  /* tdet units */
    uint32_t  bytes_total, audio_offset;     /* stream layout */
    uint16_t  kbps;
    char      path[LIB_MAX_PATH];            /* for open / reopen */
    /* tags and cover are NOT here: the library index (or, for a non-library file, the tag reader) answers
       on demand, so the record stays small and the UI never reads half-loaded tags */
} track_t;

/* One entry per contiguous run of the ring that belongs to one track at one speed. */
typedef struct seg {
    track_t  *trk;
    uint32_t  ring_start;        /* ring push-count (R_RING_CNT domain) of the segment's first pair */
    uint32_t  ring_end;          /* known once the decoder reaches the end; 0xFFFFFFFF until then */
    uint32_t  src_start;         /* source pair index of the first pair (a seek lands mid-track) */
    uint16_t  ratio_num, ratio_den;  /* output pairs per source pair x 1: 1/1 plain; tempo 1/s */
    uint8_t   flags;             /* SEG_GAPLESS_IN, SEG_AFTER_FLUSH, SEG_TEMPO */
} seg_t;
```

**The timeline** is a 4-entry queue of `seg_t`. Everything position-related derives from it and from one hardware number, the **heard count** `heard = R_RING_CNT - FIFO_level` (pairs the DAC side has consumed, in ring-push units):

* *Which track is on screen* = the segment with `ring_start <= heard < ring_end`.
* *Position in the track (seconds)* = `(src_start + (heard - ring_start) x ratio) / rate`. This one formula is the clock, the progress bar and the seek origin for plain play, a manual change, a seek, a gapless swap and tempo. There is no `gp_pre_pp`, no "decoded position minus lead", no `ui_sec` accumulator.
* *Swap event* = `heard` crossing `ring_start` of the next segment. The event applies that track's ReplayGain target to the gain stage (its ramp is 5 ms, so two gains meet without a step), re-bases the meters, and tells the UI to render the new track.
* *Underrun* = `FIFO_level == 0` while `ring_fill > 0` (the reader fell behind: must stay 0), or `ring_fill == 0` (the producer fell behind: counted, with the segment id).

A segment that closes with no pair is dropped from the queue (it is never heard, so there is no swap to it and back) and still counts against the accounting (`tl_close`, host-proven: without the rule a zero-length track produced a phantom swap).

`heard` is exact only if the push count and the FIFO level are sampled together; the reader pushes one pair per request, so the sample is taken twice and retried until equal (a three-line loop in `aout_heard()`).

## 6. Transport

### 6.1 State axes (never one enum)

| Axis | Values | Owned by |
|---|---|---|
| **Transport** | `IDLE`, `OPENING`, `PREPARING`, `PLAYING`, `PAUSED`, `SEEKING`, `ENDING`, `ERROR` | `transport` |
| **View** | `NOWPLAYING`, `LIBRARY`, `SETTINGS`, `FULLSCREEN`, `INFO`, `DIAGNOSTICS` pages | `ui` |
| **Overlay** | none, toast, loading, error dialog | `ui` |

Opening Settings does not change transport; pausing does not change the view. The old single flags (`set_open`, `lib_ui_open`, `ui_fullscreen`, `paused`, `stop_req`, `reload_pending`, `soft_restart_req`) become the three axes plus per-request generation numbers.

### 6.2 Commands and events

Commands (input, queue, UI, remote): `PLAY(track)`, `NEXT`, `PREV`, `PAUSE`, `RESUME`, `SEEK(pair)`, `STOP`, `SET_SPEED`. Events (internal): `IO_DONE(gen, status)`, `DEC_BLOCK`, `DEC_EOF(seg)`, `SEG_ENTER(seg)`, `RING_LOW`, `SLOT_CHANGED`. Each command carries the **generation** it was issued under; an event whose generation is not the current one is dropped and counted (`stale_events`). This replaces the identity-hash watchers (`sw_head_hash`, `sw_prev_hash`, `reload_armed`) that exist to tell a user reload from a self-inflicted one: the module that switched the slot knows it did.

### 6.3 Transitions that matter

| Situation | Behaviour | Why |
|---|---|---|
| **Manual NEXT/PREV/PLAY** | flush the ring and FIFO (`aout_flush`), start a new segment with `SEG_AFTER_FLUSH`, fade in (gain stage's own 5 ms ramp from silence plus the 44 ms policy), prepare the track | a deliberate change should be immediate; the old tail is not wanted |
| **Natural end, gapless-compatible** | see 6.4 | gapless |
| **Natural end, incompatible** (other rate or codec, open failure) | let the ring play out (`ENDING`, hold off), then flush and play the next normally; the screen changes at the heard swap | rate is a hardware setting that must not change early |
| **SEEK** | `SEEKING`: flush, reposition the stream, decoder reaches the exact pair, new segment with `src_start = achieved`; the clock is correct by construction (section 5) | the old seek computed the clock from a measured rate and drifted |
| **PAUSE** | `aout_hold(1)` (the reader stops; the FIFO drains in 46 ms), gain ramp to silence first so there is no click; resume reverses | already how the ring behaves |
| **Tempo** | the stretcher sits between decoder and `aout_put`; segments carry `ratio`; the ring stays on (today the ring is off with tempo) | the timeline makes position exact, so the ring and tempo no longer conflict |
| **Error mid-track** | the segment is closed at the error; the tail plays; `ERROR` shows a dialog; no flush unless the user acts | a bad frame should not cut good audio |

### 6.4 The gapless boundary (the protocol, step by step)

1. **Decode reaches the end of segment A.** `A.ring_end = put_count` (the exact count, trim-aware). The P3 check runs: `A.pairs_put == A.trk->pairs_total` else `acct_err++` and the pair is logged. The decoder module is released (its arena is free).
2. **`queue_peek_next()`** returns the next track (shuffle, repeat and end-of-list are the queue's decision). Compatibility is decided **from the index details with no card access**: same codec, same rate, same channel count, speed unchanged, ring and gain stage on. If not compatible: 6.3 "incompatible".
3. **Prepare B in the background** while the ring plays A's tail (it holds at least `LEAD_MIN` = 600 ms, section 9): `OPENING` (target `0192`, then wait for the slot to settle: today about 80 ms in 30 ms sleeps) then `PREPARING` (decoder init from the *index details*, so **the head read is skipped**: delay, padding, frames, ReplayGain, rate and channels are already known; only a non-library file reads its head). Today's prepare costs 447 to 464 ms [HW]; skipping the head read (117 ms) and the cover decode (moved off the critical path, step 5) targets 250 ms [EST].
4. **Decode B into the ring** as soon as it is ready. B's first pair is exactly `trim_start`; `B.ring_start = A.ring_end`. No flush, no fade, no gain change yet.
5. **At the swap** (heard crosses `B.ring_start`): apply B's ReplayGain, re-base the meters, render B (title, art from the sidecar or a background job, chrome). The UI work happens now, not at load time, and never before the audio it describes.
6. **Fallbacks** (all land in the 6.3 incompatible row): `OPENING` or `PREPARING` fails; the lead was shorter than the prepare (the ring ran dry: `ENDING` becomes a normal load and the gap is counted as `late_boundary`); the user presses a key (flush, normal).

**Lead rule.** A boundary is gapless only if the ring held at least the prepare time at A's end. After a start or seek the ring fills at (decode speed - 1.0); a *fast fill* mode (audio pump only, UI sliced to its minimum) runs until the ring reaches `LEAD_MIN`, so a track that ends soon after a seek is still safe; a track shorter than the fill time cannot be gapless and the counter says so.

**What the invariant would have caught today.** The MP3 pair lost 41 ms and the FLAC pair about 2,053 pairs at the boundary [HW, 2026-10-15]; the host decode of the same files is exact [HOST]. Under P3 the first boundary reports `put != expected` and the device names the segment. The firmware cause is still open (`GP AUDIT` row in DEV 127/128 is the instrument); Player 2 does not need the answer to be correct, but the answer decides whether the old player is worth patching in the meantime.

## 7. Audio output (`aout`) and the Cymo chain

### 7.1 Interface

```c
typedef struct { uint32_t cnt, fifo, ring_fill, heard; } aout_snap_t;     /* one coherent sample, retried until stable */
aout_snap_t aout_snap(void);

/* producer side: HOT, inline, no calls out of the module */
static inline uint32_t aout_room(void);                       /* pairs that can be put now (cached; the reader only frees space) */
void aout_put(const int16_t *pcm, uint32_t pairs, bool stereo);  /* block store into the PSRAM window; publishes the write index once per block */

void aout_flush(flush_reason_t why);     /* ring + FIFO, one MMIO write; resets nothing the clock needs (re-base only) */
void aout_hold(bool on);                 /* pause: the reader stops */
void aout_set_source(uint32_t rate, speed_t sp, uint32_t seg_id);   /* the rate tick and the Cymo guard: applied when the segment is HEARD, not when it is decoded */
void aout_gain(gain_cmd_t g);            /* volume x ReplayGain target, fade; the gain stage owns the ramp */
const aout_counters_t *aout_counters(void);   /* pushed, put, flush, underrun_ring, underrun_empty, ring_min, ring_max, gain_changes */
```

Rules: `aout_put` is the only way audio reaches the ring; it takes **raw** pairs (the gain stage is at the FIFO output). It stores sequentially into the window (26 cycles a word [HW]), then writes `RING_WPTR` **once per block** (not per 64 pairs): the reader sees at most one block of latency, which is far below the FIFO. The cached `room` is conservative because only the reader frees space, so the per-block check reads `RING_RIDX` once, never per pair. The 10-15 fast path (hot, no calls) becomes the *only* path.

**Set-source timing.** `pcm_rate_apply()` today runs at decode time and flips the resampler guard and the global tick early. In Player 2 the source rate, the guard (live only for 44.1 kHz at 1.00x) and the tempo ratio are properties of a **segment**, applied by the `SEG_ENTER` event. A rate-crossing boundary is never gapless (6.3), so this matters only for speed changes, which flush.

### 7.2 What moves to hardware, what stays

| Function | Today | Player 2 |
|---|---|---|
| Volume, ReplayGain, fade | software `pcm_gain_apply` (a fallback) and the hardware stage | **hardware stage only**; the software path is deleted |
| Resampler enable | `cymo_guard_apply` from `pcm_rate_apply` at decode time | `SEG_ENTER` applies the segment's guard |
| Underrun detection | sticky flag, `und_sample()`, edges | free-running hardware event counter (P6, RTL: about 40 flip-flops) or documented edge counting meanwhile |
| Meter feed (software peak/scope) | called at decode time, 2 s early with a ring | `meters_tick()` reads the heard 1,152-pair window back from the ring (37,000 cycles per 26 ms, about 2% [EST], measured in B-740) and calls the existing `meters_feed` |
| Spectrum, wave | hardware taps, heard-aligned | unchanged |
| Clock, progress | `ui_sec` accumulator | the timeline formula (section 5) |
| Halcyon preset load | firmware loader | unchanged (cold module `eq`) |

### 7.3 The decode-to-ring contract

A decoder returns a `pcm_blk_t { int16_t *pcm; uint16_t pairs; uint32_t src_pos; }` of **already trimmed** pairs (MP3 trim, FLAC none). `transport` calls `aout_put` and adds `pairs` to `seg.pairs_put`. A decoder never touches the ring, the FIFO, the gain stage or the screen. This is the boundary at which the host differential test compares the old and new players (section 15): same files, same `pcm_blk_t` stream.

## 8. Decoders

```c
typedef struct dec_ops {
    int      (*open)(dec_t *d, stream_t *s, const track_t *t);   /* header/params; 0 or -error */
    dec_step_t (*step)(dec_t *d, uint32_t budget_cycles, pcm_blk_t *out);  /* STEP_BLOCK, STEP_NEED_DATA, STEP_EOF, STEP_ERROR */
    int      (*seek)(dec_t *d, uint32_t pair, uint32_t *achieved);       /* exact: decodes and discards to the pair */
    void     (*close)(dec_t *d);
    uint32_t (*arena_bytes)(const track_t *t);                           /* working memory the codec needs for this track */
} dec_ops_t;
```

`step` returns after at most `budget_cycles` of work (an *atomic step limit*: MP3 about 12 ms because Helix decodes a frame at a time; FLAC at most 8 ms because the new decoder yields at partition and sub-block boundaries). `STEP_NEED_DATA` is non-blocking: the decoder never waits for the card (section 9.3).

### 8.1 MP3

* **Vendor:** Helix (`third_party/libhelix-mp3`, RealNetworks licence, kept, attributed). Its six local modifications (profiling and the polyphase window hook) are re-applied as a small patch set in `third_party/libhelix-mp3/tau.patch` with a host test that macro-off output is bit-identical to the vendor.
* **The glue is new:** sync search, frame feed from `stream`, Xing/Info/LAME header (`lame_tag.h`), trim (`mp3_trim.h`: first kept pair `1152 + delay + 529`, count `frames x 1152 - delay - padding`; the arithmetic is proven on the host against the Test Album and, 2026-10-15, against the tone album: exactly 1,058,400 samples, phase continuous [HOST]), ReplayGain tags, the hardware polyphase window pipelined start/finish (the CPU overlaps the unit's 4,400-clock compute with the next slot's software DCT, B-587, kept as is), and the LAME-less fallback (whole frames).
* **Untrimmed files** (no LAME numbers) play every frame; the P3 expectation is then `frames x spf`.

### 8.2 FLAC (replaces the inherited decoder)

`fw/flac.c` (HarpMudd-origin, 73% [READ]) is replaced by a new decoder written from RFC 9639 and the existing test corpus, not by editing the old one. Requirements, all tested against the same 8 real files, the synthetic vectors (`tools/flac_make_test.py`) and libFLAC's MD5 for each:

* **Streaming and bounded:** one frame at a time from `stream`; worst-case memory stated (block size up to 4,608; 16 or 24 bit; stereo); the *channel 0 buffered, channel 1 streamed* trick of the old decoder is kept if no better layout fits the heap (a planar int16 block in PSRAM is the alternative; costs 26 cycles a word on write and must be measured before it is chosen).
* **Yielding:** `step()` returns after each residual partition group (about 512 samples), so the longest atomic step is about 8 ms, not 85 ms.
* **Hardware use:** the LPC unit (`tau_flac_lpc.sv`, proven, 45-bit accumulator suffices [HOST, B-365/B-366]) for both channels; the Rice fast path (B-560..B-562, -45% simulated cycles); the bit reader aligned to the LPC unit's feed. Channel 1's cost being double channel 0's is a *finding to design around*: the new layout decodes both channels through the same sequence so neither pays the streaming overhead alone.
* **Behaviour kept:** the refusal reasons (NO PLAY wording), ReplayGain from Vorbis comments, seek by frame probing or SEEKTABLE with exact landing, 24-bit to 16-bit conversion exactly as `to16` does today (no dither; D-G03).
* **Not in this stage:** a different codec for hi-res; the 48 kHz cap stays a measured policy, not a decoder limit.

### 8.3 Tempo (WSOLA)

`wsola_core.h` and `tempo_core.h` are kept (Tau-written, bit-exact against the integer twin, B-549..B-558). Player 2 changes only where it plugs in: the stretcher consumes `pcm_blk_t` from the MP3 decoder and calls `aout_put`; the segment carries `ratio`; the staging ring (+6 MiB) stays. Tempo remains MP3-only (D: FLAC has no headroom, B-553).

## 9. Scheduler and I/O

### 9.1 Loop

```c
for (;;) {
    t0 = cycles();
    input_poll();                                     /* <= 0.3 ms: buttons, key repeat, chords */
    io_poll();                                        /* advances the one in-flight target request; never blocks */
    fill = aout_fill();                               /* ring pairs now */
    mode = governor(fill);                            /* see 9.2 */
    if (mode != SHED_ALL) transport_pump(budget_for(mode));   /* decode steps while fill < HI; boundary prepare; seek */
    ui_pump(ui_budget_for(mode));                     /* <= 1.5 ms slices: regions, meters, settings animation */
    diag_pump();                                      /* counters, once per second */
}
```

There is **no other loop and no wait that calls `poll_input()`**. Anything that today spins (slot settle, head read, TIM1 decode, settings SD writes, the Settings crossfade, a full-box redraw) is a state machine or a sliced job that returns to this loop at least every 1.5 ms. `check_blocking.py` (new) fails the build if a function marked `SLICED` contains an unbounded loop without an `io_poll()`/`yield()`.

### 9.2 Governor (what the ring level buys)

| Ring fill (of 48,672) | Mode | Decode | UI | Meters | Rationale |
|---|---|---|---|---|---|
| above 50% (24,336, 550 ms) | `NORMAL` | until HI (95%) | full | full | two worst atomic steps plus an I/O wait fit twice over |
| 25 to 50% | `SHED_UI` | until HI | slices halved, animations skipped | software meters off (hardware ones stay) | decode must regain the lead before the next long step |
| 10 to 25% (4,867, 110 ms) | `AUDIO_ONLY` | continuous | none except input | off | decode alone until back above 25% |
| below 10% | `PANIC` | continuous, input ignored except STOP | none | off | two FIFOs from dry; counted as `panic` |

Decode stops at HI (95%) so the producer does not hold the CPU when the ring is full: `NORMAL` with a full ring is the steady state at 1.00x and leaves the CPU to the UI. The thresholds are constants in `governor_core.h`, host-tested against an independent twin and a scheduling simulation (`sim/test_p2_governor.py`). **What the simulation says (2026-10-15, with the spec's own numbers):** an MP3 (11.5 ms per 1152-pair frame, 2 ms of UI per pass) stays in `NORMAL` 99.7% of the passes and never underruns; a hard FLAC (4,096 pairs per 85 ms of CPU, sliced into 8 ms steps) **underruns constantly without the governor and not at all with it, but the governor keeps the lead near the `AUDIO_ONLY` threshold and the UI receives only about 3.5% of the time** (the decoder makes 48.2 pairs per ms of CPU against the 44.1 the output consumes, a 9% margin that the loop's own 0.4 ms per pass and any UI work eat). Two consequences: the governor is necessary, not decorative; and the FLAC decoder's speed is the lever for the UI on hard files, which is why section 8.2 gates the new decoder on idle time, not elegance. **Hardware, S3 (2026-10-16):** after a flush or at the start the ring is empty, so the governor passes through `PANIC` and `AUDIO_ONLY` on its way up (the counters showed one entry of each per flush). That is the *fill phase*, not a failure: the real transport must treat "the stream has not started yet" as its own state (decode flat out, no shedding statistics, no `panic` count) and count `PANIC` only while audio is playing. A decoder slower than real time cannot be saved by scheduling (the model underruns, as it should); a stall from a full ring survives 900 ms and not 1,300 ms (the 1.1 s lead).

### 9.3 I/O (`stream`) and the slot

* **One request type at a time** in the platform layer: `OPEN(path)`, `READ(off, len, dst)`, `IDENT`. Completion comes from `io_poll()`; the result carries the **generation**. A completion for a closed generation is dropped and counted. Cancel is "bump the generation and let the in-flight command finish" (the APF command cannot be aborted).
* **Read-ahead** keeps at least 2 frames of compressed data in the 24 KB ring (protected, today's `RING_SIZE`) and starts the next read *before* the decoder needs it (the old `flac_pull` lesson: reads were only started once the ring was dry, so "O" was a consequence of "D reaching zero"). The size of each read is a measured constant ([OPEN]: the S1 experiment sets it; today's value is read from `refill_pump` at that point).
* **`SLOT_CHANGED`**: today the firmware polls the slot's file identity (`0190`) every 100 ms after an open. Player 2 first tries the **host `0x008A` data-slot-update** if the bridge exposes it (it is not used today, [OPEN] S1 probe: install a core that logs `0x008A`, reload the slot from the Pocket menu, read the counter). If it works, polling is deleted; if not, polling is a `stream` internal and the *self-inflicted* switch is known from the generation, so the hash watchers still go away.
* **Errors** normalise to `OK, NOFILE, SHORT, TIMEOUT, STALE, BADPATH`; a late failure is recorded on its own segment and never corrupts the playing one.

## 10. Library, queue, settings

* **Queue** (`queue/`): the library index reader (`library_core.h`, kept) and the playlist reader behind one `queue_t`: `peek(n)`, `advance(dir)`, shuffle (seeded, reproducible), repeat modes, history (persist words), "up next" for the UI. The queue knows nothing about playing; transport asks it. The legacy `.m3u` mode is already gone; a playlist is a library playlist.
* **The index details record (32 B per track)** becomes the source of truth for `track_t`: rate, channels, bits, kbps, size, delay, padding, frames, ReplayGain, cover flags. A file the index does not know (a track added without a sync) takes the **head-read path**, the one the old player always took, kept as the slower fallback. This is what makes the boundary prepare fast and what lets the UI show full details for the previous and next track without opening them (the details report added 2026-10-15 already reads these fields).
* **Settings** (`settings/`): a typed value table (id, width, range, default, persist word and bits, migration), one `apply` function per value that the table calls on load and on change, so *persisted state, runtime state and the menu* come from one description (the manifest idea of the meter modules, applied to settings). The persist-word allocation (`tools/persist_registry.json`) is generated from it; the 16-entry interact.json cap and the reload-bit clutter (KB-079, KB-080) are checks in the generator. `SW_*` and Halcyon preset indices keep their current ids so no saved setting is lost on upgrade (a migration test loads a card image from each released version).

### 10.1 Skip silence (owner request 2026-10-16; design only, not built)
Long silence at the start or end of some tracks (live albums, hidden tracks, fade tails) can be skipped. **Detection is offline, in Tau Omega** (it decodes the file anyway): it writes, per track, `lead_skip` and `tail_skip` in source pairs next to the existing gapless numbers in the library details record (`tools/tau_details.py`; the 32 B record has a 4-byte `enc` word and flag bits, a field addition needs a details version bump, decided with Omega). **The player only applies the numbers**, as a second trim on top of the encoder trim: for MP3 the kept window becomes `[S + lead_skip, S + P - tail_skip)` (`mp3_trim_cfg` gets two extra arguments, host-proven like the existing trim), for FLAC a start offset by frame probing and an early end. The P3 expectation (`pairs_total`) is the shortened length, so the accounting stays exact; the clock shows the shortened time. A Settings switch (Playback > SKIP SILENCE, off by default) turns it on; a track without the numbers plays whole. A threshold (for example below -60 dBFS for at least 2 s) and a "never skip inside a track" rule belong to Omega's detector, with a per-track override. **Why offline:** an on-device detector would have to decode the silence it wants to skip, which costs the CPU it is meant to save and cannot know the tail in advance; seeking past a lead needs the stream seek that S6 already provides for the ID3 tag.

## 11. UI

The UI is the **third-party-style consumer** of the counters and the heard segment; it owns pixels and nothing else.

* **Views** (`ui/views.c`): a static table of `view_t { enter, exit, input, draw, invalidates }` (the Helios section 11 design, now complete with `draw` and `input`), one `view_switch(to)` that is the only place a view is left. Opening a view never touches transport.
* **Regions:** Helios's dirty-region flush remains the drawing model; the meter's own element-level skip caches stay inside the meter (the review rejected unifying them, and nothing here changes that).
* **Now playing** renders `heard_seg->trk` through the library accessor; the previous and next track are available for transitions and the details page. The loading state is an overlay on the *incoming* segment.
* **Meters** take `mtr_in_t` as today (`fw/meter.h`); the feed comes from `meters_tick()` (section 7). The meter framework (manifest, generated tables, Configure page, PSRAM state, cost gate) is unchanged by Player 2.
* **Info and reports**: generated from the counter registry (section 12.2). The Info page, the `SR_T_INFOTEXT`/`SR_T_TRACKINFO` report entries and the stress logs are three views of the same registry.
* **Details**: the track-details page reads `track_t` and the index; it is no longer a side effect of `lib_det_id`.
* **The Figma redesign** (node 194:1630, parked) lands here as new `view_t` implementations, not as edits to a monolith; that is the payoff of the layer.

## 12. Memory, counters, capabilities

### 12.1 Memory

On-chip RAM is a budget table, generated into a header and checked at link time (`tools/ram_report.py`, today's snapshots); every region has one owner (P7):

| Region | Owner | Budget | Notes |
|---|---|---|---|
| compressed-data ring | `stream` | 24 KB | protected, unchanged |
| decoder arena | `decode` | static, per codec | MP3 Helix; FLAC the new decoder's stated worst case; never both at once |
| PCM block + meter capture | `transport`/`ui` | about 4.6 KB shared by lifetime | `pcm` (4,608 B) is not live with the art accumulator (RAM_BSS_AUDIT) |
| timeline, tracks, counters registry | `transport`, `counters` | 1 KB hot | fixed capacity |
| UI state | `ui` | per view, cold where possible | views are cold code; `view_t` tables are cold data |
| heap gap | none | **at least 12 KB release, 4 KB default Diagnostic** | the acceptance numbers of section 1 |

PSRAM (all through the window; one owner each; `generated/memmap.h` from `memmap.json`, with an overlap test):

| Region | Owner | Address (window) |
|---|---|---|
| art accumulator | `ui`/art | 0x0000 to 0x3FFF |
| report record | `diagnostics` | 0x4000 to 0x7FFF (16 KB since 2026-10-15) |
| Check scratch | `diagnostics` | 0x8400 to about 0x84A0 |
| persistent meter state | `ui`/meters | 0x9000 to 0xFFFF (28 KB) |
| library image | `queue` | +0x10000 (up to 4 MiB) |
| queue area | `queue` | +0x510000 |
| tempo staging ring | `decode`/tempo | +0x600000 to +0x640000 |
| **PCM ring** | `aout` | +0x700000 (52,000 pairs = 208,000 B) |
| cold image | `platform` | 8 MiB (1 MiB reserved) |

### 12.2 The counter registry

```c
typedef struct { const char *name; uint8_t unit; uint8_t scope; uint8_t width; volatile void *src; fmt_fn fmt; } cnt_t;
/* registered once, in a cold table: COUNTER(aout, "RING MIN", UNIT_PAIRS, SCOPE_RUN, &aout_c.ring_min, fmt_u32) */
```

The Info page iterates the table; the report exporter emits one `SR_T_INFOTEXT` per entry; the stress logs pick entries by name. The labels live in cold data as a fixed-width table (today's `set_info_label`, which already moved to cold data on 2026-10-15 for a 300 B gain). `SET_INFO_ROWS` and the three hand-maintained places disappear. A new module adds counters by adding table rows in its own file.

### 12.3 Capabilities

`caps_t caps;` filled once by `platform_probe()`. Today there are separate probes with separate failure behaviour (`COLD_READY`, `BLIT_READY`, `RRECT_READY`, `BLEND_READY`, `CYMO_RESAMP_READY`, `hw_poly`, `hw_lpc`, the ring probe, TIM1). Player 2 keeps each proof (they are right: a mismatch must be tested safely) but exposes one result struct, and the **firmware/bitstream feature check** (`TAUFWNEED`) refuses a ROM whose needs the bitstream does not meet before boot continues. An **optional** read-only `CAPS` MMIO register (one 32-bit word, bits for each unit, written in RTL as constants) would replace the probes that exist only to ask "is it there" (ring, gain, resampler, polyphase, LPC, wave, spectrum); the proofs that test *behaviour* (CLUT, rrect, blend) stay. This is the only RTL change Player 2 proposes for its first release, and it is optional.

## 13. Required platform, and what is dropped

Player 2 requires: a bitstream with the PCM ring (the `pcmring-b745` fit; **not in any published release yet**, so Player 2's first release is the first to ship it), the hardware gain stage (in every build since `TAU DEV 105`), the cold image, the 192 KB RAM link and CLK66. **Dropped from the product:** the software volume/fade path, the direct FIFO push loops (MP3 `pcm_push_pairs`, FLAC `cymo_push`, tempo push), `rg_playout`, `gp_save/gp_load` and the whole `gp_*` display shadow, `pcm_rate_apply` at decode time, `ui_sec`/`ui_sec_acc`/`ui_last_frames`, `sw_head_hash`/`sw_prev_hash`/`reload_armed`, the three `pl_ui_restore` call sites, the legacy playlist overlay, the alpha-release `TAU_PCM_RING_FW` macro (the ring is not optional). Releases on a pre-ring bitstream stay on the old player until it is retired; the ring bitstream therefore ships first with the old player's `TAU_PCM_RING_FW` switched on (that is the existing G1/G2 work, hardware-proven in pieces on DEV 123 to 128) or with Player 2, whichever is ready first.

## 14. Retiring the inherited code (what, when, how)

Provenance is read from the 2026-10-13 report [READ]; the rule is the one already in `docs/PROVENANCE.md` (D-P01): the replacement is written from a **behavioural specification and tests**, not by editing the inherited file; the old tree stays in git history and the dated report.

| Inherited component | Origin share today | Disposition | Stage |
|---|---|---|---|
| `fw/player.c` `main`, `poll_input`, `load_track`, `read_track_head`, `vbr_frame_count`, `refill_pump`, seek (`flac_seek_locate`, MP3 seek), `ui_draw_dynamic_cold`, `meters_feed` | 67 to 98% per function | **replaced** by `app`, `transport`, `stream`, `decode` glue, `ui`, `meters_tick` | S3 to S7 |
| `fw/art.inc` | 83% | **replaced** by `ui/art` (a sliced `art_job`, TIM1 first, the embedded-JPEG fallback behind a macro that is then deleted per the 2026-10-14 decision once Omega writes sidecars) | S6 |
| `fw/playlist.inc` | 79% | **deleted** (library playlists only) and the loader folded into `queue` | S4 |
| `fw/settings.inc` | 42% | **replaced** by the typed table, ids preserved | S5 |
| `fw/flac.c`, `flac.h` | 73 to 84% | **replaced** by the new decoder (8.2); the old file is used as the differential reference until acceptance, then removed | S8 |
| `fw/alloc.c`, `fw/sysio.c`, `fw/start.S`, `fw/link.ld` | 100%, 100%, 83%, 42% | **rewritten** small (arena with a high-water mark, newlib stubs, startup, linker map generated from `memmap.json`); trivial but inherited | S2 |
| `third_party/libhelix-mp3`, `picojpeg`, Inter font | vendor | **kept**, attributed, patch set separated | n/a |
| `core_top.v`, APF shell, `data_loader.sv`, `sound_i2s.v`, `sync_fifo.v`, `sdram_fb.sv` | external/inherited | **kept** (frozen platform boundary) | n/a |
| `pcm_fifo.v` (84%), `tgt_cmd.v` (92%), `mp3_fb.sv` scanout (44%), `mp3_soc.v` (26%) | inherited bases | **R6**: decided separately; Player 2 does not need to change them, only to observe them better (P6 counter) | after S8 |
| `tools/` inherited scripts (FLAC checkers, previews, `rv32sim.py`) | mixed | kept as test infrastructure | n/a |

**What this achieves and what it does not.** After S8 the product firmware (not the RTL, not the vendor code) contains no identifiable HarpMudd *application* source; the report's own caution applies: the figure to publish is a re-measurement with `provenance-2026-10-12/measure.py` against the new tree, not a claim of independence. HarpMudd's work remains in the history, in the FLAC test tooling, in the platform shell and in the RTL bases, and the attribution files must say so. Renames and moved `.inc` bodies do not count (the report's own rule).

## 15. Verification (a gate for every stage)

**Layers** (each stage must add tests at the layer it touches):

1. **Host, pure cores** (existing `make test-host`, red-then-green with mutants): every `cores/*.h` and each new module's logic, including the governor, timeline arithmetic, queue, settings table and the counter registry.
2. **Host, whole player on the rv32 simulator** (`tools/rv32sim.py`, the `host/` harnesses): the new `transport`+`aout`+`stream` against a software model of the ring reader, FIFO, gain stage and card (replay of recorded target-command timings, including the 1,074 ms/33-read outlier of B-740). Properties: P3 for every boundary and seek; no underrun in any trace whose longest stall is below the lead; `heard` monotone; the timeline formula equals ground truth.
3. **Differential against the old player** at the `pcm_blk_t` boundary: the same file, same speed, same seek script, the new and the old decoders produce identical pairs (MP3 with trim, FLAC); documented differences only where a bug is fixed (the boundary).
4. **System RTL test** (`sim/tb_pcm_ring_soc.v` and kin) with the **real CPU and firmware**: two synthetic tracks through the real ring, FIFO and reader; the marker, the swap event and the counts; a FLAC-rate case (the existing test never exercised a decode path at real rates, which hid the 2026-10-15 FLAC cost bug).
5. **Hardware gates** (the Pocket, the Check, the loopback tools): section 15.1.

### 15.1 Hardware acceptance

| Gate | Measure | Pass |
|---|---|---|
| **Gapless tone** (`test music/gapless_tone/`: MP3 pair, FLAC pair, mixed) | `cymo_loopback.py track` on the EVO 4 recording of each boundary; the Info `P3` counters | no level dip over 0.5 dB, no phase jump over 2 degrees, `put == expected` on every segment, mixed pair falls back with `late_boundary == 0` |
| **Clock** | the displayed clock at each swap, a 12 s track, Stop, seek, pause across a boundary | within 0.1 s of the true time; never negative or wrapping |
| **L0 / ENDURANCE** | Check profiles USER/STANDARD/FULL/ENDURANCE | zero late underruns, `ring_min` above `LEAD_MIN` x 2 in steady state |
| **CPU** | HEADROOM idle, the Check's decode/meters/push/UI split | MP3 busy at most 35%; hard FLAC idle at least 20%; no regression at 1.75x tempo |
| **Latency** | press to sound; press to screen response | manual change at most 1.0 s (1.48 s today [HW]); input to visible response p99 at most 80 ms |
| **Memory** | `ram_report.py` on the 192 KB release and the default Diagnostic Build | heap gap at least 12 KB / 4 KB; no region overlap |
| **Fallbacks** | boot with the cold image removed; with a bitstream missing the ring; a corrupt tag; a truncated file; a card pulled mid-track | defined reduced player, no crash, a counted error |
| **Upgrade** | load a card image from each released version | every setting preserved |

## 16. Delivery plan

**Recommendation: build `player2` as a second firmware target beside the old player, sharing the platform and vendor layers, and switch the default only when the gates pass.** A strangler migration inside `player.c` was the earlier plan (R1 to R4); it is safe but keeps the coupling alive for months, and Player 2's central idea (the timeline replaces the display shadow) cannot be introduced half-way. The cost of the new target is that both exist for a while; the benefit is that the old player stays shippable and is the differential reference.

| Stage | Work | Exit gate | Size |
|---|---|---|---|
| **S0 spec and baselines** | this document accepted; pin the shipped ROM, cold image, bitstream, corpus and framebuffer snapshots; the global ownership map (R0); decide section 17 | owner decisions recorded in `DECISIONS.md` | S. **DONE 2026-10-15**: decisions D-P2-01..10 recorded; `tools/player2/baseline.py` + `docs/research/player2/BASELINE.json`; `tools/player2/globals_map.py` + `docs/research/PLAYER2_GLOBALS_MAP.md` (736 file-scope variables, 601 functions, **163 variables written by more than one cluster**, 74 touched by more than three) |
| **S1 platform probes** | `0x008A` probe core; the optional `CAPS` register decision; free-running underrun counter spec (RTL, separate fit); measure the read-size experiment | numbers written, decisions made | S |
| **S2 platform + counters** | `fw2/platform` (start, arena, link from `memmap.json`, mmio, caps, cold, timers), the counter registry, `check_deps.py`, `check_blocking.py`; boots and shows an Info page on hardware | boots on a Pocket; counters on the Info page and in a report | M |
| **S3 aout + timeline** (**DONE 2026-10-16: `aout` and the tone transport run on a Pocket, two segment boundaries with zero slips, B-757/B-758**; host cores done 2026-10-15: `timeline_core.h`, `governor_core.h`, `counters_core.h` and `tools/check_deps.py`, all with mutants, in `make test-host`; the Pocket part is not started) | `aout`, the timeline arithmetic, the governor (host-tested against recorded traces); a "tone player" that plays a built-in test pattern through the real ring | host gates 1, 2, 4; a Pocket plays a tone gapless across two segments with P3 exact | M |
| **S4 stream + queue** | `stream` (one request, generations, read-ahead), `queue` (library, shuffle, repeat, history) | host gate 2 with card timing replay; a Pocket plays a list from the library with no decoder | M |
| **S5 settings** | typed table, persist generator, migration test | upgrade gate; menu parity with a snapshot | M |
| **S6 MP3 transport** | MP3 adapter on Helix, trim, hardware polyphase, ReplayGain, seek rebuilt (Xing TOC/CBR then exact discard), gapless MP3, tempo plugged | differential gate 3, hardware gapless-tone MP3 pair, CPU gate | L |
| **S7 views and meters** | view table, now-playing, library, settings pages, fullscreen, meters via `meters_tick`, art job; Info generated from the registry | snapshot parity with the old UI; Check ENDURANCE; Figma views can start here | L |
| **S8 FLAC** | the new decoder, differential against `flac.c` over the corpus, then FLAC gapless | corpus MD5 parity, CPU gate, hardware FLAC tone pair; the old FLAC removed | L |
| **S9 retire** | delete the old player and `TAU_PCM_RING_FW`, re-run the origin measurement, update NOTICE/ATTRIBUTIONS/PROVENANCE, release | no product reference to retired code; full gates; a Preview release | M |

Each stage is one or a few reversible commits on a branch in its own **worktree** (`../tau-alpha-player2`); the shared checkout's rule about concurrent sessions applies. A bitstream change is never in flight together with a firmware stage (the CAPS register and the underrun counter are one optional fit, scheduled before S3, and Player 2 works without them).

## 17. Decisions needed from the owner

| # | Decision | Recommendation | Consequence |
|---|---|---|---|
| D1 | New target beside the old, or in-place strangler | **new target `player2`** (section 16) | two firmwares for a few releases; old stays the reference |
| D2 | Drop support for bitstreams without the ring and gain stage | **yes** (the ring bitstream is already fitted and on hardware; the pairing gate refuses mismatches) | deletes four push paths and the software gain; the ring bitstream must ship before or with Player 2 |
| D3 | Replace the inherited FLAC decoder | **yes, last (S8)**, differential against the old one | the only stage that can make hard files worse; gated on CPU numbers |
| D4 | Keep Helix as the MP3 decoder | **yes** (vendor, RealNetworks; replacing it is a separate project) | glue rewritten, codec kept |
| D5 | Gapless scope of the first release | same rate and codec only, **with** the index-details prepare; rate/codec change stays a play-out | matches GAPLESS_SPEC; cross-rate is a later segment kind |
| D6 | Tempo with the ring on | **yes** (timeline carries the ratio) | one less mode switch; the position bug class disappears for tempo |
| D7 | Optional `CAPS` register and free-running underrun counter (one small fit) | **yes, schedule before S3**, but do not block on it | cleaner probes and exact underrun counts |
| D8 | UI scope of the first release | functional parity with today's screens; the Figma redesign follows as views | decouples the audio rebuild from design work |
| D9 | Cross-file optimisation (LTO) | **DECIDED (owner, 2026-10-15): no LTO now; revisit with measurements at S6** (hot functions `static inline` and `HOT`) | keeps debugging and the cold-call check simple |
| D10 | Manual change keeps flush plus fade-in | **yes** | the clicks at manual changes are a separate, open item (HW GAIN/track-change recordings) |

## 18. Risks

* **Scope.** Player 2 is the largest firmware change since the project began. Mitigation: the stages above, the old player as a permanent differential reference, host layers before hardware.
* **CPU headroom is thin on hard FLAC** (8% idle on a 44.1 kHz test file [HW]) and at 1.75x tempo (idle 0% [HW]). Mitigation: the new FLAC decoder is gated on idle, not on elegance; the governor sheds UI before audio.
* **The 2026-10-15 boundary loss is unexplained** (firmware cause open; `GP AUDIT` is the instrument). Mitigation: Player 2 does not depend on the answer; if the answer is a one-line fix, ship it in the old player first.
* **`0x008A` may not be available** to this core. Mitigation: polling stays, hidden inside `stream`, with the generation rule replacing the hash watchers.
* **Hidden coupling** (the build-script lessons of B-250/B-448). Mitigation: `check_deps.py`, `check_blocking.py`, `check_cold_calls.py` extended (P5), the bitstream/firmware feature manifest, and per-stage hash records of every ROM.
* **Timing of PSRAM traffic.** The ring put (26 cycles/pair), the reader (32), instruction fetch from PSRAM and the meter readback share one chip (a worst access of 380 cycles was seen under load [HW]). Mitigation: counters for worst access and `ring_min`; the governor.
* **Licence/attribution hygiene.** Mitigation: the dated measurement, the notices updated at S9, and the one-way rule for any code read from other projects.

## 19. Appendix: sizing of the ring and the lead

* Prepare of the next track today: 447 to 464 ms [HW]; target 250 ms [EST]; worst seen 1,170 ms (a FLAC with a large metadata block: with index details the head read is skipped, but the **file open** still has to walk the metadata to the first frame; the new FLAC reader uses `skip` for blocks it does not need, which the old one already does).
* `LEAD_MIN` = prepare worst case (600 ms, rounded up from the largest *typical* value) plus 100 ms margin = **700 ms = 30,870 pairs at 44.1 kHz**; the ring holds 48,672, so steady state has 1.6 x the need.
* Fast fill after a start or seek at decode speed 1.3 s/s (MP3, B-591) reaches 700 ms in about 0.54 s of decode; the hard FLAC at about 1.1 s/s needs about 7 s: a short hard FLAC may end before it reaches `LEAD_MIN`; the counter says so, and the boundary falls back to play-out.
* PSRAM share: put 26 x 44,100 = 1.15 M cycles/s (1.7%), reader 32 x 44,100 = 1.41 M (2.1%), meter readback about 1.4% [EST]; cold fetch is the remaining client.
* Atomic steps: MP3 about 12 ms, new FLAC at most 8 ms, UI slice 1.5 ms, input 0.3 ms, I/O poll 0.1 ms; worst stall seen outside decode: 1,074 ms (the slot settle outlier, now a state machine).

## 20. Appendix: glossary

**heard**: pairs the output side has consumed, in ring-push units. **segment**: a contiguous run of ring pairs belonging to one track at one speed. **timeline**: the queue of segments. **lead**: pairs in the ring plus the FIFO. **atomic step**: the longest CPU work between two returns to the main loop. **P3**: the sample-accounting invariant. **generation**: a never-reused id carried by every request and event.
