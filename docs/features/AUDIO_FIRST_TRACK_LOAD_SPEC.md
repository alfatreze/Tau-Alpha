# Audio-first track load

Owner ask: track loading currently blocks audio start on album-art decode -- measured on real
hardware at **2,801 ms of a 3,731 ms load (75%)**. Reorder so audio starts first (fade in), the
previous track's cover keeps showing during the gap, the new cover decodes in the background, then
fades in once ready.

This repo has no `docs/features/` directory yet (every other spec lives flat under `docs/`, e.g.
`docs/HELIOS_SPEC.md`, `docs/MEDIA_LIBRARY_0.4_SPEC.md`); this file is placed at the path the task
asked for regardless, since a `docs/` reorg into type-based subdirectories is already underway in
another session's history (`docs: reorganize docs/ by type` is not on this worktree's branch). If
that reorg lands here later, this file's natural home is `docs/features/`.

## 1. The current sequence, read from `fw/player.c`

Everything below runs inside one function, `load_track()` (`fw/player.c:8336-8795`), called
synchronously from the main loop -- either at boot or on a track change (`fw/player.c:9173`), never
concurrently with anything else. It returns only once the track is fully ready to play; nothing else
in the firmware runs while it is in progress (no interrupts exist in this design; see
`docs/HELIOS_SPEC.md`'s UI/audio-decoupling discussion for why that was a deliberate choice).

In order:

1. Free the FLAC buffer / (re)init the MP3 decoder (`fw/player.c:8347-8353`).
2. **`pcm_flush()`** (`fw/player.c:8356`) -- drops every sample queued in the hardware FIFO and
   arms the fade-in (`fade_left = FADE_SAMPLES`, see section 2). From this instant the DAC holds a
   DC level (silence) until real decoded samples are pushed again.
3. Reset per-track state, read `R_SLOT_SZ` (`fw/player.c:8358-8409`).
4. `read_track_head()` (`fw/player.c:8412`) -- one blocking SD read, timed as `ld_head`.
5. Format-specific setup: for FLAC, opens the stream, reads STREAMINFO, decides on hi-res
   rejection (`fw/player.c:8415-8515`); for MP3 this step is nearly free.
6. The size probe is **already incremental**, not blocking (`fw/player.c:8524-8547`) -- a prior
   fix (see the comment there) turned a 480 ms blocking scan into "one read per pass" spread across
   the following seconds of playback. Timed as `ld_size`. No work needed here.
7. For a library track, `ui_loader_begin_ex(1)` draws the full player frame with a spinner
   (`fw/player.c:8560-8563`) -- see section 3.2, it deliberately does *not* touch the art stash.
8. **Cover art** (`fw/player.c:8564-8657`): the expensive step. A cheap signature check
   (`art_sig_of()`) first asks whether the stash already holds the right picture (same file restart,
   or a different track sharing its album's cover) -- if so this is nearly free. Only on a genuinely
   new/different cover does it call `art_decode()` (`fw/art.inc:423`), which does the real work: find
   the APIC/PICTURE frame, `pjpeg_decode_init()`, then a `for my / for mx` loop over MCUs
   (`fw/art.inc:507-556`) that intermixes SD reads (pulling more JPEG bytes) with the actual
   decode/scale math. Timed as `ld_art` -- this is the 2,801 ms.
9. `ui_chrome_paint()` (`fw/player.c:8665`), `ui_boot_cancel()`.
10. **`prefill()`** (`fw/player.c:8668`, defined at `:7677`) -- fills the compressed-data ring
    buffer via `refill_one()` (more blocking SD reads) up to `PREFILL_CHUNKS * REFILL_CHUNK`.
11. Decoder warm-up (`fw/player.c:8684-8755`): decodes and *discards* frames until the bit
    reservoir has genuinely warmed up (~26 ms), establishing the real sample rate before anything
    draws a duration from it. These frames are never pushed to the hardware FIFO.
12. Return. The **main loop**, not `load_track()`, is what actually starts pushing decoded PCM
    into the hardware FIFO on every subsequent iteration.

So the DAC is silent from step 2 through the end of step 12 -- head read + format setup + art decode
+ prefill + warm-up, in that exact order, entirely inside one blocking call. `ld_head`, `ld_size`
(now ~0), `ld_art`, `ld_pre` (folded into `ld_total`, see the `UI_SHOW_LOAD_TIMES` comment at
`fw/player.c:8757-8793`) and `ld_total` are all already measured and, as of this codebase's current
state, already **permanently visible** on the Diagnostic Build's Info page (`fw/settingsui.inc:598-601`,
row "LOAD MS", format `H/S/A/T`) -- no new instrumentation was needed for this investigation.

## 2. Two of the three asks are already built

Reading the code before designing anything found that two of the three behaviours the redesign asks
for already exist, just not for the reason anyone thinks:

- **"Previous cover keeps showing."** `ui_art_mount()` (which paints the plain grey plate into the
  off-screen art *stash*) is only called on the genuinely-new-cover path (`fw/player.c:8622`), and
  even then it only touches the off-screen stash at `ART_STASH_Y=360`, never the on-screen visible
  panel at `ART_Y=8`. The visible panel is only ever repainted from the stash by `ui_art_draw()`,
  which is gated `if (art_ready && art_shown)` (`fw/player.c:6284`, `:10097`) -- and `art_ready` is
  explicitly held at 0 for the entire art-decode block (`fw/player.c:8578` .. `:8656`). So **the
  on-screen panel keeps showing whatever was there before, unmodified, for the whole 2.8 s**, and
  only flips to the new picture once `art_ready = 1` (`:8656`) lets the next `ui_art_draw()` copy the
  finished stash across. This is confirmed *deliberate*, not incidental: `ui_loader_begin_ex()`'s own
  comment (`fw/player.c:3751-3758`) states outright, from B-075's fix for exactly this bug, "NOT
  `ui_art_mount()` here... The plate briefly shows the OUTGOING track's cover under the spinner
  instead of a neutral grey -- correct far more often than it is not." **No code change needed for
  this part of the ask.**
- **"Fade in."** Every `pcm_flush()` (which load_track() calls at step 2 on every load, restart or
  otherwise) already sets `fade_left = FADE_SAMPLES` (`fw/player.c:6517`, `FADE_SAMPLES = 2048`,
  `:940`). The push loop ramps gain from 0 to 255/256 across those 2048 samples whenever `fade_left`
  is nonzero (`fw/player.c:7478-7482`, `:10376-10381`) -- this exists today specifically as
  click-suppression on any discontinuity, but it already *is* the fade-in primitive the redesign
  asked whether it needed to build. **No new mechanism needed; only the moment audio starts is
  what's late.**

What's actually missing is the third piece: audio genuinely starting *before* the 2.8 s art decode,
not just visually looking calm during it.

## 3. Why the reorder is not safe as a bare swap -- the real constraint

`fw/art.inc`'s own header comment (`fw/art.inc:1-8`) states the current ordering is deliberate, not
historical accident:

> Runs ONCE per track, before prefill -- so the blocking reads it needs cannot starve the decoder,
> because playback has not started yet. Decoding later, during playback, would put hundreds of
> milliseconds of SD reads directly in the path that keeps the PCM FIFO fed.

This is confirmed by the hardware numbers, not just the comment:

- The hardware PCM FIFO is `pcm_fifo.v`'s `AW=11` = **2048 entries ~= 43 ms at 48 kHz**
  (`src/fpga/core/pcm_fifo.v:23`, and independently corroborated by the meter code's own comment at
  `fw/player.c:5837`, "FIFO is 2048 entries").
- Art decode costs **2,801 ms**, dominated by SD reads (art.inc's own comment: "the only real cost
  is reading the bytes off SD" for a large cover, `fw/art.inc:43`).

Any design that lets `load_track()` return *before* art decode -- i.e. genuinely starts pushing real
audio to the FIFO first -- and then runs the existing, un-chunked `art_decode()` as a single ~2.8 s
blocking call from the main loop, will drain the 43 ms FIFO in the first fraction of a second and
then sit through a **hard underrun for the remaining ~2.7 s**, mid-track, while the CPU is stuck
inside that one call. `pcm_fifo.v` glides gracefully to zero on an underrun rather than clicking
(`fw/player.c:931` comment), but 2.7 s of silence *mid-playback*, right after the user just heard the
track start, reads as "it broke" far more than today's *pre*-track loading pause does. **A bare
reorder is not a net improvement -- it can be a worse one.** This is the real correctness risk the
task asked to investigate, and it is why art decode was placed where it is.

## 4. What genuinely unlocks a safe version: `art_decode()` is already MCU-chunked

The one thing that changes the calculus: `art_decode()`'s hot loop already decodes **one MCU
(minimum coded unit -- an 8x8 or larger pixel block) per call** to `pjpeg_decode_mcu()`
(`fw/art.inc:511`), inside a `for (my ...) for (mx ...)` loop. picojpeg's own API is already
structured to be resumable at MCU granularity; nothing about the *decoder* needs inventing. What
would need building, to make a real background/interleaved version safe, is:

- Turning the local loop state (`info`, `my`, `mx`, `reduce`, `mcu_w/h`, `bxn/byn`, `sw/sh`,
  `aw/ah`, `next_ay`) into state that survives across separate calls (statics or a small struct),
  and exposing `art_decode_begin()` / `art_decode_step(budget_mcus)` instead of one monolithic call
  -- the exact same "incremental, one chunk per main-loop pass" shape this codebase already uses for
  the FLAC size probe (`fw/player.c:8524-8547`, "an operation that long simply cannot live here...
  It is now INCREMENTAL").
- Splitting `load_track()` so the parts needed to actually start playback (format detect, prefill,
  decoder warm-up) run and return *before* a new cover is decoded, with an `art_pending` flag
  serviced from the main loop afterward, budget-limited and checked against real FIFO headroom
  (`pcm_level()`, the same primitive `meter_afford()` already reads at `fw/player.c:5860`) before
  each chunk, so a chunk is only spent when there is genuinely room to spend it without touching the
  underrun floor.

This is real, buildable work -- but `fw/art.inc` is correctness-critical code with a documented
history of subtle, silent-corruption bugs found only by careful review or a dedicated tool (the
H1V2 block-offset bug and the accumulator row-overflow bug, both noted in `fw/art.inc`'s own
comments, `:519-524` and `:479-485`). There is currently **no test harness that runs the real
`art_decode()` C code against real JPEG bytes and checks pixel output** -- `tools/art_scale_model.py`
mirrors the *geometry* math in Python as a design aid, it does not exercise the actual firmware
function. Refactoring the hot MCU loop without that kind of harness, and without hardware access
this session (no card install permitted for this task), is exactly the class of "guessed" change
this codebase's own history warns against -- e.g. `fw/player.c:940`'s own scar tissue: "Discarding
was tried once before and made things worse," and the load-timing comment at `:809-812`: "four
attempts at this were aimed by theory and three of them made it worse."

## 5. The three load cases, and what each costs today

| Case | Path | Cost |
|---|---|---|
| Restart, same file | `cur_file_id == art_file_id` (`fw/player.c:8580`) -- `has_art = art_have` | `ld_art ~= 0` |
| Track change, same picture (common: most tracks of an album) | signature match (`art_have && sig == art_sig`, `:8609`) | `ld_art` = one signature compute, no decode |
| Track change, new picture | `art_decode()` runs | `ld_art` up to 2,801 ms measured |

Only the third case is the one this whole investigation is about; the first two are already
effectively free and must stay that way through any redesign (this is exactly what
`tools/check_art_load_order.py`, built in Phase 1 below, locks in).

## 6. Risk section, direct answer

**Is there a real correctness reason art decodes before audio?** Yes, confirmed two ways: (a) the
file's own header comment states it outright, and (b) the FIFO-depth-vs-decode-time math
(43 ms vs up to 2.8 s) independently proves that decoding after playback starts, *without* chunking,
would starve the FIFO far worse than a pre-track pause. This is not a stale/unjustified constraint
that can be casually lifted -- it needs the chunking work in section 4 to lift safely.

**What does `ui_art_mount()` clear/prepare that later code depends on?** Only the off-screen stash
region (`ART_STASH_Y`); nothing about the *visible* on-screen panel or `art_ready`/`art_shown` state
depends on it running early -- confirmed by reading `ui_art_mount()` itself (`fw/player.c:2572-2575`,
a single `fb_rect()` into the stash) and by B-075's own fix, which is precisely "don't call it early."

## 7. Build order

- **Phase 0 (done, pre-existing, no change needed):** previous-cover-holds (B-075) and audio
  fade-in-on-flush (`fade_left`/`FADE_SAMPLES`) already satisfy two of the three asks; the load-time
  breakdown (`ld_head/ld_size/ld_art/ld_total`) is already permanently visible on the Info page.
- **Phase 1 (this task, built below):** lock the two already-correct behaviours in as an explicit,
  tested contract via a static source check, so a future refactor of `load_track()` cannot silently
  regress either one without a test failing. Zero risk: touches no runtime code, changes no
  behaviour.
- **Phase 2 (future, needs a JPEG test harness first):** build a dedicated host test that runs the
  real `art_decode()` against a real JPEG fixture (via the same rv32sim-style native-compile
  technique `sim/test_chladni_module.py`/`sim/test_blit_reference.py` already use for other hot C
  code) and checks pixel output. Without this, no refactor of `fw/art.inc`'s MCU loop should be
  trusted.
- **Phase 3 (future, needs Phase 2 + hardware verification, gated behind a default-off macro per
  this project's standing discipline -- see `BLIT_READY()`/`COLD_READY()`):** the actual behaviour
  change -- `art_decode_begin()`/`art_decode_step()`, `load_track()` split so playback genuinely
  starts before a new cover is decoded, the deferred decode budget-limited and gated on real FIFO
  headroom via `pcm_level()`. This is the part that delivers the real "audio starts before art"
  win; it needs Phase 2's harness to trust, and a real Pocket to confirm no audible artifact, before
  it could ever become the default.
- **Phase 4 (future, cosmetic only, small on top of Phase 3):** the actual fade-*swap* of the new
  cover once decoded (currently the swap is instant once `art_ready` flips) -- this worktree
  predates the pipelined alpha-blend firmware wiring another session's history describes
  (`BLEND_READY()`/`fb_blend_on`/`ui_bg_blend()` do not exist here yet; only the RTL macro
  `TAU_BLIT_BLEND` exists, unconditionally shelved on this worktree's own copy of the timing
  history). Until that lands and is proven, Phase 4 is a plain instant cut, same as today.

## 8. What was built this session (Phase 1)

`tools/check_art_load_order.py`, wired into `make test-host`. A static, source-text regression guard
(same style as `tools/meter_cost_estimate.py`/`tools/check_cold_calls.py`) that asserts, directly
against `fw/player.c`'s current source:

1. `load_track()` still runs `pcm_flush()` -> cover-art decode -> `prefill()` in that order (locks in
   section 3's constraint -- if this is ever reordered without also deferring the decode past the
   function's return, per Phase 3, the check fails loudly instead of silently reintroducing FIFO
   starvation).
2. `ui_loader_begin_ex()` never calls `ui_art_mount()` (locks in B-075's "previous cover holds"
   fix).
3. `pcm_flush()` still arms `fade_left = FADE_SAMPLES` (locks in the fade-in-on-load behaviour).

Comments are stripped before each check specifically because this file's own comments routinely
*mention* call names in prose (e.g. B-075's "NOT `ui_art_mount()` here") -- a naive text search would
otherwise false-positive on the very sentence documenting the invariant.

No `load_track()`/`art.inc` behaviour was changed. Phases 2-4 are designed above but explicitly not
attempted this session: Phase 2 needs a JPEG test harness that does not exist, Phase 3 needs that
harness plus real hardware to trust (this task forbids installing on a card), and touching this
specific correctness-critical, historically fragile code without either would not be prioritizing
correctness over completeness.
