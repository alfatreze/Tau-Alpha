# Phase F spec — blit engine, M10K release, spectrum, and the kernel/3D decisions

Written 2026-09-22, before any code. Covers the next RTL build and everything competing for the block RAM it
needs. Companion to `docs/ARCHITECTURE_ROADMAP.md` (which carries the phase ordering) and
`docs/PHASE_G_SPEC.md` (cold code, which this unblocks and is unblocked by — see section 4).

Evidence labels follow the project convention: **[HW]** measured on a Pocket, **[FIT]** from a real Quartus
fit report, **[SRC]** read out of this repo's source, **[EST]** estimate, **[EXT]** external source.

## 0. The short version

Five decisions settle the scope. Each has its reasoning recorded because reversing one later without the
reasoning is how this list gets re-derived from scratch.

| # | Decision | Why |
|---|---|---|
| **D1** | **No FFT. A hardware octave filter bank instead.** | The software octave cascade already ships and looks right at **1.5% CPU**; the same firmware comment records why FFT lost: **1024-point FFT 13.1% CPU, 12 parallel biquads 31.9%** [SRC]. In hardware the cost inverts differently than expected: a Goertzel/one-pole bank needs **~0 M10K** (two state registers per band, one time-shared DSP MAC) while even a lean FFT needs real buffers [EXT]. Intel's FFT IP for Cyclone V is **not license-free** for distributed bitstreams [EXT], so it would have to be hand-built anyway. The upstream roadmap reached the same conclusion independently: "the filter bank is affordable... but lands in the budget that keeps the decoder fed" [SRC]. |
| **D2** | **The 3D GPU is dropped as a goal.** | A full-screen 16-bit Z-buffer at 400x360 is **2,304,000 bits = ~281 M10K** [EST] on a device with 308 total. 8-bit Z is still ~141. Only tile-based rendering is feasible (~20-25 blocks plus heavy ALM plus a real SDRAM-bandwidth risk), and the actual want — visualiser eye-candy — is reachable from 2.5D primitives on the 2D engine for ~2-4 blocks. See section 8. |
| **D3** | **Audio kernels stay gated on a profile that has never been run.** | Phase D step 1 says "profile first, and if it's fast enough, stop here" — it has now run (B-087). **Correction, same table:** this row originally extrapolated FLAC's bit-reader dominance (`FLAC.md`, R 64-76%) to MP3 as well. The MP3 measurement (B-087, `docs/ARCHITECTURE_ROADMAP.md` section 2) shows the opposite: Huffman/bit-reading is MP3's *smallest* stage (3-5%), Subband (the synthesis filterbank) is its largest (55-59%) — so for MP3 the pre-D3 roadmap order (filterbank first) was right, and this row's inference did not hold outside FLAC. The two codecs need separate kernel orderings, not one shared conclusion. FLAC's own R could not be refreshed (the available test vectors are VERBATIM-only, see B-086/B-087) so its bit-reader-first case stands on the original measurement, unrefreshed, not this one. |
| **D4** | **The EQ is shipped, not planned.** | `eq_biquad` is instantiated in `mp3_soc.v`, listed in `ap_core.qsf`, wired to `R_EQ` (0x68), confirmed on hardware [SRC]. `EQ_DESIGN.md` still says "Nothing here is built" — corrected. It also set the precedent this whole spec leans on: *"the EQ must not use M10K"*, solved with `ramstyle = "MLAB, no_rw_check"`. |
| **D5** | **M10K is released cheaply first; the main-RAM shrink comes last.** | MLAB migration and a font-ROM repack are near-zero-risk and fund the blit engine on their own. The 64-block main-RAM shrink depends on the meters going cold, which depends on the blit engine — so it cannot come first. Section 4. |

## 1. The M10K map (roadmap Phase B item 1 — now done)

Derived from a real fit report, `work/diagnostics/psram-diag/reports-b018-prod/ap_core.fit.rpt` (the B-018
product-configuration build), whose per-instance Block Memory table sums to exactly the 300 in
`ap_core.fit.summary`. Cross-checked against seven other saved fit reports — all show 300/308, so this is
stable, not a one-off. [FIT]

| Consumer | Declared | M10K |
|---|---|---|
| `mp3_soc` `ram0..ram3` — main system RAM, 256 KB as four byte-lane arrays | 65536x8 each | **256** |
| `mp3_fb` `font_rom` — 95 glyphs, 16x16, 4bpp | 3040x32 | **16** |
| `mp3_soc` `pcm_fifo` — "2048 entries = ~43 ms at 48 kHz" | 2048x32 | 7 |
| VexRiscv I-cache (banks + tags) — 4 KiB, 128 lines, direct-mapped | 1024x32 + 128x22 | 5 |
| VexRiscv D-cache (4 byte lanes + tags) — 4 KiB, same geometry | 1024x8 x4 + 128x22 | 5 |
| `mp3_fb` `cmd_mem` — draw command FIFO | 256x82 | 3 |
| `core_bridge_cmd` `mf_datatable` — APF boilerplate | 256x32 TDP | 2 |
| `mp3_fb` `linebuf` — scanout line buffer | 1024x16 | 2 |
| VexRiscv register file (2 banks) | 32x32 each | 2 |
| `mp3_fb` `glyphbuf` | 128x16 | 1 |
| `sound_i2s` `sync_fifo` (dcfifo minimum) | 4x32 | 1 |
| | | **300** |

Three things this settles:

- **85% of all block RAM is one thing** — the 256 KB main RAM.
- **Diagnostic and probe RTL costs nothing in the product.** SignalTap and `TAU_PHASE2_PROBE` are confirmed
  macro'd out of the checked-in `ap_core.qsf`; SignalTap's ~8 blocks of sample RAM only ever appear in a
  separately staged VM build [SRC][FIT].
- **The PSRAM instruction-fetch line-fill path costs zero blocks** — it reuses `tau_psram_bus` and ACKs each
  of the eight beats straight into the I-cache line; there is no buffer [SRC].

`tools/quartus_fit_summary.py` does not parse this table (it only reads the top-line total). Extending it to
emit the per-instance breakdown is a small, worthwhile follow-up so this map regenerates itself per build.

## 2. MLAB — the resource nobody is using

MLAB usage across every saved fit report is **zero**, while ALMs sit around 35% free [FIT]. On Cyclone V each
MLAB is **10 ALMs = 640 bits**, natively **32 words x 20 bits, simple-dual-port only** (there is no true
dual-port MLAB) [EXT]. Syntax is the one the EQ already uses:

    (* ramstyle = "MLAB, no_rw_check" *) reg [W-1:0] mem [0:D-1];

`no_rw_check` matters: MLAB read-during-write only cleanly supports old-data behaviour, and without it Quartus
inserts bypass muxing [EXT].

**Migration candidates**, all simple-dual-port and therefore MLAB-legal:

| Memory | Blocks freed | Note |
|---|---|---|
| `cmd_mem` draw command FIFO (256x82) | 3 | ~40 MLABs / ~400 ALMs [EST]. The riskiest of the four — see the Fmax caveat below. Consider whether 256 entries is actually needed before chaining that deep. |
| VexRiscv register file (2 banks, 32x32) | 2 | Each bank stores 1,024 bits in an 8,192-bit block — ~12.5% utilised. But it lives in the **generated** `VexRiscv_Full.v` netlist with no SpinalHDL source in-repo, so this means hand-patching a `ramstyle` attribute on two `altsyncram` instances. Lowest priority, highest awkwardness. |
| `glyphbuf` (128x16) | 1 | Clean fit, ~4 MLABs. |
| `sound_i2s` dcfifo (4x32) | 1 | Holds 128 bits in a whole block purely because a `dcfifo` won't go smaller. Needs an `lpm_hint` RAM_BLOCK_TYPE change. |

**Caveat [EST], not cited:** deep MLAB chaining (the 256-deep FIFO needs ~8-deep chaining) carries an
unquantified Fmax penalty, and this design already has a documented timing cliff at 100 MHz (section 11). Do
the three easy ones first and treat the command FIFO as its own decision with a timing check.

Expected: **+7 blocks** for essentially no functional change.

## 3. Font ROM — two independent wins

The ROM holds 3,040 words x 32 bits = 97,280 bits but occupies 16 blocks [FIT]. At 32-bit width an M10K is
256x32, so 3040/256 = **12 blocks is the theoretical fit** — it is packing at ~74% efficiency.

1. **Repack (+4 blocks, near-zero risk).** Split it into four byte-wide arrays — exactly the trick `mp3_soc`
   already uses for main RAM, and for the same reason. No behaviour change; one build confirms it. **[EST]** —
   Quartus inference is not fully predictable from source, so this is a hypothesis a fit report settles.
2. **Move it to PSRAM entirely (+12-16 blocks, medium risk).** It is read-only and not on the real-time
   scanout path, and the PSRAM data window is proven [HW]. Costs PSRAM latency per glyph fetch plus bus
   contention with scanout, so it wants a small BRAM glyph cache in front — call it **+12 net**. Only do this
   if the blocks are actually needed; it is the first item here that can hurt something.

## 4. The M10K ledger and the ordering constraint

**Corrected 2026-09-22 (B-102) against a real fit, not an estimate.** Section 2's "+7 MLAB / +4 font repack"
were both pre-fit predictions. Only two of the four MLAB candidates were in scope for this build (`cmd_mem` and
the VexRiscv regfile deliberately excluded, see section 2) and the font repack was measured, not assumed —
**it delivered +0, not +4.** Real result, both seeds: 298/308 used (was 300/308), a net **+2 blocks**, all of it
from `glyphbuf` + the `sound_i2s` dcfifo resolving to MLAB. `TAU_FONT_REPACK` is confirmed functionally inert
(as designed) but is *also* fitter-inert — declared content bits are unchanged by construction (section 10's
correction already covered this), and it turns out the fitter does not find fewer physical M10K primitives for
four narrow ROMs than one wide one either. **On-chip repacking is not a lever for the font ROM; PSRAM (row
below) is now the only real one if those blocks are needed.**

| Step | Frees | Running free | Risk |
|---|---|---|---|
| Today (measured baseline, B-018/A-114 product fit) | — | **8** (300/308 used) | — |
| MLAB: `glyphbuf` + `sound_i2s` dcfifo (B-101/B-102, fit-confirmed, both seeds) | +2 | 10 | very low; one seed showed a real -0.001 ns setup violation on the glyphbuf write path, the other closed positive on all four corners — pick that seed |
| Font ROM repack (B-102, fit-confirmed) | **+0** (not +4 — corrected) | 10 | none (inert, just does not help) |
| MLAB: `cmd_mem` (still needs its own timing check per section 2) | +3 (estimate, unverified) | 13 | medium — the Fmax caveat on deep MLAB chaining is still untested |
| MLAB: VexRiscv register file (generated netlist, needs hand-patching) | +2 (estimate, unverified) | 15 | low risk, high awkwardness — still deferred |
| *Blit engine consumes (CLUT + working)* | −2 | 13 | — |
| *Spectrum filter bank consumes* | ~0 | 13 | — |
| *FLAC bit-reader accelerator, if the profile justifies it* | −2 | 11 | — |
| Font ROM to PSRAM — **now the real path to font-related blocks, not a fallback** | +12 | ~23 | medium |
| **Main RAM 256 KB -> 192 KB** | **+64** | **~87** | see below |

**The ordering constraint, which is the important part of this document.** Main RAM cannot shrink first.
`RAM_WORDS` must be a power of two — a 48 KB attempt once exploded the fitter into LUTs [SRC] — so the options
are 256 KB or 128 KB. Halving needs ~93 KB more headroom and only ~34.7 KB is free, so it is unreachable.

**Proposed workaround:** instantiate **two** power-of-two arrays (128 KB + 64 KB = 192 KB), address-decoded as
one contiguous region. Each array stays a clean power of two, sidestepping the irregular-depth problem that bit
them before. That frees **64 blocks** and needs only ~29 KB more headroom.

Where that ~29 KB comes from: the cold set was estimated at 35-45 KB and only ~24 KB has moved. The two named
remaining pieces are **the meters (~17 KB) and picojpeg (~8 KB)** — almost exactly the gap. And the meters are
deliberately still hot *because they are waiting on the blit engine* (`PHASE_G_SPEC.md` says so in as many
words). So:

> **blit engine → meters go cold → ~29 KB freed → main RAM 192 KB → 64 blocks released**

You cannot fund the blit engine out of the RAM shrink. It runs the other way, and that is what fixes the
ordering of everything below.

**Held, 2026-09-24 — do not start the meter/blit integration until the Blit Test hang has a real diagnosis.**
The first concrete step toward "meters go cold" was scoped (see the "meter integration" note below), but
building it right now would mean wiring a new call site onto `blit_probe_ensure()` — the exact function
implicated in the still-open, unresolved Blit Test hang (`docs/SESSION_HANDOFF_2026-09-24_BLIT_TEST.md`: four
source-level fix attempts, the last one made it worse, ISSP diagnosis pending a JTAG cable). Its own header
comment (`fw/blit_probe.inc`) says plainly the current fix is "not confirmed on hardware." A hang triggered
from `ui_draw_dynamic()` — the live playback path, running continuously while audio decodes — would be a far
worse regression than a hang on a diagnostics page. Owner's explicit call, 2026-09-24: hold this track entirely
until ISSP gives a real root cause, rather than proceed even with the safe (`BLIT_READY()` read-only, no new
probe call) variant. Resume once the hang is diagnosed and, if `blit_probe()` itself was the cause, actually
fixed and hardware-verified.

**Meter integration, scoped and ready once unblocked (not built).** The plain-bars mode (`fw/player.c`'s
default `ui_draw_dynamic()` fallthrough, ~line 5085-5115 — the busiest per-frame path, up to `UI_WAVE_N` bars
every frame) already matches `OP_BAR`'s exact convention: lit rows at the bottom, unlit at top, same colours.
`fb_bar()` already exists (built for the Blit Test diagnostic, B-166) and needs no RTL change — B6 is already
proven under real audio load (B-146/B-164's Blit Storm Check test). The change itself is small and surgical:
replace the two `fb_rect()` calls at lines 5110-5112 with one `fb_bar()` call (the third call, the 1px peak-
hold marker at line 5113-5114, stays as-is — `OP_BAR` has no marker mode), gated on `BLIT_READY()` with the
existing two-`fb_rect()` code kept as the software fallback. Other visualizer modes (`VIZ_LEVELS`'s horizontal
bars, `VIZ_LED`, `VIZ_MIRROR`, `VIZ_SCOPE`, the waterfall) do not match `OP_BAR`'s fixed vertical-only
convention and were not analysed for conversion this pass — plain bars alone is the single largest per-frame
command-count win and the natural first step.

**A caution on the shrink itself.** It trades one scarce resource for another. Heap gap has repeatedly been
driven to its floor as features landed (4,096 B at worst, before Phase G). It is reversible only by another
45-minute build.

### 4.1 How to decide the shrink is safe — measure peak, not the static gap

**The link-time heap gap (34,752 B) is the wrong number.** It is a static figure and does not capture peak
*stack* depth, which is what actually decides whether 192 KB holds. Method:

1. **Paint the stack** with a known pattern at boot, then read back the high-water mark — the standard embedded
   technique. It drops straight into the existing Check infrastructure as a reported line (peak stack, peak
   heap, and the resulting true free figure), so it is measured the same way everything else here is.
2. **Measure under genuine worst case**, not idle: ENDURANCE profile + a full library + a large playlist + a
   fresh large cover decode (the uncached path, not the signature-reuse path) + browse-while-playing + every
   meter mode. Browse-while-playing matters most — it is the case G4 introduced and the one not otherwise
   regression-tested.
3. **Write the margin down before the measurement**, per the standing prediction discipline. Proposed:
   **16 KB** (~8% of 192 KB). Pick and record it before seeing the number, not after.
4. **Gate:** shrink only if `measured peak + margin < 192 KB` under the worst profile above.

**Result (B-230, 2026-09-25):** `TAU_DEV_49`'s stack instrumentation was read back from the prescribed
worst-case exercise (Stress R3 + heavy scroll/seek/track-switch + MP3 and FLAC with different covers,
finished with a USER CHECK). **Peak stack: 1,672 B of 16,384 B (10.2%)** — conclusively trivial, all 7
checks PASS, 0 errors, audio continuous the whole window. A real gap was found in the same reading: only
peak *stack* was ever instrumented, not peak *heap* as this section originally asked for — the QR report's
`heap_gap` field is a link-time constant (heap capacity), not a runtime measurement; `arena_limit()` (newlib's
malloc high-water mark, already used on the Info page) was never wired into Check/QR. **Owner decision:**
proceed on the existing evidence (stack trivial, a full heavy pass with zero failures); wiring `arena_limit()`
into Check/QR for a literal heap-peak number is parked as a later-build addition, not a prerequisite. Gate
considered met.

`RAM_WORDS` is already a `localparam`, so reverting is a one-line change plus a build.

### 4.2 Per-audio-frame cold-code measurement — scoped 2026-09-25, not yet built

**The conflict this resolves.** `fw/cold.inc` states, as a deliberate rule written during Phase G1
(commit `c693044`, 2026-09-21): *"Nothing that runs per audio frame may be cold."* `ui_draw_dynamic()`
(the meter/visualiser draw, section 4's whole "meters go cold" premise) runs on exactly that path —
confirmed by its own call sites, gated on `FL_UI_PERIOD = CLK_HZ / 38` (`fw/player.c`), i.e. about
every 26.3 ms, ~38 Hz, not literally once per decoded MP3/FLAC frame but close enough in spirit that
the rule plainly means to cover it. **This rule predates the blit engine by a full day** (Phase G1
landed 2026-09-21, Phase F/the blit engine started 2026-09-22) and was written when PSRAM
instruction-fetch cost against the audio decode budget was a total unknown -- it reads as a
conservative default, not a measured limit. It has never been re-examined since real numbers
existed. Before either converting `ui_draw_dynamic()` or accepting the shrink is foreclosed, measure.

**What is already known, not re-derived:**
- PSRAM instruction fetch is a genuinely separate bus/arbiter from the audio path's SDRAM traffic
  (`tau_psram_bus`, its own two-client arbiter -- B-047), so the risk is CPU stall time competing with
  the decode loop's own deadline, not bus contention with audio DMA directly.
- A cold cache-line-fill costs **~31.6 cycles/word**, hardware-confirmed (B-047 sim, B-054 hardware,
  a 30-minute soak with 0 failures) -- but that was measured for menu/settings/library code, entered
  rarely, never for something called ~38 times a second.
- The I-cache is 4 KiB / 128 lines, direct-mapped (section 1's M10K map). A cold function that fits
  inside it and isn't evicted between calls should pay the ~31.6 cycles/word cost **once**, then run
  at full (BRAM-equivalent) speed on every subsequent call until something else evicts those lines.
  `fw/cold.inc`'s own `cold_big` (3,000 instructions / 12 KB, deliberately 3x the I-cache) exists
  specifically to force a refill on every call, as the worst-case reference point.

**The real open question is eviction, not raw fetch cost:** does anything else run between two
consecutive `ui_draw_dynamic()` calls (other cold code -- menus, library browsing, the Check, the
Decode Sweep) that would evict its lines and force a refill on the very next meter draw, turning a
one-time cost into a sustained ~38 Hz one? Idle playback (nothing else touching cold code) is the
best case; browsing the library or opening Settings while a track plays (G4's own known-risky case,
already flagged elsewhere in this document as "not otherwise regression-tested") is very plausibly
the worst case.

**Proposed measurement, before any real conversion:**
1. Two synthetic `COLD_TEXT` probes, not the real `ui_draw_dynamic()` yet: one sized to roughly match
   the blit-converted plain-bars draw path's own instruction count (small, expected to fit and stay
   resident in the I-cache), one reusing the existing `cold_big` shape (12 KB, guaranteed refill every
   call) as the deliberate worst case. Call one of them from the *same* call sites `ui_draw_dynamic()`
   already uses (behind a new, default-off diagnostic macro, so the real feature is untouched), timed
   with `cycles()` around the call exactly like every other `SR_T_*`/`CT_*` Check measurement in this
   codebase.
2. A new Check test (`CT_COLDFRAME`, alongside `CT_BLT`'s own convention) reporting min/avg/max cycles
   per call over a real playback window, plus the existing late-underrun verdict machinery -- so a
   real regression fails the same objective way every other test here does, not a one-off eyeballed
   number.
3. Run it under the worst-case eviction scenario above (idle playback first, then browse-while-playing
   with the library/settings open concurrently) -- the exact profile section 4.1 already prescribes for
   the RAM-shrink safety gate, reused rather than invented fresh.
4. **Predictions, written down before running, per the standing discipline:** idle playback shows the
   small probe's cost amortize to near-zero after the first call (a few cycles of overhead, no refill);
   the 12 KB worst-case probe pays the full ~31.6 cycles/word refill on *every* call (~94,800 cycles /
   ~1.58 ms per call at 60 MHz, out of a 1,578,947-cycle/26.3 ms budget -- ~6%, likely tolerable alone
   but the real test is combined with everything else the decode loop already does); browse-while-
   playing is the case most likely to force repeated evictions and is where a real problem, if one
   exists, should show up first.
5. **Gate:** if the small probe's steady-state cost stays negligible and even the worst-case probe
   never produces a late underrun across the full test matrix, `cold.inc`'s blanket rule can be
   narrowed (e.g. "small cold functions that fit the I-cache are fine on the per-frame path; large
   ones are not") and the real `ui_draw_dynamic()` conversion can proceed with a permanent `CT_COLDFRAME`-
   style regression test guarding it forever after, the same way `CT_BLT` now guards the blit engine's
   own SDRAM traffic. If it fails, the rule stands as written and the RAM shrink needs a different
   ~29 KB source (font-to-PSRAM's own +12 blocks, section 3, is independent of this question entirely
   -- it moves draw-engine *data* through the existing hardware PSRAM window, not CPU cold *code*, so
   none of this applies to it).

**Built and measured, 2026-09-25 (B-200).** Both probes and `CT_COLDFRAME` built exactly as scoped
(gated behind default-off `TAU_COLD_FRAME_PROBE`/`COLDFRAME_BIG`, zero cost in any real build). Real
hardware results, both isolated (idle playback, not yet under the browse-while-playing worst case):

| Probe | Worst-case cycles | % of 26.3 ms budget | Late underruns |
|---|---|---|---|
| Small (`cold_frame_small`, 100 instructions, I-cache-resident) | 3,309 | ~0.21% | 0 |
| Big (`cold_big`, 3,000 instructions, guaranteed refill every call) | **94,801** | ~6% | 0 |

The big-probe number matches this section's own prediction (~94,800 cycles) to within 1 cycle -- the
31.6-cycles/word model (B-047/B-054) holds exactly, not just approximately. **Even the deliberately
worst physically-plausible case -- a function 3x oversized for the I-cache, forced to refill on
every single one of ~38 calls/second, sustained 30 seconds -- produced zero late underruns.** This
does not survive as a blanket "nothing per audio frame may be cold" rule; at minimum it needs
narrowing to something like "a cold function whose cost is within the measured margin here is fine."

**Not yet done at the time, the real remaining gate:** both runs measured the synthetic probe in
isolation against ordinary playback -- neither tested the browse-while-playing scenario (Settings/
Library open while a track plays) this section itself flagged as the likely real eviction case,
where OTHER cold code (menu drawing, library browsing) could contend for the same I-cache lines the
meter draw needs, turning an occasional refill into a sustained one.

### 4.3 Browse-while-playing (guaranteed-eviction) measurement — scoped and built 2026-09-25

Driving real UI navigation programmatically (synthetic key edges into `lib_ui_input()`/`set_input()`,
risking visible on-screen state changes mid-Check) is fragile and roundabout for what's actually being
asked: does something ELSE cold running immediately before a meter draw force that draw back to a
cold miss, and does that cost compound into anything worse than a plain refill (direct-mapped-cache
thrashing, not just occasional cold misses). Simulating the interleaving directly answers this without
touching real navigation: a new build toggle, `COLDFRAME_EVICT`, makes `coldframe_tick()` call
`cold_big()` **untimed and discarded** immediately before the timed probe call -- `cold_big` is
already guaranteed to evict the entire I-cache (3x its capacity by construction), so this reproduces
exactly what "another cold function just ran" does to the cache, worse than realistic browsing would
(real browsing doesn't evict on literally every single ~38 Hz tick the way this forces). If the timed
small probe's cost under forced eviction stays close to its own known cold-miss cost (~3,160 cycles,
100 instructions x 31.6 cycles/word) rather than something larger, that rules out pathological
thrashing beyond ordinary refill cost. **Predictions, written down before running:** the evicted small
probe should land close to 3,300-3,400 cycles (matching the already-measured near-cold-start number,
B-200's small-probe run, almost by coincidence already close to a full refill); no late underruns,
since even `cold_big` alone at ~94,801 cycles/call already passed clean.

**Measured, 2026-09-25 (B-201): 3,067 cycles, PASS, 0 late underruns** -- matching the prediction and,
more importantly, matching the isolated (non-evicted) small-probe result (3,309) closely enough to
rule out cache-thrashing amplification: forcing a full eviction before every single call does not
push the cost above the function's own ordinary cold-miss cost. **The full measurement matrix is
complete:**

| Scenario | Worst-case cycles | Late underruns |
|---|---|---|
| Small probe, idle playback | 3,309 | 0 |
| Small probe, forced eviction every call | 3,067 | 0 |
| Big probe, always-cold by construction | 94,801 | 0 |

**Conclusion: `cold.inc`'s blanket "nothing per audio frame may be cold" rule does not survive
contact with these numbers.** Every scenario this measurement was built to stress -- ordinary
operation, the theoretical worst single-call cost, and adversarial forced-eviction interleaving --
produced zero late underruns. This is evidence, not a hopeful assumption. **Recommended next steps:**
(1) narrow `cold.inc`'s rule to something evidence-based rather than a blanket ban; (2) convert the
real `ui_draw_dynamic()` to `COLD_FN`, keeping a `CT_COLDFRAME`-descended permanent regression test
the way `CT_BLT` now permanently guards the blit engine's own SDRAM traffic, so a future regression
(a much bigger meter mode added later, say) is caught automatically rather than assumed safe forever
from this one measurement.

## 5. Blit engine — feature spec

Existing engine, for reference [SRC]: opcodes `RUN`/`RECT` (flat fill), `CHAR` (AA glyph, coverage-blended
against an explicit bg, Bresenham 1x/1.5x/2x/3x per axis), `COPY` (raw SDRAM->SDRAM move); 256-entry x 88-bit
async command FIFO; scanout-first dispatcher decomposing every operation into single-burst units so a pending
scanline fill waits at most one burst (~500 cycles) against ~4,167 cycles of per-scanline slack; single-buffered
400x360 RGB565 at stride 512.

`COPY` is already load-bearing for three separate jobs — the cover-art slide panel, the waterfall meter scroll,
and an off-screen gradient stash used to erase cheaply. **Generalising COPY is therefore most of the ask, and
it is already proven in software.**

### Tier 1 — this build

| ID | Feature | M10K | Notes |
|---|---|---|---|
| **B1** | **Generalised blit** — arbitrary rect, independent source and destination stride/base | 0 | Extends `COPY`. Removes the current `FB_COPY_MAX=127` chunking quirk (a 7-bit `char_w` field where 128 truncates to 0). |
| **B2** | **Colour-key transparency** — one reserved RGB565 value, one comparator | 0 | Right default for a true-colour framebuffer. Palette-indexed hardware (Genesis, Neo Geo) used index-0 keying precisely because it is nearly free; PSX needed a real transparency bit only because it had no fixed palette [EXT]. |
| **B3** | **Sub-pixel skew + first/last column masks** | 0 | The Amiga/Atari ST mechanism: a barrel shifter on the source channel plus first/last-word masks AND'd before the write. Costs tens to ~100 LUTs [EXT]. **This is the fix for the marquee**: a firmware comment states the engine "does not clip one partially off the left edge", which is why long titles scroll by whole characters. |
| **B4** | **Scaled blit, nearest (Bresenham/DDA)** | 0 | **Key finding:** because the decoded cover already sits in a randomly-addressable buffer, *no line buffer is needed* — one read per output pixel (or four, if bilinear is ever wanted). Line buffers are a streaming-source problem, and this source is not streaming [EXT]. The block-RAM cost I originally assumed here is zero. |
| **B5** | **Alpha blend** | 0 | Two paths. A **DSP multiply** path for real 0-255 alpha — affordable, 55 of 66 DSPs are free — and a **shift-add** path for the PSX fixed ratios (`B/2+F/2`, `B+F`, `B-F`, `B+F/4`, all shifts and adds with clamping) [EXT] as a cheap mode. Note the constraint that forced PSX's design does not bind us; we choose fixed ratios for speed, not necessity. |
| **B6** | **Meter column primitive** | 0 | A bar is `(x, base_y, height, lit, unlit)`. One opcode replaces ~72 `fb_rect` calls per frame on the hottest per-frame path in the firmware. |
| **B7** | **SDRAM busy-cycle counter** | 0 | Bundled here per the standing decision (B-083). **Prerequisite, not a nice-to-have**: without it there is no way to show a new SDRAM client did not eat the audio margin. |

### Tier 2 — if it fits in the same build

| ID | Feature | M10K | Notes |
|---|---|---|---|
| **B8** | **CLUT / palette blit** | 1 | Unifies glyph, thumbnail and icon rendering into one pixel path. Also enables B9. |
| **B9** | **Palette re-index for dim/highlight — done, 2026-09-24, RTL/sim only** | 0 | The Genesis shadow/highlight trick: do not blend, just re-index to a shadow or highlight palette [EXT]. Near-free dimmed/selected UI states, and a nearly free theme or dark-mode swap once a CLUT exists. See the write-up after the B8 section for the as-built design. |
| **B10** | **Hardware RLE source blit — analysed 2026-09-24, not built (design below); value re-assessed downward** | 0-1 | Cover-art rows and meter thumbnails are *already* RLE-encoded in firmware and pushed one command per run. Reading `(run, value)` pairs from a source buffer collapses that. Architecture model is the 3DO Cel Engine's DUP/PDC split — a streaming decompressor stage ahead of a conventional pixel pipeline [EXT]. No adaptable RTL exists; design it fresh. |
| **B11** | **Hardware rounded-rect — RTL/sim built 2026-09-24 (B-205); Quartus fit FAILED timing 2026-09-25 (B-211), root cause found, not yet fixed** | 0 | Replaces `fb_round_rect_on`'s software corner-overpaint, used on every selection highlight. |

### B8 detailed design (2026-09-23) — grounded in the real target, not designed in the abstract

Before writing any RTL: `fw/meter_thumbs.h`'s `set_draw_thumb()` (`fw/settingsui.inc`) is the exact,
already-shipping use case B8/B10 exist to accelerate — worth reading before designing either, and not
previously done. Each of the 11 meter previews (56x32 = 1,792 pixels) is stored as an **8-entry palette**
(`meter_thumb_pal[viz][8]`, real RGB565) plus an **RLE byte stream** (`meter_thumb_rle`, one byte per run:
top 3 bits palette index, bottom 5 bits `run_length - 1`), decoded entirely in cold-code software today —
one `fb_rect` call per run, further split every time a run crosses a row boundary (56-pixel rows mean this
happens often). A single thumbnail can cost dozens of draw-engine commands, each round-tripping through cold
code. This is real production data, in a real production format, right now — the design below targets it
directly rather than inventing a new palette format speculatively.

**Recommendation: split B8 into two steps, don't build the whole thing in one commit.**

**Step 1: done, 2026-09-23 (B-148), RTL/sim only.** After review (the owner's "do it" following this section's
own "stops here" note), implemented and verified exactly as designed below — with one real bug found and fixed
along the way: the first cut registered `clut_raddr` (set the SAME edge `A_CBLIT_RD` captured the source word,
valid only the edge AFTER), which puts the CLUT's answer one cycle later than `A_CBLIT_WAIT` expects it —
simulation caught this immediately as an `x` in the scene dump, not spotted in review. Fixed by making
`clut_raddr` combinational (`p0_q`'s low byte directly), so `clut_q`'s own registered update — which fires on
the *same* edge `A_CBLIT_RD` transitions to `A_CBLIT_WAIT` — already sees the right address that cycle, landing
correctly one cycle later. Not a redesign, the exact two-state shape below, just a wiring correction. **Zero
regression on the full existing suite**, including the shared `A_COPYRD`/`A_WRWAIT` paths `OP_COPY`/`OP_BLIT`
depend on (`make test-rtl`, full pass, before and after) — the risk this section flagged didn't materialize,
because the design (a completely separate one-word-per-transaction path, mirroring `OP_SBLIT`, never touching
`A_COPYRD` at all) held up exactly as intended. New coverage: `sim/tb_blit_scene.v`'s scene gained a 12th
command (a real `OP_CBLIT` with a 5-entry preloaded CLUT), `tools/host/blit_reference.py` gained a matching
`cblit()` method, and a new mutation hook `BUG_CBLIT_NO_LOOKUP` (writes the raw index instead of the CLUT's
answer) is caught by the pixel-diff, same convention as every other opcode's mutation test.
**Firmware integration (the "one command per thumbnail" win) is NOT part of this entry** — no `player.c`
register defines, no `set_draw_thumb()` change, no hardware/Quartus run. This closes only the RTL+simulation
half of step 1; a real hardware timing fit is still needed before this can ship, and firmware wiring is its
own follow-up.

**Quartus fit result, 2026-09-24 (B-150): did NOT close.** The same proven G3+blit qsf combination, re-fit with
this RTL added: `RAM Blocks` 299/308 (exactly +1, the CLUT's own M10K, no surprise usage) on both seeds, but
**a real setup violation on both** — seed 1 Slow 85C -0.086 ns / Slow 0C -0.196 ns; seed 2 (better) Slow 85C
+0.088 ns / Slow 0C -0.025 ns. Hold clean on both. This is small but real and consistent in direction across
both seeds — not seed noise — so the CLUT's physical cost (a new dual-clock M10K plus its read/write logic) is
not free the way B7's busy counter or the MLAB/font-repack work were. **This bitstream is not ready to ship.**
**Path found, 2026-09-24 (B-151):** the worst setup paths all run from `mp3_fb.sv`'s internal muxes into
`glyphbuf`'s own register inputs — the exact same shared write-data selection network this phase has hit
marginal three separate times now (B-109's original MLAB-era violation, B-111's BAR retiming fix, B-116's
`TAU_BLIT_BLEND` congestion finding), each for a different reason, all converging on this one physical
bottleneck. `OP_CBLIT` is a fourth competing write source feeding it (alongside `A_COMPOSE`/`A_COPYRD`/
`A_KEYDST`/`A_SBLIT`'s own writes). **Two candidate causes, not yet distinguished:** (a) the
`BUG_CBLIT_NO_LOOKUP` mutation-test ternary failing to constant-fold away when off, or (b) simply adding a
fourth mutually-exclusive write source widens whatever select network Quartus builds, regardless of the
mutation hook. (a) is the less likely explanation on reflection — `BUG_BLEND_ALWAYS_SRC` is a structurally
similar ternary already in the same write path that hasn't caused a new violation by itself — making (b) a
real design question (does Quartus merge mutually-exclusive case-arm writes into `glyphbuf` efficiently, or
not?) rather than a one-line fix to try blind. No re-fit attempted on either hypothesis yet — worth narrowing
down further before spending another Quartus cycle guessing.

**Hypothesis (a) ruled out, exact path found, 2026-09-24 (B-154/B-157):** the `generate`-gated experiment
(B-152/B-153) produced bit-for-bit identical slack to the ternary version — (a) is dead, (b) is confirmed.
`quartus_sta -t report_timing -detail full_path` against the still-present fit database, queried at the actual
violating corner (Slow 0C, reproducing the exact -0.025 ns), traced the real worst path: `Mux21~4` (register)
-> `Add6~2` -> `Add8~8` (**4.5 ns alone — the dominant cost**) -> `Selector224~0` -> `glyphbuf`'s write-data
bit 0. **No node in this chain has a CBLIT/CLUT-associated name.** By bit position this is `px_color[0]` =
`mix_b[4]`, the blue-channel term of `A_COMPOSE`'s glyph anti-aliasing blend (`mix_b = char_fg[4:0]*cov16 +
char_bg[4:0]*inv16`, line 656) — pre-existing arithmetic, unrelated to CBLIT except for sharing `glyphbuf`'s
write port. **`OP_CBLIT` doesn't need to be slow itself to break this build** — it just has to exist as a
fourth write source, which widens `Selector224`'s fan-in enough to cost the ~0.11 ns margin an unrelated,
already-marginal path was living on. Retiming `cblit_wval` (B-154's original suggested next step) would not
fix this specific violation, since `cblit_wval` isn't on the reported worst path at all.
**Next step is therefore different from what B-154 proposed:** either (a) add one pipeline cycle to
`A_COMPOSE` so `px_color` is registered before the write instead of computed combinationally into it (a real
per-glyph cost, against a path already documented as "1.5% of a scanline's slack" — needs a budget check, not
just an RTL edit), or (b) restructure so the next pixel's `px_color` is precomputed a cycle ahead of the
current pixel's write (genuine pipelining, more design work, no added per-glyph cost). Not picked yet — a real
design tradeoff, not a one-line fix, flagged for the owner rather than guessed at with another Quartus cycle.

**MILESTONE — FIXED, 2026-09-24 (B-158/B-159): timing closes cleanly on all four corners, real margin, first
attempt.** Owner picked option (a). `A_COMPOSE` split into two states: `A_COMPOSE` now only registers
`px_color` into a new `px_color_r`, `A_COMPOSE_WR` (new `astate` value 11) does the actual `glyphbuf` write
plus the address/Bresenham advance — breaking the combinational chain that fed `Add8~8` straight into
`Selector224`/`glyphbuf`'s write port in one cycle. Cost: one extra cycle per composed pixel, worst case
64 -> 128 cycles for a 4x glyph, ~1.5% -> ~3% of a scanline's ~4,167-cycle slack. `make rtl-lint`/`test-rtl`/
`test-host` all pass (CHAR tests content-checked, unaffected by the doubled cycle count). Re-fit (seed 2, same
proven qsf) closed on the first attempt: Slow 85C setup **+1.787 ns** (was -0.086 ns), Slow 0C setup
**+1.383 ns** (was the reported **-0.025 ns** violation), hold **+0.285 ns**/**+0.270 ns** (both TNS 0.000).
RAM unchanged at 299/308. **B8 step 1 (CLUT blit) is now RTL/sim-correct AND timing-proven for the product
configuration.**

**Firmware integration done, 2026-09-24 (B-160).** `fb_clut_load()`/`fb_cblit()` added to `player.c`;
`set_draw_thumb()` now issues one `OP_CBLIT` per thumbnail instead of dozens of `fb_rect` calls, sourced from
11 flat 56x32 index buffers built **once, lazily** (~16 ms total, negligible against boot) via the CPU's
uncached SDRAM window into off-screen rows reusing the art stash's own addressing convention (no new SDRAM
region). `BLIT_READY()`'s gate widened from diagnostic-only to `TAU_METER_THUMBS`, so the fail-safe (fall back
to the kept-intact software path, `set_draw_thumb_soft()`) actually runs for this feature instead of being
dead code — necessary because the blit engine has not shipped in the release bitstream yet. All builds compile
clean, `dist/`'s release ROM confirmed byte-identical (feature is off there).
The considered-and-parked zero-CPU alternative (APF's `data_slots[].address` bridge auto-load pushing a
pre-baked asset straight into SDRAM) is recorded in section 13.

**Boot-blocking bug found and fixed, 2026-09-24 (B-162).** The first hardware install (`TAU_0_5_0_A_6`) hung on
every boot — `blit_probe()`'s gate had been widened to run unconditionally at boot for any `TAU_METER_THUMBS`
build, the first time that function had ever executed on real hardware in any build. Its `fb_wait()` never
returned, freezing the firmware right before the loading-progress animation. Deferred to `blit_probe_ensure()`,
run at most once on first actual need (`set_draw_thumb()`), never at boot.

**MILESTONE — proven on real hardware, 2026-09-24 (B-164).** `TAU_0_5_0_A_7` (the B-162 fix) booted normally
and Settings opened cleanly — real evidence `OP_BLIT` itself was never broken, only running the probe too
early in boot. A 30s `Blit storm` Check test **PASSED**: SDRAM busy 15.8% (identical to B-146's own measurement
on the pre-CBLIT bitstream — this session's RTL changes cost nothing extra), audio continuous the whole
window, zero late underruns. Every other test passes except the pre-existing, unrelated `Track changes` bug.
**B8 is now proven end to end**: RTL/sim-correct, timing-closed, firmware-integrated, and hardware-verified
under sustained load with real audio — the same three-legs-of-proof bar B1 met at B-146.

**The two-state design, as built:**
- **Opcode 7** (`OP_CBLIT`) — the last value the existing 3-bit `cmd_op` field has room for, no width change
  needed (a nice coincidence, not a constraint that shaped the design).
- **A 256-entry x 16-bit CLUT RAM, one M10K** (matches the table's own budget), written by the CPU through a
  dedicated indexed pair — `R_CLUT_IDX` (0-255) / `R_CLUT_DATA` (RGB565) — kept **separate** from
  `R_BLT_IDX`/`R_BLT_DATA` rather than folded in: those five sticky fields are small, mostly-static per-command
  config; a 256-entry table load is a different kind of write traffic (an infrequent bulk load, once per
  palette swap) and conflating the two would make the sticky-field address space do double duty for no benefit.
- **Source format for step 1: one palette index per 16-bit SDRAM word** (low byte used, high byte unused) —
  matches every other opcode's "one word = one pixel" convention exactly, at the cost of wasting 8 bits per
  source word. Deliberately not packing 2 indices/word yet: B4's own finding was that this engine's design
  already accepts one word per output pixel as fine (no line buffer needed, because the source is
  randomly-addressable, not streaming) — packing now would be optimizing a cost this design doesn't actually
  have evidence is a problem, before the simpler version has even been tried.
- **Dispatch: extends `OP_BLIT`'s own addressing exactly** (sticky `SRC_BASE`/`SRC_STRIDE`/`DST_BASE`/`DST_STRIDE`,
  the same per-row stepping `A_WRWAIT` already does) — the only change is what happens to the source word once
  read: instead of writing it straight to `glyphbuf` (what `OP_BLIT` does), index the CLUT with its low byte
  and write *that* value instead. One new mux, no new addressing logic, no new state machine.
- **No key/blend interaction in this step** — `OP_CBLIT` is independent, matching `OP_COPY`'s own precedent of
  "never keys" (B2's header comment). Layering key/blend onto a CLUT blit is a real question (key against the
  *index* or the *resolved colour*?) worth its own decision later, not bundled in here.
- This alone would let `set_draw_thumb()` issue **one command per thumbnail** instead of dozens, if firmware
  pre-expands the RLE into a flat 56x32 index buffer once (in PSRAM, off the hot path) — real savings, but not
  the full win, since the RLE expansion itself still costs cold-code cycles once per thumbnail-set change.

**Step 2 (folds in B10 for this exact format) — read the RLE bytes directly, no firmware expansion at all.**
Once step 1's CLUT exists, add a second small mode (a sticky enable bit, not a new opcode) that reads the
*same* `(idx<<5 | run-1)` byte stream `meter_thumb_rle` already produces, unpacking two bytes per 16-bit
source word (even byte first) and running a small counter that writes `run` pixels before advancing to the
next byte, wrapping at the sticky destination width exactly like `set_draw_thumb()`'s own `col`/`seg` loop
does today. This is `B10`'s "3DO Cel Engine DUP/PDC split" in miniature: a tiny run-length front end feeding
the same CLUT-indexed pixel pipeline step 1 already built, rather than a second unrelated design. **Explicitly
deferred, not designed further here** — it needs its own state machine (byte-vs-word source addressing is new
to this engine, everything else has been word-per-pixel) and its own mutation-test coverage, and step 1 should
prove the CLUT mechanism itself works before adding a decoder on top of it.

**What this is NOT yet solving:** icon rendering (mentioned in the Tier 1 table's own B8 note) wasn't read for
this design pass — icons may already be small enough that the draw-call overhead this targets doesn't apply to
them; check before assuming B8 helps there too.

### B9 (palette re-index), 2026-09-24 — RTL/sim, no Quartus fit yet

Built while waiting on JTAG cable access for the Blit Test hang (see the 2026-09-24 session handoff) — Tier 2,
0 M10K, no dependency on the hang or on any hardware. **Design: an 8-bit sticky offset (`blt_reindex`, section 9
field 6), added to `OP_CBLIT`'s palette index before the CLUT lookup.** Not a second table, not a blend — the
Genesis trick is precisely "re-index, don't blend": firmware pre-bakes a shadow or highlight variant of a
palette into a different segment of the same 256-entry CLUT (e.g. the meter thumbnails' 8-colour palette at
indices 0-7, a dimmed copy at 32-39, a highlighted copy at 64-71 — any 8-aligned bank the firmware picks), and
this offset just selects which segment a given `OP_CBLIT` reads from. Default 0 is a true no-op (`index + 0 =
index`), so — unlike B2/B5 — there is no separate enable bit to gate: cost is one 8-bit adder feeding the CLUT's
existing read address (`clut_raddr = p0_q[7:0] + blt_reindex`), reusing B8's own read port and CDC-free registered-
read idiom exactly, no new state, no new dispatch path. Wraps naturally (8-bit add), same "near free" reasoning
every other sticky field here already uses.

MMIO: `R_BLT_IDX`/`R_BLT_DATA` field 6 (`bits[7:0]` = offset), the sticky-field burst now 7 writes instead of 6
(wraps 6->0). `docs/MMIO_ALLOCATION.md` updated.

Verified: `sim/tb_blit_scene.v`'s scene gained a 13th command — a second `OP_CBLIT` reading the *same* source
bytes as command 12's row 0 (so any regression in command 12's own reindex=0 default would show up too) but with
`blt_reindex=32`, landing on a separate CLUT bank preloaded with distinct values; `tools/host/blit_reference.py`'s
`cblit()` gained a `reindex` parameter (`idx = ((s & 0xFF) + reindex) & 0xFF`, the same 8-bit wrap as the RTL); new
mutation hook `BUG_IGNORE_REINDEX` (forces the CLUT read address to the raw index always) confirmed caught by the
pixel-diff (4 mismatches, exactly the reindexed command's own words). `make rtl-lint`/`test-rtl`/`test-host` all
pass, 0 failures, zero regression on every existing opcode/mutation case (6 mutation hooks for this file now, all
independently caught).

**Not done:** no Quartus fit (this is RTL/sim-only, matching B8 step 1's own "prove correctness before spending a
Quartus cycle" convention); no firmware register defines or a real shadow/highlight palette baked by
`meter_thumbs.h`'s tooling — the actual "near-free dimmed/selected UI state" product win needs both, and is its
own follow-up once B8's CLUT mechanism itself has a hardware timing fit that includes this addition (the next
full G3+blit re-fit should bundle B9 in, the same way B-101/B-132 bundled the running set of Tier 1/2 features
rather than fitting each in isolation).

**What the open RTL question resolved to:** rather than pipeline the CLUT lookup into the shared `A_COPYRD`
burst path (the option this section originally weighed, and the one that would have touched `OP_COPY`/`OP_BLIT`'s
own state), `OP_CBLIT` got its own two-state path (`A_CBLIT_RD`/`A_CBLIT_WAIT`, new `astate` values 9/10),
structurally identical to `OP_SBLIT`'s existing one-word-per-transaction shape — `A_COPYRD` itself was never
touched. This is a deliberately slower-per-pixel design (one full read transaction per pixel, like `SBLIT`, not
one burst per row like `BLIT`/`COPY`) traded for zero risk to the already-hardware-verified shared path; a
future step could burst-read a whole row's indices and pipeline the CLUT lookups if this trade turns out to
matter in practice, once there's a real workload to measure it against.

**Not done:** no Quartus fit (RTL/sim only, per this entry's own scope); no firmware register defines or
`set_draw_thumb()` change (the actual "one command per thumbnail" win needs both, and is its own follow-up);
step 2 (B10's RLE decode) untouched, waiting on step 1 to prove out on real hardware first.

### B10 (hardware RLE source blit) — analysed 2026-09-24, design only, not built; value re-assessed

**Value finding, before any design: B10's original motivation is already substantially met.** The Tier 2
table entry above was written before B8's own firmware integration (B-160) shipped. Read the actual code
(`fw/settingsui.inc`'s `set_draw_thumb()`/`set_thumb_flat_build()`, not assumed): B8 step 1 already avoids
per-draw RLE decode entirely — it expands `meter_thumb_rle` into flat 56x32 palette-index buffers **once**,
lazily, the first time Settings opens (~16 ms for all 11 thumbnails combined, per the existing A-094
uncached-access measurement this code's own comment cites), and every redraw after that is already one
`fb_clut_load()` + one `fb_cblit()` — already hardware-accelerated, already one command per thumbnail. What
B10 would add on top of the *shipped, hardware-proven* B8 step 1 is narrower than the table entry implies:
(a) eliminating that one-time ~16 ms build (paid once per session, only if/when Settings is ever opened,
not every boot), and (b) freeing the ~38.5 KB of SDRAM the 11 flat buffers occupy — SDRAM is not the scarce
resource on this device (M10K on-chip block RAM is, per section 4's ledger, and B10 does not touch that
either). Kept as a design-only entry for completeness (owner's explicit choice, 2026-09-24) rather than
built, given this materially smaller payoff than Tier 2's own table implied when it was written.

**Exact source format** (`fw/meter_thumbs.h`, generated by `tools/gen_meter_thumbs.py`, not assumed):
`meter_thumb_rle[]` is one byte per run — `byte = (idx << 5) | (run_length - 1)`, `idx` 0-7 (3 bits),
`run_length` 1-32 (5 bits) — exactly the format section 5's original B10 note already predicted, packed two
bytes per 16-bit SDRAM word (even byte first, matching every other opcode's "one word read = one unit of
source data" convention, just two RLE units per word here instead of one pixel). 11 thumbnails, one
`meter_thumb_off[]` byte-offset table already exists in firmware to delimit them (reusable as-is: firmware
would pass the same offsets it already uses for `set_draw_thumb_soft()`/`set_thumb_flat_build()`).

**Design, refined 2026-09-25 after B11 shipped (`cmd_op` is already 4 bits — B10 is `OP_RLEBLIT = 4'd9`,
the next free value, no further widening needed).** Reuses B8's CLUT (`clut_q`) for the colour lookup
exactly as `OP_CBLIT` does, and — the actual insight here — a *decoded run* is just a solid-colour
horizontal span, which is precisely what `OP_RECT`'s existing **constant-data burst** write already does
(`p0_wr_stream = 0`, `p0_data` latched once per burst) — cheaper than `OP_CBLIT`'s own one-word-per-pixel
read path, not a new write mechanism. So each run becomes one RECT-shape burst of `clut_q[idx]`, for `seg`
words, where `seg` is the run clamped to however many columns remain before the destination width wraps
(mirroring `set_thumb_flat_build()`'s own `seg = width - col` split exactly) — i.e. this generalises the
SAME "queued chain of RECT-shape bursts" shape B11 already built. A run longer than one row's remaining
width chains multiple burst segments, same as the software version's own inner `while (left)` loop.

**Real gap found while starting the RTL, not previously flagged: the RLE byte stream needs staging into
SDRAM first, which is the same class of one-time cost B10 exists to avoid.** `meter_thumb_rle[]` lives in
cold PSRAM/on-chip data today, not the framebuffer's SDRAM chip — the draw engine's `p0_addr` port only
ever reaches that one physical SDRAM, the same one pixel data lives in, never PSRAM or on-chip RAM
directly. For `OP_RLEBLIT` to read the RLE bytes via `p0_addr` (the only way any opcode reads a "source"),
firmware must copy `meter_thumb_rle` into a small SDRAM staging area once per session (the same "prove it
once, reuse it" shape `set_thumb_flat_build()` already has) — smaller than the current flat-buffer expansion
(raw RLE bytes, not fully-decoded palette indices, so still meaningfully less data and less one-time cost),
but not the *zero*-staging design the Tier 2 table's original framing implied. This does not remove B10's
value (the ~38.5 KB SDRAM saving and the smaller one-time cost both still hold), but it is a real correction
to record before anyone assumes this opcode needs no firmware-side preparation at all.

**Real conflict found while starting the RTL: B10 and `OP_CBLIT` cannot share `clut_raddr` unmodified.**
`clut_raddr` (`mp3_fb.sv`) is deliberately **combinational off `p0_q[7:0]`** — the just-arrived SDRAM word,
valid only in the exact cycle `A_CBLIT_RD` captures a fresh source read, timed so `clut_q`'s registered
update lands correctly one cycle later in `A_CBLIT_WAIT` (B-148's own hard-won fix: a registered `clut_raddr`
put the answer a cycle late). B10's palette index does NOT come from a live `p0_q` at the moment it is
needed — it comes from a byte already sitting in a **register** (decoded out of a previously-fetched source
word, `rle_idx` below), stable for as long as needed, not a one-cycle-only value. The two addressing sources
must be muxed (`clut_raddr = rle_mode ? (rle_idx + reindex) : (p0_q[7:0] + reindex)`, safe since `OP_CBLIT`
and `OP_RLEBLIT` are never in flight together, the same "never simultaneous" precedent `OP_CHAR`/`OP_SBLIT`
already share), and — because `rle_idx` becomes valid on a REGISTER edge, not combinationally in the same
cycle a fresh word arrives the way `p0_q` does — B10 needs its own explicit two-cycle wait after decoding a
byte before `clut_q` can be trusted (one cycle for `rle_idx` itself to settle, one for `clut_q`'s own
registered read to catch up), mirroring `A_CBLIT_RD`/`A_CBLIT_WAIT`'s two-state shape but for a different
reason (waiting on a decode+lookup pipeline, not a fresh SDRAM read).

**State needed, refined to avoid a live multiply:** the original note's "total pixel count (`w x h`)" would
need a real `w*h` multiply to bound the loop — exactly the kind of live arithmetic this whole phase has
learned to avoid (B-109/B-111/B-150/B-157). Reuse `char_w`/`char_rows_left`/`char_rows_left_nz` **unchanged**
instead — the same row-count-register-decremented-once-per-row shape `OP_COPY`/`OP_BLIT`/`OP_CBLIT` already
use, needing no multiply at all: `char_w` bounds each row's column wrap, `char_rows_left` counts rows down,
exactly like every other multi-row opcode. Full register list: `rle_word` (16 bits, the current fetched
word), `rle_hi` (which byte is next to consume), `rle_idx` (3 bits, current run's palette index), `rle_run_left`
(6 bits, current run's remaining pixel count, 0 = "need to decode the next byte"), `rle_col` (9 bits, output
column within the row, reusing `blit_dst_addr`'s own per-row sticky-stride step for the row-to-row advance,
not pinned to the flat-buffer convention), and a 1-bit CLUT-settle counter for the two-cycle wait above.
Source word advances (one new 1-word SDRAM read, same shape as `A_CBLIT_RD`) every time both bytes of the
current word are consumed — every *other* run boundary, not every run, since two runs share one word.

**What is genuinely new versus every other opcode built so far:** every existing opcode advances through
source/destination data at a *fixed, address-computable* rate (one word per pixel, or one word per row).
B10's source-side rate is *data-dependent* — how many destination pixels one source word covers depends on
the two run lengths it decodes to, which can only be known after reading it. Unlike B11's LUT-driven
skip-ahead, there is no "skip this segment" case here — every decoded run always produces at least one
output word, so the state machine is simpler in that one respect than B11's, but the data-dependent
consumption rate itself has no precedent in this codebase to copy from.

**Verification plan for later:** an independent Python reference in `blit_reference.py`, a pixel-diff scene
command in `tb_blit_scene.v` (matching the "not a copy of the RTL" discipline every opcode since B1 has
used), and a mutation hook — `BUG_RLE_IGNORE_RUN`, forcing every decoded run to length 1 — needs checking via
**source-word over-read/under-read**, not just output pixel content, since a run-length bug can still
produce byte-identical pixels while consuming the wrong number of source bytes (a detail no other opcode's
mutation test has needed, worth designing carefully rather than copying one of the existing hooks blind).

**Not done:** no RTL, no MMIO, no testbench, no firmware change — design-only (owner's explicit choice,
2026-09-25, to design now and build/verify properly in a dedicated later pass rather than in the same
sitting as B11), and still lower priority than the table originally implied, now doubly so given the
SDRAM-staging gap found above reduces the "zero firmware preparation" framing the original note implied.

### B11 (hardware rounded-rect) — analysed 2026-09-24, design only, not built

**Status update, 2026-09-25 (B-211): the Quartus fit for this design FAILED timing.** RTL/sim below is
still correct and unchanged; the fit combining it with the proven G3+blit-engine macro set (the same
combination B-134 already closed cleanly) came back **-2.366 ns worst-case setup slack**, contrary to
this section's own expectation (below) that B11 would avoid B8's timing fight by never touching the
shared `glyphbuf` write-data network. It does avoid that network — but `quartus_sta -t report_timing`
found a *different*, genuinely new combinational chain inside B11's own corner sequencer: `rrect_row`
(register) -> `rrect_dy` (subtract) -> `rc_cut_lut` read -> two wide address adders (`rrect_seg_addr`)
-> straight into `rect_addr`, all evaluated in one cycle (8 logic levels, 12.1 ns against a ~10 ns
period). This is the same *shape* of bug as B-111 (`OP_BAR`) and B-114 (`OP_SBLIT`/`OP_CHAR`) — an
address computed by a multi-stage arithmetic chain feeding a register combinationally, needing exactly
their proven fix: register `rrect_cut`/`rrect_seg_addr` one cycle ahead of when `rect_addr` consumes
them (computed at each of the three points `rrect_row` changes: dispatch, the `cut==0` skip case, and
row/segment retirement), the same `cmd_mem_rd`-style lookahead those two fixes already used. **Not yet
fixed** — full detail and the exact violating path in `docs/AUDIT_TRAIL.md` B-211.

Read the real target before designing (same discipline B8 used): `fw/player.c`'s `fb_round_rect`/
`fb_round_rect_on` (lines ~2234-2276). Two variants exist, only one is in scope here.

**`fb_round_rect_on(x, y, w, h, r, color, bg)` — the one this targets.** One full `w x h` fill in
`color`, then for each of `r` rows: an integer quarter-circle search (`while (inner+1)^2 + dy^2 <= r^2:
inner++`) gives that row's corner inset `cut`; if `cut != 0`, four 1-row rects punch `bg` into the top-
left/top-right/bottom-left/bottom-right corners. Up to `1 + 4r` separate `cmd_push`es today — for the
panel border (`r=8`) that is up to 33 commands for one shape, the same per-frame-command-count problem
B6 (BAR) already solved for meters. **`fb_round_rect(x, y, w, h, r, color)` (no `bg`) is a different,
harder problem — out of scope for this design.** It samples `ui_grad_at()` (the background gradient) at
each corner row instead of a flat colour, so replicating it in RTL means porting the gradient LUT too;
it has exactly 2 call sites (the panel border) versus `fb_round_rect_on`'s 6 (every selected list row,
in `library.inc`/`settingsui.inc`/`player.c`'s playlist), so the flat-`bg` variant alone captures almost
all of the actual per-frame command-count win.

**Real blocker found, not assumed: `cmd_op` is already full.** All 3 bits (values 0-7) are taken —
`OP_CBLIT` (B8) is explicitly documented as "the last value the existing 3-bit field has room for". A
new opcode needs `cmd_op` widened to 4 bits. This is a smaller change than it sounds: the FIFO's 88-bit
command word already reserves `5'd0` as unused padding (`{cmd_op, cmd_addr, cmd_fg, cmd_bg, cmd_w,
cmd_h, cmd_glyph, cmd_sx, cmd_sy, 5'd0}` in `mp3_fb.sv`'s FIFO-write logic), so taking one padding bit
for `cmd_op` costs nothing in FIFO width — but it does touch `mp3_soc.v`'s `R_FB_GO` decode
(`fb_cmd_op <= dDAT_MOSI[2:0]`) and every `cmd_op`-width declaration in both files and `core_game.vh`,
a shared, hardware-verified path every existing opcode depends on. Purely additive (existing values 0-7
keep their exact meaning) and nowhere near the `glyphbuf` write-select network B8's own timing fight was
about, but it is not a zero-risk change and deserves its own careful re-verification of all 8 existing
opcodes before trusting a re-fit, not just the new one.

**Do NOT compute the quarter-circle search live in RTL.** This session's own history — B-109 (`glyphbuf`
write-data arithmetic), B-111/B-114 (BAR/SBLIT/CHAR retiming), B-150/B-157 (`OP_CBLIT` exposing an
unrelated marginal path) — is a long, consistent lesson that single-cycle combinational arithmetic chains
on this device run out of margin fast, and the firmware's search is a *data-dependent iterative* multiply-
compare loop (unbounded in the sense that its depth depends on `r`, not a fixed small function like
`cov_weight` or `scale_nd`). The right analogue is **B8's own choice**: offload the computation to
firmware (which already computes it correctly, is the reference implementation, and never has to run in
one clock edge) and give RTL a small **lookup table** to read from instead of logic to compute. Concretely:
a new sticky table, own MMIO pair (own register pair, not folded into `R_BLT_IDX`/`DATA` — same reasoning
B8's own CLUT-vs-sticky-field split gives: a bulk table load is different write traffic from five mostly-
static per-command fields), e.g. `R_RC_IDX`/`R_RC_DATA`: 16 entries x 5 bits (`cut(dy)` for `dy` 0..15,
covering every radius this UI actually uses — 3, 4, 5, 8 — many times over; 16 x 5 = 80 bits, plain flops,
no M10K, matching the table's own "0 M10K" budget), loaded once whenever the corner radius set changes
(rare — a handful of fixed radii across the whole UI), not once per command.

**Field reuse, following B6's own precedent exactly:** `cmd_glyph` (7 bits, "otherwise unused outside
CHAR", already reused by BAR for the lit-row count) carries the radius `r` (max 127, the UI's actual max
is 8). `cmd_fg`/`cmd_bg` are already the fill/corner colours — no new command fields needed at all beyond
the wider opcode.

**Dispatch shape — generalises BAR's `bar2_pending` from one chained segment to a counted sequence.**
`OP_RRECT` dispatch: (1) arm the exact same `rect_active` full-`w`-x-`h` fill in `cmd_fg` `OP_RECT`
already does — no new logic there at all; (2) queue a corner pass behind it: a row counter `rr_row`
(0..r-1) and a 2-bit segment counter `rr_seg` (0..3, selecting TL/TR/BL/BR) drive a small sequencer that
fires when the main fill's last row retires (same trigger BAR's `bar2_pending` uses), each step computing
one 1-row rect from `(x, y, w, h, rr_row, cut_lut[rr_row])` and re-arming `rect_active` with `bg` — same
single-row burst write A_WRWAIT already does for every other opcode, so no new SDRAM-facing machinery,
only new *sequencing* logic. **Rows where `cut_lut[rr_row] == 0` must be skipped without emitting any
segment** (the firmware's own `if (!cut) continue;`) — a combinational read of `cut_lut[rr_row]` at the
point the sequencer would otherwise arm a segment, advancing `rr_row` instead when it reads 0. Bounded to
at most `r` (<=127) skip cycles, and skipping only ever happens between bursts (at `A_IDLE`-equivalent
dispatch points), so it cannot delay a pending scanline fill the way anything mid-burst could.

**Verification plan, before any of this is built:** extend `tb_mp3_fb.v` with a direct port of
`fb_round_rect_on`'s own algorithm as the check (the same "independent reference, not a copy of the RTL"
discipline `tools/host/blit_reference.py` already established for B1-B9) rather than hand-computing a
handful of cases; a mutation hook forcing `cut_lut` reads to always return 0 (degenerates to a square rect
— must be caught) is the natural first one, mirroring `BUG_CBLIT_NO_LOOKUP`'s own "prove the mechanism is
actually exercised" role. Re-run the full existing `mp3_fb.sv` suite afterward specifically because of the
`cmd_op` width change, even though nothing about it should logically affect opcodes 0-7.

**Not done:** no RTL, no MMIO register, no testbench changes — this is the design pass only, per the
owner's explicit choice (2026-09-24) to keep this session's remaining Tier 2 work lower-risk after B9,
rather than build a second multi-part change (widened shared opcode field + new sticky table + new chained
sequencer) in the same session without a chance to re-verify each piece separately.

### Meter redesign audit and proposed bar-family opcodes (2026-09-25) — analysed, not built

**Why this exists.** B-208 (`docs/AUDIT_TRAIL.md`) surveyed the 10 visualizer modes other than `VIZ_BARS`
against `OP_BAR`'s exact shape and found none of them match it — each was independently designed (different
sessions, different eras of the firmware, well before the blit engine existed) with its own bespoke draw
sequence. The owner asked two follow-on questions: what would we change about how these are *designed*, not
just retrofitted, to fit the blit engine better; and what other hardware accelerator primitives — beyond
patching today's 10 modes — are worth having on the books, including for meters the owner designs later.

**The core insight.** Every "bar-shaped" mode (`BARS`, `WATER`, `SCROLL`, `MIRROR`, `LEVELS`, and `LED` in
spirit) is really the same idea — *a column (or row) divided into a lit region and the rest* — wearing five
different, independently-reinvented costumes: different anchor (bottom-edge, top-edge, centred, left-edge),
different background (flat colour vs. a per-row gradient), and different fill discipline (continuous vs.
gapped/segmented). `OP_BAR` only covers one point in that space (bottom-anchored, flat bg, continuous). The
redesign lesson, independent of any new RTL: **future meters should be designed as configurations of one
small parametric "column primitive" family, not as bespoke draw code each time** — matching the same
discipline `docs/PHASE_F_SPEC.md` section 9 already applied to the MMIO register file itself (sticky state +
small per-command fields, not a new register per feature). Concretely, that means a firmware-side
`fb_bar2(x, y, w, h, anchor, lit, fg, bg_mode)`-shaped API from day one for any new meter, so the *visual*
design and the *hardware mapping* are decided together instead of the hardware being reverse-fitted onto a
shape chosen for other reasons (as happened here). Three specific, low-effort redesign levers, no new RTL:

- **`VIZ_LEVELS` can be made `OP_BAR`-exact for free by reorienting it, not by changing the hardware.**
  Its horizontal left-lit/right-unlit bars are horizontal only because that is what "L/R levels side by
  side" happened to look like when it was written. A vertical pair of dual channel bars (the classic
  vertical peak-meter layout, distinct from `BARS`'s 36-column moving history) is `OP_BAR`'s exact shape —
  bottom-anchored, flat bg, continuous — with zero RTL cost. This is the cheapest possible win in the whole
  survey and worth doing on its own regardless of any new opcode.
- **`WATER`/`SCROLL`'s gradient background is a design choice, not a technical requirement.** Both are
  already only 3-4 hardware ops per updated column (not a real bottleneck today), so there's no efficiency
  case for touching them — but if a future redesign wants them cheaper still, dropping the per-row gradient
  for a flat bg would let them use `OP_BAR` directly, at the cost of the visual richness the gradient gives.
  Recorded as a real tradeoff to make deliberately, not a recommendation either way.
- **`LED`'s gapped/delta discipline is already close to the right hardware shape for a *segmented* bar** —
  see B15 below — but is a case where a purpose-built opcode, not a redesign, is the right lever, because
  the gaps and delta-only redraw are the point of the visual, not an accident of how it was written.

**Proposed new opcodes**, in the same B-numbered Tier scheme as B1-B11, none built yet:

| ID | Feature | M10K | Notes |
|---|---|---|---|
| **B12** | **`OP_HBAR` — column-split bar (axis-mirrored `OP_BAR`)** | 0 | Same convention as `OP_BAR` but splits along **columns** instead of rows: `w` total, `lit` of them from one edge. Directly solves `VIZ_LEVELS` if it stays horizontal (see redesign note above — a vertical reorientation makes this opcode unnecessary for that one case, but a generic "fill from the side" primitive is broadly useful for any future horizontal gauge, e.g. a progress bar, a pitch/tempo slider, a battery icon). Expected near-mechanical: `OP_BAR`'s row-counter compare becomes a column-counter compare against the *burst* index instead of the *row* index; reuses the same `bar2_pending`-style two-segment sequencer B6 already built, just walking the other axis. Real risk, not yet assessed: `OP_BAR`'s row-wise split is cheap because each row is already a natural burst unit (one SDRAM burst per row); a column-wise split works *within* a burst, which may need a different, less trivial mechanism (masking columns inside one row's burst rather than choosing which whole rows to write) — this needs to be checked against the RTL, not assumed free, before treating it as B6-equivalent effort. |
| **B13** | **Gradient-fill bar (row-indexed background via B8's CLUT) — HELD, 2026-09-25 (`docs/HELIOS_SPEC.md` section 6)** | 0 (reuses B8's CLUT) | Would extend `OP_BAR` so `bg` can optionally read a per-row colour from B8's existing 256-entry CLUT instead of one flat register. The CLUT read-port contention risk was re-assessed as a timing-margin question, not functional (Talos dispatches one command at a time). **But `OP_BAR`'s actual RTL, read for the first time while scoping this build, is not a row-by-row iterator at all** — it is two stacked solid-colour rectangle BURSTS (unlit segment, lit segment), each capable of covering many rows in one SDRAM transaction, which is exactly why it is cheap. A genuinely per-row gradient needs one burst PER ROW instead, giving up that whole-segment efficiency — for a tall panel (the stated `WATER`/`SCROLL` target), that could mean dozens of transactions instead of two, potentially *worse* than the 3-op software sequence it was meant to replace. Held pending a real cost/benefit re-scope once Helios/Talos otherwise ships. |
| **B14** | **Floating/offset bar (lit region not anchored to an edge)** | 0 | Generalises `OP_BAR`'s fixed `[h-lit, h)` lit range to an arbitrary `[top, top+lit)` window within the `h`-row span, unlit both above and below. Solves `MIRROR` (a bar centred on the meter's mid-line) directly, and is the natural primitive for any future centred gauge (a bipolar level meter, a pan/balance indicator). Cheap in registers (one more field, `top`, alongside `lit`) but the *dispatch* logic changes from a single edge compare to two boundary compares — closer in shape to B11's already-built two-boundary corner sequencer than to B6's one-boundary original, so B11's as-built RTL is the right reference to generalise from, not B6's. |
| **B15** | **Segmented/gapped bar (LED-style stepped fill)** | 0 | A repeating `(segment, gap)` pitch along the fill axis instead of a continuous run — the real shape `LED` already draws by hand. Useful beyond `LED` for any stepped/retro meter aesthetic (the kind of look the owner has said they intend to design themselves later). The real open question, flagged honestly: `LED`'s current firmware also does *delta-only* partial-column redraw (only rows whose lit/unlit state actually changed get touched), which a single whole-column `OP_BAR`-family call — segmented or not — cannot reproduce, since one command always redraws its whole span. Whether the single-command win (fewer commands, more pixels touched per change) beats the current many-small-command-but-fewer-total-pixels approach is a real measurement question (the same class as B-116's DSP-congestion finding), not something to assume either way — worth a real before/after SDRAM-busy-percentage comparison (reusing B7's counter and the B-127 blit-storm Check discipline) before committing to it. |
| **B16** | **Point/dot-list command (batched scatter fill)** — bigger lift, Tier 3/4 | TBD | `SCOPE`, `DOTS`, and `EYE`'s glow pool are all scatter/point patterns (many independent 1x1-2x2 fills), where the real cost is likely **CPU-side dispatch overhead** (one `fb_wait()` + register-write sequence per point, up to ~200/frame for `SCOPE`'s full history) more than SDRAM traffic itself. A command that reads a short list of `(dx, dy, colour)` offsets from a source buffer and issues each as its own small fill without a CPU round-trip per point would help this class broadly, including whatever scatter/trace-style meters the owner designs later. This is architecturally bigger than B1-B15 — it needs a source-list read path B1-B11 don't have (closer to B10's shelved RLE-source-blit problem than to any bar variant) — and should stay Tier 3/4 until there's a concrete design, not folded into this list's cost estimates. |
| **B17** | **Hardware line draw (Bresenham, single or N-px wide)** — Tier 3/4 | TBD | `WAVE` already does the "span from previous sample to this one" trick per column by hand (a cheap column-local approximation, not a real line); `SCOPE` connects its newest trace with individual midpoint dots rather than real segments, explicitly to avoid tripling the command count. A real connected-line primitive would serve any future line-graph or connected-trace style meter more directly than B16's dot-list would, and is a classic 2D-blitter feature many chips of this era had. More speculative than B12-B15 (no concrete target in today's firmware forces it the way `VIZ_LEVELS` forces B12), so it is recorded as an idea worth having on the books rather than scoped in detail here. |

**Priority read, for an owner decision, not a recommendation to build all of it:** B12 and B14 are the two
with a concrete, named target in today's firmware (`LEVELS`, `MIRROR`) and the clearest RTL reuse story (B6
and B11 respectively); B13 is the one with the broadest reuse beyond meters but the least-checked shared-CLUT
risk; B15 has an open measurement question before it is even known to be a win; B16/B17 are real ideas but
belong to a later, more speculative pass. None of B12-B17 has any RTL, MMIO, or testbench work started —
this is the design-and-options pass only, matching the discipline B9/B10/B11 already established (design
first, verify the real cost/win before committing a Quartus slot).

### Tier 3 — after this phase (see section 13)

2.5D primitives, an overlay compositing layer, double buffering.

## 6. Prior art studied, and what may actually be used

**Correction, 2026-09-22:** an earlier draft of this section claimed the repository had no LICENSE file. That
was wrong — the claim came from a shell-globbing artifact, not from the tree. **`LICENSE` exists, is MIT, and
has been tracked since the first commit**, carrying two copyright lines (HarpMudd, and alfatreze for the Tau
modifications). `NOTICE.md` and the README's Credits section already document the third-party boundaries
thoroughly and accurately.

That settles the constraint rather than removing it, and it settles it in the direction that matters here:
**because Tau's own code is MIT, copyleft RTL cannot be copied in without relicensing the whole project.** So
the licence column below is a hard constraint — GPLv2/GPLv3 sources are study-and-reimplement only, not
copy-and-adapt. Techniques are decades-old silicon and safe to reimplement independently; code is not.

**Decision: keep MIT.** It matches the ecosystem this project draws from and gives back to (agg23's utils,
VexRiscv and PocketQuake are all MIT), the two real obligations below are per-file and already quarantined
under `third_party/`, and changing it would need HarpMudd's agreement anyway since their copyright line is in
it. No action required — this is recorded so the question is not reopened without reason.

**Licence hygiene checklist** (small, not urgent, none of it blocks Phase F):

1. **Verify VexRiscv's licence properly.** The README asserts MIT but the generated `src/fpga/rtl/VexRiscv_Full.v`
   carries no licence header of its own — the claim is credit-only, not in-tree evidence. Check it against the
   upstream VexRiscv/SpinalHDL project and record the result in `NOTICE.md`. **Not done here** because it needs
   an external source, and asserting a licence without verifying it is exactly the failure mode this checklist
   exists to prevent.
2. **Add a provenance line for `assets/branding/` and `assets/ui/tau-loading-source.jpg`** in `NOTICE.md`.
   Currently it is only implicit that these are owner-authored. **Not done here** — only the owner knows.
3. **Optional: SPDX identifiers** (`// SPDX-License-Identifier: MIT`) on the project's own source files. Cheap,
   machine-readable, makes any future audit trivial. A bulk edit across many files, so left as a deliberate
   choice rather than done in passing.

Two in-tree obligations are already correctly recorded in `NOTICE.md` and stay relevant to anything new:
**Helix MP3 is RPSL 1.0** (per-file source-disclosure, vendored unmodified in `third_party/`, not relicensed by
Tau's MIT) and **Inter is SIL OFL 1.1**, with the generated `font_rom.v` treated as a derivative font work under
the same terms — worth remembering in section 3, since moving the font ROM to PSRAM changes how it is stored
but not what licence it carries. Separately, `src/fpga/apf/` is under **Analogue's proprietary EULA**, not an
open-source licence, which the existing don't-edit rule already handles.

| Implementation | Licence | Verdict |
|---|---|---|
| **Minimig / Minimig-AGA Amiga blitter** (`agnus_blitter.v`) | **GPLv3** | **Study, reimplement.** Two barrel shifters, a 256-minterm generator from `bltcon0`, BLTAFWM/BLTALWM first/last-word masks, fill-mode carry latch, ~13-state FSM. The single best architectural reference for B3. The *technique* is 1985 silicon and safe to reimplement independently; the *code* would pull GPLv3 in. |
| **PSX_MiSTer GPU** | **GPLv2** | **Study.** The semi-transparency ALU is the direct reference for B5's shift-add path. |
| **Atari ST blitter** (AtariST_MiSTer) | **none stated** | **Study only — do not copy.** No LICENSE file found; treat as all-rights-reserved. Architecture mirrors the Amiga's anyway. |
| **Saturn VDP1** (Saturn_MiSTer) | **none stated** | **Study only — do not copy.** Framebuffer-based quad renderer; less relevant than expected. |
| **Neo Geo LSPC** | docs only, no clean RTL | **Technique only.** Two things worth taking: a **ping-pong double line buffer** scanline renderer (we already have the line buffer), and **vertical shrink via a lookup table** rather than an accumulator — a real alternative to Bresenham if table-driven zoom ever fits better. |
| **Genesis/MD VDP** | study | **Technique only.** Colour-key compositing at the line buffer (B2) and the shadow/highlight palette trick (B9). |
| **3DO Cel Engine** | vendor docs, no RTL | **Model only.** The DUP/PDC decompressor-ahead-of-pixel-pipeline split, for B10. |
| **agg23/analogue-pocket-utils** | **MIT** | **Usable**, and already used for PSRAM/CDC/I2S plumbing — but it contains **no 2D/draw IP**. Nothing to borrow for this. |
| **PocketQuake** | **MIT** wrapper | **Directly relevant structurally.** Same chip class, same problem: partition memory so a latency-sensitive buffer (its z-buffer, in a dedicated SRAM) stays off the bus carrying bulk assets, with an accelerator absorbing the hot per-pixel loop, alongside real-time audio. Read it for the partitioning, not the rasteriser. |

## 7. Spectrum — a hardware filter bank, not an FFT

**Status 2026-09-25 (B-263): built in RTL and simulated, not yet fitted or run on hardware.** `src/fpga/core/tau_spec_bank.sv` is the firmware cascade below, bit for bit (sequential FSM, no multiplier, logic registers so no M10K), fed from the PCM FIFO's sample strobe; it publishes 16 window means (1024 samples) at MMIO 0xDC/0xE0/0xE4. `sim/tb_tau_spec_bank.v` compares it against a behavioural model of the firmware loop over four windows and two mutants are caught. Firmware uses it when `R_SPEC_ST` bit 0 is set (software cascade stays as the fallback); Info > SPECTRUM shows HW/SOFTWARE. Gain table, log scale and ballistics stay in firmware.

Per D1. What ships today is an **octave cascade of one-pole low-passes**, gated to run only while its meter is
on screen, at ~1.5% CPU [SRC]. The firmware comment is explicit that this is "NOT an FFT, and not a bank of
parallel band-passes — both are far too expensive here", and that a real filter bank was the one addition that
could reintroduce audio glitches.

**The plan is to move that same structure into RTL**, not to replace it with something grander:

- **Cost: ~0 M10K.** Per band, a Goertzel or one-pole IIR needs two state registers and 2 MACs per sample; a
  single DSP-based MAC time-shares across all 16-32 bands inside one clock at 48 kHz [EXT]. State lives in
  ALM/MLAB, exactly as the EQ's does.
- **It removes the reason the feature was constrained.** At 0% CPU it can run continuously instead of only
  while its meter is visible, and it stops competing with the budget that keeps the decoder fed — which is what
  both the upstream roadmap and the firmware comment identified as the risk.
- **Precision:** for a display-only path, 10-12 bits is ample [EXT] — far below the 36-bit Q20.16 the EQ needed
  after 32-bit caused limit-cycle rumble [SRC]. Different problem, different budget.
- **If linear bins are ever genuinely wanted**, the cheap route is a streaming **R2SDF** FFT whose delay-feedback
  stages are mostly shallow enough to live in MLAB, with **CORDIC-generated twiddles** instead of a ROM —
  reportedly near-zero block RAM at these sizes [EXT]. Parked, section 13.

## 8. 3D — the verdict, and what to build instead

Dropped per D2. Recorded so it is not re-derived:

- Full-screen 16-bit Z at 400x360 = ~281 M10K; 8-bit Z = ~141. The device has 308 **in total** [EST].
- Z in SDRAM is possible but read-modify-write per pixel multiplies framebuffer bandwidth on a bus already
  shared with scanout, audio and the CPU window — and Phase H already projects scanout alone rising to 35-45%
  of SDRAM cycles at 720 [EST].
- Tile-based rendering is the only feasible form: ~8 blocks tile Z + ~8 tile colour + bin lists + texture cache
  ≈ **20-25 blocks**, plus substantial ALM for the rasteriser and a real threat to the **zero-late-underrun**
  record that every stress run has held so far.
- PocketQuake proves 3D *is* possible on this chip alongside real-time audio — but it spends a dedicated
  external SRAM on the z-buffer to do it, which this design does not have free.

**What the 3D ambition is actually for is visualiser eye-candy**, and that is reachable without any of the
above. Tier 3 adds, at ~2-4 blocks total: **per-row scaled blit** (perspective floors and tunnels), **rotated
blit**, and an **affine texture-mapped quad**. This is the MilkDrop/Winamp lineage; none of it needed a 3D GPU
either. If true 3D is still wanted after the 192 KB shrink frees 64 blocks, it can be revisited honestly rather
than squeezed in now.

## 9. MMIO — decided: split engine state from per-command fields

Only **0xBC-0xFC is free** (17 words), in a page where **8 offset bits are decoded** (0x100+ aliases) [SRC].
The blit engine alone wants source base, destination base, both strides, alpha level and mode, colour key,
palette select and per-axis scale factors; the spectrum bank and any future kernel want their own. One
register per parameter exhausts the page.

**Decision: do not add a register per parameter, and do not widen the FIFO word either.** Widening `cmd_mem`
from its inferred 82 bits to the ~200 a full descriptor needs would take it from 3 M10K to **~7** at 256 deep
[EST] — eating most of what the MLAB migration frees. Instead, split the two kinds of parameter apart:

- **Sticky engine state** — written rarely, lives in flops, never enters the FIFO: source/destination base and
  stride, colour key, alpha mode and level, palette select, per-axis scale factors.
- **Per-command FIFO word** — varies every operation: opcode, x, y, w, h, source offset. Stays near the
  current width, so the FIFO does not grow (and could be narrowed).

**Three MMIO registers total, regardless of how many state fields exist:**

| Register | Behaviour |
|---|---|
| `R_BLT_IDX` | Write selects a state field by index; auto-increments on each `R_BLT_DATA` write. |
| `R_BLT_DATA` | Writes the selected state field, then auto-increments the index. A burst of stores loads a whole state block with one index write. |
| `R_BLT_GO` | Opcode + flags + the per-command fields; latches and enqueues. Same trigger pattern as today's `R_FB_GO`. |

That is **3 of the 17 free words for an unbounded number of state fields**, leaving ~14 for the spectrum bank
and any kernel. Precedent: this is exactly the Amiga split — `BLTCON`/`BLTAFWM`/`BLTALWM` are persistent
registers and only the size write triggers the blit [EXT].

**Do this before any Phase F RTL is written.** Retrofitting after the blit engine, the spectrum bank and a
kernel have each claimed registers ad hoc is the expensive version.

## 10. Build plan

**Macros, one per separable piece** — this is what makes a failed build bisect instead of rebuild:

| Macro | Covers |
|---|---|
| `TAU_BLIT` | The new opcodes and the descriptor/state register file (section 9). |
| `TAU_BLIT_BLEND` | The DSP blend and scale pipeline — **kept separate on purpose**: it is the deepest new pipeline and therefore the documented -1.888 ns timing-cliff risk. Droppable without losing the rest of the engine. |
| `TAU_MLAB_MIGRATE` | Section 2. Functionally inert. |
| `TAU_FONT_REPACK` | Section 3 item 1. Functionally inert. |
| `TAU_SDRAM_BUSY` | The busy-cycle counter (B7). |

**Step 1 — synthesis only, no fit (~5-6 min, not ~45). Done 2026-09-22 (B-100); corrected from this
paragraph's original claim.** Ran `TAU_MLAB_MIGRATE` + `TAU_FONT_REPACK` through `quartus_map` (compared
against a same-tree baseline with both macros off) and read the RAM summary. The result is **not** simply "the
block-count drop is the entire result" — that holds for one of the two pieces but not the other:

- **MLAB migration: confirmed at this stage, and this stage is sufficient.** The RAM Summary table's `Type`
  column is a direct report of what Quartus resolved each RAM to, and `glyphbuf` and the `sound_i2s` dcfifo both
  resolved to `MLAB` (baseline: 0 MLAB bits; step 1: 2,176 MLAB bits, and the M10K-pool bit total dropped by
  exactly that much). A `ramstyle`/`lpm_hint` request either resolves to the requested type or it doesn't — the
  SignalTap proof build hit the *doesn't* case (an MLAB request silently fell back to M10K over capacity), so a
  synthesis-stage type check is a real, load-bearing thing to confirm before spending a fit.
- **Font ROM repack: synthesis-only is the wrong tool for this question, and cannot confirm it.** `quartus_map`
  reports each RAM's *declared content size* — 4x 24,320 bits (97,280 total), identical to the original single
  3,040x32 array's 97,280 bits, because it is the same content in different lanes. Whether four 8-bit-wide ROMs
  actually pack into *fewer physical M10K primitives* than one 32-bit-wide ROM (the entire point — going from
  ~74% packing efficiency to something tighter) is decided by the **fitter's block-allocation pass**, which
  synthesis does not run and does not preview. Checked the full `quartus_map` log for any packing/physical-block
  hint; there is none. **This piece's win is genuinely unconfirmed until Step 2's fit.**

So step 1 de-risks the MLAB piece (go ahead with confidence) but does not de-risk the font repack the way this
document originally claimed — that risk transfers to step 2 unchanged. Evidence: `docs/AUDIT_TRAIL.md` B-100.

**Step 2 — one full build, multi-seed**, with everything bundled (the counter is needed to validate the engine,
so they belong together):

1. Blit engine Tier 1 (+ Tier 2 if it fits)
2. MLAB migration
3. Font ROM repack
4. SDRAM busy-cycle counter

**If timing fails:** first bisect is dropping `TAU_BLIT_BLEND`. Keep the inert items on — they change no
behaviour, so any trouble they cause is a packing/routing effect that the multi-seed convention should absorb.

**Timing failed on both seeds (B-107/B-109, 2026-09-23) — but not on the path this bisect assumes.**
`report_timing` shows the violation is entirely inside `glyphbuf`'s existing MLAB write-data arithmetic, the
same path `B-102` already flagged as near-zero-margin *before* the blit engine existed, not on the new
`TAU_BLIT_BLEND` pipeline. Dropping `TAU_BLIT_BLEND` is still the cheapest next experiment (less logic overall
may relieve the congestion pushing this path over), but it is not a proven fix for the actual failing path — see
section 11's corrected row. If it doesn't recover positive slack, the real fix is pipelining that specific
`glyphbuf` write-data path (an extra register stage on the address-to-write-port arithmetic), independent of
the blit engine's own macros.

**Bisect run (B-110, 2026-09-23): recovered almost everything, but the worst path moved.** Both seeds went from
-2.5/-2.6 ns to +0.02/-0.11 ns (seed 2) and +0.05/-0.10 ns (seed 1) — consistent across seeds, so a real
structural gap remains, not noise. The `glyphbuf` violation itself is gone; the new worst case is a *different*
pre-existing path, B6/BAR's `cmd_q` -> `char_fg` clamp+subtract chain (same anti-pattern: combinational logic
off a BRAM-registered value, straight into another register, same cycle). **Fixed by retiming (B-111)**: computed
the same arithmetic off the raw BRAM read `cmd_q` itself registers from, on the same clock edge, instead of
after it — same function, same timing relative to everything else, verified bit-for-bit in simulation. Not yet
re-fit to confirm it closes the gap.

**The full audit (B-112) found this bug shape recurs and should be fixed proactively, not one path at a time.**
`OP_SBLIT`'s dispatch (`sblit_ext` clamp/shift off raw `cmd_q`, registered same-cycle) has the identical
structure to the just-fixed BAR bug and was not caught only because BAR happened to be what Quartus reported as
worst first. `OP_CHAR`'s `char_base` compute is a milder variant. **Recommended before the next fit: apply
B-111's exact retiming technique to `OP_SBLIT` (and optionally `OP_CHAR`) at the same time**, rather than
discover each one via another failed multi-seed build. Separately: the blend write-back into `glyphbuf`
(`A_COPYRD`, `TAU_BLIT_BLEND`-only) lands on the same write port as the original violation — meaning B-110's
"congestion relief" theory may be incomplete; a direct fix (one fewer input to that port's write-data mux) is at
least as plausible and hasn't been distinguished from the congestion theory yet. Full detail: `docs/AUDIT_TRAIL.md`
B-112, `docs/FULL_AUDIT_2026-09-23.md`.

Compare the product-config build against the shipped RBF as B-018 did, noting B-021's finding that shared-RTL
changes make bit-identity unattainable even with macros off — it is a review aid, not a gate.

Seeds: follow the existing rule (multi-seed, pick by the pre-set criterion). Compare the product-config build
against the shipped RBF as B-018 did — noting that B-021 already found shared-RTL changes make bit-identity
unattainable even with macros off, so the comparison is a review aid, not a gate.

## 11. Risks

| Risk | Why it is real here | Mitigation |
|---|---|---|
| **Timing cliff — RESOLVED for the no-blend configuration (B-109..B-117)** | The real multi-seed fit failed setup on both Slow corners, both seeds (-2.5 to -2.9 ns). `report_timing` traced it to `glyphbuf`'s MLAB write-data arithmetic (`Add32~8` -> `Selector222~1` into the write port, B-102's pre-existing near-zero-margin path), not the new `TAU_BLIT_BLEND` pipeline. Dropping `TAU_BLIT_BLEND` (B-110) recovered nearly all of it but exposed a *different* pre-existing path as new worst case: B6/BAR's `cmd_q` -> `char_fg` clamp+subtract chain, fixed by retiming (B-111); `OP_SBLIT`'s dispatch had the identical shape and `OP_CHAR`'s `char_base` a milder variant, both also fixed by retiming (B-114). Blend/`glyphbuf` theory resolved (B-116): congestion relief, not a direct fan-in fix. **The re-fit combining no-blend + B-111 + B-114 (B-117, seed 2) closed cleanly on every corner: Slow 85C +0.727 ns, Slow 0C +0.597 ns, both comfortably positive** — the first build in the whole B-107..B-117 sequence with zero known timing violations. | **Resolved for this configuration.** Confirm with a second seed before final adoption (this session's own convention, though the margin here is large enough that seed variance alone is very unlikely to flip it). `TAU_BLIT_BLEND` itself remains shelved — the `glyphbuf` write-port chain (`Add32~8`/`Selector222~1`) has never been retimed and would need it (or `KB-045`'s `DSP_BLOCK_BALANCING` idea) before blend could safely return. Full detail: `docs/AUDIT_TRAIL.md` B-109..B-117 (final), `docs/FULL_AUDIT_2026-09-23.md`. |
| **The L0 invariant** | Every stress run to date reports **zero late underruns**. It is the strongest quality signal this project has, and a new SDRAM master is exactly what threatens it. Confirmed still true of the full history in the B-112 audit (~59 mentions, one explained early false-positive, no confirmed contention-caused late underrun ever recorded). | The busy-cycle counter (B7, built, RTL-only — **no firmware consumer yet**, confirmed by B-112) is the instrument; the blit-storm Check test (section 12.1, **not built yet**, deliberately deferred per B-101 since no firmware issues blit commands to generate the traffic pattern it would test) is the regression net. Current exposure is low precisely because nothing exercises the blit engine's SDRAM traffic yet — revisit urgency once firmware starts issuing real blit commands during playback, not before. |
| **MLAB Fmax on deep chains** | The 256-deep command FIFO needs ~8-deep MLAB chaining; the chain depth is now confirmed exactly (a Cyclone V MLAB is a fixed 32x20/640-bit block, so 256/32 = 8 chained instances is precisely right, per Intel's Embedded Memory Blocks docs, 2026-09-23), but the **Fmax penalty of chaining them is still undocumented [EST]**. | Do the three easy migrations first; treat the FIFO separately, and consider reducing its depth instead. |
| **`FITTER_EFFORT` is `AUTO FIT`, not `STANDARD FIT` (found 2026-09-23, `KB-048`)** | Auto Fit explicitly stops optimizing once it estimates "good enough" and skips optimizations that affect timing/routability, specifically to save compile time -- confirmed as Tau's actual current qsf setting. Given this project has spent B-107..B-117 chasing sub-nanosecond violations by hand, it's plausible the Fitter itself has been leaving real margin unclaimed the whole time. | Try `STANDARD FIT` on the next timing-marginal build before further manual retiming -- if it closes a gap on its own, it's a strictly better fix (applies automatically to any future marginal path too), at the cost of a build that may run 2x+ longer. Full validation plan: `KB-048`. |
| **RAM shrink trades scarcity** | Heap gap has hit its floor repeatedly as features landed. | Measure the hot set with margin and write down a floor before shrinking. Reversible only by another build. |
| **720 (Phase H) invalidates bandwidth assumptions** | Scanout goes from ~12% to 35-45% of SDRAM cycles [EST]. | Parametrise width/height/stride/base now, as Phase F already requires. |
| **Licence** | Tau's own code is **MIT**, so copyleft RTL cannot be copied in; the best references are GPLv2/GPLv3 (Minimig, PSX_MiSTer) or carry no stated licence at all (AtariST_MiSTer, Saturn_MiSTer). | Reimplement from technique; never copy from a GPL or unlicensed repo. Verify VexRiscv's licence properly — the README asserts MIT but the generated `VexRiscv_Full.v` carries no header. |

## 12. Verification and fail-safe

**Verification, following the pattern this project already uses** (the library loader, cold code and the QR
encoder were all validated against host-side references before hardware):

- **Done, 2026-09-23 (B-125).** `tools/host/blit_reference.py` (a from-scratch Python reimplementation of every
  opcode — RUN/RECT/COPY/BLIT with key+blend/BAR/SBLIT/CHAR with the real gamma-fitted anti-aliasing table,
  CHAR's glyph data parsed from the shipped `font_rom.v` rather than re-rasterised) plus `sim/tb_blit_scene.v`
  (an 11-command scene through the real `cmd_push` interface) and `sim/test_blit_reference.py` (the diff
  driver), wired into `make test-rtl` as `test-rtl-blit-reference`. All 4 existing mutation hooks
  (`BUG_IGNORE_BLIT_STRIDE`, `BUG_IGNORE_KEY`, `BUG_SBLIT_NO_SCALE`, `BUG_BLEND_ALWAYS_SRC`) confirmed caught
  by the pixel-diff, reused rather than reinvented, satisfying the injected-fault requirement below. Two real
  testbench-modelling bugs found and fixed along the way (a keyed pixel re-writes the pre-read destination,
  it doesn't skip the write; the scene needed a non-default sticky stride for `BUG_IGNORE_BLIT_STRIDE` to have
  anything to diverge on) — see `docs/AUDIT_TRAIL.md` B-125 for the full account.
- Put it in `make test-rtl`, with an injected-fault case that must be caught, matching the PSRAM/G3 mutation
  tests. **Done above.**
- The `tools/host` harness already exists and is the natural home for the reference renderer. **Done above.**

**Fail-safe — done, 2026-09-23 (B-126), but not the way this row originally assumed.** Reading the RTL to
build this found that `TAU_BLIT` doesn't actually gate the opcodes at all — `mp3_fb.sv` has no
`` `ifdef TAU_BLIT `` and `mp3_soc.v`'s own comment says the register file exists "regardless of
`BLIT_ENABLE`." There is no bitstream feature bit the way PSRAM's `PS_ID` or cold code's `IF_CFG` provides;
every bitstream from B-103 onward already has the full opcode set unconditionally. The real distinction is
"pre-B-103 bitstream" vs everything since, detected via `fw/blit_probe.inc`'s `BLIT_READY()`: exploits the
one real difference an old bitstream shows — its 2-bit opcode decode silently truncates `OP_BLIT` to
`OP_RUN` — by issuing a 2-row, custom-stride `OP_BLIT` into never-displayed framebuffer padding (columns
400-511) and checking via the CPU's uncached SDRAM window whether the second row actually got written.
Gated behind a new `TAU_BLIT_PROBE` macro (default off, byte-identical product ROM confirmed); nothing
calls `BLIT_READY()` operationally yet since no feature uses the blit engine. The "real BLIT honours a
custom stride" half is indirectly verified by B-125's own scene test; the "old 2-bit decode truncates to
RUN" half rests on B-103's documented claim, not a fresh RTL-in-the-loop test — full detail and the exact
limit of what's verified: `docs/AUDIT_TRAIL.md` B-126.

Firmware ships from the SD card independently of the bitstream, so new-firmware-on-old-bitstream is a real
configuration that has already bitten this project once (the E18 cold-code refusal, which behaved correctly).

**Split:** the blit engine, the spectrum bank and the M10K work are all **shipping** features. Only the
busy-cycle counter's readout and any new Check tests are Diagnostic-Build surface.

### 12.1 The blit-storm Check test — the L0 regression net

The zero-late-underrun record is this project's strongest quality signal, and it has so far been *observed*
rather than *defended*. A new SDRAM master is exactly what threatens it, so it needs a test, not a habit.

**Done, 2026-09-23 (B-127), with a scope correction from this row's original wording.** New `CT_BLT` in
`fw/suite.inc`: full-height `OP_BLIT` commands into framebuffer columns 400-511 (the never-displayed strip
B-126's `BLIT_READY()` probe already proved safe), re-issued the instant the draw engine goes idle — a
non-blocking poll rather than `fb_wait()`, so the test cannot itself manufacture an underrun by blocking the
main loop. **Added to STANDARD, FULL and ENDURANCE**, as specified. Verdict is late underruns only (same rule
as CT_R1-3); the SDRAM busy permille over the window rides along as the test's reported value — the first real
consumer of the B7 counter, gated behind a new firmware macro `TAU_SDRAM_BUSY` (default off, matching whichever
bitstream is actually installed; reports N/A rather than a false 0% when the counter isn't wired). **Scope
actually shipped is narrower than "full-screen scaled and blended":** this is B1 (generalised blit) load only,
across 112 of the 512-word stride (the safe off-screen strip, not the full 400-column display width), and does
not exercise B4 (scaled) or B5 (blended) traffic — blend is shelved pending its own timing fix (section 11) and
a scaled-blit firmware helper doesn't exist yet. Widen this test once either lands. **Deliberately kept as one
fixed test in all three profiles, not scaled like `CT_R1`-`CT_R3`'s three intensity levels or `CT_SOAK`'s
level/duration options** (owner question, 2026-09-23, `AskUserQuestion`: decided "not now — decide after the
first hardware run," rather than guess at intensity levels with no real busy-percentage number yet to reason
from). `make test-host` passes, the release (`player`) ROM is confirmed byte-identical (CT_BLT is entirely
`#if CHK_DEV`).

**Hardware result: PASS, 2026-09-23 (B-146) — MILESTONE.** After B-134's timing closure and a run of card
bugs unrelated to the engine itself (B-136, B-141, B-142, B-143), `TAU_0_5_0_A_4` ran STANDARD and FULL twice
each independently: **`Blit storm (30 s)` PASS both times, SDRAM 15.8% busy over the window (`busy_permille`
158), audio confirmed continuous the entire 30 s (B-139's `audio_full` flag true both runs) — zero late
underruns.** This is not ambiguous the way B-138 was: the audio-continuity check exists specifically to rule
out "passed because nothing was actually contending," and it reads clean. The predicted-busy-percentage
convention this row asked for was never actually recorded before the first run (a process gap, not backfilled
retroactively now that the real number is known) — 15.8% is the first real number to reason from for any
future scaling decision. Real margin remains before the SDRAM port would be a concern. `Track changes (10)`
failed in both runs, same as B-138, still unexplained and not investigated. **This closes the loop section 12
always intended: correct in simulation (B-125), timing-closed on real hardware (B-134), and now load-tested
on real hardware with real audio (this result) — the blit engine's foundation is proven, not just built.**

## 13. Parked — revisit after this phase

Kept deliberately, with reasoning, so none of it has to be re-invented. None of these are commitments.

**2D engine ideas (the wild list, preserved):**

- **Cover-art crossfade** — dissolve old into new instead of sliding, once B5 exists.
- **Theme swap via palette rewrite** — with a CLUT (B8), a dark mode or per-track accent recolour is ~256 word
  writes instead of a full UI redraw.
- **Screen-transition dissolve** — render the next screen off-screen, hardware-ramp alpha over a few frames for
  a genuine cross-dissolve between library/settings/playlist, at near-zero CPU.
- **Autonomous multi-frame animate descriptor** — hand the engine a start state, end state and frame count once
  and let it run unattended; also drives idle-screen animation without repeated CPU wakeups (a battery angle).
- **Icon/badge overlay layer** — generalising the openFPGA template's hardware-cursor pattern (the one real
  compositing primitive in Analogue's own example corpus) to play/pause/shuffle badges that composite without a
  row redraw.
- **Independent multi-row tickers** — per-row scroll offsets once B3 exists, for a persistent artist/album
  ticker.
- **Overlay compositing layer** via a second line buffer, Neo Geo LSPC ping-pong style (~2 blocks).
- **Double buffering** — ~288 KB in SDRAM plus a vsync pointer swap; simpler and safer than single-buffer beam
  fencing, but only worth it once drawing is complex enough to tear visibly.
- **Bilinear cover scaling** — free of line-buffer cost for the same reason B4 is; four reads per output pixel.
- **Neo Geo-style zoom lookup table** as an alternative to Bresenham if table-driven scaling ever fits better.

**Type/font system, broader than today's fixed ASCII-only M10K atlas:**

- **Broader glyph coverage (Latin diacritics through CJK).** Upstream HarpMudd v1.5.0 shipped and hardware-
  verified this already: `tools/gen_font_ext.py` builds one SD-card-loaded binary (4bpp Inter for Latin-1/Ext-A/
  Greek/Cyrillic, matching the ROM's own AA style; 1bpp Unifont-JP for kana + the full CJK Unified Ideographs
  block — a bitmap face reads sharper than a downscaled outline font at 16 px, their reasoning, not assumed
  here), read through the existing `CHAR` opcode via a glyph-index sentinel into the same row registers the ROM
  path already fills. Comes with full UTF-8 string-pipeline hardening (ID3 UTF-16, FLAC tag boundaries,
  filenames, `.m3u` BOM) that would also close this project's own open BUG-001 (accented filenames skipped).
  **The one deliberate deviation from their design, not a copy:** they stream glyph rows live from SDRAM; this
  project would want the asset in **PSRAM** instead, through the already-proven data window, to avoid the exact
  SDRAM-contention risk the rest of Phase F exists to protect. Their timing note — compose "tipped to -1.888 ns,
  fixed by splitting into two registered stages, now +2.093 ns" — is the same upstream cliff `PHASE_F_SPEC.md`'s
  own risk section (11) already cites; the fix (an extra pipeline stage) is already proven upstream if this is
  ever picked up.
- **Crispness at scale.** Today's 4bpp coverage atlas is baked at one fixed cell size (16x16) and read pixel-
  for-pixel — there is no scaling path for text today. Once B4 (scaled blit) exists, the same nearest-neighbour
  approach used for cover art would work for text too, but coverage-based AA that looks right at 1x can look
  wrong scaled up (blocky edges) or down (lost fine strokes, especially CJK stroke detail at 1bpp) — worth a
  real look rather than assuming it transfers, particularly for any eventual UI scale setting.
- **Multiple font support.** Today there is exactly one typeface (Inter SemiBold) baked into one ROM. A second
  face (a monospace variant for tabular/diagnostic screens, or a CJK-appropriate face distinct from Latin) is
  architecturally a second atlas plus a font-select bit somewhere in the glyph index — cheap in concept once any
  atlas is PSRAM-resident (SD-card assets are easy to add to), but each additional face multiplies the storage
  and glyph-generation-tooling surface, and font mixing/fallback rules (which face wins for a given code point)
  need an actual policy, not just "whichever loads."

**Beyond the 2D engine:**

- **APF bridge auto-load (`data_slots[].address`) for bulk SDRAM asset init, parked 2026-09-24.** Considered as
  a zero-CPU-cost alternative to CPU-driven expansion of the B8/B10 meter-thumbnail flat buffer: today only the
  `Firmware` slot (`tau.rom`) uses `address` (boot-time bridge push into on-chip BRAM); every other slot is
  `deferload` + CPU-pulled. Routing a new slot's `address` into SDRAM instead of BRAM would need the core's
  bridge-write decode extended to accept an SDRAM target — a real RTL question, not investigated further
  (owner: explore later once there's a better outlook on system performance, not now). The one-time boot-time
  CPU-uncached-write expand (~16 ms, see B8 step 1 firmware integration) is the interim approach.
- **True FFT via R2SDF + CORDIC twiddles**, if linear frequency bins are ever genuinely wanted (section 7).
- **Tile-based 3D**, revisited honestly after the 192 KB shrink frees 64 blocks (section 8).
- **JPEG hardware acceleration** — the dominant user-visible cost today is decode time (multiple seconds for a
  fresh large cover), not draw time. A much larger project, parallel to the parked MP3 acceleration idea, but
  it is the item with the biggest perceived-performance payoff if load time matters more than draw effects.
- **MP3 synthesis filterbank / IMDCT kernels** — only if the Phase D profile justifies them (D3). The V[] buffer
  alone is ~8 blocks and must be on-chip.
- **PSRAM-vs-BRAM album-art comparison** — parked since B-028, waiting on this engine.
- **`quartus_fit_summary.py` extension** to emit the per-instance memory table automatically, so section 1
  regenerates per build instead of being hand-derived.

## 14. Recommended order — and what to pick up next

Steps 3 and 4 are strictly ordered; the rest have some freedom.

| # | Step | Needs a Quartus slot? | Gated on |
|---|---|---|---|
| **1** | **Profile the software decoder** | No | **Done — B-086..B-098, on hardware** |
| **2** | Decide the MMIO descriptor model in RTL terms (section 9) | No | **Done — B-085** |
| **3** | Blit engine Tier 1/2 (opcodes + MMIO register file) | Yes | 2 — met; **Tier 1 RTL/sim complete, verification/fail-safe/Check-test work all done (B-125/B-126/B-127, section 12/12.1 closed). B-130's caveat RESOLVED by B-134: the real full-G3-macros + blit-engine fit closed cleanly on both seeds (seed 1: setup +0.634/+0.501 ns, hold +0.322/+0.305 ns; seed 2: +0.395/+0.310/+0.311/+0.302 ns), same 298/308 RAM and 11/66 DSP as the blit-only fits — B-111/B-114's retiming survives the real product configuration with no further RTL change. Packaged as 0.5.0-alpha.1 (B-131's semver convention), not yet installed.** Real multi-seed fit failed timing (B-107/B-109), bisect (drop `TAU_BLIT_BLEND`, B-110) recovered nearly all of it, both exposed retiming bugs fixed (B-111 BAR, B-114 SBLIT/CHAR), the blend/`glyphbuf` theory independently verified as congestion relief not a direct fix (B-116); the re-fit combining all three fixes closed cleanly on every corner (B-117 final, seed 2: Slow 85C +0.727 ns, Slow 0C +0.597 ns). **B-130 found that every one of B-100 through B-117's fits, including this one, was built with ONLY `TAU_MLAB_MIGRATE`/`TAU_FONT_REPACK`/`TAU_SDRAM_BUSY`/`TAU_BLIT` on top of the bare `USE_SDRAM=1` base — none of them ever included the shipped product's own `TAU_PHASE2_WINDOW`/`TAU_PSRAM_PROBE`/`TAU_PSRAM_WINDOW`/`TAU_PSRAM_IFETCH` macros.** Packaging B-117's RBF with real product firmware (`player-library-diagnostic-profile`) made this concrete: on hardware, TAU DEV 43 loaded nothing and had no menu access at all, because that firmware needs the window/PSRAM paths this bitstream never had, and the "no BRAM fallback" playlist path (since A-105) has nothing to fall back to. **The recorded timing margin is not proven to survive combining with the full product configuration** — a genuinely new, not-yet-run "full G3 macros + blit engine" fit is required before this bitstream family can be trusted as a product candidate. Software reference renderer (B-125), `BLIT_READY()` fail-safe (B-126) and the blit-storm Check test + busy-counter consumer (B-127) are all built and host-verified, and remain correct RTL/firmware — only the *bitstream pairing* was wrong. B3 analysed, needs firmware coordination, not RTL-only. |
| **3a** | MLAB migration (`glyphbuf` + dcfifo) + font repack + busy-cycle counter, scoped out from 3 as everything not needing the blit opcodes | **Done — B-101/B-102, real multi-seed fit, both seeds Successful** | none |
| 4 | Meters to cold code | No (firmware) | **Done — B-199..B-202.** Cold-code-per-audio-frame question measured (3 scenarios, all clean, `cold.inc`'s blanket rule corrected) and the real `ui_draw_dynamic()` -> `ui_draw_dynamic_cold()` (G4 step 4, `TAU_G4>=3`) conversion built and hardware-confirmed: 27,308 cycles worst-case (~1.73% of budget), 0 late underruns, +19,520 B heap gap freed. `release` stays `G4=2` pending an ENDURANCE soak (owner deferred, to run at their own convenience) before promoting. |
| 5 | Main RAM 256 -> 192 KB | Yes | 4 — met, **and now hardware-confirmed under sustained load (B-213, 2026-09-25): `TAU_DEV_47`'s ENDURANCE soak passed all 10 checks (0 late underruns, cold-frame cost 28,847 cycles, consistent with B-202's original measurement)** — the "hardware confirmation of each" gate this row named is met. Picojpeg — done, B-203: +11,456 B measured (bigger than the ~8 KB estimate), combined with the meters' +19,520 B for a total +30,992 B, comfortably clearing the ~29 KB target. Both moves are firmware-only and, as of B-214 (2026-09-25), **promoted to `release`'s defaults** — `release`'s heap gap jumped from 30,528 B to 61,808 B (RAM usage 82.6% -> 65.3%). **The section 4.1 peak-usage gate is met (B-230, 2026-09-25):** `TAU_DEV_49`'s stack instrumentation read back from the full prescribed worst case (Stress R3 + heavy scroll/seek/track-switch + MP3/FLAC with different covers + USER CHECK) shows peak stack 1,672 B of 16,384 B (10.2%), all checks PASS, 0 errors. Heap-peak instrumentation (`arena_limit()` into Check/QR) was never built and is parked as a later-build addition per the owner's explicit call, not a blocker. **RTL work done, timing-clean (B-235, 2026-09-25):** `tau_main_ram.sv` (B-223) hit and resolved a real RAM-inference failure (B-224..B-228: a split-region ternary read broke Quartus's inference pattern-matcher; fixed by giving each region its own plain registered read, muxed only after being already-registered values); a combined fit with B11's own timing fix (B-231) closed cleanly on both seeds, all four corners positive (seed 1 selected: setup min +1.406 ns, hold min +0.098 ns), RAM Blocks 235/308 (76%, down from ~298-300/308). **Firmware-side gate NOT met (B-236, 2026-09-25):** `fw/link.ld`'s opt-in `RAM_192K=1` ceiling was built (a real toolchain gotcha found and fixed along the way — `DEFINED()` has no effect inside a `MEMORY` block's `LENGTH` in this toolchain, moved to a plain symbol expression instead) and, once correctly wired, shows `release` currently **short by ~12.6 KB** against the 192 KB target — the B-203/B-214 "+29 KB clears it" accounting has been eroded by real feature growth since (the Winamp Bars/Scope editor, the meter-yield diagnostic, others). **Partially closed (B-244, 2026-09-25):** `fw/playlist.inc` (all but `pl_cmd`, its audio-refill-coupled spin-wait primitive) and `fw/settingsui.inc`'s remaining functions converted to cold code, each verified reachable only through the existing top-level `COLD_READY()` gate; measured 4,416 B recovered, `release` now short **~8.5 KB**, not ~12.6 KB. **A hard install prerequisite, not just a nice-to-have (B-245, 2026-09-25):** `TAU_RAM_192K` is a synthesis-time RTL parameter that physically removes 64 KB of BRAM — B-235's RBF genuinely only has 192 KB of on-chip RAM, not 256 KB with room to spare. Checked all five actively-used firmware targets against `RAM_192K=1`: all fail to link, same as `release`. Pairing B-235's RBF with any of today's 256 KB-linked firmware would silently alias/corrupt RAM on hardware (`fw/link.ld`'s own comment named this exact danger and assumed it couldn't arise) — held, nothing installed. The firmware trim must reach zero (a real successful `RAM_192K=1` link on at least one build) before this RTL work can be adopted on hardware at all. |
| 6 | Spectrum filter bank in RTL (section 7) | Yes — can ride a later build | Nothing; cheap in blocks |
| 7 | Audio kernels, smallest first (FLAC bit reader) | Yes | **1** — done, unblocked, but stays ordered after 3-5 (owner decision, 2026-09-22) |

### Timing-experiment backlog (added 2026-09-23, B-118..B-120)

Zero-RTL-change experiments to try before any further manual retiming, cheapest/least-disruptive first. None
of these are tried yet; each has a KB entry with a concrete validation plan.

1. **`FITTER_EFFORT` to `STANDARD FIT`** (`KB-048`) — `ap_core.qsf` currently reads `AUTO FIT`, which
   Intel's own docs say explicitly stops optimizing once "good enough" and skips timing-affecting
   optimizations to save compile time. Try this on the next timing-marginal build (e.g. re-run B-117's
   no-blend + B-111 + B-114 combination) before assuming more manual retiming is needed — if it closes a gap
   on its own, it's a strictly better fix, since it applies automatically to any future marginal path too.
   Real cost: builds may run 2x+ longer (already 50 min-1h45m today).
2. **Per-instance `DSP_BLOCK_BALANCING`** (`KB-045`) — only relevant if/when `TAU_BLIT_BLEND` is revisited.
   Forces the specific `Add32~8` adder off DSP-block mapping without touching the real blend/EQ multiplies,
   targeting the exact placement collision B-116 confirmed. Cheaper than the RTL-restructure fallback also
   listed there.
3. **Quartus Rapid Recompile** (`KB-046`) — a workflow/iteration-speed item, not a timing-closure fix; see
   the roadmap's Tooling track item 5. Worth enabling once build-iteration count becomes the bottleneck again
   rather than timing itself.
4. **M10K native-width check** (`KB-047`) — not a Phase F item, belongs to the still-unexecuted Phase G RAM
   shrink; listed here only so it isn't missed when that work starts (also cross-referenced in the Phase G
   section of `docs/ARCHITECTURE_ROADMAP.md`).

### Item 1 result (for the record)

Measured on hardware, real content (the permanent "Audio Test Suite" test album, source outside the repo,
synced via `tools/sync_media.py`): **MP3 is filterbank-dominated (IMDCT+Subband, not Huffman)**, matching the
roadmap's assumed ordering. **FLAC's bit-reader share measured 7-15%**, not the 64-76% `docs/FLAC.md` reported
on different content — that gap is open, not resolved. A real cross-format measurement bug (FLAC accumulators
leaking into the MP3 reading shown right after a FLAC track) was found and fixed (B-097) before trusting the
numbers. Full detail: `docs/ARCHITECTURE_ROADMAP.md` section 2, `docs/AUDIT_TRAIL.md` B-086..B-098.

### Item 3, step 1 result (for the record)

**B-100, 2026-09-22.** Ran the synthesis-only pre-check (`quartus_map`, no fit) with `TAU_MLAB_MIGRATE` +
`TAU_FONT_REPACK`, compared against a same-tree baseline with both off. **MLAB migration confirmed and
de-risked** — `glyphbuf` and the `sound_i2s` dcfifo both resolved to `MLAB` in the RAM Summary table (0 -> 2,176
MLAB bits, an exact 1:1 move out of the M10K-bit pool); only 2 of the 4 candidates from section 2 are in scope
(`cmd_mem` and the VexRiscv regfile deliberately excluded, per their own risk/awkwardness notes there).
**Font ROM repack's actual win is still unconfirmed** — synthesis reports declared content bits, which are
identical before and after by construction (same content, different lanes), so the real question (does the
fitter pack four 8-bit ROMs into fewer physical M10K blocks than one 32-bit ROM) needs step 2's fit. This is a
correction to section 10's original framing of what synthesis-only would prove — see that section for detail.
Evidence: `docs/AUDIT_TRAIL.md` B-100.

### Item 3a result: the real fit, scoped to everything except the blit opcodes (for the record)

**B-101/B-102, 2026-09-22.** Owner scoped step 2 down to "everything except the blit engine" (asked after being
told the spec's literal step 2 bundles blit RTL that does not exist yet). Added B7 (the SDRAM busy-cycle
counter, section 5) since it has no RTL dependency on the opcodes, then ran a real multi-seed fit (not
synthesis-only) of `TAU_MLAB_MIGRATE` + `TAU_FONT_REPACK` + `TAU_SDRAM_BUSY` together.

**Font repack's real answer, settled: +0 blocks, not +4.** Both seeds fit to identical **298/308 RAM blocks**
(baseline 300/308 — a net +2, all of it the two MLAB items). Corrected in section 4's ledger. On-chip repacking
is not a usable lever for the font ROM; PSRAM is now the only real path to those blocks if they are ever needed.

**Timing: real difference between seeds, and it matters.** Seed 1 closed with a genuine (if tiny) violation —
**setup slack -0.001 ns / -0.101 ns** on the two slow-silicon corners, traced to one exact path: `mp3_fb.sv`'s
`Mux3~4` (the px_color arithmetic feeding `glyphbuf`'s write-data port) into `glyphbuf`'s newly-MLAB-mapped
write port. Seed 2 closed **positive on all four corners** (setup +0.091/+0.005/+5.552/+5.790 ns; hold
+0.315/+0.301/+0.138/+0.127 ns) — same RTL, same macros, different placement. **Seed 2 is the build to carry
forward** (RBF sha256 `a0942341...1a465`); seed 1's result is recorded because it is a real, reproducible
finding (not just a bad seed to discard and forget) — the glyphbuf MLAB write path has effectively zero margin
in the worst corner, so any future change that adds even a few picoseconds there (routing shift, a nearby logic
change) could reopen it. Worth a note if `glyphbuf` or its feeding arithmetic changes again.

**Process note, also worth keeping:** the first seed-2 attempt showed 3 errors from a corrupted run — two
`make fpga` invocations had raced on the same project directory (traced via impossible log timestamp ordering,
Assembler starting before the Fitter that must precede it), an artifact of a launch-script mistake, not an RTL
problem. Redone cleanly with a verified single process before trusting the result.

### Item 3 progress: MMIO descriptor register file + B1 (generalised blit), simulation-verified (for the record)

**B-103, 2026-09-22.** Started item 3 itself. Built, in order:

- **The MMIO descriptor register file (section 9), as designed there** — `R_BLT_IDX`/`R_BLT_DATA` (0xC0/0xC4) in
  `mp3_soc.v`, four sticky fields (SRC_BASE, SRC_STRIDE, DST_BASE, DST_STRIDE), index auto-increments on each
  DATA write. **One deliberate deviation from the spec's literal "three registers":** no new `R_BLT_GO` — the
  existing `R_FB_GO`/`fb_cmd_op` per-command path (already proven, already tested) was widened from 2 to 3 bits
  instead, using a bit that was already unused padding in `R_FB_GO`'s word layout, and the new opcode rides that.
  Reusing proven infrastructure over adding a parallel one, not a spec violation without reason.
- **`OP_BLIT` (B1), the first Tier 1 opcode** — a genuine generalisation of `OP_COPY`, not a parallel code path:
  same row-at-a-time streaming-write datapath (`A_COPYRD`/`A_WRWAIT`/`glyphbuf`), but destination and source are
  each `sticky_base + flat_offset`, stepping by the sticky `STRIDE` per row instead of `OP_COPY`'s fixed
  `FB_BASE`=0/512. No multiplier needed — the per-row step was already a plain add; swapping a register in for a
  constant cost nothing extra. Both addresses are full 25-bit SDRAM addresses (not the 19-bit FB_BASE-relative
  window `OP_COPY`/`OP_RECT`/`OP_CHAR` stay confined to), so a blit can reach anywhere in SDRAM.
  **Deliberately not fixed here, a separate follow-up:** `OP_COPY`'s existing row-buffer width limit
  (`glyphbuf` is 128 entries, so widths above 127 silently truncate) — carries over unchanged to `OP_BLIT`
  because fixing it is an M10K/MLAB cost decision (a wider row buffer), not an addressing one, and bundling it
  in would have obscured which change caused what.
- **Fail-safe, for free rather than built:** `q_op` is 3 bits now but old RTL only ever reads 2 (`R_FB_GO`'s
  `dDAT_MOSI[1:0]`), so new firmware sending `OP_BLIT` (value 4) to an old bitstream is truncated to 0 (`OP_RUN`)
  before it even reaches the FIFO — the existing "unknown opcode degrades to a RUN" behaviour, not a hang. A
  full `COLD_READY()`-style feature-bit check (section 12) is still worth doing once more opcodes exist to gate,
  not for one opcode alone.

**Verification, following section 12's pattern:** extended `sim/tb_mp3_fb.v` rather than writing a parallel
testbench, since `OP_BLIT` extends `OP_COPY`'s own machinery. Two properties checked: (1) **equivalence** — with
the sticky registers left at their power-up defaults (base 0, stride 512), `OP_BLIT` reproduces `OP_COPY`'s
existing passing test byte-for-byte; (2) **independence** — with `SRC_BASE=0x8000/STRIDE=64`,
`DST_BASE=0x9000/STRIDE=96` (neither matching `FB_BASE`=0/512), every address and every per-row step matches
hand-computed expected values, not the old hardcoded ones. A `BUG_IGNORE_BLIT_STRIDE` mutation parameter
(reverting the stride step to a fixed 512, reproducing the exact bug this feature exists to prevent) is
confirmed caught — `make test-rtl-fb-mutation`, a real functional mutation test, not the kind of physical-timing
hazard B-101's CDC counter found it could not meaningfully mutation-test. `make rtl-lint`, `make test-host` and
`make test-rtl` (now including this) all pass, 0 failures; `mp3_soc_sim.v` regenerated correctly and the
unrelated PSRAM testbenches confirmed unaffected.

**Not done, at B-103:** `TAU_BLIT_BLEND` and B2-B6 — B1 alone was scoped as a real, complete, verified foundation
rather than shallow progress across all six. No Quartus slot spent yet; RTL/simulation only.

### B2 (colour key) and B6 (meter column), simulation-verified (for the record)

**B-104, 2026-09-22.** Both built on B1's `OP_BLIT`/`R_BLT_IDX`/`R_BLT_DATA` foundation.

- **B6, `OP_BAR`** (section 5: "a bar is `(x, base_y, height, lit, unlit)`") is two chained `RECT` fills, not a
  new burst mechanism — the existing `rect_active`/`A_WRWAIT` row loop runs twice per command, the second
  segment queued (`bar2_pending`/`bar2_addr`/`bar2_rows`/`bar2_fg`) and re-armed the moment the first segment's
  last row retires. `cmd_addr` is the span's top-left (the convention every other opcode already uses);
  `cmd_glyph` (otherwise unused outside `CHAR`) carries the lit-row count, clamped to the span height; colours
  reuse `cmd_fg`/`cmd_bg`, the same "no other use for these fields" reasoning `OP_COPY` already established for
  its source address. **One convention decided here, since nothing upstream pinned it down:** lit rows are the
  *bottom* of the span (the usual meter-fills-from-the-floor reading of "base_y"), unlit rows the top —
  documented in the code, not just assumed.
- **B2, colour-key transparency**, needed more than "one comparator" to be *correct*: showing the destination
  through a keyed source pixel means the destination has to be read at all, which `OP_BLIT`/`OP_COPY` never did
  before (write-only). Added a genuine destination pre-read phase (`A_KEYDST`, structurally identical to the
  existing `A_COPYRD` read-into-`glyphbuf` loop) that runs before the source read whenever `blit_mode &&
  blt_key_en`; the source read (`A_COPYRD`, one line changed) then simply *skips* writing into `glyphbuf` for any
  word equal to the sticky `KEY` colour, leaving the pre-read destination pixel already sitting there — no
  separate per-pixel select/blend stage needed. `OP_COPY` is untouched and never keys, matching B1's own
  precedent of leaving `COPY` as the simple case. New sticky field 5 (`R_BLT_IDX`=4: bit16=enable,
  bits[15:0]=colour); `blt_idx` widened 2->3 bits, wraps 4->0 instead of counting to 5, so a burst of exactly 5
  `R_BLT_DATA` writes loads the whole state.

**Verification:** both extend `sim/tb_mp3_fb.v`. BAR: three cases (a split bar, a lit-clamped-to-height fully-lit
bar with no phantom second phase, a fully-unlit bar with no phase 2 firing at all) checking row count, exact
per-segment addresses/stride and exact colours. B2: a keyed blit where one of four words in row 0 matches KEY
(confirmed it keeps the destination's own pre-read value, not the source's) while the other three and all of
row 1 take the source normally (confirming the key does not universally suppress writes), plus the same
command with keying disabled (confirms the previously-keyed word reverts to plain source, i.e. `A_KEYDST` never
even ran). Two new mutation parameters, both confirmed caught by `make test-rtl-fb-mutation`:
`BUG_IGNORE_BLIT_STRIDE` (from B-103, still passing) and new `BUG_IGNORE_KEY` (disables the colour-key compare
entirely — the keyed-word check fails as expected). `make rtl-lint`, `make test-host` and `make test-rtl` all
pass, 0 failures.

**Not done, at B-104:** `TAU_BLIT_BLEND` and B3-B5 — still RTL/simulation only, no Quartus slot spent.

### B3 (analysed, not built as RTL-only) and B4 (scaled blit, `OP_SBLIT`), simulation-verified (for the record)

**B-105, 2026-09-22.** Owner said "continue with B3 and B4." B3 turned out to be a real scoping finding, not a
straightforward build; B4 is a complete, verified new opcode.

**B3: the literal mechanism does not translate to this architecture, and the real gap needs firmware
coordination first — not built as RTL-only.** Section 5 describes an Amiga/Atari ST mechanism: a barrel shifter
plus first/last-*word* masks, built for a format that packs many 1bpp pixels per word. This engine is one pixel
per SDRAM word — there is no sub-word packing here for a shifter or a mask to act on, so the literal mechanism
has nothing to translate to. What it would *buy* — an arbitrary source column offset, and an output width
independent of the source rectangle — `OP_BLIT` already has, for free, from B1's own generalised addressing; no
new hardware needed for blits. The one genuine gap is CHAR-specific: the marquee's own comment ("does not clip
one partially off the left edge") is about sub-*glyph* clipping, which is real and would fix the marquee's
whole-character scroll. **Checked rather than assumed that retrofitting it is safe, and found it is not:**
`fw/player.c`'s `fb_char()` never writes `R_FB_SIZE` (`cmd_w`/`cmd_h`) at all — those registers hold whatever an
earlier, unrelated `fb_rect()`/`fb_copy_span()` call left in them by the time a `CHAR` command is pushed.
Repurposing `cmd_w`/`cmd_h` as CHAR clip fields, as originally considered, would silently feed garbage leftover
RECT/COPY dimensions into every existing glyph draw. This needs a firmware change (dedicated clip fields
`fb_char()` actually sets) before it is safe, which is real coordination work outside an RTL-only delivery's
scope — recorded here so it is not re-attempted the same way, not because it is unimportant.

**B4 (`OP_SBLIT`, scaled blit, nearest):** built as a genuinely new opcode, so none of B3's compatibility risk
applies — no existing caller to break. Reuses CHAR's own Bresenham registers directly
(`char_num`/`char_den`/`acc_x`, `char_numy`/`char_deny`/`acc_y`) rather than duplicating them, since CHAR and a
blit are never in flight at the same time; the ratios come from the same `nd_x`/`nd_y` wires CHAR's own dispatch
already computes. The one real generalisation: `sblit_ext()` computes the output extent from a *variable* source
width/height (`cmd_w`/`cmd_h` — safe here, brand new opcode) and the same four scale factors, instead of CHAR's
fixed-16px-cell lookup, using only a small multiply-by-constant (x3, for 1.5x/3x) plus a shift, not a general
divider. Matches section 5's own "no line buffer needed... one read per output pixel" finding directly: every
output pixel issues its own single-word SDRAM read at the Bresenham-selected source column, returning through
`A_IDLE` between pixels exactly like every other transaction in this engine (new state `A_SBLIT`) — scanout can
preempt between *any* two words, not just between rows, which the module's own header comment establishes as
the whole point of routing everything through one dispatch point. The destination still advances by the sticky
`DST_STRIDE` every output row, reusing B1's `blit_dst_addr`/`blit_mode` unchanged; only the *source* row
advances, and only when the Y-Bresenham condition says to (a new conditional step in `A_WRWAIT`, `sblit_mode`
selecting it over `OP_BLIT`'s own unconditional per-row stride step). Same 128-entry `glyphbuf`/127-word output
limit as `OP_COPY`/`OP_BLIT`, same reasoning, same "not silently widened here either" note.

**Verification:** two cases in `sim/tb_mp3_fb.v`. 1x (no scaling): confirms the per-pixel read path agrees with
the row-burst path pixel-for-pixel for the trivial case, including the source row correctly stepping by the
sticky stride every output row. 2x: a 2x1 source region doubles to 4x2 output — hand-computed expected values
(every source pixel repeats twice per axis) matched exactly on the first run, a good sign the Bresenham reuse
is genuinely correct rather than coincidentally close. New mutation parameter `BUG_SBLIT_NO_SCALE` (forces the
X step to fire every pixel regardless of the accumulator, i.e. silently drops back to an unscaled 1:1 copy) is
confirmed caught — the 2x test's doubling checks fail exactly as expected. `make rtl-lint`, `make test-host` and
`make test-rtl` (now 3 mutation cases for this file) all pass, 0 failures.

**Not done, at B-105:** `TAU_BLIT_BLEND`/B5, and the CHAR sub-glyph clipping half of B3 (needs the firmware
coordination described above). No Quartus slot spent yet.

### B5 (alpha blend), behind its own `TAU_BLIT_BLEND` macro, simulation-verified (for the record)

**B-106, 2026-09-22.** Owner said "continue with B5" — the last Tier 1 item, and per section 10's build plan the
one that actually needs care: it is the deepest new pipeline and carries the documented -1.888 ns timing-cliff
risk, so it is kept droppable on its own, separate from B1/B2/B4/B6.

**A real gap caught before it shipped: the macro wasn't wired to anything.** Built the blend datapath first,
then went to add the separate `TAU_BLIT_BLEND` macro the build plan requires — and found `mp3_fb`'s existing
instantiation in `core_game.vh` passes **no module parameters at all**. Every earlier mutation-test parameter in
that module (`BUG_IGNORE_BLIT_STRIDE`, `BUG_IGNORE_KEY`, `BUG_SBLIT_NO_SCALE`) was therefore always silently at
its default regardless of any macro, which was fine for THOSE (test-only, default-off is correct), but for a
real feature parameter like `BLIT_BLEND_ENABLE`, that same silence would have meant the whole feature was
unreachable even with `TAU_BLIT_BLEND` defined. Fixed before it became a real bug: added
`mp3_fb #(.BLIT_BLEND_ENABLE(...))  u_fb (` and the `TAU_BLIT_BLEND` -> `TAU_BLIT_BLEND_EN` derivation
(requires `TAU_BLIT`, matching the existing dependency-check convention). Caught by building the feature
end-to-end and checking the wiring, not by a test that happened to exercise it — worth naming as a real finding
about how easy it is for a "just add a parameter" step to be silently inert.

**The blend itself:** new sticky field 6 (`R_BLT_IDX`=5: enable, 3-bit mode, 8-bit alpha — DSP 0-255 alpha or
one of the four PSX shift-add ratios section 5 lists: B/2+F/2, B+F, B-F, B+F/4, all clamped not wrapped). Shares
B2's destination pre-read phase (`A_KEYDST`) rather than adding a second one — its trigger condition generalised
from "`blt_key_en`" to "`blt_key_en` OR `blend_active`". **Found and fixed a real latent bug while generalising
that condition, not after:** the existing key-match check (`key_dst_done && (p0_q == blt_key)`) relied on
`key_dst_done` implying `blt_key_en` was on, which was true before this change (nothing else could trigger the
pre-read) and stopped being true the moment blend could trigger it too — a blend-only blit could have
accidentally treated a source pixel equal to a stale/leftover `blt_key` register value as keyed, with keying
never actually enabled. Fixed by adding an explicit `blt_key_en` check (new `pixel_keyed` wire) rather than
relying on the old implication. Key takes priority over blend where both apply: a keyed pixel is fully
transparent, so blending it would be wrong, not merely redundant. One blend function (`blend_ch`, parameterised
by the channel's own max value) serves R/G/B alike rather than three near-copies; DSP mode approximates `/255`
as `>>8` (weight 256), the same pragmatic trade CHAR's own `cov_weight` already makes and documents.

**Verification:** two cases in `sim/tb_mp3_fb.v`. DSP mode at alpha=128 reduces exactly to a per-channel average
(128/256 = 0.5, no rounding surprise) — hand-computed R/G/B values matched on the first run. PSX mode 2 (B+F)
deliberately chosen to overflow the 5-bit R channel (20+20=40) to confirm clamping, not wrapping. New mutation
parameter `BUG_BLEND_ALWAYS_SRC` (blend silently does nothing, always writes the source pixel) confirmed caught.
`make rtl-lint`, `make test-host` and `make test-rtl` (4 mutation cases for this file now) all pass, 0 failures.
**Tier 1 is functionally complete in RTL/simulation as of this entry** — B1/B2/B4/B6 built, B3 analysed and
correctly scoped out, B5 built behind its own separable macro. No Quartus slot spent on any of it yet.

### Next item, in enough detail to start cold

**Step 2 of section 10's build plan: the first real multi-seed fit of the blit engine.** Bundle `TAU_BLIT`
(+`TAU_BLIT_BLEND`) with the counter (B7, already fitted and proven in B-102) and re-verify MLAB/font-repack
still hold (they should — this build doesn't touch them, but section 10 bundles everything together precisely
because it is the first real spend of a Quartus slot on this new RTL). Watch specifically for the documented
-1.888 ns risk; if timing fails, the first bisect is dropping `TAU_BLIT_BLEND` per section 10, keeping the rest.
Software reference renderer + pixel-diff fixtures (section 12) and the `COLD_READY()`-style feature-bit fail-safe
are still open items ahead of any card install — this session's RTL testbenches cover functional correctness
per-opcode, not yet the full render-a-scene-and-diff-the-buffer pattern section 12 describes. See sections 3-6
and 9-13 for the rest of the plan.

## 15. UI controller — tearing investigation and a blit-engine-native render path

**Trigger (2026-09-25, B-232):** owner reported "very minor glitching and tearing... seems to be due to
partial screen updates" as a long-standing, low-level UI artifact, and asked whether menu/UI drawing takes
advantage of the blit engine, then asked for a real investigation plus a scoped design for a UI controller
that does, considering other parked features (rounded corners named explicitly) and where they'd benefit.

### 15.1 Root cause, confirmed by reading the actual RTL (not inferred)

**There is no frame-synchronized draw commit anywhere in this core.** Traced the full video pipeline in
`src/fpga/core/mp3_fb.sv`'s video-timing block (`hc`/`vc` counters, `H_TOT`/`V_TOT`): the core already does
row-level *read* pipelining for scanout — `do_fill`/`fill_line_req`/`linebuf` prefetch the NEXT scanline's
row from SDRAM into a small on-chip line buffer one full scanline period ahead of when it's displayed, so
the actual pixel *fetch* is isolated from live SDRAM read latency. **This protects reads for display. It
does nothing for CPU writes.** The CPU's draw commands (`fb_rect`/`fb_char`/`OP_BLIT`/etc., arbitrated only
against the SDRAM port via `can_sdram`) land in SDRAM the instant arbitration allows, with zero relationship
to where the scanout beam currently is beyond that one-row prefetch. A UI update spanning multiple rows can
have its top rows already scanned (showing OLD content) while the CPU is still writing the lower rows (which
then show NEW content) — a horizontal tear visible for exactly one frame, worse the more separate draw
transactions one logical update needs, since more transactions means more elapsed time between "first row
touched" and "last row touched."

**Confirmed available hooks, neither wired up today:**
- `core_top.v` has a real, **already-unused** `vblank` *input* port, fed by APF's own video/scaler pipeline
  (`apf_top.v` line ~425) — flagged as literally dead in a Quartus warning read earlier this session
  ("No output dependent on input pin vblank"). Using it needs a CDC synchronizer into `clk_sys` (it arrives
  in whatever clock domain APF drives it in, not necessarily `clk_sys`).
- The core also **generates its own internal vertical sync** (`vs_pulse`, from the same `hc`/`vc` counters
  already described, in the `clk_vid` domain) — a cleaner source than the external `vblank` pin for CPU-side
  gating, since it's derived from the exact same timing the scanout itself uses (no ambiguity about which
  frame it corresponds to). CDC into `clk_sys` is the same proven Gray-code technique `tau_cdc_gray_ctr.sv`
  already uses for B7's SDRAM busy counter (B-101) — a genuinely reusable pattern, not a new invention.

### 15.2 A concrete, high-value adoption opportunity found in the same investigation: `OP_RRECT` for `fb_round_rect`/`fb_round_rect_on`

`fb_round_rect`/`fb_round_rect_on` (`fw/player.c`) are **pure software**: a per-row iterative nearest-integer
circle search (`while ((inner+1)^2 + dy^2 <= r^2) inner++`), each row issuing up to 4 separate `fb_rect`
calls for the corner cuts plus one for the main fill — for the radius-8 rounded rects used everywhere
(the whole player-screen title panel, every selected row in every list/menu/playlist/settings page, the
tape meter's shell/label/bezel, the magic-eye base), that's up to ~33 separate small SDRAM transactions
**per rounded rectangle**, issued on every single list-navigation key press across the entire UI. This is
almost certainly the single most frequently executed draw pattern in the whole firmware, and — per section
15.1 — also close to a worst case for tear exposure, since it spreads one "logical" visual update across
dozens of independent SDRAM writes with real elapsed time between the first and the last.

**B11 (`OP_RRECT`, B-205, timing-fixed B-231) already does exactly this shape in hardware** — one CPU command
composes the main fill plus all four corner segments via a precomputed cut-per-row LUT, no live search, far
fewer separate transactions for the same visual result. Converting `fb_round_rect`/`fb_round_rect_on` to
`fb_rrect()` (a new thin wrapper, mirroring `fb_bar()`'s own convention) once B11's re-fit (B-229/B-231's
pairing, still pending) confirms clean timing is a direct win on both CPU cost and tear-window size, and
needs no new opcode design — it is squarely what B11 was built for, just not yet wired into any caller.

### 15.3 Design: a phased UI controller, cheapest lever first

Matching this project's own synthesis-first, measure-before-committing discipline (B-100 precedent) — no
Quartus slot spent, no firmware change made yet, this section is the scoped plan:

**Phase T0 — RTL, cheap, foundational.** CDC the internal `vs_pulse`/scan-position into `clk_sys` (the
proven B7/`tau_cdc_gray_ctr.sv` technique), expose as new MMIO: a vblank-active bit at minimum, ideally also
a "lines remaining until the beam reaches row N" figure if cheap (a plain compare against the CDC'd `vc`).
No behavior change by itself — this is purely giving firmware a real, hardware-verified answer to "how much
safe time is left this frame," which does not exist anywhere in the CPU-visible register file today.

**Phase T1 — firmware, the actual "UI controller."** Two independent, separable pieces:
1. **Convert `fb_round_rect`/`fb_round_rect_on` to `OP_RRECT`** (section 15.2) — a direct, self-contained win,
   buildable the moment B11's re-fit is confirmed clean, independent of T0.
2. **A real draw-batching layer**, replacing the current pattern of scattered immediate `fb_rect`/`fb_char`
   calls throughout `settingsui.inc`/`player.c` with: collect what changed for this frame, then flush it as
   one pass timed to start right at vblank (using T0's new MMIO), using the fewest possible hardware-composed
   commands (`OP_BAR`/`OP_RRECT`/`OP_BLIT`/`OP_CBLIT` over multiple small `fb_rect` calls wherever a pattern
   already matches one, per the same "batch into fewer transactions" principle B8/B9/B11 all embody). This is
   the part that most directly answers "does the UI controller take full advantage of the blit engine" —
   today only the *meter* draw code has been converted (B-198, B-215/B-216); Settings/menus/overlays have not
   been touched at all and are a real, scoped opportunity of their own.

**Phase T2 — RTL, invasive, hold until T0/T1 are tried and measured insufficient.** True double buffering.
**SDRAM capacity is not the blocker** — the chip is 64 MiB (`mp3_fb.sv`'s own header comment, confirmed
against `docs/UPSTREAM_MEMORY_AUDIO_KNOWLEDGE.md`) and the entire visible framebuffer plus every existing
off-screen stash region (art stash, thumbnail flat buffers, playlist/library SDRAM regions) together occupy
well under 2 MiB — a second full framebuffer costs a rounding error against total capacity. The real cost is
RTL complexity (a buffer-select mux gated cleanly by vblank on both the CPU write-address path and the
scanout prefetch's read-address path, synchronized so neither flips mid-frame) and firmware-side auditing of
every fixed-address convention (`FB_BASE`, the off-screen stash rows) to confirm none of them need duplicating
across both buffers. Worth real design work only if T0/T1 measurably fail to eliminate the reported artifact.

### 15.4 Other parked capabilities this same controller would naturally pick up

Once a real batching/vblank-aware draw layer exists, it is also the natural adopter for opcodes already
designed or built but not yet wired into any real UI caller: **B8's CLUT** (already used for meter thumbnails,
section 12; the same palette-indexed blit could serve any multi-colour icon/badge), **B9's palette re-index**
(built, unused — a re-themed icon set without a second CLUT upload), and **B13's proposed gradient-fill bar**
(section 5; would collapse `WATER`/`SCROLL`'s current 3-op shift/gradient/fill sequence into one command, and
generalises to any "flat panel against the themed gradient backdrop" — which is most of this UI). None of
these are re-scoped here; they are simply the concrete backlog a real UI controller would draw from once T1
exists, cross-referenced from `docs/ARCHITECTURE_ROADMAP.md`'s "UI/UX redesign" placeholder (B-115), which
this section now gives a first real, evidence-based answer to its own open question of "how it interacts
with the Phase F blit engine."

**Not done:** no RTL, no firmware, no Quartus slot spent on any of this section. Investigation and design
only, awaiting an owner decision on whether to proceed to T0.
