# Current engineering status

**Latest session (2026-09-24): full handoff in `docs/SESSION_HANDOFF_2026-09-24_BLIT_TEST.md`.**
B8 (CLUT blit) is done and proven on real hardware. A new "Blit Test" diagnostic hangs on real
hardware and four source-level fix attempts have not resolved it -- do not attempt another blind
fix; ISSP (live JTAG register/state readback) is built, fit-proven, and its procedure is verified
against the real tooling, waiting on cable access. Read that file before continuing this thread.

**Earlier snapshot:** 2026-09-22. Tau **v0.4.0** is released and installed on the owner's card (media library, Phase G cold code,
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
  an album pick - not reproduced from source, needs a description/screenshot of the gap when revisited. **Open question,
  not yet investigated (owner, 2026-09-23):** does the library index or the cold image need a migration path across real
  Tau version updates - a schema bump, playlist entries surviving a re-sync, an old index on new firmware. Prompted by
  B-136 (a stale-index bug in a test-build install, not a real version update, but a real coupling worth designing for
  deliberately). Full writeup: `docs/MEDIA_LIBRARY_0.4_SPEC.md` section 15.

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

**MILESTONE, 2026-09-23 (B-146): the blit engine's first real hardware load test — PASS.** After the install
finally worked (three unrelated card bugs found and fixed in between: a mis-rooted library index B-136,
`core.json`'s undocumented-to-us field limits B-141/B-142, and a stale Pocket catalog cache B-143), `TAU
0.5.0-alpha.4` ran STANDARD and FULL Check twice each: **`Blit storm` PASS both times, SDRAM 15.8% busy over
the window, audio confirmed continuous throughout (not ambiguous the way an earlier run was) — zero late
underruns.** The engine is now correct in simulation, timing-closed on real hardware, and load-tested on real
hardware with real audio — all three legs of proof this phase needed. `Track changes (10)` still fails in
both runs, unexplained. Separately: Start now closes the library/legacy-playlist overlays outright (B-145,
owner request), not yet installed/run on hardware.

**B8 step 1 (CLUT blit) built and simulation-verified, 2026-09-23 (B-148)** — new `OP_CBLIT` opcode, zero
regression on the hardware-verified paths. **Its Quartus fit did NOT close timing, 2026-09-24 (B-150)** — a
small but real setup violation on both seeds (worse: -0.196 ns; better: -0.025 ns), the CLUT's physical cost
(a new dual-clock M10K + logic) eating into B-134's margin.

**MILESTONE — B8 timing FIXED, 2026-09-24 (B-157/B-158/B-159).** `quartus_sta` traced the exact violating path
to a pre-existing, unrelated `A_COMPOSE` glyph-blend adder (`px_color`/`mix_b`) — `OP_CBLIT` merely widened the
shared `glyphbuf` write-select network enough to expose it, not a CLUT-specific slowness. Fix: split
`A_COMPOSE` into two states so `px_color` is registered one cycle before the write (cost: one extra cycle per
composed pixel, ~1.5% -> ~3% of scanline slack — cheap). Sim-verified (`test-rtl`/`test-host` clean), re-fit
closed on the **first attempt**: Slow 85C setup +1.787 ns, Slow 0C setup +1.383 ns (was the -0.025 ns
violation), hold positive on both — real margin, not a bare pass. **B8 step 1 is now timing-proven for the
product configuration.** RBF `9a1bc223...` saved in `work/diagnostics/blit-cblit-b157-20260924/`. B0.5.0 (the
working `TAU_0_5_0_A_N` line) is unaffected and still the pre-B8, pre-fix configuration on the card.

**Firmware integration done, 2026-09-24 (B-160).** `fb_clut_load()`/`fb_cblit()` added; `set_draw_thumb()`
issues one `OP_CBLIT` per thumbnail, sourced from a one-time-at-boot-lazy flat-buffer expand (~16 ms).

**A real boot-blocking bug found and fixed, 2026-09-24 (B-161/B-162).** The first hardware install
(`TAU_0_5_0_A_6`) hung on every boot — `blit_probe()` ran unconditionally at boot for the first time ever on
real hardware and its `fb_wait()` never returned. Rolled back the card, deferred the probe to first actual use
(`blit_probe_ensure()`, never at boot), rebuilt, reinstalled as `TAU_0_5_0_A_7`.

**MILESTONE — B8 proven end to end on real hardware, 2026-09-24 (B-164).** `TAU_0_5_0_A_7` boots normally,
Settings opens cleanly, and a 30s `Blit storm` Check **PASSED**: SDRAM busy 15.8% (identical to B-146's
measurement on the pre-CBLIT bitstream — no added contention), audio continuous, zero late underruns. Every
other test passes except the pre-existing, unrelated `Track changes` failure (confirmed still present, not a
regression). **B8 is done**: RTL/sim-correct, timing-closed, firmware-integrated, and hardware-verified.

**B9 (palette re-index), 2026-09-24 (B-179) — RTL/sim only, no Quartus fit.** Built while waiting on JTAG cable
access for the Blit Test hang (see the 2026-09-24 session handoff) — independent of that thread and of the card,
which stays untouched. An 8-bit sticky offset (`blt_reindex`, section 9 field 6) added to `OP_CBLIT`'s palette
index before the CLUT lookup — "re-index, don't blend," the Genesis shadow/highlight trick — reusing B8's CLUT
read port exactly, one adder, no new state. Verified: `tb_blit_scene.v`'s scene gained a 13th command (reindexed
CBLIT through a separate preloaded CLUT bank), `blit_reference.py`'s `cblit()` gained a matching parameter, new
mutation hook `BUG_IGNORE_REINDEX` caught. `make rtl-lint`/`test-rtl`/`test-host` all pass, 0 failures, zero
regression anywhere (confirmed via the full `make test-rtl` suite, not just the touched testbenches). Not done:
Quartus fit, firmware integration (no shadow/highlight palette baked yet) — full detail: `docs/PHASE_F_SPEC.md`
section 5's B9 write-up, `docs/AUDIT_TRAIL.md` B-179.

**B11 (hardware rounded-rect), 2026-09-24 — analysed, design only, not built.** Owner chose design-only over a
full build this pass, given real scope found: `cmd_op` is already full (all 8 values of the 3-bit field taken,
`OP_CBLIT` was explicitly "the last value it has room for"), so a new opcode needs it widened to 4 bits — the
FIFO already has exactly enough reserved padding to do this for free, but it touches the shared, hardware-
verified `R_FB_GO`/`cmd_op` decode path every existing opcode depends on. The corner-cut geometry (an iterative
quarter-circle search) should NOT be computed live in RTL, per this session's own repeated lesson about single-
cycle timing margins (B-109/B-111/B-150/B-157) — the right analogue is B8's own choice: firmware loads a small
precomputed cut-per-row table via a new sticky MMIO pair, RTL just reads it. Full design (field reuse, dispatch
shape generalising BAR's chained-segment trick, verification plan): `docs/PHASE_F_SPEC.md` section 5's B11
write-up. Precise enough to build cold next time.

**B10 (hardware RLE source blit), 2026-09-24 — analysed, design only, not built; value re-assessed downward.**
Read the real target before designing (`set_draw_thumb()`/`set_thumb_flat_build()` in `fw/settingsui.inc`) and
found B10's original motivation is already substantially met: B8 step 1 (shipped, hardware-proven) already
avoids per-draw RLE decode by expanding the RLE data into flat index buffers once, lazily, per session (~16 ms
total) — every redraw is already one `fb_clut_load()` + one `fb_cblit()`. B10 would only additionally save that
one-time ~16 ms and ~38.5 KB of SDRAM (not the scarce M10K resource). Design recorded anyway for completeness:
each decoded RLE run becomes a chained `OP_RECT`-shape constant-data burst of `clut[idx]` (cheaper than
`OP_CBLIT`'s own per-pixel reads), sharing B11's identified `cmd_op`-widening need and its "chain of bursts"
sequencer shape — the one genuinely new wrinkle is a *data-dependent* source-consumption rate (unlike every
other opcode's fixed one-word-per-pixel/row rate), which needs its own mutation-test design, not a copy of an
existing hook. Full design: `docs/PHASE_F_SPEC.md` section 5's B10 write-up.

**M10K/RAM-shrink track, 2026-09-24 — scoped, then held.** Owner asked whether it's safe to move ahead. Found
and documented the real gate (`docs/PHASE_F_SPEC.md` section 4): meters must go cold before the shrink, and
that needs the meter drawing rewritten to use the blit engine first (a much smaller per-frame instruction
footprint is what makes moving it to PSRAM affordable). Scoped the first concrete step — the plain-bars mode
matches `OP_BAR`'s exact convention, `fb_bar()` already exists, a small surgical change (two `fb_rect()` calls
-> one `fb_bar()` call, `BLIT_READY()`-gated with the existing code as fallback). **Then held, deliberately:**
enabling it means calling `blit_probe_ensure()` from the live playback path, and that function is the one
implicated in the still-unresolved Blit Test hang. Owner's call: hold the entire track until ISSP gives a real
diagnosis, rather than build on top of an actively-suspect subsystem even with a safe read-only gate. Full
write-up: `docs/PHASE_F_SPEC.md` section 4.

**Next, in order:** B11 or B10's actual build (both designs are ready; B11 has the larger proven payoff — every
selected list row and the panel border, versus B10's now-marginal one-time/SDRAM savings). Then, once the Blit
Test hang has a real diagnosis (ISSP, waiting on JTAG cable access): resume the meter/blit integration and the
pre-existing `Track changes` Check failure (open since B-138, unrelated to B8) — both blocked either on the
hang diagnosis directly or on a card write the handoff says to hold until then.

**Parked (2026-09-22, not acted on):** broader type/font support — CJK, crispness at scale, multiple typefaces —
researched against upstream HarpMudd v1.5.0's hardware-verified Japanese/UTF-8 work and recorded in
`PHASE_F_SPEC.md` section 13. Revisit there if this becomes a real near-term want.

**Full upstream v1.5.0 review, 2026-09-24: `docs/HARPMUDD_UPSTREAM_1.5_REVIEW.md`.** Correction to the note
above: Tau's actual fork point is right before v1.5.0 (`git merge-base main v1.5.0`), not an early "v0.3.0"
baseline — the earlier note conflated Tau's own v0.3.0 release tag with upstream's identically-named tag.
So none of v1.5.0 has ever reached Tau, not just the CJK font. Also found an active `release/1.5.1` branch
with real hardware-measured work toward v1.6.0. Three real findings beyond the already-parked font work:
(1) **`clk_sys` 60 -> 66.667 MHz** (release/1.5.1, hardware-measured, +11.1% CPU headroom) — Tau shares the
exact same PLL/VCO constraint and has the identical two silent-failure hardcode traps upstream found
(`eq_biquad`'s `CLK_HZ`, `pcm_rate`'s reset default); directly relevant to the still-gated audio-kernel
decision, but needs its own Quartus experiment against Tau's own blit-engine timing margins before trusting
it, not a drive-by port. (2) **PCM FIFO start-of-track cushion** (v1.5.0) — a hardware-confirmed track-start
glitch fix; Tau's `pcm_fifo.v` is untouched since the fork, so this is a clean, low-risk, self-contained
port. (3) **Meters yield to audio** (`meter_afford()`, release/1.5.1) — firmware-only CPU-budget technique,
zero RTL, zero dependency on the blit engine or `blit_probe_ensure()` — buildable right now, in parallel with
waiting on the Blit Test hang's JTAG diagnosis, and a useful complement to the currently-held meter/blit
integration (B-182). Smaller items and drops in the review doc.

**Ported, 2026-09-24 (B-184): four of five items, `clk_sys` explicitly parked.** Owner: bring in the PCM
FIFO cushion, meters-yield-to-audio, the UTF-8/Windows-1252 tag guess, and the cassette meter; park
`clk_sys` 60->66.667 MHz until after the blit engine and the M10K/RAM-shrink track are done, at a minimum.
All four built and verified (`make rtl-lint`/`test-rtl`/`test-host` pass, every named firmware build target
still links). **The cassette meter (new `VIZ_TAPE`) needed real gating, not a drive-by port:** its drawing
code + hub table cost ~3.6 KB combined with the other three items, which the bare `player` target (no
library/cold-code infrastructure, `make firmware`'s own build, already at 84.5% of usable RAM) could not
absorb -- gated the heavy parts behind `TAU_METER_THUMBS` (the same "does this build have room for extras"
flag the meter-preview thumbnails already use), with a `VIZ_CYCLE_COUNT` that excludes `VIZ_TAPE` from the
meter-cycle button and the Settings choice list on builds without it, so nothing offers a mode it cannot
draw. The shipping `release` target has full headroom regardless (34,752 -> 31,088 B heap gap for all four
items combined). **A real, otherwise-undetected regression found and fixed along the way:** the pre-existing
`sim/tb_pcm_decay.v` hangs forever with the new priming cushion in place (it pushes one sample at a time,
which never crosses the cushion) -- confirmed the identical bug exists unfixed in upstream's own
`release/1.5.1` too, so this was latent, not introduced by the port. Fixed with a single priming burst before
the first decay check. The cassette's meter-preview thumbnail is a hand-authored placeholder (no source
photo exists for it, unlike the other ten) -- flagged in `fw/meter_thumbs.h`'s own header comment for a real
Figma export later. Full account: `docs/AUDIT_TRAIL.md` B-184, `docs/HARPMUDD_UPSTREAM_1.5_REVIEW.md`'s
status update.

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
