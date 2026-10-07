# Performance: measured, not assumed

What the hardware drawing engine, the PSRAM cold code, the hardware MP3 window unit and the fast cover reader changed about how Tau uses its
resources, in plain terms with the real numbers behind each claim. Every figure here is a hardware measurement recorded in [AUDIT_TRAIL.md](AUDIT_TRAIL.md)
(entry ids in the Source columns) unless it is marked as an estimate. **TBD** means the measurement is planned and the tools exist, but no real number has
come back from a Pocket yet; this page is updated as they arrive rather than left to go stale.

Contents: [In plain terms](#in-plain-terms) · [The numbers](#the-numbers) · [Audio safety](#audio-safety) · [Tradeoffs and honest limits](#tradeoffs-and-honest-limits) ·
[How we measure](#how-we-measure)

## In plain terms

| What we checked | In plain terms | The numbers | Roughly speaking |
|---|---|---|---|
| MP3 decoding cost of the filterbank | How much of decoding time is spent in the stage that moved into hardware | 22% of decode at 1.0x, was 55-59% (B-087, B-309) | **about 2.5x smaller share** |
| Speed headroom | How fast MP3 playback can run before audio breaks up | Clean at 1.75x (1.25x used to stutter, owner-reported, B-309); clean through 2.0x in owner tests of the Diagnostic Build (B-328) | **much more headroom** |
| Album cover appearance | Time from loading a track to seeing its cover | About 90 ms from a pre-scaled `.timg` file, vs 2.6-15.8 s decoding the embedded JPEG (CHANGELOG v0.5.0; hardware-confirmed for MP3 and FLAC albums, B-330) | **roughly 30x to 175x faster** |
| Room left for new features | Free heap on the chip's own memory in the release build | 15,808 bytes in v0.6.0-alpha.1 (192 KB RAM layout, adopted this release); was 49,712 bytes in v0.5.0 on the older 256 KB layout | **smaller heap margin, in exchange for 64 KB of on-chip block RAM freed on the chip itself for hardware features** |
| FLAC decoding cost | How much a real-hardware worst-case LPC reconstruction call costs | Software 11 ms; hardware unit 5-6 ms (B-386) | **roughly half the worst-case spike** |
| Drawing the classic bar meter | Drawing steps the chip does per frame | 36, was 72 (B-198) | **50% less work every frame** |
| Music stability under the heaviest load | Whether audio ever stutters with the busiest visuals and stress traffic running | 0 late underruns in every run so far, including a 30 s blit storm plus audio (B-146) and ENDURANCE runs (B-213) | **no glitches found** |
| Cost of running cold code from PSRAM | Extra time for code kept off-chip | Worst case about 28,800 cycles per meter draw call, about 1.7% of one audio frame (B-202, B-213) | **a small, deliberate trade for the memory gained** |
| Screen drawing tearing | Whether meters draw across the display beam | Meter drawing waits for the beam (Helios, B-267); the Info page's BEAM row shows how often it had to wait. Frame rate seen by firmware: about 60/S (B-266) | **tear-free meter drawing** (a numeric wait share: **TBD**) |
| Equalizer cost | CPU cost of the EQ | None: it is hardware; 116 of the 1,250 clocks between output samples (under 10% of one multiplier's time) | **free to the CPU** |
| New Winamp meters vs the classic bars | Draw cost, measured | Static estimate 16-32 commands/frame vs 36 for classic bars (B-215); **measured with Meter Sweep: TBD** | **TBD** |
| Opening the live meter-tuning screen while music plays | Whether tweaking a meter can be heard as a hiccup | **TBD** | **TBD** |
| Battery life with the new visuals | Whether the fancier meters drain the battery faster | **TBD** (plan: [BATTERY_AND_POWER_PLAN.md](BATTERY_AND_POWER_PLAN.md)) | **TBD** |
| Menus and library feeling snappier | Whether moving code to PSRAM changed how quickly they respond | **TBD** | **TBD** |

## The numbers

Phase F added the blit engine (rect/copy, a meter-column primitive, scaled, palette and rounded-rectangle blits); Phase G moved menus, the library and the meter draw path from
on-chip RAM into PSRAM; v0.5.0 added the hardware MP3 window unit and the fast cover reader.

| Metric | Before | After | Source |
|---|---|---|---|
| Draw commands per frame, bar meter (36 columns) | 72 (`fb_rect` pair per column) | 36 (one `OP_BAR` per column) | B-198 |
| Winamp Bars / Scope meter | did not exist | 16-32 commands/frame (estimate; Meter Sweep measurement TBD) | B-215 |
| MP3 filterbank share of decode | 55-59% (Subband dominated; Huffman 3-5%) | 22% at 1.0x (39% at 1.75x) | B-087, B-309 |
| MP3 window unit self-check | n/a | `HW 404712 SLOTS 0 BAD 0 TMO`; later `909864 SLOTS 0 BAD 0 TMO` | B-309, B-328 |
| Speed with clean playback | 1.20x (1.25x micro-stuttered) | 1.75x (owner-reported); 2.0x clean in later owner tests | B-135, B-309, B-328 |
| Cover appearance | 2.6 s (small cover), 5.3 s (455 px), 15.8 s (1400 px) via JPEG | about 90 ms via `.timg` | A-120, B-027, CHANGELOG v0.5.0 |
| Free RAM (heap gap), release build | 30,528 B (82.6% used) | 61,808 B (65.3% used) at B-214; 49,712 B in v0.5.0 after Winamp, Chladni, themes and the MP3 window firmware were added; **15,808 B in v0.6.0-alpha.1** on the adopted 192 KB RAM layout | B-199..B-203, B-213/B-214, B-331, B-333/B-458 |
| FLAC LPC reconstruction, worst-case call | 11 ms (software) | 5-6 ms (hardware unit) | B-386 |
| On-chip block RAM (M10K) used, shipped bitstream | 304 of 308 (98.7%) in v0.5.0 | 240 of 308 (78%) after the RAM shrink + `glyphbuf` single-write-port fix | B-316, B-398/B-437 |
| Free RAM after the media library shipped (v0.4.0) | on-chip only | menus, library, settings code in PSRAM, "roughly quadruples the player's free memory" | CHANGELOG v0.4.0 |
| Sustained blit load during real playback | not measurable (no counter existed) | 15.8% of SDRAM cycles busy, 0 late underruns over a 30 s blit storm plus audio | B-146 |
| Cost of running the meter draw path from PSRAM | n/a | 27,308-28,847 CPU cycles worst case per call (about 1.7% of the 26.3 ms audio-frame budget) | B-202, B-213 |
| SDRAM window access (Diagnostic Build Tests) | n/a | read about 48 / 57 / 335 cycles, write about 31 / 38 / 350 (fastest / average / slowest); worst single access about 373-380 | A-126, B-054, B-062 |
| Worst measured stack use | n/a | 1,672 B of a 16 KB stack (heavy R3 stress, MP3 + FLAC + covers) | B-230 |
| Hardware spectrum and level blocks | software cascade on the CPU | hardware; the software spectrum cascade was removed | B-263, B-283 |
| Memory soak evidence | n/a | PSRAM window: 1,000 passes / 1.05 billion checks, 0 failures; 30-minute soak on real playback 29.8 M operations, 0 failures, 0 late underruns | B-022, B-054 |
| FLAC decode load | n/a | a 24-bit 44.1 kHz track uses about 80% of the CPU time available | [FLAC.md](FLAC.md) |

## Audio safety

The invariant the whole design serves: the audio FIFO must never run dry while the CPU is busy. The tests separate two counters. **Late underruns** (the FIFO ran dry mid-track)
must stay 0 and have stayed 0 in every stress, soak and ENDURANCE run to date, including under the added blit and cold-code load. **Early underruns** follow track changes and
screenshots and are not a fault. The Info page's **METER YIELD** row shows how long the meters were held off to protect audio, and its worst-since-boot value is the thing to watch.

## Tradeoffs and honest limits

Not everything paid off, and not everything is finished; recorded here instead of left implicit.

- **A font-ROM repack, expected to free block RAM, measured a net +0 blocks.** Synthesis-stage reports cannot see physical packing; only a real Fitter run could, and it showed no improvement:
  a negative result kept on record (B-101, B-102). The `glyphbuf` single-write-port fix (T2-00, B-398) is what actually recovered block RAM, not the font repack.
- **Hardware rounded rectangles (`OP_RRECT`) had a timing failure, now fixed.** A -2.37 ns setup violation in the corner sequencer (B-211) was retimed away (B-231), and the combined fit closed on both seeds (B-235).
- **Alpha blending is built, closes timing, and ships in v0.6.0-alpha.1.** The single-cycle version never closed (worst setup about -2.5 to -2.9 ns); the three-stage pipelined version closed on 2026-09-27
  (setup min +0.755 ns on seed 1; B-326, B-327) and is now in the shipped bitstream, used for the Settings menu cross-fade (B-405).
- **The on-chip RAM shrink (256 to 192 KB) is adopted in v0.6.0-alpha.1.** The RTL closed timing and frees 64 blocks (B-235); the firmware now links at 192 KB with 15,808 B of free heap
  ([RAM_SHRINK_192K_PLAN.md](RAM_SHRINK_192K_PLAN.md)) -- smaller than v0.5.0's 49,712 B (256 KB layout), by design, in exchange for the freed on-chip block RAM.
- **Theme and Dark/Light mode now persist across a restart; the Winamp meters' Configure settings still do not.** `interact.json` (the file APF uses to persist settings) has a hard 16-entry display
  cap (B-456); theme and mode fit inside it, the three meter-preset indices were dropped to stay under it. The Diagnostic Build can export a Configure setup as a QR code as a stopgap.
- **The hardware scope path is compiled out.** Drawing a 256-column scope cost about 21x a normal meter and caused audio jitter, so the software scope runs (B-302); batched drawing is on the roadmap.
- **`CPU LOAD` on the Info page reads 100% in every state** and cannot show headroom. Use the per-stage decode percentages and the speed at which audio breaks up.
- **No unit-off baseline for the MP3 window unit on the same bitstream, and no HarpMudd comparison yet**; the 22% versus 55-59% comparison is across builds (B-309).
- **`Track changes` fails in the Diagnostic Build's Check** (0 of 10 done), a pre-existing test problem that is not yet explained; playback itself is unaffected.
- **Speeds above 1.20x are Diagnostic Build only** for now.
- **The numbers marked TBD above** (new-meter cost vs classic, Configure page audio impact, battery life, menu and library responsiveness) have measurement tools ready (Meter Sweep, Check, Info rows) but no result yet.

## How we measure

| Tool | What it measures | Where |
|---|---|---|
| **Check** (Diagnostic Build) | Memory-window tests and speed, cold code, library, playback counters, blit storm with the SDRAM busy share, stress levels and soak; a PASS/FAIL report as text and QR | [guide/DIAGNOSTICS.md](guide/DIAGNOSTICS.md), [TEST_SUITE_SPEC.md](TEST_SUITE_SPEC.md) |
| **Meter Sweep** | Draw cost of every meter (about 10 s each): draw stall, SDRAM busy share, underruns | [guide/DIAGNOSTICS.md](guide/DIAGNOSTICS.md), [METER_MODULE_SPEC.md](METER_MODULE_SPEC.md) |
| **Decode Sweep / profile build** | Per-stage decode percentages (Huffman, IMDCT, filterbank) over a library queue | AUDIT_TRAIL B-086..B-092 |
| **Info page rows** | UNDERRUNS, DRAW STALL, LOAD MS, WINDOW READ, MP3 WINDOW, TIM1 COVER, VBLANK, BEAM, METER YIELD, FREE RAM | [guide/DIAGNOSTICS.md](guide/DIAGNOSTICS.md) |
| **Stress and soak** | Extra memory traffic beside playback for 5-60 minutes; late underruns must stay 0 | Menu > Diagnostics > Stress |
| **Fits and host tests** | Timing slack per corner on two seeds; simulation and host tests for correctness | [DEVELOPERS.md](DEVELOPERS.md) |
| **QR decode on a computer** | `tools/decode_tau_suite.py --qr screenshot.png` turns a screenshot into the exact report | [guide/MEDIA_AND_TOOLS.md](guide/MEDIA_AND_TOOLS.md) |

Rules we hold ourselves to: a claim names its evidence entry; an estimate is labelled as one; a number is not stated if it was not measured; and a comparison across builds is called out as such.
