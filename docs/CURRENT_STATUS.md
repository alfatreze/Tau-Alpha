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
  hot, waiting on the blit engine to be worth doing). **Re-parked deliberately (owner, 2026-09-23, B-115):** the release-vs-
  diagnostic boot-restore mismatch has no known mechanism in the code (escalated by the B-112 audit from its earlier "UX
  friction" framing), but investigating it now is judged premature - a ground-up UI/UX redesign is planned (see the roadmap's
  new "UI/UX redesign" item) and may change or remove the boot/idle code path this bug lives in entirely.
  `docs/issues/021-boot-restore-release-vs-diagnostic-mismatch.md` has the full history and says explicitly not to
  re-investigate until the redesign's boot/idle flow is settled. **Still parked, unrelated:** the missing loading-message on
  an album pick - not reproduced from source, needs a description/screenshot of the gap when revisited.

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

**The blit engine: Tier 1 functionally complete in RTL/simulation, 2026-09-22 (B-103..B-106).** MMIO descriptor
register file (section 9) built — `R_BLT_IDX`/`R_BLT_DATA` at 0xC0/0xC4 (6 sticky fields), plus `R_FB_GO`'s
opcode field widened 2->3 bits, reusing the existing proven per-command path instead of adding a parallel one.
Five opcodes built and simulation-verified:
- **B1 (generalised blit, `OP_BLIT`)**: independent 25-bit source/destination addresses and per-row stride,
  both sticky, not `OP_COPY`'s fixed FB_BASE=0/512 — verified for equivalence and independence, plus a mutation
  test confirming a reverted-to-hardcoded-stride bug is caught.
- **B2 (colour-key transparency)**: needed a genuine destination pre-read phase to be correct — verified with a
  keyed blit where one word of four correctly keeps the destination's value, plus a mutation test.
- **B4 (scaled blit, `OP_SBLIT`)**: reuses CHAR's own Bresenham registers against a *variable* source size;
  every output pixel is its own SDRAM read, returning through the single dispatch point so scanout can preempt
  between any two pixels — verified with a hand-computed 2x-scale case that matched exactly, plus a mutation test.
- **B5 (alpha blend)**: shares B2's destination pre-read rather than adding a second one. DSP mode (0-255 alpha,
  `>>8` approximation) and four PSX shift-add ratios (B/2+F/2, B+F, B-F, B+F/4, clamped not wrapped), one shared
  per-channel function for R/G/B. Kept behind its own `TAU_BLIT_BLEND` macro, separate from `TAU_BLIT`, per
  section 10's build plan — it's the deepest new pipeline and carries the documented -1.888 ns timing-cliff risk,
  so it has to be droppable on its own. **Two real bugs found and fixed while building it, not after:** (1) the
  new macro was originally wired to nothing — `mp3_fb`'s instantiation passed no module parameters at all, so
  `BLIT_BLEND_ENABLE` would have stayed 0 regardless of the macro; (2) generalising B2's pre-read trigger to
  also fire for blend exposed a latent key-check bug — `key_dst_done` alone used to safely imply keying was on,
  and stopped being safe the moment blend could trigger the same pre-read too. Both fixed before shipping.
  Verified: DSP alpha=128 reduces to an exact per-channel average (matched hand-computed values); PSX B+F mode
  deliberately chosen to overflow a channel, confirming clamp-not-wrap; plus a mutation test.
- **B6 (meter column, `OP_BAR`)**: two chained `RECT` fills — verified for a split bar, a fully-lit bar and a
  fully-unlit bar (no phantom phases in either edge case).

`make test` (host + RTL, 7 mutation cases now) passes, 0 failures. **B3 (sub-pixel skew/masks): analysed, not
built as RTL-only** — the literal Amiga bit-packed mechanism doesn't translate to this one-pixel-per-word engine;
what it would buy for blits, B1's addressing already provides. The real gap (CHAR sub-glyph clipping, the
marquee's limitation) needs a firmware change first (`fb_char()` never writes `R_FB_SIZE`), out of scope here.

**Step 2's real multi-seed fit: both seeds Successful, but timing FAILS — and not where predicted, 2026-09-23
(B-107/B-109).** Both seeds (`tau-local/blit-engine-s1-20260922`, `-s2-20260922`) compiled cleanly (0 errors,
RAM 298/308, matching the already fit-proven MLAB+font+counter baseline) but violate setup on both Slow corners
(-2.5 to -2.9 ns, worse than the -1.888 ns figure previously cited). **`report_timing` traced the actual
violating path to `glyphbuf`'s existing MLAB write-data arithmetic — the exact near-zero-margin path `B-102`
already flagged, before the blit engine existed — not to the new `TAU_BLIT_BLEND` pipeline section 11 blamed.**
The documented "drop `TAU_BLIT_BLEND`" bisect is therefore not a proven fix; it's the cheapest next experiment,
but the evidence-based fallback is pipelining that specific `glyphbuf` write-data path directly. `docs/PHASE_F_SPEC.md`
sections 10 and 11 corrected. Full detail: `docs/AUDIT_TRAIL.md` B-109.

**Bisect run, both seeds — recovered almost all of it, but the worst path moved, 2026-09-23 (B-110).** Dropping
`TAU_BLIT_BLEND` took Slow-corner setup slack from -2.5/-2.6 ns to +0.02/-0.11 ns (seed 2) and +0.05/-0.10 ns
(seed 1) — both seeds land in the same tight range, confirming a real structural gap, not seed noise. The
`glyphbuf` violation is gone, but a *different*, pre-existing path (B6/BAR's lit/unlit row-split arithmetic,
`cmd_q` -> `char_fg` through a compare+subtract chain) became the new worst case once it did.

**Fixed, RTL/simulation-verified, 2026-09-23 (B-111).** Retimed the BAR arithmetic to compute off the same raw
BRAM read `cmd_q` itself registers from, on the same clock edge, instead of combinationally after it — same
function, same cycle timing, bit-for-bit unchanged behaviour, verified against `tb_mp3_fb.v`'s full BAR test
suite and all 4 existing mutation cases. **Not yet fit-tested** — simulation proves correctness, not timing.

**Full audit, 2026-09-23 (B-112, `docs/FULL_AUDIT_2026-09-23.md`):** the BAR bug was one instance of a pattern,
not a one-off — `OP_SBLIT`'s dispatch has the *identical* structural shape (same source register, never
retimed) and should get the same fix before the next fit. Separately, the blend write-back into `glyphbuf`
(`TAU_BLIT_BLEND`-only) lands on the exact write port B-102/B-109 already flagged — meaning the bisect may have
fixed the *original* violation directly (one less input to that write port's mux) rather than merely relieving
routing congestion as first theorized; unverified either way, cheap to check with a targeted `report_timing`
query before assuming the same bisect works again once blend returns.

**`OP_SBLIT`/`OP_CHAR` retimed, RTL/simulation-verified, 2026-09-23 (B-114).** Same technique as B-111, applied
to `OP_SBLIT`'s output-extent compute and `OP_CHAR`'s glyph-base compute (the two other instances of the same
bug shape B-112 found) — both retimed off the raw BRAM read on `cmd_q`'s own clock edge. `tb_mp3_fb.v`'s full
CHAR/SBLIT test suite and all 4 mutation cases pass unchanged.

**Blend/`glyphbuf` write-port theory resolved, 2026-09-23 (B-116): congestion relief confirmed, not a direct
fix.** A `report_timing` query against the still-present full-blend build (no re-fit needed) showed all 10 worst
violated paths are the identical `Add32~8`/`Selector222~1` chain B-109 found, with zero blend-related cells
anywhere in them. `Add32~8` is DSP-mapped; freeing the 3 DSP blocks `TAU_BLIT_BLEND` used relieves placement
pressure around it without changing its logical fan-in — B-110's original theory was right.

**MILESTONE — timing CLOSED for the no-blend configuration, 2026-09-23 (B-117 final).** The re-fit combining
no-blend + B-111 + B-114 (seed 2, after one relaunch following a VM power outage) closed cleanly on every
corner: Slow 85C **+0.727 ns**, Slow 0C **+0.597 ns** setup slack (vs. -0.112 ns before the fixes) — real
margin, not a bare pass. RAM 298/308, DSP 11/66, both matching every prior no-blend fit exactly. This is the
first build in the entire B-107..B-117 sequence with zero known timing violations on any corner.

**RTL fix committed, 2026-09-23 (B-124):** the owner deliberately skipped the second-seed confirmation
("don't jinx it") and committed `src/fpga/core/mp3_fb.sv` as-is — an accepted-risk judgement call, not a
verified one, recorded plainly in `docs/AUDIT_TRAIL.md` B-124.

**Software reference renderer + pixel-diff fixtures done, 2026-09-23 (B-125).** `tools/host/blit_reference.py`
(every opcode reimplemented in Python, CHAR's anti-aliasing included), `sim/tb_blit_scene.v` (an 11-command
scene through the real `cmd_push` interface) and `sim/test_blit_reference.py` (the diff driver, wired into
`make test-rtl`) — all 4 existing mutation hooks confirmed caught. This closes the first half of section 12.

**`BLIT_READY()` fail-safe done, 2026-09-23 (B-126) — with a real finding along the way.** Building this found
`TAU_BLIT` doesn't actually gate the opcodes at the RTL level at all (no bitstream feature bit exists, unlike
PSRAM/cold code); every bitstream since B-103 has the full opcode set unconditionally. `fw/blit_probe.inc`
detects the real distinction instead — an old (pre-B-103) bitstream's 2-bit opcode decode silently truncates
`OP_BLIT` to `OP_RUN` — via a 2-row custom-stride probe into never-displayed framebuffer padding. Gated behind
`TAU_BLIT_PROBE` (default off, product ROM confirmed byte-identical); nothing calls it operationally yet.
This closes section 12 in full.

**Blit-storm Check test + busy-cycle consumer done, 2026-09-23 (B-127).** New `CT_BLT` in `fw/suite.inc`, added
to STANDARD/FULL/ENDURANCE: full-height `OP_BLIT` commands into framebuffer columns 400-511 (never displayed,
the exact safe strip B-126's probe already proved), re-issued the instant the draw engine goes idle — a
non-blocking poll, not `fb_wait()`, so the test itself cannot manufacture an underrun by stalling the main loop.
Verdict is late underruns only (same rule as CT_R1-3); the SDRAM busy permille over the window rides along as
the test's reported value, first real consumer of the B7 counter (`R_SDR_BUSY`, MMIO 0xBC), gated behind a new
firmware macro `TAU_SDRAM_BUSY` (default off — the counter has no hardware ready-detect of its own, so this
must be set to match whichever bitstream is actually installed; reads 0xFFFF/"N/A" when off, never a false 0%).
`R_BLT_IDX`/`R_BLT_DATA`/`FB_OP_BLIT` promoted from `blit_probe.inc` into player.c's shared register block so
both features use one definition. Verified: `make test-host` passes unchanged, `player-library-diagnostic-profile`
builds clean (heap gap 28,112 B), the plain `player` (release) ROM is confirmed byte-identical (CT_BLT is fully
inside `#if CHK_DEV`). **Honest scope note:** this exercises B1 (generalised blit) load only, at 112 of the
frame's 512-word stride (the never-displayed strip) — not literally the full 400-column width, and not B4
scaled or B5 blended traffic, since blend is shelved and a scaled-blit firmware helper doesn't exist yet; widen
this test later if either of those lands. Not yet run on hardware — no bitstream with `TAU_SDRAM_BUSY` wired
has been fitted/packaged/installed; that's the same B-117 no-blend bitstream, still VM-only. `TAU_BLIT_BLEND`
itself stays shelved — the `glyphbuf` write-port chain would need its own retiming (or `KB-045`'s
`DSP_BLOCK_BALANCING` idea) before blend can safely return.

**TAU DEV 43 was unusable on hardware, 2026-09-23 (B-130) — root cause found, card restored.** The owner reported
a starting screen with no playlist, no menu access, nothing loaded. Cause: **the entire B-100..B-117 blit-engine
bitstream series was built with only `TAU_MLAB_MIGRATE`/`TAU_FONT_REPACK`/`TAU_SDRAM_BUSY`/`TAU_BLIT` over the
bare `USE_SDRAM=1` base — never with the shipped v0.4.0 "G3" bitstream's own `TAU_PHASE2_WINDOW`/`TAU_PSRAM_PROBE`/
`TAU_PSRAM_WINDOW`/`TAU_PSRAM_IFETCH` macros.** `player-library-diagnostic-profile` firmware needs those for the
library, playlist (no BRAM fallback since A-105) and the PSRAM-cold-code menus/settings (Phase G4) to work at
all — without them the fail-safes correctly disabled everything, which looked identical to "broken." Fixed on
the card: `TAU_DEV_43` removed, `TAU_DEV_42` restored from its verified backup — card is back to exactly `TAU`,
`TAU_DIAGNOSTIC`, `TAU_DEV_42`, the same working state as before B-127. **The real consequence: B-117's "closed
timing" result is not proven for the product** — none of B-107 through B-117 ever combined the blit engine with
the product's PSRAM/window RTL, which adds its own real logic/routing/DSP pressure (PSRAM alone was 8,044
registers per B-018/B-021). A genuinely new "full G3 macros + blit engine" fit, never yet attempted, is needed
before this bitstream family is trusted again. Full account: `docs/AUDIT_TRAIL.md` B-130.

**The combined fit closed cleanly, 2026-09-23 (B-134) — first attempt, real margin, no further RTL needed.**
Both seeds Successful, 0 timing failures, same 298/308 RAM / 11/66 DSP as the blit-only fits; seed 1 (better
margin on all four corners: setup +0.634/+0.501 ns, hold +0.322/+0.305 ns) selected. B-111/B-114's retiming
survives combination with the real PSRAM/window RTL with no changes needed. Packaged as **`0.5.0-alpha.1`** —
the first real use of B-131's semver naming — with `player-library-diagnostic-profile` firmware built with
`SDRAM_BUSY=1`; `check_tau_package.py` PASS. **Not yet installed** — awaiting owner confirmation before any
SD-card write, doubly so after B-130.

**Installed on the Pocket, 2026-09-23 (B-135): `TAU 0.5.0-alpha.1`**, additively alongside `TAU`/`TAU_DIAGNOSTIC`/
`TAU_DEV_42` (all untouched). Bitstream/firmware macro pairing re-checked one more time before the write (the
exact check that would have caught B-130) -- full match this time. All hashes verified after copy. **Not yet
run** -- first correct hardware verdict on the blit engine, `BLIT_READY()`, `CT_BLT` and `R_SDR_BUSY` is the
owner's next Check run (Settings > Diagnostics > Check, any profile exercises `CT_BLT`).

**Next, in order:** read that result, then Tier 2 (B8-B11) or the M10K/RAM-shrink track, per the roadmap's own
ordering.

**Parked (2026-09-22, not acted on):** broader type/font support — CJK, crispness at scale, multiple typefaces —
researched against upstream HarpMudd v1.5.0's hardware-verified Japanese/UTF-8 work and recorded in
`PHASE_F_SPEC.md` section 13. Revisit there if this becomes a real near-term want.

**Re-parked deliberately, non-blit (owner, 2026-09-23, B-115):** the release-vs-diagnostic boot-restore mismatch
(escalated by the B-112 audit, no known mechanism) is left parked on purpose — a ground-up UI/UX redesign is
planned (`docs/ARCHITECTURE_ROADMAP.md`'s new "UI/UX redesign" item) and may change or remove the boot/idle code
path this bug lives in, so investigating it now risks wasted work. See the "Not validated / deliberately open"
bullet above and `docs/issues/021-boot-restore-release-vs-diagnostic-mismatch.md`.

## Where to look

- **The active plan for what's next: `docs/PHASE_F_SPEC.md`** (see the section above).
- Full state, decisions, procedures, open items: `docs/SESSION_HANDOFF_2026-09-22_RELEASE_0.4.md`.
- Design docs: `docs/MEDIA_LIBRARY_0.4_SPEC.md`, `docs/PHASE_G_SPEC.md`, `docs/TEST_SUITE_SPEC.md` (the Check).
- Earlier architecture decisions (still correct): `docs/SDRAM_MEMORY_ARCHITECTURE.md`, `docs/PSRAM_IMPLEMENTATION_PLAN.md`,
  `docs/PSRAM_TIMING_CONTRACT.md`, `docs/SETTINGS_ARCHITECTURE.md`, `docs/MMIO_ALLOCATION.md`.
- Roadmap and ordering of what's not started: `docs/ARCHITECTURE_ROADMAP.md`.
- Older issue records: `docs/issues/` (001 = BUG-001, accented names, still open; 018/019 resolved in the A-series).
