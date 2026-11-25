# Alpha blend (B5, `TAU_BLIT_BLEND`): why timing never closes, and the options

Status: analysis, 2026-09-26 (B-326). Nothing built. Evidence is Quartus STA on three existing fit databases plus Intel documentation.

## 1. Result

Every fit with `TAU_BLIT_BLEND` fails setup on the Slow corners; every fit without it closes:

| Fit | Blend | Worst setup |
|---|---|---|
| B-107 blit-engine s2 | on | -2.534 ns |
| B-239/B-243 blend-test s2 | on | -2.949 ns |
| B-110 noblend s2 | off | +0.023 ns (different, unrelated path) |

All violating paths (200+) are one family: `glyphbuf` read-address register -> 128:1 asynchronous MLAB read mux (~3.8 ns, fanout 64) -> DSP multiply-add `Add32~8` (4.341 ns, **unregistered**) -> mode mux and write select (~3.7 ns) -> `glyphbuf` write. Total about 12.8 ns of data delay against a ~10 ns budget at 100 MHz. It is not seed noise and not congestion alone: the path is structurally too long.

The source is the read-modify-write in `mp3_fb.sv` A_COPYRD: `glyphbuf[i] <= blend_px(glyphbuf[i], p0_q, mode, alpha)`, one clock for read, multiply, select and write.

## 2. Options (ranked)

1. **Pipeline the blend (recommended).** Three stages: (a) read `glyphbuf[i]` and latch dst and src plus alpha into registers, (b) DSP multiply-add with input and output registers (Intel: best Fmax, latency 3), (c) mode select and write back. A row of 128 words takes about 3 extra cycles once, not per word, if the address pipeline is fed one word per cycle; a drain state closes the row. Same retiming pattern proven five times in this project (B-111, B-114, B-231). Cost: some registers, one state, the write for word i lands two clocks after its read, so the key/blend pre-read and the row-end write-out must respect the delay. Needs RTL, a sim update (`tb_mp3_fb.v`, reference renderer already covers blend) and a two-seed fit.
2. **Shift-add / quantised alpha only (drop the multiplier).** Removes the DSP delay but the MLAB read plus mode mux is still ~7.5 ns and the write mux is shared; margin is thin and only 4-5 alpha levels are available. Not enough alone.
3. **Logic-cell multiplier (`DSP_BLOCK_BALANCING = Logic Elements`).** Trades a DSP for a slower LE multiplier, usually slower, not faster, for an 8x9 product plus add. Useful only as a congestion experiment.
4. **Multicycle path constraint.** Unsafe here: the path is exercised every cycle during a burst, so an SDC multicycle would hide a real failure.
5. **Seed and fitter-effort tuning.** Cannot recover 2.5-3 ns from a 12.8 ns path.
6. **Scanout compositing** (blend at video read time, two planes): removes the read-modify-write entirely but is a much larger design (second read port, more SDRAM bandwidth, video-side timing). Only worth it if alpha becomes a headline feature.

## 3. Value check

Per-command uniform alpha covers fades and translucent panels. Figma-style per-pixel alpha is not available either way (RGB565). Today no shipping firmware uses blend; all UI gets by with opaque draws. So blend is a nice-to-have. Recommendation: keep it shelved unless the UI redesign names a concrete use (fading overlays, scope trails), then do option 1.

## 4. Sources

Intel Quartus II handbook timing-closure chapter (register pipelining, avoid unregistered DSP functions, multiply-adder pipeline registers), Cyclone V DSP block documentation, Intel `DSP_BLOCK_BALANCING` setting, project KB-065 (local), KB-045.
