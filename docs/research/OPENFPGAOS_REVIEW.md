# openfpgaOS review and CPU clock options (2026-09-26)

Research only. Sources: openfpgaOS/openfpgaSDK and thinkelastic/PocketDoom READMEs (read once, not verified on hardware or RTL). Evidence: [DOC] = read from a README, [EST] = my inference.

## 1. openfpgaOS spec (Pocket)
- CPU VexiiRiscv rv32imafc (FPU, atomics, compressed) at 100 MHz [DOC]. Tau: VexRiscv rv32im, 60 MHz, no FPU.
- Caches: SDK page says 8 KB I + 32 KB D; PocketDoom says 64 KB I + 64 KB D, 2-way write-back D [DOC, the two disagree; different variants or versions]. Tau: 4 KiB I + 4 KiB D, one way, 32 B lines.
- Three AXI4 buses: fetch, data cache, uncached MMIO. Code runs execute-in-place from 64 MB SDRAM; 8-32 KB BRAM holds the kernel and hot functions (`OF_FASTTEXT`) [DOC].
- Video 320x240, 8-bit indexed with hardware palette (also 4/2-bit, RGB565/555/5551), three SDRAM buffers, vsync-locked flip as a GPU command [DOC]. Tau: RGB565 400x360, single buffer.
- GPU: DMA-fed command ring (16 KB), spans, colormap lookup, masked pixels, translucent spans through a 32 KB lookup table, clears, flips; the os30 variant adds textured triangles and a Z-buffer [DOC].
- Audio: 32-voice hardware PCM mixer, 48 kHz, 8 MB sample pool in SDRAM, hardware OPL3 (18 voices) mixed with saturating add before I2S [DOC].
- 100 MHz timer, periodic interrupt callbacks, link-cable FIFOs, 10 save slots in CRAM0 [DOC].
- The 100 MHz CPU clock is derived from a 12.288 MHz pixel clock (= 256 x 48 kHz), so CPU, video and audio share one PLL plan [DOC]. Tau pins a 12 MHz pixel clock for an exact 60.000 Hz frame, which fixes the VCO at 600 MHz.

## 2. Clock options for Tau
| Option | Verdict |
|---|---|
| 66.667 MHz | Upstream HarpMudd measured it with positive slack, +11% CPU. Same PLL constraint as Tau. Worth one fit experiment (already recommended in `HARPMUDD_UPSTREAM_1.5_REVIEW.md`). Needs the three hardcodes fixed: `eq_biquad CLK_HZ`, `pcm_rate` reset default, cycle-counter wrap (71.6 s becomes 64.4 s). |
| 75 MHz | Upstream closed with 69 ps only. No. |
| 100 MHz on this CPU | Not realistic [EST]: Tau's own paths already sit at tenths of a ns at 60/100 MHz, and the generated VexRiscv is not built for it. openfpgaOS reaches 100 MHz with a different CPU. Only worth it as part of a CPU swap, a large project. |
| Off-grid (e.g. 80 MHz) | Needs a different pixel clock plan (openfpgaOS style). Unknown whether the Pocket scaler tolerates it for Tau's 400x360 mode. Open question, not recommended. |

Pros of higher clock: more decode headroom (+11%), more audio-safe speed range. At 100 MHz `clk_sys` would equal `clk_sdram` and the CDC in the SDRAM bridge could go [EST].
Cons: timing risk on marginal paths (four found so far), audit of clock-dependent constants, slightly more power. Gain is smaller now that the MP3 window unit cut filterbank cost from ~55% to ~22% of decode; measure remaining CPU need before spending a fit.

## 3. Ideas from openfpgaOS worth keeping
1. Bigger cached window for SDRAM/PSRAM: Tau's uncached window costs ~48 cycles per access, cold code runs at ~31 cycles/word. openfpgaOS runs whole applications from cached SDRAM. Tau's "cached window" gate is the same idea; the RAM shrink (235/308 M10K) frees blocks for 8-16 KB caches [EST].
2. Table-lookup blend: translucency through a lookup table avoids the multiplier path that failed timing for B5. Only fits palette-indexed sources (CLUT blit), not RGB565.
3. 8-bit indexed framebuffer with scanout palette: half the bandwidth and free theme recolouring (Light/Dark and accent = palette swap). Conflicts with anti-aliased text; a redesign, park for the UI rewrite.
4. Hardware double buffering with a flip command: matches Helios H2.
5. Hardware mixer with saturating add: candidate for the MOD/tracker spec (`MOD_TRACKER_SUPPORT_SPEC.md`) instead of a software mixer.
6. Proper interrupts (timer callback): Tau has none by design; a VexiiRiscv swap would bring them.
7. Idle hook during DMA waits (pump audio while a file read blocks): Tau does the equivalent.

Recommendation: keep the 66.667 MHz experiment queued after the current blend fit; do not chase 100 MHz.

## 4. Feasibility and priority (added same day)
Facts used: the draw engine runs in `clk_sdram` (100 MHz), the CPU and the newer units (poly, wave, spectrum bank, CDCs) run in `clk_sys`; M10K is 304/308 on alpha.33; the 192 KB RAM shrink frees 63 blocks but the firmware is ~8.5 KB short of fitting; `eq_biquad` DIV is not exact at 66.667 MHz (1388.9, 0.008% off, harmless).

| Pri | Item | Feasibility | Specific gain | Alignment | Verdict |
|---|---|---|---|---|---|
| P1 | RAM shrink decision (enabler) | RTL done, timing clean (B-235); firmware short 8.5 KB | frees ~63 M10K | prerequisite for the cache | decide first |
| P2 | 66.667 MHz | High. Free pre-check: read worst `clk_sys` slack in the existing fit; period drops 1.67 ns, so need about +1.7 ns at 60 MHz. Then 2-seed fit. Review SDC for 60 MHz assumptions | +11% CPU: FLAC/JPEG decode, Chladni and Configure CPU load, 1.75x speed margin | none blocks on it; independent | do after the blend fit |
| P2 | Cached SDRAM/PSRAM window | Medium-hard: shared CPU bus RTL; needs ~7-15 M10K for 8-16 KB; draw engine and mailbox writes make CPU lines stale (needs flush/invalidate rules) | cold code ~31 to a few cycles/word on hits; art decode (PSRAM 4x slower than BRAM); lets the 192 KB firmware fit without CPU cost | RAM shrink, Phase G, library thumbnails | after shrink; own build, own soak |
| P3 | Hardware double buffer + flip | Medium: base-pointer latch at vsync is small RTL, but partial-redraw UI needs a 288 KB copy per flip | structural tear-free frames | Helios H2, fullscreen Chladni, waterfall | only for full-frame animated modes; beam-aware drawing (B-267) already covers the UI |
| P4 | Table-lookup blend | Low value now: works on palette-indexed sources only, UI is RGB565 | none over B5 if the pipelined fit (B-327) closes | B5 fallback | keep as fallback only |
| P5 | Indexed framebuffer + palette | Rejected: AA text and gradients need true colour | half scanout bandwidth, free recolour | theme system | park in DECISIONS |
| P5 | Hardware mixer | Not needed: 4-channel MOD is ~0.2 M mixes/s, trivial for the CPU | none | MOD spec | keep software mixer |
