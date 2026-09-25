# Current engineering status

**MILESTONE, 2026-09-25 (B-197): the whole B-166..B-194 "Blit Test hangs" saga is fully resolved.**
The test was never hanging. `bt_advance()`'s terminal branch (`fw/suite.inc`) set `bt_state` to
`BT_DONE`/`BT_QR` on completion but never set `set_dirty` -- the one flag `fw/player.c:10882` gates
the entire UI redraw dispatch on -- so the screen froze on the last running HUD frame forever with
zero visual difference between "still running," "just finished," and "genuinely hung." Found via a
live JTAG read of the `DBGM` checkpoint (B-195/B-196's own new instrumentation) showing it constant
at crumb 20 -- `bt_finish()`'s own last checkpoint -- while `PCAD`/`IFPS` kept changing (real CPU
execution continuing elsewhere) and `BLIT` stayed idle: the test had actually *completed*, not
stalled. One-line fix (`set_dirty = 1u;` before the terminal `return;`), hardware-confirmed: the
Blit Test now reliably shows "BLIT TEST DONE" with a QR report, and the first full run decoded
clean -- **all 24 (op, level) windows PASS, 0% stall, across every opcode (RUN/RECT/CHAR/COPY/BLIT/
BAR/SBLIT/CBLIT) at all three concurrency levels.** The entire blit engine (Tier 1 B1/B2/B4/B5/B6
plus B8's CBLIT) is now validated together in one clean hardware pass. Full account:
`docs/AUDIT_TRAIL.md` B-195 through B-197 (the addendum has the decoded QR result).

**Earlier in the same session (2026-09-24): full handoff in `docs/SESSION_HANDOFF_2026-09-24_BLIT_TEST.md`.**
B8 (CLUT blit) is done and proven on real hardware. A new "Blit Test" diagnostic hangs on real
hardware; four source-level RTL fix attempts did not resolve it, and **ISSP's first real hardware
read (B-186) proved the draw engine itself is not hung** -- normal idle/scanline-fill cycling,
empty command FIFO, no opcode in flight. A follow-up CPU-side checkpoint probe (B-187/B-188) first
*looked* like it found something bigger (`bt_begin()`'s own first line never executing), but
**B-189 found that specific read unconfirmed** (stale firmware predating the probe register) and
**B-190 redid it correctly**: `bt_begin()` really does never execute, but the PSRAM instruction-fetch
arbiter is completely idle when it happens -- weakening rather than confirming the PSRAM-fetch-stall
hypothesis. **B-191 (MILESTONE) resolved it completely**: a fourth ISSP probe (PCAD, raw
`iADR`/`dADR`/cycle signals) plus a disassembly of the frozen PC found the CPU stuck on
`lhu s0,964(a5) # a00003c4` -- **`bt_crumb_read()`'s own SDRAM halfword readback**, called from
`bt_draw()`'s idle-screen branch on *every* redraw, before the user's Start press ever reaches
`bt_begin()`. The diagnostic checkpoint mechanism B-176 built specifically because it "cannot hang
even if the draw engine itself is what's stuck" is itself what hangs. Leading hypothesis for *why*
(not yet confirmed): `blit_probe()` uses the identical unguarded raw-pointer SDRAM-window access and
works reliably, but only ever runs after `blit_probe_ensure()` has fired at least once (from
`bt_begin()` or from drawing a meter-preview thumbnail in Settings) -- if the user reaches
Diagnostics > Blit Test without ever seeing a meter thumbnail first, `bt_crumb_read()`'s access may
be the very first CPU touch of this SDRAM window all boot. **B-192 A/B-tested and ruled this out**:
the identical hang occurs on pre-HarpMudd firmware, on the real shipped (non-ISSP) bitstream, and
even right after a proven-passing Window Test -- it is a real, pre-existing, unconditional bug, not
caused by anything built this session. Root cause, confirmed: `0xA0000000`-relative raw CPU
pointers hang unconditionally (the draw engine's own framebuffer/guard region, below the CPU's
actually-mapped SDRAM window which starts a full 1 MiB later at `PL_SDRAM_BASE`/`0xA0100000`).

**B-193 fixed all three affected functions**: `bt_crumb()`/`bt_crumb_read()` (B-176's own
checkpoint) relocated onto `PL_SDRAM_BASE`; `blit_probe()` rewritten to use the SDRAM mailbox
instead of a raw pointer (its sentinel can't simply move -- `fb_cmd_addr` is hard-capped at 19 bits,
so the draw engine can never reach `PL_SDRAM_BASE` at all); `set_thumb_flat_build()` (same bug, found
proactively) rewritten to issue `fb_rect()` draw-engine commands instead of per-pixel CPU writes.
Two secondary bugs found and fixed along the way in `blit_probe()`'s mailbox rewrite: an unverified
partial byte-enable assumption (caused a false "not detected"), and a genuine cross-client race
between the draw engine's SDRAM write and the CPU mailbox's own readback (fixed with an explicit
settling delay). After all three: the Blit Test idle screen no longer hangs, `BLIT_READY()` passes,
and `bt_begin()` completes into `bt_advance()`'s normal per-tick loop.

**B-194: a new anomaly, not yet resolved.** Past all three fixes, the Blit Test itself now runs far
longer than its fixed ~48-second design duration (`BT_OPS*BT_LEVELS*BT_WIN_S`) without completing,
hanging on one instruction, or resetting -- live-monitored for 5+ minutes with the PC still visibly
moving but the CPU-side checkpoint stuck reading exactly the same value the entire time, suggesting
`bt_advance()` isn't being freshly re-entered as a new tick any more even though something is still
executing. Diagnosis paused here at the owner's call ("we might well be here hours") rather than
keep waiting live. Needs a new ISSP probe (`bt_op`/`bt_lvl`/`bt_at`/`bt_ops_done`) to pin down
precisely -- not yet built.

Two real JTAG/tooling gaps found and fixed along the way (B-189/B-190): loading a core through the
Pocket's own menu silently overwrites a JTAG-loaded debug bitstream with the SD card's own `.rbf`,
and `issp_read_probe_data`'s correct form is positional (`issp_read_probe_data $path`), not
`-instance $path` (the latter silently returns the string `"error"` instead of raising).

**B-195: B-194 follow-up, firmware-only, not yet run on hardware.** Added the cheap-first-step
instrumentation the handoff suggested instead of a new ISSP probe: per-cell checkpoints inside
`bt_draw_cell()`'s loop, and full coverage of `bt_finish()`'s three sub-steps
(`sr_finish`/`sr_text`/`qr_encode`, previously zero crumb coverage and the single least-tested path
in the whole test) plus the post-loop fall-through in `bt_advance()`. `make test-host` and every
firmware target build clean; `dist/`'s release ROM rebuilt with the same change (Blit Test isn't
`CHK_DEV`-gated). Not installed — awaiting the owner before the next card write.

**Uncommitted at session end:** `fw/blit_probe.inc`, `fw/settingsui.inc`, `fw/suite.inc` (B-193's
three fixes plus B-195's new checkpoints), plus `dist/Assets/tau/common/{tau.rom,tau-cold.bin}` (the
shipped `release` target rebuilt clean with the same fixes, since it also sets `TAU_METER_THUMBS`).
Card has the pre-B-195 build on `TAU_0_5_0_A_12`; other cores untouched. Read `docs/AUDIT_TRAIL.md`
B-188 through B-195, and `docs/JTAG_DEBUG_ACCESS.md` section 6, before continuing this thread.

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

**MILESTONE, 2026-09-25 (B-204): the peak-usage gate is built and gives a first real, reassuring
number.** Stack-painting high-water-mark measurement (`fw/start.S` paints the whole 16 KB stack with
a sentinel at boot; `stack_high_water()`/new `SR_T_STACK` report tag reads it back) — the standard
embedded technique, wired into every Check run. First hardware result (FULL profile, stress R1-3 +
Soak + Blit storm): **peak 1,980 B of 16,384 (12%), 14,404 B free** — a large margin under a
genuinely demanding load. Not yet section 4.1's actual prescribed worst case (ENDURANCE + full
library + large playlist + fresh cover decode + browse-while-playing + every meter mode combined),
but given the margin already shown, the real worst case would need to be ~9x deeper to become a
concern. That comparison is the one test still needed before the RTL shrink itself is scoped.

**Status update, 2026-09-25 (B-211): B11's Quartus fit FAILED timing (-2.366 ns worst setup slack).**
RTL/sim (B-205, below) is still correct; the fit found a genuinely new combinational chain inside B11's
own corner sequencer (`rrect_row` -> subtract -> cut-LUT read -> two wide address adders -> `rect_addr`,
all in one cycle) — the same *shape* of bug as B-111/B-114, a different instance, not the `glyphbuf`
congestion that hit B8. Root cause found via `quartus_sta`, fix understood (retime the cut/address
computation one cycle ahead, matching B-111/B-114's technique) but **not yet applied**. Full detail:
`docs/AUDIT_TRAIL.md` B-211, `docs/PHASE_F_SPEC.md`'s B11 section.

**MILESTONE, 2026-09-25 (B-205): B11 (hardware rounded-rect) built and verified in RTL/simulation.**
`cmd_op` widened 3->4 bits (mechanical, every existing opcode's full test suite passes unchanged);
new `OP_RRECT` generalises `OP_BAR`'s one-shot `bar2_pending` into a bounded, LUT-driven corner
sequence (`R_RC_IDX`/`R_RC_DATA`, MMIO 0xD4/0xD8, 16 entries x 5 bits, never computed live in RTL).
Verified with hand-computed test cases (mixed real-segment/skipped-row + a plain `r=0` case) and a
new mutation hook (`BUG_IGNORE_RC_CUT`) confirmed caught -- found and fixed a real bug in the test
itself along the way (an unbounded `wait()` that a row-count-reducing mutation makes unreachable,
hitting the global timeout instead of a "FAILED" verdict). Full `make test-rtl` passes clean, no
regression. Not yet fit on Quartus, no firmware integration. **B10 (RLE source blit) was scoped
alongside it but two real gaps surfaced while starting its RTL** (the source data needs staging into
SDRAM first -- a cost the design assumed it avoided; the CLUT read address can't be shared with
`OP_CBLIT` unmodified) -- design refined and recorded (`PHASE_F_SPEC.md`), owner chose to defer the
actual build to its own dedicated pass rather than rush it alongside B11.

**MILESTONE, 2026-09-25 (B-203): picojpeg moved to cold code, the RAM-shrink's headroom target is
now cleared.** `objcopy --rename-section` on the vendored, unmodified `third_party/picojpeg/
picojpeg.c` object file (it compiles separately from the project's own `.inc` translation unit, so
the usual `COLD_FN` attribute didn't apply) -- `art_decode()`, picojpeg's only caller, is already
cold and already `COLD_READY()`-gated, so no new gate was needed. **+11,456 B measured** (bigger
than the ~8 KB estimate `PHASE_F_SPEC.md` had cited). Combined with the meter conversion's +19,520 B:
**+30,992 B total, clearing the ~29 KB the RAM shrink needs.** New opt-in toggle `PICOJPEG_COLD`
(default off) -- a real process gap was caught and fixed along the way: the first cut gated only on
`TAU_COLD_CODE=1`, which `release` already sets, so a routine rebuild silently changed the shipped
ROM with zero hardware validation before this was caught via `git status`. Not yet hardware-tested;
`release` itself is untouched. Next: install a diagnostic build with `PICOJPEG_COLD=1` and confirm
real cover-art decode still works, then section 4.1's peak-usage gate is the one remaining blocker
before the actual RTL shrink.

**MILESTONE, 2026-09-25 (B-202 addendum): hardware-confirmed.** First real run on `TAU_DEV_47`:
`CT_COLDFRAME` **PASSED, 27,308 cycles worst-case** (~455 microseconds, ~1.73% of the 26.3 ms
budget) for the real `ui_draw_dynamic_cold()`, 0 late underruns, `audio_full: true`. Higher than the
tiny synthetic probe (expected -- real function, real visualizer code) but well within budget with
real margin. G4 step 4 is functionally proven on real audio playback. Next: an ENDURANCE soak before
considering promoting `G4=3` to release's default.

**MILESTONE, 2026-09-25 (B-213): the ENDURANCE soak is in and PASSED.** `TAU_DEV_47`'s full ENDURANCE
profile: **all 10 checks passed**, including Blit storm (30s) and Cold frame (30s) together -- 0 late
underruns, `audio_full: true`, `stall_ms: 0`, cold-frame cost 28,847 cycles (consistent with the
27,308-cycle figure above, now reconfirmed under sustained load rather than a single run). This is the
hardware-confirmation gate `PHASE_F_SPEC.md` section 14 row 5 named as the remaining blocker before
promoting `G4=3`/`PICOJPEG_COLD=1` to `release`'s defaults -- met, and **done (B-214, same day):**
`fw/build.sh`'s defaults flipped, `release` rebuilt (heap gap 30,528 -> 61,808 B, RAM 82.6% -> 65.3%),
every other build target reverified clean, `make test-host` passes. Not committed yet. Separately read (opportunistically) `TAU_DEV_49`'s
persist file: FULL profile, clean except the pre-existing unrelated `Track changes` failure -- not the
same thing as section 4.1's own prescribed worst-case stack exercise, whose actual peak-byte reading
still hasn't been read back from a QR screenshot of that specific run.

**MILESTONE, 2026-09-25 (B-202): the real meter/cold-code conversion is built.** `ui_draw_dynamic()`
split into a thin hot wrapper and `ui_draw_dynamic_cold()` (`COLD_FN3`, a new G4 step 4 gated on
`TAU_G4 >= 3`), with the exact same `COLD_READY()` fail-safe every other G4 step uses and `CT_COLDFRAME`
now permanently measuring the real function (not just the synthetic probes B-199-201 built it with).
Built and confirmed: `G4=3` heap gap **23,440 -> 42,960 B (+19,520 B)**, matching the ~17-20 KB
"meters" estimate almost exactly. `release` stays `G4=2` (untouched functionally; a 400 B code-size
cost from the wrapper split itself, not a behavior change). **Not yet hardware-tested** -- next step
is installing a `G4=3` build and running `CT_COLDFRAME` for real numbers on the actual function, plus
an extended soak, before considering promoting this tier to release's default.

**MILESTONE, 2026-09-25 (B-199/B-200/B-201): the M10K/RAM-shrink track's cold-code question is fully
measured, and the answer is clean.** Three scenarios, all hardware-measured over real 30 s playback
windows at `ui_draw_dynamic()`'s own real ~38 Hz call rate: a small I-cache-resident probe under
ordinary idle playback (3,309 cycles worst-case), the same small probe under a forced full-eviction-
every-call worst case simulating browse-while-playing (3,067 cycles -- no thrashing amplification),
and `cold_big`'s deliberate always-cold worst case (94,801 cycles, matching the written prediction to
within 1 cycle). **All three PASS, zero late underruns.** This does not support `cold.inc`'s blanket
"nothing per audio frame may be cold" rule -- it was written a day before the blit engine existed and
has now been tested against every scenario it was meant to guard against. Recommended next: narrow
the rule's wording, then convert the real `ui_draw_dynamic()` to `COLD_FN` with a permanent
`CT_COLDFRAME`-descended regression test, the same way `CT_BLT` guards the blit engine's own SDRAM
traffic. `docs/PHASE_F_SPEC.md` sections 4.2-4.3 have the full numbers and reasoning.

**M10K/RAM-shrink track, 2026-09-25 (B-199): a real conflict found and a measurement scoped, not yet
resolved.** Continuing the track after B-197/B-198 unblocked it, found that `fw/cold.inc`'s own
Phase G1 rule ("nothing that runs per audio frame may be cold") directly forbids the planned
`ui_draw_dynamic()` COLD_FN move -- a real conflict between two of this project's own documented
decisions, not a new external constraint. The rule predates the blit engine by a day and was written
against a total unknown; it has never been re-measured against real PSRAM-fetch numbers that now
exist (B-047/B-054). `docs/PHASE_F_SPEC.md` section 4.2 scopes a concrete measurement (two synthetic
cold probes, a new `CT_COLDFRAME` Check test, run under idle playback and the browse-while-playing
worst case) to settle whether the rule can be narrowed before either converting the real meter code
or accepting the shrink is off the table. Not yet built. Font-to-PSRAM (+12 blocks, section 3) is
confirmed independent of this question and remains a standing, unstarted option either way.

**M10K/RAM-shrink track, 2026-09-24 — scoped, held, now unblocked (2026-09-25, B-197).** Owner asked whether
it's safe to move ahead. Found and documented the real gate (`docs/PHASE_F_SPEC.md` section 4): meters must go
cold before the shrink, and that needs the meter drawing rewritten to use the blit engine first (a much smaller
per-frame instruction footprint is what makes moving it to PSRAM affordable). Scoped the first concrete step —
the plain-bars mode matches `OP_BAR`'s exact convention, `fb_bar()` already exists, a small surgical change (two
`fb_rect()` calls -> one `fb_bar()` call, `BLIT_READY()`-gated with the existing code as fallback). **Held at the
time** because enabling it meant calling `blit_probe_ensure()` from the live playback path, and that function
was implicated in the then-unresolved Blit Test hang. **B-197 resolved that hang completely and found it was a
UI redraw bug unrelated to `blit_probe_ensure()`/`BLIT_READY()`/the draw engine itself** (B-193 had already
fixed the actual `blit_probe()` SDRAM bug, and B-197's own hardware run proved the whole opcode set + probe path
clean, 0 stalls) — the original reason to hold no longer applies. Full write-up: `docs/PHASE_F_SPEC.md` section 4.

**MILESTONE, 2026-09-25 — the active plan is now `docs/HELIOS_SPEC.md` (Helios/Talos).** Same-day arc:
B11's real timing violation was found and fixed (retiming, same technique as B-111/B-114) and the RAM-shrink
RTL's own inference bug was found and fixed (a split-region ternary read broke Quartus's pattern-matcher) —
**a combined fit closes cleanly on both seeds, all four corners positive, RAM Blocks 235/308** (B-235,
MILESTONE). The RAM shrink's own firmware side is NOT yet usable, though: `fw/link.ld`'s opt-in 192 KB
ceiling was built (with a real toolchain gotcha found along the way — `DEFINED()` has no effect inside a
`MEMORY` block's `LENGTH` in this toolchain) and, correctly wired, shows `release` currently **short by
~12.6 KB** against the 192 KB target — the earlier "+29 KB clears it" accounting has been eroded by real
feature growth since (B-236). **B-244 (2026-09-25) closed part of that gap:** all of `fw/playlist.inc`
(except `pl_cmd`, deliberately left hot) and `fw/settingsui.inc`'s remaining functions converted to cold
code, each confirmed reachable only via the same top-level `COLD_READY()` gate `library.inc`/`art.inc`
already rely on — a measured 4,416 B recovered, leaving `release` short by **~8.5 KB**, not ~12.6 KB. The
remaining candidates (`read_track_head`/`load_track`/`id3_text_body`, core UI functions in `player.c`) need
individual boot-order tracing before converting — `settings_load()` was checked and rejected this pass since
it runs before `cold_boot_load()` at boot and would crash if made cold. Separately: the long-reported UI tearing was root-caused (no frame-synchronized
draw commit exists anywhere) and a full UI-controller/graphics-library design was written up after prior-art
research (Amiga Copper/blitter, PS1 Ordering Tables, LVGL, u8g2, MiSTer OSD) — named **Helios** (the library)
over **Talos** (the blit engine RTL, B1-B11). `docs/HELIOS_SPEC.md` is the complete proposal, superseding
`docs/PHASE_F_SPEC.md` section 15 (B-232, B-237). Owner: "let's build Talos and Helios" — build order is
`docs/HELIOS_SPEC.md` section 9 (H0 vblank MMIO / H1 Helios core + `OP_RRECT` conversion + B13's gradient
bar / H2 double buffering, held). Four Winamp Bars/Scope bugs from the same session were found and fixed
(B-234), installed as `TAU_DEV_51` after a card cleanup down to three cores (`TAU`/`TAU_DIAGNOSTIC`/
`TAU_DEV_51`) — not yet run on hardware. The 192 KB shrink's own remaining firmware trim is down to **~8.5 KB**
after B-244's playlist/settingsui cold-code conversion. **H1's display-list core itself is started**
(`fw/helios.inc`, B-244/H1 entry) but deliberately inert — no real screen routes through it yet, held until
H0's vblank MMIO is hardware-confirmed. **B-239's "try blend alongside everything else" experiment came back
negative (B-243):** `TAU_BLIT_BLEND` re-enabled on top of B11's fix + the RAM shrink + H0 does NOT close
timing (worst-case setup -2.972 ns) — blend stays shelved; the no-blend B-235 fit (timing-clean) is still the
one that will actually carry `TAU_VBLANK`/the RAM shrink to the card, and hasn't been installed yet. The
pre-existing `Track changes` Check failure (open since B-138) remains open, unrelated to Helios/Talos.
**B-245: caught before any card write** — B-235's RBF physically only has 192 KB of on-chip RAM
(`TAU_RAM_192K` removes 64 KB of BRAM at synthesis time, not a runtime toggle), and NO current firmware
build links within 192 KB yet (checked all five actively-used targets, all fail the same way `release` did
in B-244). Installing it with any of today's 256 KB-linked firmware would silently alias/corrupt RAM on
hardware — `fw/link.ld`'s own comment already named this exact danger and assumed it couldn't arise.
**Hold this whole install until at least one real firmware build actually links under `RAM_192K=1`** — the
~8.5 KB (or more, for other targets) cold-code trim is now a hard prerequisite for adopting the RAM shrink
at all, not just a nice-to-have.

**B-246: sidestepping the blocker for now, per the owner's "most functionality now" call.** Launched a
separate fit with B11's fix + H0 vblank but WITHOUT `TAU_RAM_192K` (RAM stays physically 256 KB — no firmware
trim needed, every existing build is safe on it) and without `TAU_BLIT_BLEND` (B-243: doesn't close). Reuses
B-134's already-proven `blit_g3` macro bundle, which closed cleanly two days ago; adding B11's already-clean
retiming fix and a small vblank CDC on top is expected to close at least as well as B-235 did (which carried
strictly more logic, the RAM-shrink RTL, and still closed). Result pending on the VM. If clean, this becomes
the first hardware install path for both B11 (`OP_RRECT`) and H0 (vblank) — unblocking Helios H1's next real
step without waiting on the RAM shrink at all. The RAM shrink itself stays exactly where B-245 left it,
untouched by this.

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

## Parked: firmware modularization
`docs/FIRMWARE_MODULARIZATION_PLAN.md` (B-254): phased plan to turn the monolithic `fw/player.c` into real modules. Not started; waits for the key features and the owner's architecture decisions (its section 5).

## Where to look

- **The active plan for what's next: `docs/PHASE_F_SPEC.md`** (see the section above).
- Full state, decisions, procedures, open items: `docs/SESSION_HANDOFF_2026-09-22_RELEASE_0.4.md`.
- Design docs: `docs/MEDIA_LIBRARY_0.4_SPEC.md`, `docs/PHASE_G_SPEC.md`, `docs/TEST_SUITE_SPEC.md` (the Check).
- Earlier architecture decisions (still correct): `docs/SDRAM_MEMORY_ARCHITECTURE.md`, `docs/PSRAM_IMPLEMENTATION_PLAN.md`,
  `docs/PSRAM_TIMING_CONTRACT.md`, `docs/SETTINGS_ARCHITECTURE.md`, `docs/MMIO_ALLOCATION.md`.
- Roadmap and ordering of what's not started: `docs/ARCHITECTURE_ROADMAP.md`.
- Older issue records: `docs/issues/` (001 = BUG-001, accented names, still open; 018/019 resolved in the A-series).
