# Audio signals for meters: what exists, where it runs, what it costs (2026-10-03)

Signals the core offers every meter (read from `mtr_in_t`, or called from `fw/meter_core.h`). **Every cost number below is an estimate unless it says
"measured"**: nothing here has run on a Pocket, and the new RTL has not been fitted. Clock 66.667 MHz, display updates about 38 per second, audio
44.1 kHz. CPU cost is cycles per display frame, then percent of the whole CPU.

| Signal | Computed where | Hardware cost (estimate, needs a fit) | CPU per frame (estimate) | Share of CPU | Memory | Status |
|---|---|---|---|---|---|---|
| Peak L/R, spectrum (16 bands), waveform | Hardware blocks (`tau_wave_meter`, `tau_spec_bank`) | already built | 0 beyond the reads | ~0 | already counted | shipped, hardware-proven |
| Envelope, paused flag | Host loop | none | ~100 | <0.01% | 72 B | shipped (step 1) |
| Energy (mean level), silent | Host, once per frame | none | ~60 | <0.01% | 2 B | built, host-tested (not yet read by a meter) |
| Slow band weights, slow band power, onset | Library, per meter, ~26 ms | none | ~150 to 400 each | <0.02% each | state owned by the meter | built; used by Chladni and Layered Wave |
| **RMS (L and R)** | **Hardware sums** + software square root | 1 DSP block; ~450 to 550 ALMs for the whole stats block (accumulators, latches, read mux); 0 M10K | 2 square roots ~300 | ~0.02% | 4 B | RTL built and simulated (5 faults caught), **not fitted** |
| **Stereo correlation** | **Hardware cross sum** + software 64-bit root and divide | shares the stats block above (no extra hardware) | ~1,500 (the 64-bit divide dominates) | ~0.09% | 2 B | as above |
| **Crest factor** | Software: peak / RMS | none | ~40 | <0.01% | 2 B | built, host-tested |
| **Spectral centroid** | Software, from the 16 bands | none | ~150 | <0.01% | 2 B | built, host-tested (always valid, no hardware needed) |
| **Clip count** | **Hardware**: two comparators and two 16-bit counters in the stats block | included above | ~20 (one read) | ~0 | 4 B | RTL built and simulated, **not fitted** |
| Hardware statistics read-out as a whole | 7 index writes and reads plus window-counter checks | | ~2,200 to 2,500 total | **~0.14%** | cold code 796 B; RAM 17 B | in the Diagnostic and release builds, inert on any bitstream without `TAU_STATS` |

What the same signals would cost **without** the hardware block (estimate): RMS and correlation need three 16x16 multiplies per sample pair, accumulated
into 64-bit sums on a 32-bit CPU, about 25 to 30 cycles per sample, so roughly 1.2 to 1.3 million cycles a second (**about 1.9% of the CPU**) inside
the decode loop, which is the audio-critical path, plus about 0.4% more for clip counting. The hardware block removes all of that from the CPU for the
price of 1 DSP and a few hundred ALMs, which is why RMS, correlation and clipping went to hardware and the rest stayed in software.

**Measured on the host (not hardware):** every function agrees with an exact integer reference over thousands of random cases
(`sim/test_meter_core.py`); the RTL agrees with a 64-bit model across 8 signal types and 5 injected faults are all caught
(`make test-rtl-audio-stats`, `make test-rtl-audio-stats-mutation`); heap gap on the 192 KB release 14,464 B (was 14,560 B before these signals,
so the hot cost is 96 B), diagnostic profile 5,680 B (was 5,776 B), still above the 6,144 B and 4,096 B floors.

**Resource headroom to check at the fit:** DSP was 20/66 and M10K about 256/308 in the last closed fit; this block adds 1 DSP and no M10K. ALM headroom for
the current bundle is not quoted in the audit trail, so read it from the fit report before launching. Fit bundle:
`tools/blit_g3_poly_blend_ram192_clk66_dbuf_lpc_cymo_stats_qsf_append.txt`.

**How to read it on a Pocket once fitted:** Settings, Diagnostics, Info, the last row, AUDIO STATS: `L<rms> R<rms> C<+/-percent> K<crest> X<band> CL<l>/<r>`.
Expected: a 0 dBFS sine reads K1.4 (peak / rms = 1.414); mono material reads C+100; a phase-inverted channel reads C-100; silence reads L0 R0 C+0;
clip counters stay 0 unless the track is mastered to the rails.

Registers: `docs/MMIO_ALLOCATION.md` 0x180 to 0x18C.
