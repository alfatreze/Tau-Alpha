# Barcode study: better ways to carry a Tau report off the Pocket

Branch `barcode-study` (worktree `../tau-alpha-barcode`). Host study only: nothing runs on a Pocket, no firmware or RTL changed. Lab: `tools/lab/barcode_lab.py` (run with `work/venv-qr/bin/python`).

## 1. What the channel really is

The Check/Sweep/Info reports leave the Pocket as a **screenshot PNG** on the SD card (`Memories/Screenshots`), read by `tools/decode_tau_suite.py` and Tau Omega. Measured on the 13 real screenshots in Tau Omega's `testdata/screenshots/`:

- 400x360, 8-bit RGB, lossless PNG;
- exactly **32 / 64 / 32 distinct levels** in R / G / B, all equal to the RGB565 -> 888 bit-replication values. So every screen pixel carries **16 recoverable bits**, with no scaling, blur or colour shift.

A QR code is built for a camera in a hostile channel (blur, perspective, glare, bad lighting) and spends most of its area on that: finder patterns, timing, masks, Reed-Solomon, 1 bit per module, a base64 text layer so scanner apps show copyable text. The current path (level L, version <= 38, 2 px modules) caps at **about 2.7 KB of text, about 2.0 KB of binary**, and the densest symbols need OpenCV's Aruco detector plus a 2x upscale to decode at all (B-056). Pain points that follow: multi-page Full runs, the 48-track Decode Sweep cap, a 6 KB cold-code encoder, 13.2 KB QR + 2.8 KB text + 2 KB record buffers in PSRAM.

The question is whether to keep paying for camera robustness we mostly do not use. Phone scanning is the only thing that needs it, and it is a secondary path (the short code and Omega are the main ones).

## 2. Candidates

| | QR (today) | 3-plane QR | JAB Code | Tau pixel grid (TPG) |
|---|---|---|---|---|
| Idea | 1 bit/module, text payload | three independent QRs in the R, G, B planes; plain decoders read each plane | colour 2D code (4 or 8 colours), LDPC, master + docked slave symbols | row-major cells, N bits per colour channel, fiducial frame, fixed-mode header with geometry, CRC32 |
| Capacity, one screen | **~2.0 KB** binary (v38) | ~3x, **~6 KB** [EST, x3 of v38] | **~4-5 KB** [EST, see below] | **258 KB** (1 px, 5 b/ch), 38 KB (2 px), 9.7 KB (4 px, 3 b/ch), 3.2 KB (4 px, 1 b/ch) |
| Implementation | already built, tested | small: three QR calls + plane split; firmware must still carry the encoder | **large**: ISO 23634 spec, LDPC encoder, palette/metadata layers, docking; reference code is a C library with a restrictive licence for an MIT repo [unverified, from memory, check before any reuse]; no Rust decoder known | **small**: about 100 lines each side, no tables, no encoder library; the QR encoder (6 KB cold code, 16 KB buffers) can be dropped |
| Decode time (host, measured) | 6-36 ms (OpenCV, v38 needs Aruco + 2x) | 3x QR | not measured (needs the reference library) | **0.0-4 ms** (numpy slice + CRC) |
| Survives JPEG q80 / chat resize / camera | v14: yes, v22: not camera, v37: not resize/camera | as QR, worse at 2 px | designed for camera [from spec, unverified] | 1 px: **no** (lossless PNG only); 4 px 2 b/ch: JPEG and resize yes, camera no; 4 px 1 b/ch same |
| Phone scan / copyable text | yes | partly | needs a JAB app, no text | no |

Measured survival table from the lab (payload at 80% of each mode's capacity; the Pocket's RGB565 quantisation applied; "cam" = blur 1.1 px + gamma + noise + JPEG 80):

```
mode                payload    png jpeg95 jpeg80  half  cam   decode ms
qr v14 4px              320     ok    ok     ok    ok    ok       6
qr v22 3px              748     ok    ok     ok    ok  FAIL       6
qr v37 2px             1920     ok    ok     ok  FAIL  FAIL      36
grid 1px 5b/ch       206966     ok  FAIL   FAIL  FAIL  FAIL     3.6
grid 2px 3b/ch        31036     ok  FAIL   FAIL  FAIL  FAIL     0.8
grid 4px 3b/ch         7752     ok    ok   FAIL    ok  FAIL     0.3
grid 4px 2b/ch         5164     ok    ok     ok    ok  FAIL     0.2
grid 4px 1b/ch         2577     ok    ok     ok    ok  FAIL     0.0
```

The "cam" column is a crude model, not a phone: it says only that none of the dense colour modes should be expected to survive a photograph. JAB is the only candidate here designed to, and it pays for that with its complexity and its LDPC overhead (the same cost as QR on a channel that does not need it).

## 3. Recommendation

1. **TPG as the primary report format for the screenshot path.** It is 100x denser than QR on the channel we actually use, decodes in milliseconds with only a PNG reader (Tau Omega needs the `png` crate, not an image-processing stack), and deletes the QR encoder and its buffers from the firmware. The record stays the binary TLV (`TAUD1` body), no base64: another 25% and no text layer.
2. **Mode ladder, one format.** A fixed 4 px, 1 b/ch header strip (always readable, even after JPEG or resize) carries `cell`, `bits`, length and CRC32. The firmware picks the densest mode that fits the report: 1 px / 16 bpp for anything that fits in rows, falling back to the 4 px, 2 b/ch mode (about 5 KB, survives JPEG and chat-app resizing) when the owner chooses a "shareable" variant. CRC32 detects every failure, so a bad channel fails loudly instead of decoding wrong.
3. **Keep a small QR for the phone case**, version <= 14 at 4 px (survives every damage model above): verdict, run counter and the existing 36-character short code. A key on the page (A) toggles between the QR summary and the grid, so neither shrinks the other. The QR encoder therefore stays, but only for small versions (a smaller table set and buffer than version 38).
4. **Skip JAB** unless the phone-camera path for large reports becomes a real requirement. Complexity and licence cost are high and the capacity gain over a good TPG robust mode is marginal on a 400x360 screen. **Skip 3-plane QR**: it triples a format that already fails on 2 px modules.

Other options considered and dropped: Aztec, Data Matrix, PDF417 (all 1 bit per module, kilobyte-scale, no gain over QR here); animated or multi-frame QR streams (needs the Pocket to hold still across screenshots, the screenshot path cannot capture video); a second RGB565 plane through the existing Chladni mailbox mechanism is the *implementation* of TPG, not an alternative.

## 4. Open risks (must be settled on a Pocket before any firmware work)

1. **Dense-pixel fidelity.** The 13 screenshots only show the colour levels are exact. A 1 px checkerboard and a full 16 bpp ramp must survive a real screenshot without filtering. Test: a "PIXEL GRID TEST" Diagnostics page drawing a known pattern; compare the card screenshot to the generator.
2. **How the firmware writes the pixels.** The CPU cannot touch the framebuffer directly (raw `0xA0000000` pointers hang, B-192/B-193). The route is the one Chladni already uses: write a plane through the SDRAM mailbox and place it with `OP_BLIT`/`OP_SBLIT`, or use `OP_CBLIT` for 8 b/px indexed cells. Cost is estimated, not measured: about 130k words at roughly 50 cycles per uncached mailbox access is about 0.1 s [EST]. Needs the H2 double-buffer (`R_DBUF_CPU`) and `FB_HELD()` bracket handled, the two traps that bit the Settings crossfade and the Scope trail.
3. **Pattern scaling/cropping by the screenshot path** under any video-mode change: the grid must be drawn after the frame is stable, with the header strip as the geometry check.
4. **JAB numbers are estimates** from my memory of the spec, not from running the reference implementation.

## 5. Proposed steps

1. Hardware pixel-fidelity test page (no new RTL): confirms or kills TPG. Smallest possible spend.
2. `tools/tpg.py` reference encoder/decoder + golden test in `make test-host`, `decode_tau_suite.py --grid`; hand the format description to Tau Omega through `docs/CROSS_PROJECT_INTERFACE.md` (reference, never copy files).
3. Firmware pixel writer in cold code, behind a build switch; QR encoder trimmed to version <= 14.
4. Migrate Check, Sweep, Info export, Layered Wave config export; raise the 48-track Sweep cap.
