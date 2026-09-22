# Current engineering status

**Snapshot:** 2026-09-22. Tau **v0.4.0** is released and installed on the owner's card (media library, Phase G cold code,
on-device diagnostics; two zips: TAU and TAU_DIAGNOSTIC). Full detail: `docs/SESSION_HANDOFF_2026-09-22_RELEASE_0.4.md`.
Committed locally, not pushed, not tagged. Earlier releases: v0.3.0 (PSRAM in the bitstream) - see
`docs/SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md`; the SDRAM-side history (A-001..A-137) and PSRAM P0-P4 (B-001..B-023) are
unchanged and still correct as recorded there.

**Since that snapshot (B-086..B-099, same day):** the software decoder profile (Phase D step 1) is **done** - see below.
The gate is closed: audio kernel work is confirmed still ordered after the blit engine. Committed through `422e0a9`
(local `main` is 26 commits ahead of `origin/main` - not pushed).

## Executive state

- **Product:** v0.4.0 = the G3 bitstream (SDRAM CPU window + PSRAM data window + PSRAM instruction fetch) + firmware with the
  media library, the settings menus, the playlist, and Phase G ("cold code": about 24 KB of firmware runs from PSRAM instead
  of on-chip RAM). Release heap gap 34,752 B; Diagnostic Build ~29-34 KB. `dist/` equals the release zip's contents.
- **Media library: built, on hardware, working.** `tools/sync_media.py --library` builds the index; Select opens it when
  present; Legacy Playlist Mode (the old behaviour) when it isn't. History (what was last playing) resumes after a restart -
  confirmed working on the Diagnostic Build, **not yet confirmed on the release build** (B-080, open item, instrumented with
  an Info-page marker, evidence pending).
- **Phase G: complete through step 3, proven on hardware, fail-safe proven on hardware.** Library UI, settings menus, the
  playlist loader/overlay and the cover-art glue all run from PSRAM; a bitstream without PSRAM instruction fetch loses those
  features cleanly (E-code shown, single-file playback still works) rather than crashing. Not moved: the track-open path,
  boot/idle code, and the meters (`ui_draw_dynamic`, ~17 KB, deferred to the blit engine).
- **On-device diagnostics ("the Check"): built, on hardware, working** (Diagnostic Build only). USER CHECK/STANDARD/FULL/
  ENDURANCE profiles; QR-code report (level L, up to version 38) plus a 36-character short code and a four-word persisted
  summary, all decoded by one host tool (`tools/decode_tau_suite.py`).
- **Two bugs fixed since 0.3.0** that were in the shipped release: Left/Right in the settings menus (incl. Volume) briefly
  sought the playing track; album art could go blank after a track change within an album. Both root-caused (see section 3
  of the 0.4 handoff), not just patched around.
- **Not validated / deliberately open:** restart-based Check tests, a "browse while playing" regression test (G4's real risk,
  currently only manually tested), BUG-001 (accented file names), the meter/visualizer split and the JPEG decoder (both still
  hot, waiting on the blit engine to be worth doing). **Parked (B-082, owner: UX friction, not blocking):** the release-vs-
  diagnostic boot-restore mismatch and the missing loading-message on an album pick - both cosmetic, evidence/instrumentation
  left in place for whenever they're picked back up.

## Evidence that closes the Phase G / library gates (Pocket unless stated)

| Gate | Result | Record |
|---|---|---|
| PSRAM instruction fetch, RTL | first cold call = 1 line fill (8 beats); cached call = 0 fills; 12 KB function = 31.6 cycles/word; no DQ contention | B-047 (sim) |
| PSRAM instruction fetch, hardware | cold code test PASS 31.6 C/W as predicted; SDRAM-unchanged gate PASS (89/48-56-335/31-38-344); 30-min soak 0 failures | B-054 |
| Library, first hardware run | 30 tracks/3 albums/2 playlists open and play correctly; Left/Right skip bug found and fixed | B-058, B-060 |
| G4 step 1 (library+settings cold) | heap gap 4,096 -> 21,584 B (diagnostic); menus draw identically | B-069 |
| G4 steps 2-3 (playlist+art cold) | heap gap -> 30,016 B; fail-safe confirmed (no library/menus/covers, single file plays) on the old bitstream | B-070, B-071, B-077 |
| Album-art stash bug | found and fixed (loader cleared the reuse cache before the reuse check ran) | B-075 |
| The Check, hardware | all four profiles run; QR/short-code/persist-word decode agree; B stops a run, Y reruns | B-058, B-062, B-066, B-067 |
| v0.4.0 release build | both zips build and check (`make_release.py --test`); library+cold data slots declared via shared `tools/tau_data_slots.py` | B-078 |
| v0.4.0 first boot | idle/blank-screen bug found and fixed; boot-restore mismatch instrumented (Info `R0`/`R1`), not yet resolved | B-080 |

## Memory budget (256 KiB block RAM)

Release ROM built with the library and Phase G: heap gap **34,752 B** (floor 6,144 B) - up from 6,608 B at v0.3.0 and briefly
as low as 4,096 B before Phase G moved code out. Diagnostic Build (adds Check + the old Tests/Stress menus): **~29,648 B**
(floor 4,096 B). Remaining hot code of note: the MP3/FLAC/JPEG decoders (unchanged), `ui_draw_dynamic` (~17 KB, the meters),
the track-open path and boot/idle code (kept hot deliberately - see the 0.4 handoff section 2 for the full list and why).

## The active plan (2026-09-22): Phase F

**`docs/PHASE_F_SPEC.md` is the working plan for everything not yet started.** It carries the blit-engine
feature tiers, the M10K map and release ledger, the decisions below, the build/verification/fail-safe plan,
and a parked-ideas list. Read it before starting any of this work.

Decisions recorded there (ask before reversing): **no FFT** - port the shipped software octave filter bank
into RTL instead (~0 M10K, and it already runs at 1.5% CPU); **no 3D GPU** - a full-screen Z-buffer is ~281
M10K on a 308-block device, and 2.5D primitives on the 2D engine give the same visual payoff for ~2-4 blocks;
**audio kernels stay gated on a decoder profile that has never been run**, and the roadmap's kernel ordering
is corrected - the only real measurement puts the FLAC bit reader at 64-76%, not the filterbank; **the EQ
already shipped** (`EQ_DESIGN.md` corrected); **MMIO splits engine state from per-command fields** (3
registers, not one per parameter); **licence stays MIT**, which means GPL RTL can be studied and reimplemented
but never copied in.

The load-bearing constraint: **main RAM cannot shrink first.** Blit engine -> meters go cold -> ~29 KB freed
-> main RAM 256 KB to 192 KB (two power-of-two arrays) -> **64 M10K blocks released**. 85% of all block RAM is
that one 256 KB array.

**Decoder profile: done (B-086..B-098), on hardware, on real content.** Built per-stage cycle counters (MP3
Huffman/IMDCT/Subband; FLAC residual/LPC), a Check field (`SR_T_DECPROF`) and a new **Decode Profile Sweep**
feature (`SR_T_DECSWEEP`, Settings > Diagnostics > Decode Sweep, Diagnostic Build only) that runs a whole
album/playlist end to end at a chosen speed and reports one QR code with per-track stage costs. Built a
permanent 11-track "Audio Test Suite" test album (FLAC/MP3 at several sample rates/bitrates + a spoken-word
clip, one consistent album so future regressions/comparisons reuse it) - source lives outside the repo at
`/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/test music/Test Album/`, synced onto the card via
`tools/sync_media.py`. **Result: MP3 is filterbank-dominated (IMDCT+Subband, not Huffman) as the roadmap
already assumed; the FLAC bit-reader share measured 7-15% here vs. 64-76% assumed from FLAC.md's own older
number on different content - open discrepancy, not resolved, flagged rather than force-explained.** Found and
fixed a real cross-format measurement bug along the way (B-097: FLAC accumulators leaking into the reading
shown for the MP3 track right after a FLAC track; fixed by gating every stage reading on `track_fmt`, confirmed
by two clean re-runs). Also found and deliberately parked B-093 (track-open is fully synchronous, blocks the UI)
for after the blit engine frees up M10Ks and the UI model changes. Owner decision: **follow the roadmap's own
order** - kernel work stays gated behind the blit engine, do not jump ahead.

**Blit engine step 1+3a: done and real-fit-confirmed, 2026-09-22 (B-100/B-101/B-102).** Scoped down to
everything not needing the (still unbuilt) blit opcodes: MLAB migration (`glyphbuf` + `sound_i2s` dcfifo, both
resolved to MLAB) + font ROM repack + the new SDRAM busy-cycle counter (B7), bundled into a real multi-seed fit
(not just synthesis). **Result: +2 M10K blocks (298/308, was 300/308), all from MLAB — font repack delivered +0,
not the hoped +4**, settling the question step 1's synthesis-only check couldn't. On-chip font repacking is not
a usable lever; PSRAM is now the only real path to font-related blocks. Seed 2 is the build to carry forward
(closes positive on all four timing corners); seed 1 had a real but tiny -0.001/-0.101 ns violation on the
glyphbuf MLAB write path, recorded as a genuine near-zero-margin finding, not discarded as noise. Full detail:
`docs/AUDIT_TRAIL.md` B-100/B-101/B-102, `docs/PHASE_F_SPEC.md` sections 4 and 14.

**The blit engine: started, 2026-09-22 (B-103/B-104/B-105).** MMIO descriptor register file (section 9) built —
`R_BLT_IDX`/`R_BLT_DATA` at 0xC0/0xC4 (5 sticky fields), plus `R_FB_GO`'s opcode field widened 2->3 bits to carry
the new opcodes, reusing the existing proven per-command path instead of adding a parallel one. Four opcodes
built and simulation-verified:
- **B1 (generalised blit, `OP_BLIT`)**: independent 25-bit source/destination addresses and per-row stride,
  both sticky, not `OP_COPY`'s fixed FB_BASE=0/512 — verified for equivalence and independence, plus a mutation
  test confirming a reverted-to-hardcoded-stride bug is caught.
- **B2 (colour-key transparency)**: needed a genuine destination pre-read phase to be correct (showing the
  destination through a keyed pixel requires reading it, which the write-only blit/copy path never did before)
  — verified with a keyed blit where one word of four correctly keeps the destination's value, plus a mutation
  test confirming a "colour key does nothing" bug is caught.
- **B4 (scaled blit, `OP_SBLIT`)**: reuses CHAR's own Bresenham registers (never both active at once) against a
  *variable* source size instead of CHAR's fixed 16px cell; every output pixel is its own SDRAM read, returning
  through the same single dispatch point as everything else so scanout can preempt between any two pixels —
  verified with a hand-computed 2x-scale case (a 2x1 source doubles to 4x2 output) that matched exactly, plus a
  mutation test confirming a silently-unscaled fallback is caught.
- **B6 (meter column, `OP_BAR`)**: two chained `RECT` fills (no new burst mechanism) for a split lit/unlit bar
  — verified for a split bar, a fully-lit bar and a fully-unlit bar (no phantom phases in either edge case).

`make test` (host + RTL, 5 mutation cases now) passes, 0 failures. **B3 (sub-pixel skew/masks): analysed, not
built as RTL-only.** The literal Amiga bit-packed mechanism doesn't translate to this one-pixel-per-word engine;
what it would buy for blits, B1's own addressing already provides for free. The real gap (CHAR sub-glyph
clipping, the marquee's actual limitation) needs a firmware change first — `fb_char()` never writes `R_FB_SIZE`,
so retrofitting `cmd_w`/`cmd_h` as clip fields would silently corrupt every existing glyph draw with stale
leftover RECT/COPY dimensions. Confirmed by reading the source, not assumed. **Not done:** `TAU_BLIT_BLEND`, B5
(alpha blend, the one with the documented -1.888 ns timing-cliff risk) — no Quartus slot spent yet.

**Next item:** B5 behind its own `TAU_BLIT_BLEND` macro, the last Tier 1 item and the one that needs real care
(the documented timing-cliff risk, section 11) — per section 10's build plan, kept separable so a timing
failure can drop just this piece.

**Parked (2026-09-22, not acted on):** broader type/font support — CJK, crispness at scale, multiple typefaces —
researched against upstream HarpMudd v1.5.0's hardware-verified Japanese/UTF-8 work and recorded in
`PHASE_F_SPEC.md` section 13. Revisit there if this becomes a real near-term want.

## Where to look

- **The active plan for what's next: `docs/PHASE_F_SPEC.md`** (see the section above).
- Full state, decisions, procedures, open items: `docs/SESSION_HANDOFF_2026-09-22_RELEASE_0.4.md`.
- Design docs: `docs/MEDIA_LIBRARY_0.4_SPEC.md`, `docs/PHASE_G_SPEC.md`, `docs/TEST_SUITE_SPEC.md` (the Check).
- Earlier architecture decisions (still correct): `docs/SDRAM_MEMORY_ARCHITECTURE.md`, `docs/PSRAM_IMPLEMENTATION_PLAN.md`,
  `docs/PSRAM_TIMING_CONTRACT.md`, `docs/SETTINGS_ARCHITECTURE.md`, `docs/MMIO_ALLOCATION.md`.
- Roadmap and ordering of what's not started: `docs/ARCHITECTURE_ROADMAP.md`.
- Older issue records: `docs/issues/` (001 = BUG-001, accented names, still open; 018/019 resolved in the A-series).
