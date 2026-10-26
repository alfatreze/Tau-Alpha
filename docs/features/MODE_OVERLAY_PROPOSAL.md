# Mode-overlay region: proposal and test plan (NOT built)

Status 2026-10-07, branch `ram-diet`. Nothing in `fw/` uses this. The prototype under `sim/overlay_proto/` and its test (`sim/test_overlay_proto.py`, red then green) only exercise the safety mechanism on the host.

## What it would be
One linker-owned region in hot RAM that several features share because they are never live at the same time. An owner claims it when its mode starts (the region is poisoned, then the owner initialises its state) and nothing else may touch it until another owner claims it.

## What could honestly share it (from the symbols, 192 KB link)
| Set | Members | Why they are exclusive | Today | With overlay | Saves |
|---|---|---|---|---|---|
| A: decoder format | `tempo_st` (6.2 KB; 4.4 KB with TEMPO_SLICE and ring 512), `fl` (800 B) | tempo is MP3 only, `fl` exists only for FLAC | 5.2-7.0 KB | 4.4-6.2 KB | **0.8 KB** |
| B: the meter being drawn | Chladni (`chl_half` 1,600 + `chl_cxm`/`chl_cxn` 640 + `chl_ring` 240 + `chl_st` 164), scope arrays (`scope_x/y` 384, `wviz_scope_y` 512), Layered Wave (`lw_row` 404, `lw_band` 192) | one meter is drawn at a time (but see hazards 2 and 3) | about 4.1 KB | about 2.8 KB | **about 1.3-2 KB** |
Not candidates: `pcm` (the MP3 decode output and the tempo input and the FLAC meter staging, live all through playback), the ring and tag buffer (DMA), the art working set (already PSRAM).

**Total: about 2-3 KB.** That is what the risk below buys.

## Hazards this project has already met (why it is not built)
1. **Sharing across calls.** FLAC's meter staging keeps data in `pcm` between calls (`fl_meter_n`); an overlay owner that assumes "I start clean" corrupts it. (RAM_BSS_AUDIT F3.)
2. **State that must survive a switch.** Chladni's ring, the scope trail and Layered Wave's history are meant to persist across redraws; Configure preview and fullscreen switch meters while the player screen's meter state is expected to still be there (B-415, B-452, B-453). An overlay drops it, so every meter needs an init that rebuilds it and a visual check that the rebuild is not visible.
3. **Two meters at once.** The Configure page previews meter X while Settings is open; fullscreen draws meter Y; the player screen's meter is paused, not gone. Any path that draws two at once (the Helios present/compose step, `helios_meter()` composing in the back buffer while the front still shows the old one) breaks exclusivity. This is the same class as the double-buffer base bugs (B-399, B-402, B-414, B-450).
4. **In-flight hardware.** A blit or the hardware wave/scope block reading scratch while the owner changes (the draw engine is asynchronous; `fb_fence()` is the only barrier).
5. **Aliasing by name.** The audit found 490 small symbols; one forgotten reference to an overlaid array compiles and runs.

## Mechanism the prototype models (`sim/overlay_proto/overlay.h`)
- R1 only the current owner may take the pointer (wrong owner: violation counted, null returned).
- R2 a switch poisons the region (0xA5) before the new owner's init runs, so a missing init reads poison, not plausible old data.
- R3 a switch is refused while the old owner is busy.
- R4 owner sizes are checked at compile time against the region (`_Static_assert`).
In firmware these would be asserts in Diagnostic builds only (zero cost in release); release builds keep R4 and the layout.

## Test plan before any firmware change
1. **Differential fuzz (done for the mechanism):** random sequences of mode switches and ticks; overlay result must equal separate-buffer result. The prototype does this for toy owners and proves a skipped init is caught (40 of 40 sequences). The real version runs the REAL meters through the golden-frame harness (`sim/test_meter_golden.py` already renders every meter to compare C and JS): render N frames of meter A, switch to B for M frames, back to A, with the overlay on and off; every frame must be identical.
2. **Switch while busy:** drive `fb_fence()` boundaries in the host model of the draw engine and assert R3 never fires in the shipped flows (Configure open/close, fullscreen enter/exit, Settings crossfade).
3. **Static check:** a script that lists every reference to an overlaid symbol and fails unless it is inside an owner's own file (catches hazard 5).
4. **Pocket:** meter switching in Configure and fullscreen on all meters with audio running (UNDERRUNS and visual leftovers), long soak; compare against the same build with the overlay off.

## Recommendation
Do not build it for 2-3 KB. The tempo slice and ring change already save 2.8 KB at zero behavioural risk, the diagnostics-on-demand system recovers more for the Diagnostic Build, and the QR removal needed no sharing at all. Revisit the overlay only if a feature still does not fit after those, and then only set A first (smaller, formats are cleanly exclusive).
