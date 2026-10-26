# Tau Alpha: HarpMudd comparison, retained code and architectural rewrite

**Started:** 2026-10-12. **Completed:** 2026-10-13 (Europe/Lisbon). **Evidence:** Git/source analysis and host verification of the analysis artifacts. No firmware changes, runtime playback tests, FPGA synthesis, Quartus runs or Pocket tests were performed for this report.

**Conclusion:** approximately **28% of Tau Alpha's maintained, handwritten firmware/RTL source still has identifiable HarpMudd origin**: **27.49%** from copy-aware Git attribution, or **27.72%** when the surviving lines recorded by the documented v1.5 port commit are included. This is a source-line estimate, not a measure of exclusive authorship or a claim that the other 72% was independently rewritten. About **70.4% of the original fork's unique handwritten firmware/RTL source-line origins still survive**, counting detected copies once. The original foundation remains substantial, especially track loading, decoding, input dispatch and the main loop.

The full source inventory is [MODULE_INVENTORY.md](provenance-2026-10-12/MODULE_INVENTORY.md). The rewrite plan below addresses both inherited code and Tau's newer coupling; replacing only HarpMudd-labelled lines would leave the architectural problem largely intact.

## 1. Revisions and scope

| Comparison | Exact revision | Meaning |
|---|---|---|
| Tau Alpha | `ae11a8fee5a7cf3e7873a4a654bccf5a7df6b908` | Current checkout, branch `mod` |
| Tau main | `f93711ffde881f5268bd9336133d7d2e2c36dbf2` | Firmware, RTL, third-party sources, tools, simulation sources and `package.py` are identical to the audited `mod` checkout |
| Original fork | `7ef8f0fb84abd4ae5a6a187805fd910351ae57dc` | Documented v1.4.0-era baseline; also the actual merge-base with current upstream |
| v1.4.0 tag | `a03d567a4cf5da4b9888e0bcf43dd1168143ac26` | No changes between tag and fork point in `fw/`, `src/fpga/`, `third_party/`, `tools/`, `sim/`, or `package.py` |
| Current HarpMudd main | `f4b8f012e0514c6240a546cfb3b48e90488f5fef` | Verified against live `git ls-remote`; matches local `upstream/main` |
| HarpMudd v1.6.0 tag | `17d9f88c87866df1b557b4cf1fe1748fd9f27c11` | Current published release generation |

HarpMudd's [current repository](https://github.com/harpmudd/HarpMudd.mp3player) describes v1.6.0. All calculations here use the pinned local Git objects, rather than a moving website snapshot. This report concerns **Tau Alpha only**. Tau Omega, Tau Web, Tau Nu, `meter-builder` and `test/720` are separate checkouts/branches and do not dilute Alpha's denominator. `mod` adds tracker research/design documents, not implemented MOD playback.

The initial working tree was clean. The analysis examined **933 tracked paths**, classified **482 source/build-script files**, and produced **767 lexical firmware function-definition records**. It measured every eligible source file automatically, then reviewed the inherited hotspots, build/link interfaces, notices and architecture audits. This is not a claim that every function was manually correctness-audited or that every build variant was executed.

## 2. How the percentage was measured

1. Pin all three revisions and enumerate their Git trees.
2. Read source from those revisions, excluding untracked work, build artifacts and unrelated checkouts.
3. Count **nonblank, non-comment physical source lines (SLOC)**. Python docstrings count as source. Preprocessor directives and braces count; this is not a token or compiled-byte metric.
4. Run `git blame --line-porcelain -w -M -C -C` for every current source file. Whitespace changes, detected moves and historical copies do not automatically relabel surviving source as Tau.
5. Compare the blamed commits against fork/upstream ancestry, preserving original paths, commits and line ranges.
6. Separately count surviving lines last introduced by the explicitly documented HarpMudd port commit `0d5e734`. Its Git author is Abel Santos, so author-only counting misses this origin.
7. Compare each same-path file to both fork and current upstream, recording identical/modified/absent status and exact-line matching quantities.
8. Separate project-maintained implementation, tests/tooling, external code/frameworks, generated data and archives.

Source extensions: `.c`, `.cpp`, `.h`, `.inc`, `.S`, `.s`, `.v`, `.sv`, `.vh`, `.svh`, `.py`, `.sh`, `.ps1`, `.tcl`, `.ld`, `.sdc`, plus `Makefile`/`Umakefil`. JSON/manifests, `.qsf`/`.qip`, binaries, fonts, images and prose have their own tracked-file ledger and are outside the source percentage. The archived cassette's `.c.txt` file is recorded but outside this extension-based denominator.

**Limits of the measurement:** Git establishes textual history, not exclusive creative or copyright ownership. A changed line can remain a derivative implementation even though blame credits Tau. A manual port can later be edited and lose its port-commit label. Copy detection has thresholds and can attribute shared boilerplate in new files. Generated sources and vendor imports can misleadingly blame the person who imported them. The maintained-source category also includes HarpMudd's platform integration and shell adaptations; it does not prove every shell statement originated independently with him. Therefore use **“about 28% identifiable HarpMudd-origin source in maintained firmware/RTL”**, not an exact ownership claim or a guarantee that 72% is independent.

The principal scope includes optional diagnostic firmware/RTL stored in the implementation directories. It is a repository-source metric, not the fraction of bytes reachable in the shipped release. Feature macros, linker garbage collection and cold sections change what is actually compiled and retained.

### Scope-specific results

| Scope | Current SLOC | Direct HarpMudd/fork-origin lines | Recognised port footprint | Direct % | Including port footprint |
|---|---:|---:|---:|---:|---:|
| Maintained firmware, excluding generated headers | 15,698 | 4,425 | 43 | 28.19% | 28.46% |
| Maintained RTL/integration/constraints, excluding external/generated files | 5,316 | 1,351 | 7 | 25.41% | 25.55% |
| **Firmware + RTL** | **21,014** | **5,776** | **50** | **27.49%** | **27.72%** |
| Tools/build/package scripts | 17,641 | 3,610 | 4 | 20.46% | 20.49% |
| Tests/harnesses | 16,013 | 2,264 | 67 | 14.14% | 14.56% |
| **All maintained handwritten source, including tools/tests** | **54,668** | **11,650** | **121** | **21.31%** | **21.53%** |
| All eligible source, including vendor/framework/generated/archive files | 86,723 | 32,999 | 121 | 38.05% | 38.19% |

**The last row measures source inherited through HarpMudd's repository. It is NOT 38% written by HarpMudd.** It includes RealNetworks, Charles Papon, Adam Gastineau, Analogue, font-derived output and test data.

The original fork contained **8,033 SLOC** of maintained firmware/RTL under this classification. Deduplicating historical copies gives **7,788 unique original source-line identities**, of which **5,481 still appear in current maintained firmware/RTL: 70.4% survival**. Current inherited occurrences total 5,776; dividing occurrences by the old 8,033 gives 71.9%, but that mixes repeated copies with original quantities and is not the preferred retention measure. Current source grew to 21,014 SLOC. That growth explains how HarpMudd can represent only 27.5% of today's implementation while most of the old foundation remains. Growth, extraction and added features are different from replacement.

At file level, **75 current source files are byte-identical to the fork**, **42 are changed same-path files**, and **365 are absent at that old path**. Against current upstream: 74 identical, 44 changed and 364 absent. There are 15 old source paths absent from Tau today; absence at a path does not prove the implementation disappeared elsewhere.

### Corrections to the previous origin report

[CODE_ORIGIN_ANALYSIS.md](CODE_ORIGIN_ANALYSIS.md) is historical evidence, not a reliable current percentage. Its breakdown includes documentation as “code”; its displayed category totals do not reconcile with its headline totals; it calls the project's bespoke `fw/flac.c` a “Helix FLAC decoder”; it refers to `ui.c` instead of the present UI in `player.c`; and it describes Helix as effectively unmodified despite identifiable Tau changes. It also treats additions as a rewrite and asserts merge/bitwise compatibility without the required runtime evidence.

The README's “60% had already been re-written” should likewise be replaced with a dated, scoped measurement when documentation is next published. This report supersedes those percentages while preserving their history.

## 3. Which HarpMudd code is still present

### Maintained implementation files with substantial inherited code

| File | SLOC | Direct inherited SLOC | Direct % | Interpretation / rewrite priority |
|---|---:|---:|---:|---|
| `fw/player.c` | 5,397 | 3,172 | 58.8% | Core player, input, load/seek/refill, UI and meter logic remain extensively inherited; highest isolation priority |
| `fw/flac.c` | 694 | 508 | 73.2% | HarpMudd's streaming FLAC decoder with Tau Rice/LPC/profiling changes; separate codec replacement gate |
| `fw/flac.h` | 62 | 52 | 83.9% | Original streaming decoder API/state model persists |
| `fw/art.inc` | 340 | 282 | 82.9% | Original artwork scanning/decoding path plus Tau placement/loading changes |
| `fw/playlist.inc` | 179 | 141 | 78.8% | Original playlist open/parse/path flow remains |
| `fw/settings.inc` | 140 | 59 | 42.1% | Persisted-state integration substantially extended; versioned migration needs explicit ownership |
| `fw/alloc.c` | 31 | 31 | 100% | Original fixed decoder allocation arena, unchanged |
| `fw/sysio.c` | 25 | 25 | 100% | Original newlib syscall stubs, unchanged |
| `fw/start.S` | 57 | 47 | 82.5% | Startup/register/BSS/stack foundation with Tau cold/memory changes |
| `fw/link.ld` | 57 | 24 | 42.1% | Original memory protection foundation expanded for SDRAM, PSRAM and cold code |
| `fw/build.sh` | 216 | 54 | 25.0% | Original decoder/build basis with extensive Tau variants and feature gates |
| `src/fpga/core/core_top.v` | 243 | 238 | 97.9% | Frozen APF shell maintained from the upstream port framework; isolate rather than mistake scaffold size for new player functionality |
| `src/fpga/core/core_game.vh` | 714 | 217 | 30.4% | Inherited clocks/bridge/integration plus extensive Tau subsystem wiring |
| `src/fpga/core/mp3_soc.v` | 1,082 | 287 | 26.5% | Original CPU/SoC/MMIO basis with many Tau additions; bus ownership and register ABI need explicit boundaries |
| `src/fpga/core/mp3_fb.sv` | 948 | 418 | 44.1% | HarpMudd scanout/draw foundation plus Talos extensions; major redesign needs separate timing/concurrency gates |
| `src/fpga/core/pcm_fifo.v` | 76 | 64 | 84.2% | Original audio buffering; another 7 SLOC last come from the documented upstream port |
| `src/fpga/core/tgt_cmd.v` | 83 | 76 | 91.6% | Original target-command CDC/protocol foundation |
| `src/fpga/core/mf_pllbase.v` | 20 | 19 | 95.0% | Original wrapper; generated PLL implementation is classified separately |
| `src/fpga/core/core_constraints.sdc` | 9 | 8 | 88.9% | Inherited timing assumptions; do not discard during a source rewrite |

Some new files contain small detected copies from old files: for example `library.inc` has 10 inherited SLOC, `psram_diag.c` 20 and `tau_psram_probe.sv` 11. Many new modules have only 1–4 detected lines, often shared boilerplate. The inventory records these without claiming that the entire new module is HarpMudd's implementation.

### The original behaviour remains in important functions

These results explain the practical significance of the file percentages. They use lexical function boundaries and direct Git-origin SLOC, not generated machine-code size.

| Function | Source line at audited revision | Inherited / SLOC | Direct % |
|---|---|---:|---:|
| `main` | `fw/player.c:9310` | 403 / 599 | 67.3% |
| `load_track` | `fw/player.c:8848` | 202 / 243 | 83.1% |
| `read_track_head` | `fw/player.c:8562` | 147 / 155 | 94.8% |
| `poll_input` | `fw/player.c:6589` | 140 / 193 | 72.5% |
| `refill_pump` | `fw/player.c:8081` | 43 / 44 | 97.7% |
| `vbr_frame_count` | `fw/player.c:8499` | 43 / 44 | 97.7% |
| `flac_seek_locate` | `fw/player.c:7664` | 71 / 74 | 95.9% |
| `ui_draw_dynamic_cold` | `fw/player.c:5435` | 317 / 419 | 75.7% |
| `meters_feed` | `fw/player.c:6434` | 51 / 52 | 98.1% |
| `art_decode` | `fw/art.inc:433` | 97 / 99 | 98.0% |
| `art_find_flac_picture` | `fw/art.inc:316` | 49 / 50 | 98.0% |
| `pl_open_try` | `fw/playlist.inc:144` | 45 / 46 | 97.8% |
| `flac_open` | `fw/flac.c:194` | 54 / 54 | 100% |
| `parse_frame_header` | `fw/flac.c:301` | 46 / 46 | 100% |

This is a strong case for isolating track loading, transport, decoder lifecycle, target I/O and UI scheduling before replacing them. Preserving the current main loop while changing only the appearance would not constitute the proposed architectural rebuild.

### Inherited tools that still exist

Entirely unchanged: `art_scale_model.py`, `flac_bitreader_check.py`, `flac_framepos_check.py`, `flac_hdrcrc_check.py`, `flac_make_test.py`, `flac_probe.py`, `flac_ref.py`, `flac_resync_check.py`, `flac_verify.py`, `font_preview.py`, `gradient_preview.py`, `meter_preview.py`, `rv32sim.py` and `strip_xing.py`.

Mixed-origin examples: `package.py`, `card_snapshot.py`, `gen_eq_coeffs.py`, `gen_font_rom.py`, `gen_text_gamma.py`, `idle_preview.py`, `library_check.py`, `push_card.ps1`, `splash_preview.py` and `xing_check.py`. All tools and host harnesses, including small cross-file copies in new Tau scripts, are listed individually in the inventory.

### Later imports and changes that a fork-point-only comparison misses

Commit `0d5e734` explicitly ports v1.5 work:

- PCM FIFO priming from upstream `4d396bf`, plus its testbench.
- `meter_afford()` and meter yield-to-audio from `8f5eb11`.
- UTF-8 detection within encoding-0 tags, adapted to Tau's glyph behaviour.
- The cassette meter from the v1.5/release-1.5.1 sequence; later retired and preserved as `.c.txt` in the archive.

**121 current maintained-source SLOC still blame that port commit**, including 50 in firmware/RTL. These include adaptation/glue as well as copied code, so adding them is a documented-origin approximation. It does not identify every later-edit derivative of a port, nor prove that all 121 lines were verbatim upstream. Upstream-source citations and current line ranges remain available for inspection.

Tau's removed old paths include the original `eq_biquad.v`, `eq_coefs.vh`, `fw/eq_curve.h`, the old EQ testbenches/model/vector generator, `fw/main.c`, and some older tools. Halcyon replaces the former fixed-preset EQ engine. Archive retention must remain visible even when those paths stop being compiled.

## 4. Other contributors and dependencies still present

The author of an import commit is not the author of the imported component. The file headers and [NOTICE.md](../../NOTICE.md), [ATTRIBUTIONS.md](../ATTRIBUTIONS.md) and [PROVENANCE.md](../PROVENANCE.md) are the controlling source evidence used here.

| Component / contributor | Present locations | Comparison / isolation action |
|---|---|---|
| **HarpMudd** | Inherited firmware, draw engine/scanout, SoC integration, tools and tests | Preserve provenance; wrap old interfaces in a future `legacy` implementation until replaced and accepted |
| **RealNetworks / Helix Community** | `third_party/libhelix-mp3/` | Retain separate licence/notices and vendored boundary; not HarpMudd-authored code |
| **Charles Papon / VexRiscv / SpinalHDL** | `src/fpga/rtl/VexRiscv_Full.v` | Generated CPU remains; replacing the music application does not replace this CPU |
| **Rich Geldreich, changes by Chris Phoenix / picojpeg** | `third_party/picojpeg/` | Decoder is byte-identical to fork and current upstream; retain behind an artwork decoder interface |
| **Adam Gastineau / agg23** | `data_loader.sv`, `sound_i2s.v`, `sync_fifo.v`, `rtl/mem/sdram_fb.sv` | Loader unchanged; other three modified. Mixed external/HarpMudd/Tau adaptations, so classify separately |
| **Analogue / openFPGA** | `src/fpga/apf/`, `core_bridge_cmd.v`, platform bridge interfaces | Framework source remains, eight measured APF source/script files unchanged; bridge handler changed. Retain platform boundary and embedded notices |
| **Intel/Altera IP** | APF megafunction wrappers, generated PLL implementation, RAM/FIFO/clock primitives | Record generated/tool-controlled origin and exact configuration; source rewrite does not remove device-IP dependencies |
| **Rasmus Andersson / Inter** | `third_party/font/`, generated `font_rom.v`/metrics | Typeface and font-derived ROM remain; isolate generator and generated products |
| **lieff / minimp3 test material** | `sim/common/test_mp3.h`, `test_mp3_128.h` | CC0 test vectors remain even though the minimp3 runtime decoder is not vendored here |
| **Robert Bristow-Johnson** | `tools/gen_eq_coeffs.py` and related coefficient design | Published EQ formulas remain an algorithmic source; not equivalent to copied implementation |
| **Nayuki reference layout / QR standards context** | `archive/qr_encoder/`, archived headers/harness | Tau's documented QR implementation/design influence is retained in the archive; it is not the active report-code path |
| **Claude and recorded model coauthors** | Git history and individual commit trailers | 21 commits have `Claude` as primary author after the fork; 594 maintained-source SLOC currently blame `Claude`. Trailers record assistance but cannot allocate line authorship |

The entire fork ancestry has 520 HarpMudd commits. Post-fork ancestry has 789 Abel Santos commits and 21 Claude commits. In maintained source, direct last-origin attribution is 42,424 SLOC Abel Santos, 594 Claude and 11,650 HarpMudd. These counts describe the historical record; Abel's number includes ports/adaptations and is not an exclusive originality score. Coauthor trailers include Claude Opus/Sonnet variants and one aider/Qwen entry, recorded verbatim in `coauthors.tsv`. Counting the same commit once for its author and again for every coauthor would double-count contributions.

**Actual Helix modifications:** six paths differ from the fork: `mp3dec.c`, new `pub/mp3_profile.h`, `real/coder.h`, `real/dct32.c`, `real/imdct.c`, and `real/subband.c` (287 inserted / 45 deleted physical lines). Profiling and the FPGA polyphase path are identifiable Tau additions. Macro-off intent is not a bit-exact verification result from this audit. `NOTICE.md`/`ATTRIBUTIONS.md` describe much of this but omit `imdct.c`; their QR paths also need updating after archival. These are documentation correction tasks, not a reason to call the vendor code Tau's independent work.

Projects studied under the existing one-way provenance rule in `PROVENANCE.md` are design references, not added dependencies merely because they were read. The Tau-specific origin of Cymo/Halcyon, WSOLA, table generation and their derivations should remain documented during the rewrite.

## 5. Comparison with current HarpMudd, beyond the original fork

The upstream branch evolved independently; Tau has not merged its v1.6.0 architecture wholesale. The merge-base is still the original fork point. The identical vendor packages do not establish that the two players behave identically.

| Area | Original / current HarpMudd | Tau Alpha now | Rebuild consequence |
|---|---|---|---|
| Player organisation | `player.c` 9,573 physical lines at fork; 11,483 currently; three included `.inc` files | 10,488 physical lines, 19 directly included `.inc` files, 111 conditional blocks | Both retain a monolithic application pattern; Tau needs its own module boundaries rather than a fresh upstream merge |
| Audio codecs | Helix MP3 plus bespoke streaming FLAC; current upstream adds Rice/predictor speed work | Same decoder ancestry; FPGA MP3-polyphase and FLAC-LPC hooks, own Rice changes | Compare codec output/seek behaviour on a shared corpus; do not assume current upstream speed fixes are automatically in Tau |
| Output / processing | Current upstream documents software crossfeed/mono and a sleep timer | Cymo resampler/feed, gain handover, Halcyon EQ, ReplayGain, tempo code and output diagnostics | Retain Tau's measured DSP direction; evaluate upstream behaviour as requirements/reference cases, not an unreviewed wholesale port |
| Text/font | Current upstream adds `fw/utf8.h`, `fw/font_ext.h`, GNU Unifont data and multilingual font loading | Those files/dependency are absent; Tau's own base font/glyph/layout path remains | A future multilingual layer needs a deliberate data/licence/memory and layout design |
| Target I/O / track/seek | Current upstream history contains stale-size/seek-intent and outstanding-read fixes | Old track/refill/seek ancestry still extensive; different build/features mean applicability is unproven | Review these bug classes during transport isolation and write bounded lifecycle tests; this audit does not assert the same bug exists in Tau |
| Memory | Current upstream adds its own `core/psram.sv` | Tau has its own PSRAM bus/controller, SDRAM bridges, cold placement and RAM budgets | Do not exchange memory controllers or maps without adapter/protocol/timing gates |
| UI/meters | Current upstream has twelve meter styles and original control scheme | Tau themes, settings/library, meter contracts/config, Helios and Talos extensions | Preserve actual Tau user value while replacing global access and draw/input dispatch coupling |
| Packaging/testing | Original core identity and firmware/font assets | Tau identity, matched firmware/bitstream checks, cold assets, release manifest and much larger test suite | These contracts survive a rewrite; rebranding or new source names do not replace them |

Upstream v1.6.0 behaviour is described in its [README](https://github.com/harpmudd/HarpMudd.mp3player/blob/f4b8f012e0514c6240a546cfb3b48e90488f5fef/README.md); implementation comparisons use the pinned tree. No new Unifont, upstream PSRAM, crossfeed or decoder code was imported for this report.

## 6. Architectural findings aligned with the earlier audits

Read together: [full audit, Sep 23](FULL_AUDIT_2026-09-23.md), [firmware modularization plan](../features/FIRMWARE_MODULARIZATION_PLAN.md), [Helios review](../features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md), [Talos review](TALOS_REVIEW_2026-09-28.md), [Talos 2 plan](../features/TALOS2_REIMPLEMENTATION_PLAN.md), [Cymo review](../features/CYMO_AUDIO_ENGINE_REVIEW.md), [audio stack review](../features/CYMO_AUDIO_STACK_REVIEW.md), [RAM diet plan](../features/RAM_DIET_PLAN.md), [architecture roadmap](../features/ARCHITECTURE_ROADMAP.md) and subsequent audit entries.

1. **The fundamental firmware coupling persists.** `.inc` files share the application's namespace and global state. `main()` spans 1,179 physical lines. Track I/O, playback and UI scheduling remain intertwined. A file split without private state and narrow interfaces does not solve this.
2. **UI ownership is incomplete.** Helios has view/invalidation and region machinery, and meter inputs are materially improved. Current `set_open`/library flags and individual dispatch sites remain. The rebuild should unify view input/draw/lifecycle dispatch while preserving the useful distinction between region invalidation and within-meter skip caches. The review explicitly rejected forcing those into one mechanism without a concrete need.
3. **Audio timing is an architectural constraint.** The newer report records 1.75x tempo at or near CPU saturation; it is not a safe workload from which to infer spare capacity. UI/module/cold-placement changes require measured decode/stretch/FIFO cost, not an assumed improvement.
4. **Cold placement and fallback are part of correctness.** Missing cold images or absent accelerators must preserve the defined base-player behaviour. Mandatory software decoder fallback cannot casually move into PSRAM/cold code. Retain explicit readiness/placement checks.
5. **Historical timing findings have changed status.** The full audit's RAM-output-to-deep-combinational-path and multiwriter row-buffer findings guide the design, but T2-00 subsequently resolved the row-buffer inference/writer problem. The original EQ is now removed. Do not re-open those old findings as current bugs without current evidence.
6. **Talos 2 is a design reference, previously declined for now.** Its front/row-job/back architecture is appropriate if a measured bottleneck or new opcode justifies it. It should not block the firmware isolation stage merely because a full rewrite is being planned.
7. **Build configuration is difficult to reason about.** The release/diagnostic/profile distinctions, macro defaults and separate FLAC compilation make a typed capability/variant manifest worthwhile. Tests must prove what reaches each translation unit and what hardware is present.
8. **Documentation drift is real.** `PROJECT.md` retains older September constraint paragraphs even alongside newer research entries; `CURRENT_STATUS.md` and October handoffs record later shipped memory/audio state. Baselines must come from dated source/build records. This report does not revive September's framebuffer-only SDRAM constraint as today's status.

The historical Preview baseline records 256/308 M10K, 20/66 DSP and positive timing, with 8,768 B release and 1,376 B diagnostic heap gaps in the latest status summary. These are prior evidence, not a new measurement here. The 24 KiB compressed ring, 4 KiB tag landing zone, static decoder arena and linker/stack guards remain protected. Rebuild acceptance must remeasure the actual linked variants and fitted hierarchy rather than spending these figures as guaranteed free capacity.

## 7. Isolation and full rewrite plan

**Decision proposed by this report:** use plain C modules with private state, explicit context structs and fixed-capacity data; a thin platform/MMIO layer; bounded cooperative audio-first scheduling; optional diagnostics linked as modules; generated data outside handwritten implementation. Replace the inherited application one boundary at a time. Keep the external decoders, CPU, framework and vendor controllers separately attributable unless a separate replacement decision is made.

This is a plan, not an implementation start or a claim of independent clean-room development. The earlier modularization plan was parked pending the owner's architectural decisions. This request authorizes the comparison/plan; it does not require executing the rebuild. The scope can be a full independent rewrite of the maintained application/integration while retaining declared external dependencies. Replacing every CPU, codec and platform dependency would be a distinct, much larger project.

### Proposed isolation layout

```text
fw/
  app/             boot, bounded scheduler, commands and independent state axes
  platform/        MMIO, capabilities, target I/O, clocks, memory/cache rules
  media/           stream, metadata, artwork, playlists, library and queue
  audio/           decoder adapters, transport, PCM/output, gain and tempo
  graphics/        Talos command API/fences, text, geometry and Helios regions
  ui/              view lifecycle/input/draw, pages, theme and meter presentation
  settings/        typed values, persistence and migration
  diagnostics/     optional tests, stress, profiling and report exporters
  legacy/          future extracted old implementation behind the same APIs
  generated/       build-owned headers/tables; never manually edited
third_party/       retained external codecs/fonts and notices
src/fpga/
  apf/             retained platform framework
  rtl/             external CPU/controller/IP boundary
  core/            Tau integration and maintained hardware modules
```

The layout is a target, not permission to move every file at once. Existing third-party paths should stay stable during the first stages. Commit hashes and this evidence set provide the old reference; `legacy/` should contain only the small components actually extracted, rather than an extra duplicate of the entire source tree.

### Module ownership and API contracts

| Boundary | Owns / interface | Current source to isolate | Improvement |
|---|---|---|---|
| `platform/io` | A single outstanding target request, result buffer, completion/error/cancel lifetime | `target_*`, `tgt_cmd.v`, loader/bridge interactions | Prevent read/open/identity polling from discarding another operation |
| `platform/memory` | Arenas, hot/cold windows, readiness, cache/DMA contracts | `alloc.c`, `sysio.c`, `link.ld`, `cold.inc` | Explicit reservations and fail-safe paths rather than borrowed globals |
| `audio/decoder` | Init/reset/feed/seek/close, format and required working memory | Helix/FLAC glue, hardware adapters | Swap implementation without exposing decoder globals or MMIO to the UI |
| `audio/transport` | Playing/paused/stopped state, track generation, EOF/reload, seek intent | `load_track`, refill, seek, `main` portions | Explicit transitions; stale completion cannot mutate a new track |
| `audio/output` | Bounded PCM push, flush, source rate, gain handover/ramp | `pcm_push.h`, gain helpers, Cymo/Halcyon integration | One measured audio path, coherent stereo pairs and atomic changes |
| `audio/tempo` | Tempo state, budget, reset/flush and degradation policy | `tempo_core.h`, `wsola_core.h` and glue | Preserve pitch/corpus behaviour; prevent menu work from consuming audio deadlines |
| `media/metadata` | ID3/FLAC tags, VBR/LAME/ReplayGain and text decoding | `id3_*`, FLAC metadata, `lame_tag.h`, `replaygain.h` | Bounded parsers and explicit error/fallback results |
| `media/artwork` | Acquisition, decoding/workspace, TIMG/cache lifetime | `art.inc`, `timg.inc`, `assets.inc` | No ownership overlap with audio PCM/DMA buffers |
| `media/queue` | Playlist paths, queue/shuffle/repeat, library and selection history | `playlist.inc`, `library.inc`, list helpers | UI browses a model; it does not own the active transport |
| `graphics/talos` | Command construction, clipping, queue/drain/fence, CLUT and buffer lifetime | `fb_*`, probes, `mp3_fb.sv` contract | Explicit transaction/fence semantics; no fixed delay masquerading as completion |
| `graphics/helios` | View/region invalidation and bounded flush | `helios.inc` | One view-dispatch vocabulary while preserving element-level meter caches |
| `ui/views` | Enter/exit/input/draw; page-local state and layout | Settings, library, fullscreen, now-playing portions | Replace global open-flag dispatch with a small static table |
| `ui/meters` | Config and drawing from `mtr_in_t`/owned scratch | Meter core/module, Chladni/VU/layered-wave/legacy meters | Keep audio telemetry sampling separate from optional rendering |
| `settings` | Typed persisted values, versioned migrations, load/apply/save | `settings.inc`, `settingsui.inc`, Halcyon preset controls | Make persistence and runtime state transitions independently testable |
| `diagnostics` | Reports/test/stress/profiling and feature registration | `suite.inc`, `stress*.inc`, exports | Release does not pay for diagnostic state/code it does not link |
| `app` | Boot sequencing, commands, scheduler policy | `main`, input orchestration | Small orchestration layer; no decoder/parser/draw implementation embedded |

**Dependency rule:** `app` orchestrates; views read models and submit commands; media/audio use platform services; graphics owns draw-engine access; decoders do not call UI; diagnostics observe through public interfaces. Avoid a callback from a low-level module into `app` that merely recreates the old cycles. A bounded service hook for polling/refill must have an explicit budget and ownership contract.

Use independent state axes: **transport** (stopped/loading/playing/paused/seeking/error), **active view** (now-playing/library/settings/fullscreen/info), and **transient overlay** (toast/loading/error). Opening Settings must not imply that playback entered an incompatible transport state. State changes carry a track/request generation so a stale I/O completion is rejected.

### Delivery stages and gates

| Stage | Concrete work | Exit gate |
|---|---|---|
| **R0: freeze and specify** | Pin released/current ROM, cold image, bitstream and source hashes; produce compiler-aware call graph and complete global ownership/read-write map; specify APIs, state axes and register/memory contracts | Every mutable global has one owner; cycles resolved; fixed budgets and baseline corpus/snapshots documented. Lexical function inventory here is input, not a substitute |
| **R1: isolate cold pure logic** | Extract parsers, queue/navigation/settings encoding/layout math; keep existing behaviour behind adapters | Differential host/corpus tests; baseline snapshots; linked size and cold-call checks. Byte identity is checked where extraction is intended to preserve codegen, not assumed |
| **R2: make real modules** | Promote leaf UI/media/settings/diagnostic `.inc` clusters to `.c`/`.h`; private state; one cluster per reversible commit | Host/snapshot parity; release and diagnostic link/budget gates; no new hot-path cost; explicit interface-only dependencies |
| **R3: rebuild view/application layer** | Implement static view table, input routing, independent state axes and bounded scheduler; progressively replace inherited input/UI/main-loop code | Every changed state has a named 400x360 framebuffer fixture; playlist/library/settings/overlay interaction matrix; concurrent playback/ENDURANCE on Pocket |
| **R4: rebuild I/O/track/transport** | Replace old global target/refill/load/seek lifecycle behind the frozen stream/decoder interfaces | Missing/truncated/corrupt/large files; stale/open/read/seek cancellation; pause/stop/EOF/shuffle/resume; cache/accelerator/cold-image fallback; PCM equality or documented intentional differences; hardware L0/headroom gates |
| **R5: decide and replace codecs** | If removal of HarpMudd's bespoke FLAC is part of the objective, select a separately proven bounded-memory decoder or independently implement from the FLAC specification; retain an adapter for Helix unless its replacement is explicitly included | Corpus parity/refusal semantics, worst-case memory/cycles/seek, CPU+hardware differential tests. No new decoder is selected merely from reputation or lines saved |
| **R6: replace maintained RTL selectively** | Rebuild SoC/MMIO/target integration and FIFO interfaces behind compatibility wrappers; Talos front/row-job/backend only when justified; retain vendor CPU/framework/controller boundaries | Complete protocol/CDC simulations and mutation checks; infer RAM as intended; matched firmware/RTL capabilities; fresh multi-seed Quartus fit/timing; Pocket audio/display/concurrency gates |
| **R7: retire legacy and publish** | Delete adapters/old code only after acceptance; update notices, source-module map, release pairing, user/docs/migrations; rerun origin measurement | No product build references retired code; current origin footprint and retained dependencies disclosed; full host/RTL/package/snapshot and appropriate hardware validation complete |

Each stage produces a reviewable change and baseline comparison. Isolation is behaviour-preserving extraction, not origin removal. Only genuinely reimplemented and validated components shrink the inherited implementation footprint; renames, moved `.inc` bodies and wrapper functions do not.

### Improvements that should be built into the rewrite

- **Audio first:** bounded scheduling; measure per-hop/frame worst-case cycles, refill waits and FIFO margin; shed meter/UI work before audio; define tempo degradation from measured headroom rather than promising 1.75x works universally.
- **Explicit I/O ownership:** one owner for open/read/cache/seek, deadlines with recoverable errors, and a cancellation/generation rule. Record late failures without corrupting the active track.
- **Static, measurable memory:** no speculative dynamic allocation scheme; module reservation table, shared scratch only for mutually exclusive lifetimes, stack high-water and linked hot/cold reports. Preserve protected DMA/ring/decoder reservations.
- **Predictable UI:** unified lifecycle/input/draw dispatch, structured invalidation, layout dimensions/stride passed as geometry, deterministic production-font framebuffer fixtures. Resolution 720 remains a later gate.
- **Typed build capabilities:** generate firmware/RTL/register/feature definitions and a versioned compatibility manifest from one schema; test actual translation-unit flags and fallback paths. Avoid a second feature-probe mechanism for a capability already present.
- **Verification at boundaries:** share parser/audio/render fixtures across old/new implementations, then hardware-test deadline/CDC/analogue behaviour that host parity cannot establish. Use current capture-chain evidence rather than superseded output-stage claims.
- **Traceable hardware:** retain the audit's registered memory/address/control boundaries, one-writer RAM inference, held request/response rules and framebuffer/audio arbitration. Separating RTL files must not accidentally alter timing, inferred storage or clock ownership.

### Alternatives and decisions

**Recommended:** staged replacement behind narrow contracts. It allows direct comparison, small regressions and measurable retirement of inherited code. **Deferred:** big-bang player/FPGA/decoder replacement, because a failure could arise from several new lifecycles at once and invalidate the current audio/memory baseline. **Not useful as the main objective:** deleting attribution-labelled lines or mechanically converting `.inc` files without state ownership; both can change the number without solving the architecture.

Keep plain C, a static view table and bounded command processing for the first version. A general event bus, retained widget/scene graph and blanket LTO are not default choices for this fixed-memory cooperative target. Introduce cross-file optimisation only after measuring size/headroom and debugging trade-offs. The expensive decoder/hardware stages remain separately gated.

No completion date, speedup, fit saving or “0% HarpMudd” result is booked in advance. New functionality and bug fixes get intentional baseline deviations with evidence. Remaining baseline defects, including saturated tempo/Track changes checks and output-measurement gates, must be labelled instead of being silently accepted as the new reference.

## 8. Complete module list and evidence

The exhaustive [source/module inventory](provenance-2026-10-12/MODULE_INVENTORY.md) lists **all 482 source/build-script files**, their SLOC/origin/port counts and fork/current-upstream status, plus every lexical HDL module declaration and the 15 removed fork source paths. [The full tracked-file ledger](provenance-2026-10-12/tracked-files.csv) covers all 933 paths, including package metadata, manifests, archived cassette source, assets and licences.

Current logical modules, grouped for navigation:

| Group | Present modules / implementation families |
|---|---|
| Firmware foundation | Player/application, startup, allocator, syscall I/O, linker/memory placement, firmware build/variants |
| Media/transport | MP3 adapter, streaming FLAC, FLAC-LPC adapter, MP3-polyphase adapter, track/refill/seek, metadata/LAME/ReplayGain, playlists/queue, library, art/JPEG, TIMG, assets/cold loading |
| Audio/control | PCM packing/push, gain handover/headroom, tempo/WSOLA, Halcyon EQ/presets/page |
| Graphics/UI | Framebuffer/Talos wrappers, Helios/geometry, font metrics, themes, fullscreen, settings/persistence/pages, meter contract/policy/config, Chladni, VU Master, Layered Wave and remaining visualizers |
| Diagnostics/export | PSRAM diagnostic, blit probe, stress, suite/core, diagnostic features/settings, report/info/wave-config export, pixel grid/TPG, key-repeat/start-gesture helpers |
| Maintained RTL | SoC, framebuffer/Talos, PCM FIFO, target bridge, core shell/game integration, PLL wrapper/constraints; CDC sync/Gray bus/Gray counter; CLUT writes; Cymo feed/resampler; FLAC LPC; gain stage; Halcyon; main RAM; MP3 polyphase; PSRAM async/bus/probe; RAM probe; SDRAM address decode/arbiter/bridge mux/CPU bridge/window probe/Wishbone adapter; SignalTap tap; spectrum bank; vblank counter; waveform meter |
| External/platform RTL | VexRiscv; APF top/common/I/O/datatable/DDIO/bridge handler; data loader; I2S bridge/sync FIFO; SDRAM framebuffer controller; generated PLL/device-IP products |
| Third-party/source data | Helix decoder/tables/headers/ARM assembly/test wrappers, picojpeg, Inter font/licence, CC0 MP3 vectors, generated font/codec/DSP/theme/meter tables |
| Tooling/verification | Package/release/card tools; compatibility/assets/library/image/version/layout; generators; audio/FLAC/DSP laboratory models; RV32 simulator; source/budget/pairing checks; UI/snapshot tooling; HDL/firmware/host differential and mutation harnesses; Director/triad tooling |
| Archives | Cassette source/thumbnail entry; QR encoder/tables/generator/harness/tests |

Evidence files:

- [files.csv](provenance-2026-10-12/files.csv): one row per measured source file.
- [summary.json](provenance-2026-10-12/summary.json): pinned revisions, scope totals, file classifications/hashes, authors and tracked inventory.
- [line-origins.json](provenance-2026-10-12/line-origins.json): SLOC ranges, original path/line, author, origin class and commit.
- [firmware-functions.csv](provenance-2026-10-12/firmware-functions.csv): 767 lexical definitions and inherited-source counts.
- [comparison-totals.json](provenance-2026-10-12/comparison-totals.json): fork/current-upstream source-size comparison under the same masks.
- [coauthors.tsv](provenance-2026-10-12/coauthors.tsv): original Git author and coauthor metadata; does not allocate ownership.
- [measure.py](provenance-2026-10-12/measure.py) and [build_inventory.py](provenance-2026-10-12/build_inventory.py): reproducible analysis, reads pinned Git objects and writes only evidence outputs.
- [verification.json](provenance-2026-10-12/verification.json) and [verify.py](provenance-2026-10-12/verify.py): host checks of all source hashes, counts, origin-range coverage, module/function records, document links and audit consistency. These do not test product playback or hardware.

For this snapshot, reproduce with `measure.py --revision ae11a8fee5a7cf3e7873a4a654bccf5a7df6b908 --upstream f4b8f012e0514c6240a546cfb3b48e90488f5fef --repo <checkout> --out <evidence-dir>`, then run `build_inventory.py` from its retained evidence directory. A future HEAD includes additional reporting tools; pinning prevents denominator drift.

**Percentage to use:** approximately **28% identifiable HarpMudd-origin source in Tau Alpha's maintained firmware/RTL** (**27.49% direct; 27.72% including the surviving declared-port footprint**). For the broader maintained repository including tools/tests, approximately **21.5%**. About **70.4% of the original maintained firmware/RTL source-line origins remain after deduplicating copies**, which is why the proposed rebuild must replace core lifecycles rather than only add features around them.
