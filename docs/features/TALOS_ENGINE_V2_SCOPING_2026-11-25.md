# Talos engine v2: scoping report for a clean-room rewrite (2026-11-25)

> **Owner decision 2026-11-25 (D-T19): Tau Gallery's extensions (`OP_CORNERS` 10, `OP_DSBLIT` 11, viewer fill, pixel doubler) join the common engine as build-parameter features, default off; updating the other projects (Gallery, Tau V, Reader, Tau Μ) is accepted.** Opcodes first, scanout features second; Gallery's pixels are pinned; reserve an escape opcode before adding more; measure each feature's fit cost. See `docs/DECISIONS.md` D-T19 and the consumer inventory.
>
> **Owner decision 2026-11-25 (D-T18): keep ALL Talos commands.** The engine is shared with the owner's other core-based apps (Tau V, Tau Μ and others mention it), so the recommendation below changes from **B-lean to B-full** (all nine opcodes); every "drop" in this report and in the requirements table is withdrawn. New open item: inventory which commands the other cores actually issue (their firmware and RTL drivers) before the contract is written; those cores become consumers whose behaviour the compatibility policy must protect. "No regressions, improvements welcome" stands.


**Evidence label: HOST-ONLY, READ-AND-ARITHMETIC.** Nothing was simulated, built, fitted, run on a Pocket or pushed. Sources: the files named in each section (read today), the audit entries B-891..B-904, B-929..B-931, B-940, B-1121, B-1130 for fit and Pocket figures, `git blame -w -M -C -C` of `tau_talos.sv` (987 code lines, 362 HarpMudd-blamed, matches the assessment's 360). Per-entity fit numbers for the engine alone were NOT available (no fit report on this machine; `work/` is not in git); the figures marked *unmeasured* say how to get them.
**Audience: the owner and the coordinator. This report describes old internals in places; it must NOT be given to the clean-room author** (see `TALOS_ENGINE_V2_CLEANROOM_PROCESS.md`). The author gets only the contract extracted from section 5.
**Owner correction applied (2026-11-25):** the goal is NO REGRESSIONS, not bit-for-bit reproduction; improvements are welcome. Sections 1 and 2 define "regression" and the compatibility policy; section 6 replaces the bit-exact differential by a classified one.

---

## 0. Summary and recommendation

* **Recommended: architecture B-lean**: a new, command-serial engine (one command in flight, one row buffer in MLAB, the existing `p0` SDRAM master port and the existing command-queue/MMIO/status interface), written clean-room from a behavioural contract, that implements every command the shipped firmware actually issues and reserves (does not implement) what nothing uses. It is **not** the old plan's front-end + row-job FIFO + ping-pong back end (architecture A): D-T12's data (command issue at most 9 % of any meter tick, fences are engine time dominated by whole-grid copies, no missed deadline) and the exhaustive review's R1 (a copy row is within 8 % of the bus floor; ping-pong buys little) give A no measured benefit, and A costs the most verification.
* Why B-lean and not B-full: **fw2 issues only RECT, CHAR (1x), BAR, BLIT (plain and DSP-blend), SBLIT (1x/2x/3x), CBLIT and FENCE**. RUN, COPY, RRECT, colour-key, palette re-index, blend modes 1-4 and fractional or scaled CHAR are never issued by shipped firmware or any pack (verified by grep, section 3). Dropping them removes about a third of the old state machine and its tests without any regression as defined in section 1. Each drop is an owner decision (section 9); B-full is the fallback if the owner wants them kept.
* **Fit budget verdict: fits with margin.** Latest closed fits: 15,484-15,507 ALMs of 18,480 (84 %), 274-281 of 308 M10K, 23 of 66 DSP, worst setup +0.655 ns (SDRAM read capture, not the engine), fast-0C hold +0.078..+0.115 ns. The old engine plus scanout plus queue plus font controller measured 1,532 ALMs, 38 M10K, 6 DSP before the split (T0). A like-for-like rewrite is budget-neutral by construction (rule: no more ALM, M10K or DSP than the code it replaces; B-lean should be 10-20 % smaller, an estimate, unmeasured). LAB packing (93 % at T0) is the binding constraint, not ALM count.
* **Effort (honest): 11-17 working sessions and 2 fits (about 3 h 30 for two seeds in parallel on the VM, 1 h 40 for one), calendar 4-6 weeks because of owner Pocket gates.** The assessment's 8-12 sessions for phase 5 does not include the classifier and the cost harness (section 7).
* **The biggest risks:** (1) timing regression in a new design that repeats the five times found "arithmetic chain into a write port" shape; (2) an unclassified pixel difference found late; (3) clean-room evidence is honour-based unless technically separated (section 8); (4) the owner's goal is provenance, and the oracle must stay in `sim/` until a Pocket confirms (D-T01), so the line count of HarpMudd origin only falls at the end.
* **Do not start the rewrite for performance reasons.** There is none to claim. Section 4 lists the cheap improvements that come free with a clean design, each separately approvable.

---

## 1. What "regression" means (for the owner to approve)

A change is a regression if it fails any row below, measured as stated. A row is passed with a stated tolerance; anything over needs an owner decision recorded in `docs/DECISIONS.md` before the old engine is deleted.

| # | Axis | Pass criterion | Measured by | Baseline (source) |
|---|---|---|---|---|
| R1 | Functional correctness | Every command class the firmware, Helios and the seven shipped packs issue (section 3) produces the pixels its approved class says (section 6), at every geometry in the legal domain, including fences, double-buffer targets, sticky-field changes after fences | classified differential + Python model + the existing scene benches | `sim/tb_blit_scene.v`, `test_blit_reference.py`, `tb_tau_gfx_*` |
| R2 | Visible quality | No visible change in any shipped meter or UI screen except approved improvements: the screenshot-vs-reference check on each of the seven meters (window and fullscreen), Now Playing, Library, Settings, Info/Report, splash; identical RGB565 unless a class says otherwise | Pocket screenshots are lossless (KB-103); golden images from the model | Blit Test port and screenshot check of T0 (D-T05) |
| R3 | Public artefacts | The SDK conformance suite (`sdk/sdk/conformance`, `canvas.py`), the lab twin (`tools/meters/preview/tau_fb.js`) and every shipped pack render as before | conformance replay on the new engine (section 6.4) | `canvas.py`, `tau_fb.js`, packs in `sdk/meters/` |
| R4 | Engine time | Per command class the engine-busy cycles (dispatch to retire, same SDRAM model) are no worse than **+5 %** of the old engine's p50 and p95 on the generated corpus, and **no class slower than +10 %**. Per meter tick on a Pocket: CPU cycles and fence cycles no worse than **+5 %** of today's | cost harness (section 6.3); Pocket Report rows | DEV 197/198: Waterfall horizontal 227 k, vertical 450 k, Bars 17.0 k, Chladni 251 k (127 k in fences), Master VU 16.3 k, Layered Wave 87.6 k (B-900, B-902, B-904; moving averages, so +/-5 % is noise) |
| R5 | Fence, idle, status | Unchanged for the driver (`R_FB_GO` bit 0 full, bit 1 idle; `R_GFX_FENCE` retire count; `R_GFX_STATUS` present/idle/free; `R_GFX_COUNTS` overflow and late lines) and idle means every accepted command retired. A change is allowed only together with its driver change (`fw2/gfx/gfx.c`), explicitly listed | `tb_tau_gfx_t3`, `tb_gfx_status_soc` kept and passing against the new engine | `TALOS_T3_SPEC.md` |
| R6 | Scanout and SDRAM | `GFX SCAN LATE` 0 and queue overflow 0 over the Pocket checks and a blit storm; a scanout fill never waits for more than one engine unit; no unit longer than about 1,000 clk_sdram cycles (a line is 4,167 cycles; fill about 400-430); no SDRAM protocol fault in the chip-model benches (`sim/sdram_sched_a/run_gfx.sh`); per-port SDRAM counters (`SDA` rows) no worse | benches, Pocket Report | B-904, B-929 (soak 46 passes, 0 failures) |
| R7 | Audio | `UNDER RING`, `HW UNDER EV`, `GOV PANIC` not worse than DEV 198/201 under the same meters; no new SDRAM bandwidth use by the engine beyond its old figure (measured: B-146 15.8 % busy under a blit storm) | Pocket Report | B-904 |
| R8 | Fit and timing | Two seeds, all corners positive; worst setup not below **+0.655 ns** at 100 MHz; hold not below **+0.078 ns** (fast 0C); no engine-internal path below the current overall worst; ALM, M10K, DSP each not above the code replaced (+5 % tolerance on ALM, 0 on M10K and DSP); LAB utilisation not above today's; RAM inferred as `altdpram`/`altsyncram` (grep gate, no per-bit register names) | `tools/vm_fit.py`, map-report grep | wdog-w3 (B-940), def-b931 (B-931) |
| R9 | Clock domains | No new crossing that is not a Gray counter/bus or a toggle handshake; every crossing listed in the SDC review; `rtl-lint` clean | R5 review, lint | `tau_cdc_gray_*`, A-094 |
| R10 | Robustness | Fuzz with fences and queue pressure: no hang, no lost command, no write outside the commanded rectangle; watchdog interaction unchanged (a cut tick still drains) | fuzz, `test-rtl-wdog-soc` | B-941 |

Not regressions (stated so nobody argues them later): a different cycle count per row that stays inside R4; different internal state; a different queue depth above the 255 the driver assumes; behaviour outside the legal domain (section 5.9).

---

## 2. Compatibility policy: what must stay pixel-identical, what may improve

**Rule.** (a) A rendering rule that a public artefact pins stays pixel-identical. The public artefacts are: the pack host table as documented in `sdk/docs/HOST_TABLE.md` and `sdk/sdk/include/tau_meter.h` (ABI **1.2**; the brief's "ABI 2" is the Player 2 pack format, to be confirmed with the owner), the conformance model `sdk/sdk/conformance/canvas.py`, the lab twin `tools/meters/preview/tau_fb.js` (and the wasm/Omega copies of it, which this repository does not contain: **unverified which exist**), and the golden vectors/fixtures under `sdk/sdk/conformance/fixtures`. (b) Anything else may change as an improvement, but a pixel change is visible on shipped screens and in every golden test, so each needs an owner decision and a safe shipping route:
* **Capability route:** the engine reports a capability bit (proposed: unused bits of `R_GFX_STATUS` [30:17]; `present` is bit 31) and `gfx.c` selects the new behaviour only when the bit is set and the caller's ABI allows it. Old packs declare ABI 1.x and keep the old behaviour.
* **ABI route:** a host-table minor (1.3) or major (2.0) for a pack-visible change, with the conformance vectors, `canvas.py`, `tau_fb.js` and the SDK docs updated together and announced through `docs/interop` (the ledger and `tau-compat.json` fields `packs_abi`, `packs_abi_minor`).
* **Silent route (allowed only for no-pixel-change improvements):** speed, area, robustness.

| Rule | Pinned by a public artefact? | Policy | Proposed improvement and effect | Affected | How to ship |
|---|---|---|---|---|---|
| Rectangle and bar fills: pixel-exact edges, bar lit rows at the bottom, lit count clamped to height, unlit on top | **Yes:** `canvas.py` rect/bar, `tau_fb.js` | (a) identical | none | | |
| Plain copies (`blit`, `blit_abs`, `scroll_*` as the driver builds them) | **Yes:** `canvas.py` (reads all source rows first, so a true memmove) | (a) identical for non-overlapping and for the overlaps the driver issues | **Overlap-safe copies:** today a downward overlapping BLIT in one command is NOT a memmove (shown by `tb_tau_gfx_move`, case B); the model says it is. A new engine can pick the row order by overlap direction: one command per down-scroll, no stash chunking | packs: none visible (the driver already chunks); lab/SDK: already the model; firmware: simpler `gfx_scroll_down` | capability bit; driver change; no pixel change for any stream the driver issues today |
| Blend (`blend_on(a)`, `blend_blit`): per channel `(f*a + b*(256-a)) >> 8`, 565 channels, `a` 0..255, `a >= 256` is a plain copy in the driver | **Yes:** `canvas.py`, `tau_fb.js` | (a) identical | An exact `/255` rounding would change at most 1 LSB per channel for an invisible gain. **Not recommended**; if wanted, ABI 1.3 with all three artefacts updated | all packs using blend (Winamp Scope trail), conformance, lab | ABI route only |
| `sblit`: nearest neighbour, scale 1, 2, 3, source pixel `floor(i/scale)`, output clamped to **127 columns** | **Yes (columns):** `canvas.py` `min(127, w*sc)`; mapping matches `tau_fb.js` | (a) identical | Lifting the 127 limit is an improvement only under a capability bit; **found divergence:** the RTL also clamps output **rows** to 127, `canvas.py` and `tau_fb.js` do not. Resolve by owner decision: fix the models to match shipped hardware, or remove the row clamp in the new engine (a bug fix, matches the models) | Chladni (the only user), SDK, lab | row clamp: verify that no pack hits it (unverified), then ship as a conformance-vector note or capability bit |
| `cblit`: low byte of the source word through the 256-entry palette, `w <= 127` | **Yes** (lookup): `canvas.py`, `tau_fb.js`; the limit is documented | (a) identical | Lifting the 127 limit (new engine splits rows internally): compatible, free; lab and conformance keep accepting `w <= 127` | Waterfall | silent (a larger accepted domain changes no existing result); document in HOST_TABLE.md at the next minor |
| `cblit` clipping to the visible frame in the lab | model clips; hardware does not (the driver and the bounds check keep legal use inside the box) | undefined by ABI; keep hardware unclipped | none | | |
| `rect` (clipping to 400 x 360 and "around fullscreen exclusions") | partly (host clips; driver clips) | stays in the driver | none | | |
| `text`: the glyph pixels (font, 4-bit coverage, the dark/light weight tables and the `>> 4` mix) | **No.** The lab draws 5 x 6 blocks and `canvas.py` says so; no golden vector holds real glyphs. But **visible on every screen** | (b) may change, **default: identical** | Make the weight tables data (not logic), so a refit is a data change. A refit of the dark table (provenance, see section 9) changes every text pixel at low coverage | all UI and 4 packs with text; Omega theme work | owner decision, own fit-free data change later; screenshot gate R2 |
| `text` advance, Latin-1 coverage, 16-pixel cell, blank for unmapped codes | driver and font RAM, not the pack ABI ("8 x 16 or the theme font" in the header) | (a) for shipped fonts | CJK / extended glyph source (`GLYPH_PSRAM`) reserved in the contract, not built | | |
| CHAR scale 1.5x, 2x, 3x | not used by fw2 | (b) drop or keep | none | none today | owner decision (section 9) |
| Palette re-index, colour key, blend modes 1-4, RRECT, RUN, COPY | not in the host table; lab-only `blend()` for modes | (b) | drop (saves area and tests); a later extension can add them back | none shipped | owner decision; lab-only `Fb.blend` loses its hardware twin |
| Fill and row edge rules inside the legal domain (w 1..511, h 1..511, 25-bit word addresses) | pinned only through the rows above | (a) identical | Behaviour outside the domain (a copy wider than 127, a burst crossing a 1,024-word page, w or h of 0 for blit-class) is defined by the new contract (split, clamp, no-op, retire) instead of truncated, wrapped or hung | none | silent |
| Double buffer rule: ordinary RECT/CHAR/BAR commands follow the CPU target for rows of the visible frame only, blit-class commands use their sticky bases, scanout follows the displayed buffer | not public (driver and firmware) | (a) identical for shipped firmware | none | | driver + engine kept together |
| Engine memory map: 512-word pitch, 1 pixel per word, visible rows 0..359, stash rows 360..1023 and columns 400..511, second buffer 1,048,576 words above | **Yes:** `mb_write/mb_read`, `blit_abs`, `rect_stash` take raw pixel addresses; the conformance bounds check assumes it | (a) frozen | none | | |
| Ordering: commands execute in queue order, a command's rows complete in order, sticky state is applied to commands dispatched after the write, software fences before changing it | driver contract | (a) | the new engine may snapshot sticky fields at push; the driver's fence-then-write discipline stays valid either way | | |

New capabilities (no existing pixel changes): reverse row order or overlap-safe copy; hardware clip rectangle; burst-gather SBLIT (speed only, same pixels); constant-source blend; gradient and pattern sources (T6, parked by D-T12).

---

## 3. Requirement inventory from the real users (a)

Full table: `TALOS_ENGINE_V2_REQUIREMENTS.csv` (one row per command or feature: users, public pin, cost, keep/drop/change, may improve, proposed improvement, test today, regression test).

**Method.** All engine register traffic in fw2 goes through `fw2/gfx/gfx.c` (grep of `R_FB_GO`, `R_BLT_`, `R_RC_`, `R_CLUT`, `R_DBUF` outside it: one hit, a capability read in `platform.c`). Packs reach the engine only through the 19-entry host table; `tau-meter test` forbids absolute addresses and registers.

**Opcodes the shipped firmware issues:** RECT (the `gfx_rect` family, 132 call sites outside `gfx.c`), CHAR at 1x (`gfx_text`, 100 sites; `gfx_char_stash` 1), BAR (8), BLIT in four stash/target combinations (9 + 6 `blit_abs`; also frame begin, compose and crossfade copies), SBLIT (5), CBLIT (9 + 5 `cblit_abs`), FENCE (`gfx_fence` 20 sites plus driver-internal fences: frame end, palette load, base change, compose, blend off), and the blend field of BLIT (5 sites).
**Never issued by fw2, the SDK header, or any pack:** RUN (0), COPY (3), RRECT (8) and its table (`R_RC_*`), colour key (BLT field 4), re-index (field 6), source and destination strides other than the 512 default (fields 1 and 3: never written), blend modes 1-4, CHAR scales other than 1x, opcodes 10-15. (RRECT, COPY and the rest were used by the retired `fw/` player; the Helios spec still names RRECT, but fw2 has no rounded-rectangle caller: `nowplaying.c` builds its "pill" from rectangles.)

**Pack usage (seven shipped packs, grep of `fw2/ui/meter_pack_*.c`):** rect: all seven; text: Chladni, Layered Wave, Sound Map, Master VU; bar: Winamp Bars, Sound Map; blit: Chladni, Layered Wave; scroll_left: Layered Wave, Waterfall; scroll_down, rect_stash, clut_load, cblit: Waterfall (rect_stash also Scope); blend_ready/on/blit/off: Winamp Scope; sblit, mb_write, mb_read, blit_abs, fence: Chladni.

**Frequency (measured, DEV 195/196/197, per tick, window/fullscreen; B-896, B-900, B-902):** Bars 0.4-9 commands, Scope 0.4-0.8, Chladni 3-5, Master VU 0.7-3.9, Waterfall 19-69 (max 370), Sound Map 0.4-0.7, Layered Wave 0.4-11 (max 466). Command issue cost is about 132 CPU cycles a command and at most 9 % of any tick; the queue never filled. **Not measured:** a per-opcode histogram of the UI views (Now Playing, Library, Settings). Cheap measure: replay the lab's `log` for each view through a counting script, or add per-opcode counters (a Diagnostic feature registered in `fw2/diag_features.json`).

**Unused candidates to drop:** RUN, COPY, RRECT (+80-flop table and sequencer), key (+ destination pre-read), re-index, blend modes 1-4, strides, CHAR scales, ops 10-15. **Keep and preserve:** RECT, CHAR 1x, BAR (9-bit lit), BLIT with sticky bases and DSP blend, SBLIT, CBLIT, FENCE, queue/idle/status, double buffer, font RAM write, CLUT write, text-light select, scanout fill and its handshakes.

---

## 4. Hardware constraints (b)

* **Device** 5CEBA4F23C8: 18,480 ALMs, 308 M10K, 66 DSP. **Latest numbers:** `wdog-w3` (the shipped v0.7 bitstream): 15,507 / 15,485 ALMs (84 %), 19.4 k registers, 274 RAM blocks, 23 DSP, two seeds closed; `save-s3-b1129` (latest, one seed): 281 RAM blocks (27 free), 23 DSP, worst setup +0.655 ns, hold +0.115 ns. Free nominally about 2,970 ALMs, 27 M10K, 43 DSP; **LABs were 93 % at T0, so the real ceiling is lower than the ALM count suggests.** The last full per-entity table is T0 (`stats-b821`): `mp3_fb` 1,532 ALMs (engine, queue, scanout, font controller, CLUT), 38 M10K (queue 3, CLUT 1, font RAM 12, line buffer and others), 6 DSP, row buffer as two MLAB copies. **Engine-only ALM today: unmeasured**; measure with the standalone probe (`tools/sdram_probe.py`-style, KB-190) or by reading `ap_core.fit.rpt` of `def-b931` on the VM per entity (`tau_talos` after the split).
* **Timing:** engine, queue read side and SDRAM master at `clk_sdram` 100 MHz; CPU side and queue write side at `clk_sys` 66.667 MHz (`TAU_CLK66`); video 12 MHz. Worst closed setup is +0.655 ns slow-85C, the SDRAM read capture (the same in `sa2-b927`, `def-b931`, `wdog-w3`, `save-s3`), so the engine currently has more margin than the capture path (T0 baseline: engine-dominated worst +1.19..+1.44 before the new PHY). Fit variance by seed is about 1.2 ns (KB-011): both seeds are mandatory. Engine paths have failed five times by the same shape (B-109, B-111, B-114, B-157, B-231): a multi-stage arithmetic chain computed in the same cycle it feeds a write port. The new design must register one stage ahead from the start.
* **Crossings** in the engine today: command queue (Gray pointers, two-flop synchronisers), sticky fields and tables (quasi-static, informal), `text_light` and `dbuf_cpu_buf` pre-synchronised by the caller, flip toggle, scanout fill-request toggle and line number (now in `tau_scanout` with `tau_cdc_gray_bus`), retire counter (`tau_cdc_gray_ctr`), idle. The rewrite should make every one explicit and no more than today.
* **SDRAM budget (computed here, not measured):** 100 M words/s peak. Scanout: 360 fills per frame x 60 = 21,600 fills/s, 400 words plus about 30 cycles overhead: about 9.3 % (the engine's own count: 144,720 cycles per frame, B-898, 8.7 %). The measured whole-system busy under a blit storm was 15.8 % (B-146, older controller). The CPU port costs about 50 cycles an access, worst 360-373 under scanout and drawing (KB-030). Three clients share `sched_v` (scanout urgent, engine, CPU); the engine is one serial master, so a fill waits at most for the one engine unit in flight (`SDRAM_SCHED_A_NOTES.md`: "not pre-emption of the engine").
* **Line deadline:** a line lasts 41.667 us = 4,167 clk_sdram cycles; the fill is requested one scanline ahead; slack is about 3,700 cycles per line after the fill. Any single non-preemptible engine unit must stay under about 1,000 cycles (an old 511-word constant row is about 520) and the counter `GFX SCAN LATE` must stay 0 (it is 0 on every Pocket run since DEV 196).
* **Video:** one mode in use, 400 x 360 at 12 MHz pixel clock, 500 x 400 total, exactly 60 Hz (integer 4x of the panel). A 720-line mode exists only as the unmerged `test/720` work and is last by owner decision; the contract takes width, height, stride and fill length as parameters so it stays resolution-agnostic.

## 5. Helios needs (c)

What a UI library wants, argued against D-T12's data (no invented need):

| Want | Today | Worth building now? | Justification |
|---|---|---|---|
| Alpha / blend | DSP blend on BLIT only (crossfade, scope trail) | Keep. Constant-colour blend (translucent fills): **later** | No caller; `canvas.py` pins today's formula |
| Clip rectangle | software clip in `gfx.c` (rect, text cells refused at the edge) | **Later**, design it in (x, y decode at fixed pitch 512 is a shift) | Firmware clips already; no measured cost |
| Rounded rectangles | RRECT exists, unused by fw2 | **Drop now**, re-add as an extension if Figma designs ask | fw2 has none; its `pill` uses rects |
| Text / glyph paths | CHAR 1x, Latin-1 RAM font; extended/CJK parked | Reserve a glyph-source interface only (code, row in, 64 coverage bits out) | Owner scope for later; PSRAM glyph fetch needs its own audio-deadline study (review R12) |
| Surface-to-surface copies | BLIT with sticky bases | Keep; add overlap-safe order | removes the chunking and the whole-grid double copy class (B-900) |
| Double buffering | built and used (frame begin/end, compose, crossfade) | Keep bit-for-bit as driver contract | |
| Gradient / pattern sources | none (parked T6) | **Not now** | no measured need; CLUT cblit covers the Waterfall |
| Partial redraw | firmware dirty regions, compose via blits | No engine support needed | |
| Reverse row order / negative stride | parked by D-T12(c) | **Design in at zero cost**, do not wire to firmware until a measured miss (vertical Waterfall 450 k cycles) | retrofit is what costs; a new design can have it from the first line |
| Burst SBLIT (speed) | one SDRAM transaction per pixel, about 20 cycles a pixel (review arithmetic, not measured on this build) | Natural in the new structure, same pixels | Chladni spends 40-52 % of 243-251 k cycles in fences (B-896/B-900); how much is SBLIT is **unmeasured**: attribute by replaying its command log through the old engine's bench (host, cheap) |

---

## 6. Candidate architectures (d)

| | **A: front end + row-job FIFO + back end (old P3)** | **B-full: serial rewrite, all nine opcodes** | **B-lean: serial rewrite, what ships (recommended)** |
|---|---|---|---|
| Shape | decode and lower every command to row jobs, ping-pong row buffers, SRC-OP-DST stages | one command in flight, single row buffer (MLAB, one write port, two read copies), per-opcode row sequencing | the same, only RECT, CHAR 1x, BAR, BLIT (+DSP blend), SBLIT, CBLIT, FENCE, reserved opcode space (retire as no-op) |
| ALM (estimate, not measured) | at or above today's; plan goal 1,800 for the whole module | about today's engine | 10-20 % below today's |
| M10K / DSP | queue 3 + CLUT 1 + font 12 as today; +0 if row buffers stay MLAB (review R10 warns of silent register fallback, about 6,500 ALMs) / DSP at most today's 6 | as today | as today (cmd word shrinks; depth keeps 3 blocks unless width halves) |
| Timing risk | high: new pipelines, more write sources, deep MLAB | medium | medium: fewer sources into the shared row-buffer write path |
| Verification burden | highest (new job protocol, ping-pong hazards, extra fuzz dimensions) | high (RRECT sequencer, key pre-read, modes) | moderate |
| Benefit measured | none (D-T12, R1) | none | provenance, simpler contract, room for free improvements (overlap-safe copy, burst SBLIT, reverse order) |
| Fallback | | | B-full; or stop and keep the old engine (D-T12) |

**Recommendation: B-lean**, structured as: command queue module (asynchronous FIFO, a generic idiom; the same module can serve other cores), command sequencer, row datapath (read, transform, write, with blend and anti-alias units each one registered stage), glyph unit (interface that a later PSRAM source can implement), SDRAM master handler with the existing `p0` protocol and the `tau_sdram_a_gfx_if` adapter unchanged, scanout fill client (the current handshake: request toggle, line number, done pulse, 400 words, priority over engine units). **Module names: `tau_talos` stays the shell and the macro that selects old or new is `TAU_TALOS2_ENGINE` (old file stays until Pocket confirmation, D-T01).** Hold the interface of `tau_talos` fixed so `tau_gfx.sv` and `tau_soc.v` do not change (an ALM and review saving: the assessment lists them as separate, undecided phases).

## 7. Behavioural contract outline (e)

Behaviour only, to be extended into the contract in phase 1. Each statement gets a conformance vector.

**7.1 Interface (unchanged unless listed).** Queue write side in `clk_sys`: a push carries `op[3:0]`, `addr[18:0]`, `fg[15:0]`, `bg[15:0]`, `w[8:0]`, `h[8:0]`, `glyph[6:0]`, `glyph_hi[1:0]`, `sx[1:0]`, `sy[1:0]`; MMIO packing (`R_FB_ADDR`, `R_FB_SIZE` `{h[17:9], w[8:0]}`, `R_FB_COLOR` `{bg, fg}`, `R_FB_GO` write `{op[3] at bit 14, glyph_hi[1:0] at 16:15, sy 13:12, sx 11:10, glyph 9:3, op[2:0]}`) is in `tau_soc.v` and stays. Sticky inputs: source and destination base (25 bits), blend enable and weight (8 bits), CLUT write port, text-light, double-buffer select/flip. Status: `idle`, `fence` (32-bit retire count), `free` (0..255), `ovf` (saturating), late-line counter. SDRAM master: the `p0` protocol (one request at a time, a read of known length with the length sideband, constant or streamed write with the one-cycle pull latency, `available` while idle, `ready` at the end). Scanout handshake as `TALOS_SCANOUT_CONTRACT.md`.
**7.2 Ordering.** Commands execute in queue order. A command's rows complete in row order; **within a row the source row is read entirely before the destination row is written** (the property the driver's one-pass scroll depends on). A sticky write affects commands dispatched after it. Zero width or height retires with no memory access. Unknown opcodes retire as no-ops (change from the old engine, which draws a row; the driver never sends them) and still count for idle.
**7.3 Commands (legal domain).**
* RECT: `w` x `h` words of `fg` at `addr` (row pitch 512). BAR: the same span; the bottom `min(lit, h)` rows in `fg`, the top rows in `bg`; `lit` is 9 bits.
* CHAR (1x): the glyph code (8 bits with `glyph_hi[0]`) selects a cell of the font memory; codes 0x20..0x7E and 0xA0..0xFF map to cells, every other code to the blank cell; the 16 x 16 cell is composed with 4-bit coverage per pixel, per channel `((fg * w + bg * (16 - w)) >> 4)` with `w` taken from a 16-entry table selected by text-light (the dark table is the open provenance item, section 9; the numbers are in `tools/host/blit_reference.py` and are the vectors); every cell pixel is written (fg on bg, nothing underneath needs erasing). Scales other than 1x: dropped in B-lean (`sx`, `sy` ignored, reserved).
* BLIT: `w` x `h` copy from `SRC_BASE + {fg[2:0], bg}` to `DST_BASE + addr`, pitch 512, source row read whole, then written; with blend on: per channel `(src * a + dst * (256 - a)) >> 8` on 5-6-5 channels; **new contract: any `w` in 1..511 works** (the old engine needed `w <= 127`).
* SBLIT: nearest-neighbour scale of a `w` x `h` source by `sx`, `sy` in {1x, 2x, 3x} (`sel` 0, 2, 3; 1.5x unused): source column `floor(i / s)`, output width `w*s` clamped to 127 (pinned by `canvas.py`); height clamp: see section 2.
* CBLIT: output pixel = CLUT[source word low byte] (256 entries, written from `clk_sys`); the re-index offset is dropped in B-lean (reserved).
* FENCE: draws nothing; retires after everything earlier; increments the retire counter once.
**7.4 Double buffering.** RECT, CHAR, BAR addresses inside the visible frame (word address below 184,320) add 1,048,576 when the CPU target is buffer 1; blit-class commands use their sticky bases only; the scanout fill reads the displayed buffer; a flip request is applied only in vertical blanking.
**7.5 Fence, idle, status.** `idle` = accepted pushes equal retired commands; a push followed one cycle later by an idle read reads busy; the fence counter counts FENCE retirements; `free` = 255 minus occupancy; a push into a full queue is dropped and counted (saturating); the late-line counter is the scanout's.
**7.6 Errors.** No hang on any input: undefined shapes (oversize, zero, page-crossing, reserved opcode) must retire; a command never writes outside its rectangle and its documented companions. Legal-domain bounds: addresses within the 64 MiB device, `w`, `h` 9 bits.
**7.7 SDRAM access.** One request at a time; reads 1..512 words with the length given up front; writes 1..512 words constant or streamed; **no request crosses a 1,024-word page** (the new engine splits; the old one hung on reads and wrapped on writes); the scanout fill (400 words) is dispatched before any pending engine unit and no engine unit exceeds about 1,000 cycles; the engine never issues while `available` is low; constants hold for the whole burst.
**7.8 What a conformance test checks:** every conformance vector of the contract, the `p0` protocol legality monitor, the idle/fence/free relations, fence ordering with respect to the last write of every earlier command, queue pressure without loss, and the stated bound on a fill's wait.
**7.9 Out of domain (not compared):** widths over 511, addresses beyond the device, reserved opcodes' pixels, behaviour while `reset` is high.

## 8. Phased plan (f)

| Phase | Work | Output | Effort |
|---|---|---|---|
| 0 | Owner decisions (section 11), measure the unknowns that are cheap (per-opcode histogram, engine-only ALM, Chladni engine attribution) | decisions, numbers | 1 session |
| 1 | Contract (extend section 7 into the full contract by the current session), conformance vectors, **reviewed by an agent that never saw the old text (checkpoint R1)** | `docs/research/TALOS_ENGINE_V2_CONTRACT.md`, vector files | 2-3 sessions |
| 2 | Test generator and model: legal-domain stream generator, the Python executable model written from the contract (not from `blit_reference.py`, which cites the old RTL line by line), the classifier, the cost harness, the old-engine harness (below) | `tools/talos_fuzz/` | 3-4 sessions |
| 3 | Clean-room implementation by a separate agent on a technically separated tree (process doc); its bench, 10+ mutants like the scanout (12) | `tau_talos` v2 file, bench | 3-5 sessions |
| 4 | Host verification: classified differential, cost comparison, conformance replay, `rtl-lint`, existing T3 benches, the chip-model benches (`sim/sdram_sched_a/run_gfx.sh`), the watchdog SoC bench; **checkpoint R3 (RTL/timing review) before any fit** | green suite | 2-3 sessions |
| 5 | Fit: launch two seeds in parallel in the evening, **about 3 h 30 for both on the VM (1 h 40 for one; say the local finish time at launch, rule 3)**; check Analysis & Synthesis at the 5-minute mark (the B-1129 lesson) | `tw2-bNNNN`, timing and map-report grep | 1 session |
| 6 | Pocket: package as the next TAU DEV number via `tools/install_dev_core.py` (dry run, `--yes` after approval, `--card /Volumes/Pock`); existing proofs: Check page all PASS, `GFX SCAN LATE` 0, `GFX Q OVF` 0, the seven meters window and fullscreen with Report rows (compare to the B-900/B-902/B-904 table), the Blit Test port and screenshot-vs-reference, SDRAM soak, the pack watchdog hang packs (`TAU DEV 204`-style); **new rows:** `GFX ENGINE VER`, per-opcode cycle counters | B-NNNN entry per run | 1-2 sessions + owner time |
| 7 | Rollout: ship the new engine as the default only after the Pocket run; old engine stays selectable and the oracle stays in `sim/` until that run (D-T01); then pre-deletion review (R6), then deletion | | 1 session |
| 8 | Optional improvements, one at a time, each with its class, owner decision, capability bit or ABI route | | per item |

Total **11-17 sessions**, 2 fits, about 4-6 weeks wall clock. If the fit fails timing: section 10.

### 8.1 Verification: the classified differential
(The bit-exact differential `tb_tau_gfx_diff` stays as it is for the old path: it is cycle-exact and random, so it cannot judge a new architecture.)
1. **Generator** (Python, seeds and a corpus file): random and directed command streams within the legal domain: all shipped command classes, random and edge geometry (width 1, 127, 128, 400, 511; heights; addresses near row ends, page boundaries and the stash), sticky changes after fences, both CPU buffers, queue pressure (bursts into a stalled queue), fence points, CLUT and font writes, text-light, blend weights 0, 1, 128, 255; plus the **real streams**: the lab's `log` of every meter and the firmware sequences of every UI view.
2. **Three executors:** the old engine (oracle) and the new engine, each in its bench with the same functional SDRAM stub (and again on the real PHY chip model), and the Python model.
3. **Classifier:** for each memory word that differs between old and new, find the last command that wrote it (the bench logs writes with command index) and check the class table (`classes.json`): class `IDENTICAL` (no difference allowed), or an approved class with a predicate over the command and an expected value computed by the model (`IMP-OVERLAP`, `IMP-WIDE`, `IMP-PAGE`, `IMP-SBLIT-ROWS`, ...). A difference that fits no class is **UNAPPROVED = REGRESSION = FAIL**, printed with the command, geometry and seed for replay. The new engine must also equal the model on every word: the model is the contract made executable.
4. **Triage:** a failing diff is first replayed minimal (shrinking), then labelled by the engineer: (i) new-engine bug (fix), (ii) model bug (fix the contract and the model, re-review), (iii) old-engine quirk outside the legal domain (tighten the generator, record in the contract), (iv) a genuine improvement (propose a class). **Only the owner approves a new class**, by an entry in `docs/DECISIONS.md` naming the class, the rule, the visible effect and the shipping route; an agent may propose, never approve.
5. **Cost comparison:** engine-busy cycles per command (dispatch to retire) from both benches on the corpus, per opcode class and for the real meter logs, p50, p95 and total; R4 tolerances.
6. **Conformance:** replay the host-table call sequences of `sdk/sdk/conformance` and the lab logs through `fw2/gfx/gfx.c` semantics onto the new engine's memory and compare with `canvas.py`'s picture; run the SDK's suite unchanged. (Whether an existing host harness already runs `gfx.c` against the RTL, `sim/p2_gfx_harness.c` or `test_p2_gfx.py`, was **not verified**; if not, phase 2 builds the shim.)
7. Mutants: at least 10 on the new engine (edge off by one, blend rounding, lit clamp, CLUT index, double-buffer rule, fence early, page split, write mask, row order, coverage table select), each killed by a named check.

## 9. Risks and fallback (g)

| Risk | Likelihood | Mitigation or fallback |
|---|---|---|
| Timing below R8 | medium | register one stage ahead throughout; MLAB single write port with the data mux before the register (R10 of the exhaustive review); probe the engine alone first (standalone Quartus project, KB-190). **Fallback:** drop to B-lean minus blend, or keep the old engine and record the result (the old engine is never removed before confirmation) |
| Fit fails in synthesis (RAM inference falls back to registers) | low-medium | map-report grep gate; confirm synthesis at 5 minutes |
| An unclassified pixel difference found late | medium | classes written before the implementation, real lab logs in the corpus from phase 2 |
| Pixel change reaches a pack | low | the policy of section 2; conformance replay; no change to pinned rules |
| Provenance claim is not credible | medium | technical separation and a similarity audit (process doc); the claim stays "honour-based plus tooling" and the owner/lawyer judgement (R7 of the assessment) is open |
| One Pocket only (D-T06) | certain | the engine is not the PHY; no new I/O; acceptance as for the scanout (DEV 198) |
| Effort overrun | medium | stop after phase 4 and keep the contract and generator as test assets (the fuzz is worth building for the old engine too) |
| Gain is only provenance | certain | stated, owner decides (section 11) |

## 10. Expert review checkpoints (h)

| # | When | Reviewer (not the author) | Receives | Must answer |
|---|---|---|---|---|
| X1 | contract done | an agent that never read the old engine or this report, plus the owner | the contract, vectors, MMIO map | Can you implement every command from this text alone? List every sentence you had to guess. Is any statement non-behavioural? Are the out-of-domain rules complete? |
| X2 | contract and classes | the owner | section 1 criteria, section 2 table, `classes.json` | Do you approve each class and each drop (RRECT, key, re-index, modes, scales, ops 10-15)? |
| X3 | before the fit | a Cyclone V reviewer agent | RTL, SDC, QSF bundle, bench log | One writer per RAM, registered mux inputs, no latch, crossings as listed, worst-path guess, MLAB vs M10K, DSP registration; will it close +0.655 / +0.078? |
| X4 | similarity audit | a script plus a reviewer | new file, old file, tool output | Is every similar run dictated by the contract or an industry idiom (queue, Gray code)? Are all copy-aware-blame lines the author's? Thresholds met? |
| X5 | Pocket result | the owner | Report rows against R1-R10 | Pass or fail per row; any new class; fit margins |
| X6 | pre-deletion | the owner with a reviewer | Pocket evidence, diff of what is deleted, grep proof of no reader | D-T01 gate: Pocket-confirmed, rollback tag, every reader gone, oracle no longer needed (or archived) |

## 11. Owner decisions and unknowns (i)

**Decisions.**
1. Reopen the engine for the ownership reason (D-T12 parked it for performance only; the goal in `HARPMUDD_REMOVAL_ASSESSMENT` is "no HarpMudd-authored lines in anything that ships"). Recommended yes if the owner wants the goal; otherwise stop (the cost is 11-17 sessions and 2 fits for about 360 inherited lines).
2. Approve section 1 (regression definition) with its tolerances.
3. Approve B-lean, or B-full.
4. Per drop: RUN, COPY, RRECT, key, re-index, blend modes 1-4, CHAR scales, strides, ops 10-15 (CSV column `keep_drop_change`).
5. Per improvement: overlap-safe copy order; SBLIT row clamp (fix the models or the engine); lifting the 127-wide limits; reverse row order flag; text weight-table refit (and the open **dark table provenance**: the 16 numbers were written by HarpMudd, the light table is Tau's own fit by `tools/gen_text_gamma.py`, itself of HarpMudd lineage in the assessment; whether 16 numbers that reproduce a fitted curve count as authored text, or should be re-derived by a new fit, is the owner's/lawyer's call; the contract carries them as data until decided).
6. Capability field in `R_GFX_STATUS`: approve claiming bits [30:17] (free per `MMIO_ALLOCATION.md` row 0x1BC; verify nothing else uses them).
7. Process: technical separation of the author's tree; similarity thresholds.
8. Fit launch timing; Pocket runs; deletion of the old engine only after a Pocket run.
9. Confirm what "ABI 2" means (the header says 1.2) and which Omega artefacts twin the engine (this repository holds `tau_fb.js` and the SDK model only).

**Unknown or unmeasured, and the cheap way to measure each.**
| Unknown | How |
|---|---|
| Engine-only ALM, M10K, DSP and worst engine path today | read `def-b931`/`wdog-w3` fit reports on the VM per entity, or a probe compile of `tau_talos` alone |
| Per-opcode command histogram per UI view | lab log replay, or Diagnostic counters |
| Share of SBLIT in Chladni's fence time | replay its command log through `tb_tau_gfx_diff`-style bench with cycle attribution |
| Whether any pack uses an SBLIT taller than 127 output rows or a copy wider than 127 | grep and lab replay of the seven packs |
| Which Omega/wasm twins of the engine exist | ask Omega through `docs/interop` |
| Whether `gfx.c` is already run against the RTL in a bench | read `sim/p2_gfx_harness.c`, `test_p2_gfx.py` |
| Similarity baseline of the scanout pair (old vs new) | run the audit tool on it first and record the numbers as the calibration |
| Seed spread for a new engine | two seeds; the fit gives it |
| Whether the dark text table can be re-derived to equal the shipped one | fit it with `tools/gen_text_gamma.py` (or a new tool) |

Related: `docs/research/HARPMUDD_REMOVAL_ASSESSMENT_2026-11-25.md` (phase 5), `docs/DECISIONS.md` D-T01, D-T07, D-T12, `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md`, `docs/research/TALOS_SCANOUT_CONTRACT.md`.
