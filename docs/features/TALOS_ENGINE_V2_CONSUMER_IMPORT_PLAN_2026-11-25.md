# Talos engine v2: import plan per consumer (2026-11-25)

Companion to `TALOS_ENGINE_V2_GALLERY_FEATURES_SCOPING_2026-11-25.md` and `TALOS_CONSUMERS_INVENTORY_2026-11-25.md`. Owner decision D-T19: Tau Gallery, Tau V, Tau Reader and Tau Μ may be updated to the new engine; each imports by tag and does not edit its copy.

**Evidence label: HOST-ONLY, READ-AND-ARITHMETIC.** The other projects' folders were only read (no build, no write, no commit); git history of Tau V, Tau Μ and Tau Reader was read with read-only `git` commands. Effort figures are estimates in working sessions (one session = one focused working day of the owner's usual sessions), not measurements. **This document names old internals; it must NOT be given to the clean-room author.**

## 0. Summary

| Consumer | State today | What it would import | Engine share of that import | Feature set |
|---|---|---|---|---|
| **Tau Gallery** (published 1.0.0) | fork of Tau Alpha `12418aab` plus +220/-13 engine lines; fw2 gfx fork | the engine (and its wrapper deltas); nothing else has moved: Tau Alpha's `src/fpga/core` differs from the pin by 4 files (+172/-4: save RAM, `core_game.vh`, `core_top.v`, `tau_soc.v`) | **about 100 %** | GALLERY |
| **Tau V** (`tau-nu`) | the product (TVC video decoder, 3.9 k RTL lines) on a fork of Tau Alpha at `6072d13` (2026-10-10); 393 commits since | its plan: Tau Alpha's rewritten player (graphics stack, SDRAM wrapper and adapters, CPU bridge, UI firmware) | **small: about 6 % of Tau Alpha's hand-written core and firmware, about 15 % of the subset its own plan names** | V (all off) |
| **Tau Reader** (`Tau Reader`) | independent history (159 commits, none shared with Tau Alpha), own video RTL and scan-out, Pocket-verified baseline "fit 44", PLL copied from Gallery | at most the 400 x 360 UI path through Talos; the 800-wide reading mode stays its own | small and optional | READER (proposal) |
| **Tau Μ** | frozen snapshot of Tau Alpha (1,421 of 1,427 commits shared; last commit 2026-10-14, 6 commits after 2026-10-12) | nothing unless the owner revives it | n/a | MU |

**The Tau V claim (verified in section 3):** the owner's view was "Tau V is mostly a video player and has little of the old core left, so most of its import would be other parts of Tau Alpha's rebuild, not the Talos engine". Half of that is wrong by lineage and the conclusion is right: 80 % of Tau V's first-party lines still descend from HarpMudd or Tau Alpha (98.6 % of its SoC and glue RTL, 90.8 % of its firmware); its product, the video decoder, is new (3,898 RTL lines, 0.4 % inherited). The engine is a small part of what it would import, and the TVC video path never touches the engine at all.

## 1. What can be imported from Tau Alpha's rebuild (the pieces)

Sizes are source lines (nonblank, non-comment) from `docs/research/provenance-2026-11-25/files.csv`; "hand-written" excludes generated tables (font ROM/RAM, resampler and polyphase tables, the CPU).

| # | Piece | Size | Notes |
|---|---|---|---|
| P1 | **Talos engine** (`tau_talos`, queue inside it) | 985 (v2: new file) | the rewrite; feature parameters of the companion document |
| P2 | graphics wrapper `tau_gfx` | 184 | engine + scan-out + font + CLUT wiring |
| P3 | scan-out `tau_scanout` (12 MHz profile) | 132 | clean-room (D-T07); viewer profiles are a later contract |
| P4 | SDRAM client adapters and the `sched_a` wrapper | 99 + 97 + 184 | `tau_sdram_a_cpu`, `tau_sdram_a_gfx_if`, `tau_sdram_sched_a` |
| P5 | SDRAM PHY and `sched_v` | about 2,800 (3,176 for all `tau_sdram*` files, which includes P4) | tags `sdram-phy-v1`, `sdram-sched-v-v1.1`; `v1.2-rc1` is unmerged |
| P6 | support: Gray bus and counter, sync1, CLUT writer, vs counter | about 190 | small, shared by P1 to P3 |
| P7 | font RAM and ROM and their generators | 12,230 generated + generators | data, not logic |
| P8 | SoC: `tau_soc`, `core_game.vh`, `core_top`, bridge | 1,238 + 759 + 249 | MMIO map, CPU, clocks (bridge files not counted) |
| P9 | audio and meter units (Cymo, Halcyon, gain, PCM ring, polyphase, FLAC LPC, statistics, spectrum, wave) | about 1,100 | Tau Alpha's player |
| P10 | PSRAM block | 396 | |
| P11 | Helios and `fw2/gfx`, `fw2/platform` | 427 + 442 | the firmware side of the engine |
| P12 | Player 2 firmware (`fw2`: ui, app, settings, cores, transport, stream, aout, decode, art, diagnostics) | 14,645 hand-written | |
| P13 | packs, meters, SDK | SDK 11,659 | |
| P14 | settings and persistence (save RAM, watchdog, layout) | about 140 RTL + firmware | |
| P15 | build and release tooling (`install_dev_core.py`, `vm_fit.py`, `tau_compat`, `make_release`) | tools 25,826 | |
| P16 | benches and conformance vectors | sim 42,453 | |

**Engine pieces: P1 to P3 (and P6), about 1,409 lines** (talos 985, gfx 184, scan-out 132, CLUT 26, Gray and sync modules 82). Tau Alpha's hand-written core RTL is 9,137 lines and `fw2` 14,645: the engine is **5.9 %** of those 23.8 k lines. Of the subset that Tau V's own plan names (graphics stack, SDRAM wrapper and adapters, CPU bridge, UI firmware: P1 to P4, P8, P11, and the `fw2` ui, app, settings, gfx and platform folders: about 9.6 k lines) it is **14.6 %**.

## 2. Tau Gallery

* **Has today:** a fork of the pin `12418aab` imported by `git archive` (no history), engine +220/-13 lines, its own `tau_scanout_v`, `tau_vidmode`, `tau_pixfx`, SoC registers, a 40 MHz clock bundle, and `fw2/gfx` calling the engine (approximate call-site counts in `fw2`: rect 28, text 54, fence 26, corners 9, blit 4, dsblit 4, blend blit 3, cblit 3, bar 2, sblit 2, rrect 2, clut load 2). Published 1.0.0 (bitstream m1g seed 1, CORE_VERSION `4D50331F`). Fit m1f: 91 % ALMs, 94 % RAM blocks, 80 % DSP, hold +0.082 ns.
* **Would import:** P1 (engine v2 with the GALLERY set), the P2/P3 deltas (viewer ports and the `H_TOT`/`MODE_EN` ports of the scan-out are already Gallery's own text and stay Gallery's), and nothing else: Tau Alpha's RTL core differs from the pin by 4 files (+172/-4), none in the engine. **Engine share about 100 %** of the import, which is why Gallery is the first consumer.
* **Feature set:** GALLERY (CORNERS, DSBLIT, VIEW, VIEW_DBL). Opcodes first, then the viewer fill and doubler (D-T19 order).
* **Order:** (1) Tau Alpha ships engine v2 with the base set and the opcode features off (Pocket-confirmed in Tau Alpha). (2) Tau Alpha adds CORNERS and DSBLIT behind their parameters, proven in simulation against Gallery's vectors. (3) Gallery imports at that tag with CORNERS and DSBLIT on, VIEW and DBL still on its own old text (a tag that keeps both is a mixed build; acceptable only if the old viewer fill can coexist, otherwise do steps 3 and 4 together). (4) Tau Alpha adds VIEW and VIEW_DBL; Gallery imports the engine in full and drops its fork of `tau_talos.sv`.
* **Test in Gallery:** its own benches against the new engine: `make test-rtl-fb-mutation` (CORNERS 2 and DSBLIT 5 mutants plus the base set), `sim/tb_mp3_fb.v` cases, `tb_tau_gfx_view` (including `DBL = 1`), `tb_tau_scanout_v`, `tb_tau_vidmode`, `tb_view_soc`, `make test-fullcore`, `make test-gallery-ci`, `test_gallery_app.py` and its mutants (382), then the report gate against the m1f baseline; **pins:** whole-frame pixel equality of the viewer and doubler, DSBLIT and CORNERS vectors, the 1.0.0 grid and thumbnail pictures (golden to be built first). Pocket pages: the M0 test page (viewer, mode switch, landing), the grid with rounded thumbnails and each grid size, 2x and 4x zoom with the doubler.
* **Must not edit locally:** the imported engine file(s), the engine wrapper's feature logic, the scan-out profile files once they are imported by tag; changes go through the contract. Gallery keeps its SoC glue, firmware and decoder.
* **Collisions to fix at import:** viewer registers move off `0x1EC` to `0x1F4` (Tau Alpha save RAM) if Gallery ever shares a header with Tau Alpha; `CORE_VERSION` namespace (companion document 2.5). Gallery's published 1.0.0 pair is not touched.
* **Cost (estimate):** 2 to 3 sessions to import and re-run the benches, one two-seed fit (4 to 5 hours of VM time each at 91 % full: m1f took 4 h 55), one Pocket pass; plus the parameters' own work in Tau Alpha (not counted here).
* **Risk: high.** Fit headroom is nearly gone (companion document, risk 2); a seed already failed routing once; the new engine must not be larger than today's in the GALLERY set. Mitigation: map-only probe of the new engine in the GALLERY set before the import starts.

## 3. Tau V

### 3.1 Measured lineage (read-only, copy-aware blame)

Method: `git blame -w -M -C -C` of every tracked source file at `main` `c6de8540`, nonblank non-comment lines (the `measure.py` masks). Origin class of a line: **HARP** = its origin commit is by HarpMudd; **TA** = the commit also exists in Tau Alpha's repository (shared history up to the fork `6072d13`), or the file was copied by hash from a Tau Alpha tag into Tau V (the PHY and scheduler files, listed in its `UPSTREAM_TAU_ALPHA.md`; blame cannot see across repositories, so those 2,480 lines were reclassified by hand); **TV** = a commit only in Tau V. 1,419 of Tau V's 1,930 commits are shared with Tau Alpha; 393 commits are Tau V's own since the fork.

| Area | Lines | HARP | TA | TV | Inherited |
|---|---:|---:|---:|---:|---:|
| RTL, Tau V's own video decoder (`tau_tvc_*`, `tau_mp4v_*`) | 3,898 | 13 | 1 | 3,884 | **0.4 %** |
| RTL, SDRAM PHY and `sched_v` imported by hash from Tau Alpha tags | 2,488 | 8 | 2,480 | 0 | 100 % |
| RTL, inherited fork (SoC, `mp3_fb` engine, audio units, glue, ROM tables) | 25,245 | 12,046 | 12,848 | 351 | **98.6 %** |
| Firmware `fw/` (old player; the TVC player and TNV3 modules are a part) | 18,587 | 4,426 | 12,450 | 1,711 | **90.8 %** |
| Simulation | 22,209 | 4,368 | 12,736 | 5,105 | 77.0 % |
| Tools | 27,179 | 4,103 | 14,478 | 8,598 | 68.4 % |
| Analogue framework | 1,020 | 1,020 | 0 | 0 | 100 % |
| Vendored and reference code (`modules/`, `third_party/`, `ref/`, `toolchain/`) | 47,689 | 7,019 | 845 | 39,825 | 16.5 % |
| **First-party total (everything above except vendored)** | **100,978** | **25,044** | **55,927** | **20,007** | **80.2 %** |

The product code is new: the decoder RTL (3,898 lines) and the TVC firmware modules (`tvc_*`, `tnv3_core`, `stage_cache`, `audio_ops`, `memtest`: 1,287 lines). Everything around it is the old player's tree. Font and resampler tables inflate the inherited RTL figure (about 11 k lines are generated data), but excluding them the RTL glue (`mp3_soc.v` 1,121 lines, `core_game.vh` 1,022, `mp3_fb.sv` 948, `core_bridge_cmd.v`, `core_top.v`) is still almost entirely inherited. Verdict on the premise: **not "little of the old core left"**; the old core is the larger part of the tree, and the new part is the decoder.

### 3.2 Does the engine matter to Tau V today?

* **The video path (`tau_tvc_*`) does not use the engine.** A search of every Tau V RTL file for `mp3_fb`, `tau_talos` and `tau_gfx` finds the engine only in `mp3_fb.sv` itself, `mp3_soc.v`, `core_game.vh`, and comments (`tau_tvc_video.sv` cites `mp3_fb`'s geometry in its first line). The video output is Tau V's own `tau_tvc_video` at 400 x 360, 500 x 400 total, 12 MHz.
* **The TVC bitstream has no engine.** `core_game.vh` (the `TAU_TVC` branch) states "no CPU window to the SDRAM, no framebuffer engine, no `mp3_fb`"; menus and messages are a 25 x 22 cell text layer (`tau_tvc_text`, V-285), not engine commands.
* **The TVC firmware issues no engine command.** `fw/player.c` compiled with `TAU_TVC_FW` calls `tvc_main()` ("the TVC core has no framebuffer engine: no UI, play the test film and signal status by beeps") before any drawing.
* **The old engine and the old firmware still exist in the tree** for the non-TVC builds (the MPEG-4 and AV1 cores): `fb_*` call sites in `fw/player.c` and the `.inc` files: rect 200, text_clipped 123, fence 31, blit 24, bar 15, cblit 12, copy 10, char 8, text_boxed 7, rrect 5, sblit 5, copy_span 5, run 2 (the inventory's 208 and 7 counted other folders too). These are the realistic conformance load for COPY, RRECT, RUN and the CHAR scales (D-T18).

### 3.3 Engine share of the import, stated plainly

The engine (P1 to P3 and P6, 1,409 lines) is **a small part** of what Tau V would import: 5.9 % of Tau Alpha's hand-written core and firmware, 14.6 % of the subset its own plan lists. The large pieces are the player firmware (P12, 14.6 k), the SoC and glue (P8, 2.7 k), the SDRAM wrapper and adapters (P4, 0.4 k) and the UI. What the engine adds is the **risk concentration**, not the size: it is the block with the worst timing history and the one Tau V's own risk list names (RAM blocks 289 of 308 without a framebuffer, margin 0.18 ns at 80 % ALMs).

### 3.4 Plan for Tau V

* **Would import:** Tau V's plan (V-288, revised after V-289): Tau Alpha's rewritten player at a tag declared stable: P1 to P4, P6, P8, the matching UI firmware (P11, P12) and the settings (P14); P5 it already has by tag (PHY `rc1`, `sched_v` v1.1; Tau Alpha has `v1.2-rc1` unmerged), P9 only as far as its TVC audio uses it. About 10 of the 16 pieces, of which the engine is 3.
* **Feature set:** V = all features off. The UI path needs only the base nine opcodes (the old-player firmware shows what it issues). CORNERS could be turned on if its UI wants rounded boxes. A viewer feature is a possible later extension (Tau V's `O2` build ran 800 x 720 at 40 MHz on a Pocket, and Gallery took those timing positions from it) but is not planned.
* **Order:** after Tau Alpha's player tag; engine pieces arrive with P2 to P4 in one import; steps in its own plan: copy at the tag, attach the decoder as scheduler clients, map-only probe for blocks and ALMs, then RAM mitigations (CPU RAM 160 to 128 KB, macroblock store, font), RTL tests, a two-seed fit.
* **Test in Tau V:** `make test-rtl-tvc-core`, `-top`, `-top-sv` (named in its import notes), the golden-frame compare for the decoder into the framebuffer, its old-player drawing code run against the new engine in a host test (the first conformance input for COPY, RRECT, RUN, CHAR scales), and Pocket pages: TVC first light, the UI over video.
* **Must not edit locally:** every file imported by hash (its own rule: "fixes found here are to be reported to Tau Alpha, not edited into the copies"); the thin wrapper `tau_tvc_mem` stays Tau V's.
* **Cost (estimate):** the engine part is under one session of Tau V's own testing; the whole rebase onto the player is not estimable here (its plan says so) and is dominated by the UI firmware and the RAM and timing closure, not by Talos.
* **Risk:** RAM blocks and timing of the combined core (its own plan); a second fork point (`6072d13`) means Tau V's non-TVC code differs from Tau Alpha's after a month of Tau Alpha changes, so the import is a hand merge in `core_game.vh` and the SoC.

## 4. Tau Reader

* **Has today:** independent repository (159 commits, none shared with Tau Alpha, last 2026-11-22), Pocket-verified baseline "fit 44, seed 3" (`AGENTS.md`), its own video subsystem (`src/fpga/core/video`: scan-out RGB565, glyph blitter, glyph cache fetch, command bridge, fill engine, page flip, vidmode, about 27 files), video device ops of its own (`FILL`, `GLYPH`, `FLIP`), a copy of the SDRAM PHY and `sched_v` (`shared/sdram`), an Alpha-derived platform folder (`core_top`, `core_game.vh`, bridge, PLL files copied **from Tau Gallery** commit `f88767e`, Apache-2.0, `TAU_VID40` 40 MHz clocks), no Talos command issued yet.
* **Would import:** by its draft: "Talos split, status/fence, burst palette blits, clean-room scan-out and shared SDRAM PHY/scheduler" for the 400 x 360 catalogue and settings UI via Helios/Talos; its 800-wide reading mode keeps its own blitter and scan-out ("adapt Gallery's packed RGB888 fetch to RGB565"). Pieces: P1, P2, P3 (400 x 360 profile), P11 (Helios), P8's MMIO map (it already maps Alpha's watchdog registers `0x1DC` to `0x1E8` and the expansion window).
* **Engine vs rest:** small; it imports the engine to avoid writing a second UI blitter, not for the reading path.
* **Feature set:** READER (proposal): base set; CORNERS possibly for the catalogue; VIEW with `VIEW_LINE_WORDS` 800 (RGB565) only if the owner wants the viewer fill to replace its own 800-wide fetch. Its 40 MHz profile is the same clock as Gallery's, so the viewer scan-out contract of step 2 would be its natural reference. **Decision for the owner at its R milestone;** nothing is built for it before.
* **Order:** last of the active consumers (its first milestones do not need the engine).
* **Test in Reader:** its own `quartus_map` after RTL changes and the fit comparison against "fit 44", its host tests and the Pocket pages it already has; Talos conformance vectors for the commands it adopts.
* **Must not edit locally:** the imported engine and scan-out files; its own video RTL stays its own.
* **Cost (estimate):** 1 to 2 sessions for the UI path through Talos (fit and Pocket included separately); the 800-wide RGB565 viewer fill would be a further session after the contract exists.
* **Risk:** two video stacks in one core (its own and Talos) share one SDRAM scheduler and RAM budget; its RAM-block budget is its main constraint.

## 5. Tau Μ

* **Has today:** a snapshot of Tau Alpha at 2026-10-12 (1,421 of 1,427 commits shared, 6 commits after), the old `mp3_fb.sv`, the same old-player firmware as Tau V's non-TVC builds (same call counts), no `tau_gfx` split, no new SDRAM stack (it has `tau_sdram_arbiter` and the CPU bridge). Last commit 2026-10-14; not active.
* **Would import:** nothing is needed to keep it working: it copies by hash and is not affected by the rewrite. Updating it means the whole player rebase (P1 to P16), not the engine.
* **Recommendation:** leave it on its snapshot unless the owner revives it; if revived, treat it as Tau V's twin (V set, all features off) and import the player at a stable tag, not the engine alone.
* **Cost, risk:** none now; at revival the same as Tau V's player import, engine share small.

## 6. Order across projects

| Step | Where | What |
|---|---|---|
| 0 | Tau Alpha | engine v2 base set (B-full), Pocket-confirmed; the old engine stays until then (D-T01) |
| 1 | Tau Alpha | probe level 1 (map-only) of the features; ESC reserved; capability bits |
| 2 | Tau Alpha, then Gallery | opcode features CORNERS and DSBLIT, Gallery's vectors; Gallery imports the engine at that tag |
| 3 | Tau Alpha, then Gallery | VIEW and VIEW_DBL, scan-out viewer contract (second step); Gallery drops its engine fork |
| 4 | Tau V | after Tau Alpha's player tag; the engine rides with the player |
| 5 | Tau Reader | at its own milestone, UI path only; viewer question open |
| 6 | Tau Μ | no action unless revived |

## 7. Owner decisions and unknowns

1. Approve the order above (Gallery first, Tau V with the player, Reader at its milestone, Μ idle).
2. Tau V: confirm the V set (all off) and whether its non-TVC builds matter to the rebase (they carry the only live COPY, RRECT, RUN and CHAR-scale callers).
3. Tau Reader: adopt VIEW with an 800-word RGB565 plane, or keep its own scan-out (decide at its R milestone).
4. Tau Μ: leave frozen, or revive.
5. Gallery: allow map-only probes on the VM before the import (needs a yes for VM time).
6. Unknown, cheap to measure: the real command frequency in Tau V's non-TVC firmware (the engine's `R_GFX_COUNTS` or a host trace); whether Tau V's drawing passes against the new engine in a host test; whether Reader will issue Talos commands beyond its UI path.
7. Licence statement for Gallery-authored code that moves into Tau Alpha (Apache-2.0 repository, MIT here): one owner line in `NOTICE.md`.

## 8. What this document does not show

Tau V's real build flags per product, its non-TVC builds' current state, whether any of its fits would close with the new engine, Tau Reader's RTL beyond its docs and file list, and any Pocket behaviour. Blame figures are textual origin, not creative ownership and not a legal conclusion.
