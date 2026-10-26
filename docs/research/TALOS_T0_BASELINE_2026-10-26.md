# Talos T0: resource baseline from the shipped fit `stats-b821` (2026-10-26)

Source: the Quartus reports of the fit `stats-b821-s1` on the VM (`ap_core.fit.summary`, `ap_core.fit.rpt` "Fitter Resource Utilization by Entity"), read 2026-10-26. **One seed only** (the project rule is two seeds for a timing claim); this is the bitstream `0a4b7a58...` that DEV 185 and the v0.7 rehearsal use. It replaces the stale numbers in the Talos 2 plan section 3 (17,285 ALMs, 240 M10K).

## Device totals (5CEBA4F23C8)
| Resource | Used | Available | Free |
|---|---|---|---|
| ALMs needed | 13,784 (75%) | 18,480 | 4,696 nominal |
| LABs (partly or fully used) | 1,724 (93%) | 1,848 | 124 LABs, about 1,240 ALMs before the fitter must pack denser |
| M10K blocks | 272 (88%) | 308 | **36** |
| Block memory bits | 2,080,768 (66%) | 3,153,920 | |
| MLAB memory bits | 15,776 | | |
| DSP blocks | 23 (35%) | 66 | 43 |
| Registers | 16,258 | | |
Timing (one seed): worst setup Slow 85C +1.440 ns, Slow 0C +1.190; hold +0.261..+0.280; Fast corners +5.2 / +5.5 setup, +0.11..+0.13 hold. All positive.

## The graphics path today
| Entity | ALMs needed | M10K | DSP | Notes |
|---|---|---|---|---|
| `mp3_fb:u_fb` (scanout + Talos engine + command FIFO + font RAM + CLUT) | **1,532** (1,418 own + the two row-buffer copies about 60 + 53) | 38 | 6 | 238,080 block bits; the row buffer `glyphbuf` is two MLAB copies (altdpram, 40 memory ALMs each); `clut`, `cmd_mem`, `linebuf`, `font_ram` are altsyncram (M10K) |
| `sdram_fb:u_sdram` (the SDRAM controller) | 334 | 0 | 0 | |
| `mp3_soc:u_soc` (CPU, bus, audio blocks, the rest) | 9,895 | | | includes `tau_audio_stats` 431 |
| `core_bridge_cmd` | 303 | 2 | | APF side |

## What it means for Talos 2
* **The existing engine is already at the plan's area goal.** The plan's goal 1 is "at or under 1,800 ALMs with blend"; `mp3_fb` is 1,532 with blend. **A rewrite cannot be justified by area.** Any new engine must be at most about the same size, and every new feature (column list, gradient, glyph source) has to be paid for inside roughly 1,240 ALMs of LAB headroom, shared with every other v0.7 RTL item.
* **M10K is not as scarce as the plan feared:** 36 free (the plan assumed 4). The column-list RAM (about 4 kbit) and a PSRAM glyph cache fit; Extended-A at about 16 M10K (FONT_SPEC) would take nearly half of what is free.
* **RAM inference is healthy:** `glyphbuf_a/b` appear as `altdpram` with 40 memory ALMs each, `clut`, `cmd_mem`, `linebuf`, `font_ram` as `altsyncram` (the grep gate of the plan: no per-bit register names for these arrays: satisfied today, and this is the pattern a new build must reproduce).
* **The fitter already optimises `mp3_fb`:** the report lists many `mp3_fb` adders as modified by Physical Synthesis timing optimisation; a clone-first module will not automatically land at the same timing.
* **LAB pressure (93%) is the binding resource, not ALM count.** A change that adds registers without logic can still fail to pack.
