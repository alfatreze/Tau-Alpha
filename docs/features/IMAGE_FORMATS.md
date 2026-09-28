# Tau image formats: study, tools and how to extend them

Status: **offline study, nothing reads these formats on the device yet** (owner decision B-284, 2026-09-26). The
library thumbnail file is still deferred (`MEDIA_LIBRARY_0.4_SPEC.md` section 3, "RGB565 assumed"); this is the
evidence for choosing its pixel format, and the tools to generate real files as soon as a reader exists.

## Why not a better codec
The core is a 60 MHz rv32im with no FPU, and the decoder shares the CPU with audio. Measured on the Pocket, decoding the
embedded cover costs 2.6 s (small cover), 5.3 s (455 px) and 15.8 s (1400 px) (A-120, B-027); it is dominated by the
weight of the *source* file, not by the codec. WebP, AVIF, JPEG XL and HEIF compress better but need arithmetic/ANS
decoding, loop filters and tens of KB of RAM: rejected. PNG needs inflate and is larger for photos; QOI only suits small UI
art. The wins are: (1) do the scaling offline, (2) use formats the hardware can expand (CLUT blit, `OP_CBLIT`), or that
decode with no entropy coding (BC1).

## Formats compared (`tools/tau_image.py`, container `TIM1`)
| Variant | What it is | Bits/px | Notes |
|---|---|---|---|
| `rgb565` | raw framebuffer format | 16 | reference; no decode |
| `pal256` / `pal64` / `pal16` | median-cut + Floyd-Steinberg, CLUT in RGB565, indices packed 8/6/4 bit | 8 / 6 / 4 | drawn by the existing CLUT blit (B8); same idea as the meter thumbnails |
| `bc1` | S3TC/DXT1 4x4 blocks | 4 | no entropy coding; random access; endpoint search is crude (a real encoder would do better) |
| `jpg60/75/85` | pre-scaled baseline JPEG 4:2:0 | ~1-2.6 | the decoder the player already has; smallest, softest, slowest |
| `auto` | smallest of pal64/pal256/bc1 within 1.5 dB of the best | - | per-cover choice; not the default |

Container: 16-byte header (`TIM1`, format, bpp, width, height, colours, payload length) then payload; exact layout in the
module docstring. **Not frozen**: it changes freely until a firmware reader exists.

## Results (test covers; smooth generative art, which flatters palettes)
PSNR is against the resized original quantised to RGB565. Load times are **model estimates** (`tau_image.estimate_ms`);
the SD read rate is an assumption (0.5-2 MB/s) because no clean device measurement exists.


##### cover455.png -> 92x92
| Variant | Bytes | Bits/px | PSNR dB | Load est. |
|---|---|---|---|---|
| rgb565 | 16,944 | 16.02 | 99.0 | 17-42 ms |
| pal256 | 8,992 | 8.50 | 36.8 | 16-37 ms |
| pal64 | 6,492 | 6.14 | 34.3 | 14-32 ms |
| pal16 | 4,280 | 4.05 | 29.5 | 12-27 ms |
| bc1 | 4,248 | 4.02 | 35.0 | 13-20 ms |
| jpg60 | 1,120 | 1.06 | 32.7 | 78-223 ms |
| jpg75 | 1,414 | 1.34 | 33.4 | 78-224 ms |
| jpg85 | 1,892 | 1.79 | 33.8 | 78-225 ms |
auto picks: pal256 (8,992 B, 36.8 dB)

#### cover455.png -> 128x128
| Variant | Bytes | Bits/px | PSNR dB | Load est. |
|---|---|---|---|---|
| rgb565 | 32,784 | 16.01 | 99.0 | 28-77 ms |
| pal256 | 16,912 | 8.26 | 35.6 | 25-67 ms |
| pal64 | 12,432 | 6.07 | 33.0 | 22-57 ms |
| pal16 | 8,240 | 4.02 | 29.8 | 19-48 ms |
| bc1 | 8,208 | 4.01 | 33.6 | 20-35 ms |
| jpg60 | 1,647 | 0.80 | 31.5 | 134-392 ms |
| jpg75 | 2,312 | 1.13 | 31.8 | 134-394 ms |
| jpg85 | 3,252 | 1.59 | 32.1 | 135-396 ms |
auto picks: pal256 (16,912 B, 35.6 dB)

#### cover1400.png -> 92x92
| Variant | Bytes | Bits/px | PSNR dB | Load est. |
|---|---|---|---|---|
| rgb565 | 16,944 | 16.02 | 99.0 | 17-42 ms |
| pal256 | 8,992 | 8.50 | 40.9 | 16-37 ms |
| pal64 | 6,492 | 6.14 | 37.3 | 14-32 ms |
| pal16 | 4,280 | 4.05 | 30.4 | 12-27 ms |
| bc1 | 4,248 | 4.02 | 32.7 | 13-20 ms |
| jpg60 | 1,733 | 1.64 | 28.4 | 78-224 ms |
| jpg75 | 2,141 | 2.02 | 29.3 | 78-225 ms |
| jpg85 | 2,726 | 2.58 | 30.8 | 78-226 ms |
auto picks: pal256 (8,992 B, 40.9 dB)

#### cover1400.png -> 128x128
| Variant | Bytes | Bits/px | PSNR dB | Load est. |
|---|---|---|---|---|
| rgb565 | 32,784 | 16.01 | 99.0 | 28-77 ms |
| pal256 | 16,912 | 8.26 | 39.8 | 25-67 ms |
| pal64 | 12,432 | 6.07 | 36.5 | 22-57 ms |
| pal16 | 8,240 | 4.02 | 30.4 | 19-48 ms |
| bc1 | 8,208 | 4.01 | 34.8 | 20-35 ms |
| jpg60 | 2,431 | 1.19 | 30.6 | 134-394 ms |
| jpg75 | 3,021 | 1.48 | 31.8 | 135-395 ms |
| jpg85 | 3,858 | 1.88 | 33.1 | 135-397 ms |
auto picks: pal256 (16,912 B, 39.8 dB)


Reading: palette 64/256 look almost identical to the original on this art and load in tens of milliseconds; BC1 shows
4x4 blocking on smooth gradients but is the fastest to expand; pre-scaled JPEG is the smallest file but 5-10x slower than the
others (still 10-100x faster than today's 2.6-15.8 s, because the source is 92 px, not 455-1400 px).

**Caveat:** this table is synthetic smooth art; the next section re-runs it on real covers.

## Results on real covers (owner's test set, 2026-09-26)
Nine illustrated covers (1024 px square, one 2048 px, one 1024x1540 portrait; PNG with alpha, two JPEG; alpha is opaque except
about 0.1% of pixels on the portrait one, so dropping it is harmless), 92x92, PSNR in dB:

| Variant | Bytes | Range over the set | Verdict |
|---|---|---|---|
| pal256 | 8,992 | 31.8-36.6 | best on all nine; near-identical to the original by eye |
| pal64 | 6,492 | 27.8-32.6 | clean, slight posterising in gradients |
| bc1 | 4,248 | 23.8-31.4 | blocky and colour-shifted on line art and saturated colour |
| jpg75 (pre-scaled) | 2,700-4,550 | 21.8-29.1 | smudged at 92 px, and larger than on the smooth test art |

Conclusions: `auto` chose pal256 for every cover (palette beat BC1 on all of them), so on this kind of art BC1 is not worth its
crude encoder; pre-scaled JPEG is no longer tiny once the cover has detail (3-4.5 KB, against 4.2 KB for BC1) and is 5x slower
to show. Palette 64 is the size/quality compromise (6.5 KB); palette 256 is the quality choice (9 KB). The recommendation above
stands with BC1 demoted to "keep in reserve for photographic covers" (none of these is one).

## Decision (owner, 2026-09-26): palette 256 at 128 px
* **Default variant `pal256`, main cover size 128 px on the long side.** A 128x128 cover is 16.9 KB (8.3 bits/px including the
  512 B CLUT); estimated load 25-67 ms (model). Real-cover PSNR at 128 px: 31.8-36.2 dB, same ordering as at 92 px.
* **Non-square covers are scaled proportionally, never cropped and never padded**: the long side becomes 128 px and the
  header carries the real width and height (1024x1540 becomes 85x128, 11.4 KB). Any layout that needs a square slot decides
  how to place a non-square picture; no letterbox pixels are stored.
* `pal64`, `bc1`, `jpg*` and `auto` stay available (`--art-variants pal64,bc1`), they are just not the default.

## Original recommendation (superseded by the decision above)
1. Palette 64 or 256 plus the CLUT blit for library thumbnails and now-playing art (hardware and index model exist).
2. BC1 kept for photographic covers, chosen per cover by `auto`.
3. Pre-scaled JPEG only as the tiny fallback for the embedded-art path.
The now-playing art still comes from inside the user's track (the player never reads a cover file beside it); a device reader
for these files is future work with the thumbnails / UI-controller phase.

## Tools (use them whenever you copy to the Pocket)
```
tools/lab/img_format_lab.py covers/*.jpg --size 92 --size 128 --markdown   # compare formats on real pictures
tools/lab/img_format_lab.py cover.jpg --sheet sheet.png                    # side-by-side image
tools/sync_media.py ALBUM --core alfatreze.TAU_DEV_NN --art-variants       # default: pal256 at 128 px -> ALBUM/tau-art/cover_128.pal256.timg
tools/sync_media.py ALBUM --core ... --art-variants pal256,pal64 --art-size 128 --art-size 64
```
* `--art-variants [LIST]`: no value = `pal256`; `--art-size` sets the long side (default 128); `auto` picks per cover. Source: the folder's cover
  image (or `--cover`), else the first MP3's embedded art. Sidecars are copied unchanged by later `--from-core` clones, so they
  survive the usual "carry media to the new build" step.
* The firmware and the library index builder ignore `tau-art/`; adding it does not change any existing behaviour.
* `sim/test_tau_image.py` (in `make test-host`; skipped without Pillow/numpy) covers packing at every width, exact sizes, a
  hand-built BC1 block, round-trip quality floors and a fake-card sync including a `--from-core` clone.

## How to keep expanding it
* New codec: add an encoder (`bytes` from `pack()`), a decoder in `DECODERS`, an entry in `VARIANTS`; it appears in the lab and
  in `--art-variants` automatically. Add a size/quality floor to the test.
* Better BC1 (stb_dxt / squish style endpoint search), 2-colour-per-block or 8bpp+RLE variants, per-cover palette sharing,
  B9 palette re-index for theming icons, alpha-carrying formats: candidates, none built.
* Replace the load-time constants with device measurements when a reader exists (they are named at the top of the estimate
  section in `tau_image.py`); update the table here in the same commit.
* Cross-project: Tau Omega may want to produce the same files; the container is documented in `tau_image.py` and listed in
  `CROSS_PROJECT_INTERFACE.md` as unfrozen.
