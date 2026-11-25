# The text weight table, redone from first principles (2026-11-25)

**Status:** the table is derived and tested; **no RTL has changed**. It goes into the Talos v2 contract as data (decision D-T20, item 5 and the "dark table" follow-up). Tool: `tools/derive_text_weights.py` (new, MIT, written without reading any earlier fitting tool); test: `sim/test_derive_text_weights.py`; picture: `img/text_weights_dark_preview_2026-11-25.png`.

## What the table is
Each glyph pixel has a 4-bit coverage `c` (0..15). The engine turns it into a blend weight `w` (0..16): `out = bg + (fg - bg) * w / 16`. The table maps `c` to `w`. Light text on a dark ground needs MORE weight than plain linear alpha, because a half-covered pixel emits half the foreground's light, and light adds linearly while the panel's encoding is non-linear.

## How it is derived
For each `c`, over the real theme palettes (36 weighted text-on-ground pairs from `themes/tau.json` and `themes/ocean.json`, dark polarity), pick the integer `w` that minimises the squared error in CIE L* between the physically ideal pixel (linear-light mix at `a = c/15`, then encoded) and what the engine produces (quantised to RGB565). Ends are exact (`w(0)=0`, `w(15)=16`) and the table never decreases. The numbers follow from the physics and the palette; they are not a creative choice.

## Result (dark polarity, light text on a dark ground)
| coverage c | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **new (derived)** | 0 | 4 | 6 | 7 | 8 | **9** | 10 | 11 | 12 | **12** | 13 | 14 | **14** | 15 | **15** | 16 |
| current (inherited) | 0 | 4 | 6 | 7 | 8 | 10 | 10 | 11 | 12 | 13 | 13 | 14 | 15 | 15 | 16 | 16 |
| linear alpha (Tau Reader's blend) | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |

Mean / worst |dL*| against the physical ideal: **new 1.88 / 6.45**, current 2.24 / 7.27, linear alpha 12.52 / 34.40. Ten of the fourteen interior entries are identical to the current table; four (c = 5, 9, 12, 14) are lower by one step (1/16 of the foreground-to-background difference). The picture shows ideal, new, current and linear side by side: the first three are visually the same, linear alpha is visibly thinner.

## What Tau Reader's text work gave
Read-only. Reader has **no weight table**: it blends in firmware with a plain linear alpha (coverage expanded 4 to 8 bit, `alpha = coverage * 17`) when it fills its glyph cache, and its RTL glyph blitter draws opaque pre-coloured RGB565. Its pipeline (offline rasteriser to 4-bit `TFN1` files, glyph cache in SDRAM, layout and font host tests, a screen-legibility analysis in `SCREEN_LEGIBILITY.md`) is a useful design reference, and its linear blend is the measurable argument for a derived table (12.5 mean error against 1.9). Its pre-coloured cache is a different architecture (blending moved out of the engine); it is not needed for this table. Reader's code is Apache-2.0 and nothing was copied.

## The light-text table (not asked, same method)
`python3 tools/derive_text_weights.py --polarity light` gives `0,1,1,2,3,3,4,5,5,6,7,8,9,11,13,16` for dark text on a light ground; the table in use (fitted in B-316 with the inherited tool) is `0,1,1,2,3,3,4,5,6,7,8,9,10,12,13,16`, one step higher at c = 8 to 13. Redoing it the same way is optional.

## Shipping it (no regression, improvement behind a switch)
* The Talos v2 contract states the weights as data, derived by this method; the clean-room author receives the 16 numbers and the method, not the old text.
* A one-step change on four edge levels is a pixel change in every text edge. Owner decision, with "update both projects at once" available: apply it to Tau Alpha and Gallery in the same step (Gallery's text goldens re-captured together), or keep the current numbers behind a parameter for one project. Tau V and Reader do not use the engine's text path.
* A new fit and a Pocket text check are needed when it reaches RTL; nothing is fitted now.
