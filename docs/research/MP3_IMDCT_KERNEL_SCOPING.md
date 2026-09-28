# MP3 IMDCT/AntiAlias kernel: scoping (nothing built)

Status: scoping only, 2026-09-27 (B-341). Read Helix's real `imdct.c`. No RTL, no firmware change,
no design decision made yet.

## 1. Why this needs its own scoping pass, not a straight repeat of the window unit

The MP3 window unit (`tau_mp3_poly.sv`, B-292, hardware-confirmed alpha.30) was tractable in a few
days because Helix's `FDCT32` is **one fixed-shape transform, called the same way every time**, with
only 32 unique output words per call (docs/features/MP3_FILTERBANK_KERNEL_DESIGN.md).

`IMDCT()` (`third_party/libhelix-mp3/real/imdct.c`, 784 lines) is not that shape:

- **Two different transforms**, chosen by block type: `IMDCT36`/`idct9` for long blocks, `IMDCT12x3`/
  `imdct12` for short blocks -- and a granule can be **mixed** (some of its 32 bands long, some short),
  so the choice is per-band, not per-call.
- **`AntiAlias`** (inter-band smoothing) and **`WinPrevious`**/**`FreqInvertRescale`** (windowing,
  overlap-add, frequency inversion) run around whichever transform, with their own state.
- **Overlap-add state carried across granules**, per band -- unlike the window unit's own V-history,
  which was already characterized and small (16 kbit/channel).

The existing profiler (`mp3_profile.h`) already folds all of this -- Dequantize, AntiAlias, both
transforms, windowing -- into one bucket ("I", 9-13% of decode, docs/features/MP3_FILTERBANK_KERNEL_DESIGN.md
line 5). There is no measurement yet of which piece inside "I" actually costs the most; profiling
finer than that, the same way B-086/B-087 first separated H/I/S before designing the window unit, is
the honest first step here, not a guess.

## 2. What the existing design doc already estimated (not measured)

`docs/features/MP3_FILTERBANK_KERNEL_DESIGN.md`'s own options table: **IMDCT alone, about 6-9% CPU saving, about
2-3 M10K blocks** ("a later, separate kernel") -- an estimate made before any IMDCT-specific
characterization, carried over from the original H/I/S split.

## 3. What "free blocks" actually changes here

The RAM shrink (fit-proven, B-339's `ram192-blend-b333`: 240/308 M10K used, 68 free) removes the
resource constraint that made this "later" in the design doc's own table. It does **not** remove the
design complexity above -- IMDCT is real hardware-design work regardless of how much M10K is free,
closer in scope to the window unit's own multi-week arc (B-290..B-308) than to a quick follow-on.

## 4. The standing, already-flagged decision this collides with

`docs/ROADMAP.md` item 10 (still marked `*(owner)*`, unresolved): the FLAC bit reader was ordered
*ahead* of any further MP3 kernel work (owner, 2026-09-22), because it measured as the dominant FLAC
cost (64-76% of FLAC decode, `FLAC.md`) with a **simpler, non-block-type-dependent shape** (a bit-serial
reader, not two alternating transforms) -- a materially lower-risk target than IMDCT by this session's
own read of the code. This scoping pass does not re-decide that order; it surfaces the same choice the
roadmap already named, now with IMDCT's real shape on the table instead of an estimate.

## 5. Recommended next step, not started

1. Split `mp3_imdct_cyc` into its real sub-pieces (Dequantize, AntiAlias, the two transforms, windowing)
   -- cheap, firmware-only, same instrumentation technique already in `mp3_profile.h`.
2. Measure on real tracks (the same Test Album used for B-086/B-087) which sub-piece actually
   dominates -- likely the two transforms, but not measured.
3. Only then design a kernel for the dominant piece, verified host-side against real Helix output
   first (the same discipline `sim/mp3_poly_probe.c` used), before any RTL.
