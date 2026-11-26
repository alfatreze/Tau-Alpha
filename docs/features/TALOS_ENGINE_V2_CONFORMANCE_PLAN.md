# Talos engine v2: conformance plan (v0.1, goes with `TALOS_ENGINE_V2_CONTRACT.md`)

**Status:** plan, 2026-11-26, audit B-1178. Nothing here is built yet except the oracle-side probe `sim/tb_talos_v2_contract_probe.v` (used to settle contract rules). Rule identifiers (C-...) and tags (PINNED, MATCH, FREE, CHANGE) are the contract's.
**Two sides.** The **oracle side** (the contract author and the coordinator) may run the old engine (`src/fpga/core/tau_talos.sv` as shipped, `sim/oracle/tau_talos1_oracle.sv`, Gallery's engine at `26d9b1c` in a separate tree) to produce and cross-check goldens. The **author side** (the clean-room implementer) receives only the package of section 9: the contract, vectors produced by the **new** Python model, the interface files and the bench requirements. No vector is copied from old-RTL output into the package; old-RTL runs only confirm that the model and the oracle agree, class by class.

---

## 1. Golden sources

| Golden | What it is | Used for | Side |
|---|---|---|---|
| G1 new model `tools/talos_model/` (phase 2, to write) | Python, written from the contract only (not from `tools/host/blit_reference.py`, which transcribes the old RTL) | every vector handed to the author; the per-word expected value in the differential | both (the model's source goes to the author) |
| G2 `sdk/sdk/conformance/canvas.py` | the SDK's model of the pack host table | PINNED rules C-RECT-1, C-BAR-1 (after the clamp fix), C-BLIT-1, C-BLIT-3, C-SBLIT-1/2 (after the row fix), C-CBLIT-1, C-MAP-1 | both (public) |
| G3 `tools/meters/preview/tau_fb.js` | the lab twin (also the lab-only `Fb.blend` modes 1..4) | the same rules plus C-BLIT-4 | both (public) |
| G4 Gallery bench cases (`sim/tb_mp3_fb.v` lines 202-240, 418-427, 538-580 at `26d9b1c`) | DSBLIT seven cases with an inline formula; CORNERS geometry and ordering | C-DSB-1, C-CORN-1: re-expressed as memory-level vectors by G1, cross-checked by running Gallery's engine (oracle side) | oracle side reads; author gets G1 vectors |
| G5 Gallery 1.0.0 real-picture goldens (D-T22: captured from Gallery 1.0.0) | a thumbnail set through DSBLIT at each grid size, and rounded with CORNERS | C-DSB-1, C-CORN-1 on real pictures | both (pictures and expected outputs; captured on the oracle side) |
| G6 Gallery viewer frame (`sim/tb_tau_gfx_view.v`, base 4,096, stride 1,208, 576,000 pixels; `DBL=1` runs) | whole-frame pixel compare of the viewer fill | C-VIEW-1/2, C-DBL-1 | the bench is ported to the new tree (it instantiates by port, no white-box reads) |
| G7 measured oracle outputs (probe, `tb_tau_gfx_move`, `tb_tau_gfx_t3`, `tb_helios_dbuf`, `tb_tau_gfx_fill`, `tb_tau_gfx_cblit`, `sim/sdram_sched_a/run_gfx.sh`) | facts about the shipped engine | to confirm G1 on MATCH rules; costs (section 7) | oracle side only |
| G8 the font memory images (`FONT_SPEC.md` layout, the shipped Latin-1 atlas, the ROM atlas) | data | CHAR vectors | both (data, black-box modules) |
| G9 the derived weight tables (`tools/derive_text_weights.py`) | data and method | C-CHAR-4 | both |
| G10 Tau V / Tau Μ old-player drawing sequences (`fw/` of `tau-nu`: `fb_copy`, `fb_rrect`, `fb_run`, CHAR scales) | real streams for the commands Player 2 never sends | MATCH rules of COPY, RRECT, RUN, CHAR scales | oracle side extracts streams; author gets them as G1 vectors |

**Gaps to close before the hand-off (phase 2):**
1. G1 does not exist. It is written from the contract by someone other than the RTL author; review X1 reads it with the contract.
2. No DSBLIT model existed anywhere outside Gallery's bench; G1 adds it; G4 and G5 cross-check it.
3. `canvas.py` and `tau_fb.js`: (a) SBLIT output rows are not clamped to 127 (D-T21 says correct them to the hardware); (b) BAR does not clamp `lit` to `h` (contract Q6). Until corrected, the replay of section 2.3 skips SBLIT calls with more than 127 output rows and BAR calls with `lit > h` (none in the seven shipped packs: to be confirmed by the replay's own count).
4. G5 does not exist yet (D-T22 approves the capture from Gallery 1.0.0).
5. G10 streams are not extracted yet.

---

## 2. Directed vectors

### 2.1 Vector format (what the author receives)
One JSON-lines file per vector set. A vector = (a) the build parameters; (b) the initial memory image as a list of (word address, words) regions plus a fill pattern for the rest (`k * 0x9E37 + 0x1234` modulo 2^16 at word `k`, the probe's pattern, so every untouched word is distinct); (c) the CLUT, font and corner-table contents; (d) a sequence of CPU actions: sticky writes (field, value), `text_light`, `dbuf_cpu_buf`, flip requests, pushes (all `cmd_*` fields), "wait for idle", "fence"; (e) the expected memory as regions that must match word for word plus a CRC of the whole memory; (f) for status vectors, the expected register reads at given points. The bench requirement: every vector runs on the functional stub (section 7.1) and on the realistic model, with the protocol monitor of 2.4 armed.

### 2.2 Per command and per PINNED rule
| Set | Rules | Cases (each with the double-buffer CPU target 0 and 1 where it applies) |
|---|---|---|
| V-RECT | C-RECT-1, C-RUN-1, C-MAP-1, C-GEN-1 | w, h in {0, 1, 2, 127, 128, 400, 511} x {0, 1, 2, 16, 359, 360, 511}; addresses at row starts, row ends (x + w = 512), stash rows, row 359/360 straddle, near 2^19 |
| V-BAR | C-BAR-1 | h in {1, 10, 200, 511}; lit in {0, 1, h-1, h, h+1, 127, 128, 511}; w 0 and 1..511 |
| V-CHAR | C-CHAR-1..6, C-FONT-1 | every code 0..255 at 1x with the font RAM (both `text_light` values); every code 0..127 with the ROM; the four scales on each axis independently (16 combinations) for 10 codes; fg/bg pairs: black/white, white/black, the theme palettes of `themes/*.json`, channel extremes (31/0, 63/0); weight tables: the v0.1 defaults and the shipped dark table (for the IMP-TEXT-DARK class) |
| V-COPY | C-COPY-1 | w 1, 2, 126, 127; h 1..20; overlapping left/right/up/down; both CPU buffers |
| V-BLIT | C-BLIT-1..6, C-ORD-1/2 | bases 0, 2^24, 2^25 - 2^16 (top of memory), strides 0, 1, 511, 512, 513, 1023; w 1..127 edge values; plain, key (the key at row start, row end, every word), blend modes 0..7 with alpha 0, 1, 127, 128, 254, 255 and channel extremes; key + blend; overlaps left (exact, case A), down in one command (case B: the MATCH result), down in chunks (case C) |
| V-SBLIT | C-SBLIT-1..4 | sizes s in {1, 2, 3, 42, 43, 63, 64, 127, 128, 200}; selectors 0..3 on each axis; the clamp on columns and on rows (60 x 60 at 3x); sticky key and blend on (must not change the output) |
| V-CBLIT | C-CBLIT-1..3 | random palettes; REINDEX 0, 1, 200, 255 (wrap); high byte of the source non-zero; key and blend on (ignored) |
| V-RRECT | C-RRECT-1/2 | r 0..15 and the 7-bit field 16, 64, 127 (clamped to 15); tables: zero, monotone, Helios' default; w, h at the domain edges (r = h, cut = w) |
| V-CORN | C-CORN-1 | Gallery's four cases (r = 2 on 10 x 6, r = 3 on 20 x 9, r = 0, w = 0, the RECT queued after it) plus the same sweep as V-RRECT; feature off: no-op |
| V-DSB | C-DSB-1..3 | Gallery's seven cases (each ratio on both axes, two mixed, 96 wide at 2:3 by 3:4) on the probe's distinct-word pattern; sizes giving extents 0, 1, 126, 127 and clamped; strides 512 and 1,208; key and blend on (ignored); G5 thumbnails |
| V-FENCE | C-FENCE-1, C-RET-1, C-IDLE-1/2 | FENCE alone, after each command class, 50 in a row, interleaved with 300 pushes into a stalled memory |
| V-ESC | C-ESC-1, C-OP-1 | opcodes 10..15 with every feature off and on; sub-opcodes 0, 1, 63 |
| V-DBUF | C-DBUF-1..3 | every frame-offset command with the CPU buffer 1 straddling row 360; blit-class commands never offset; flip requested 0, 1, 2 times before a vertical-blank edge; fill reads after a flip |
| V-FILL | C-FILL-1..5 | fills of lines 0, 1, 358, 359 with each displayed buffer; a request during a long command of every class (wait monitor); requests during a fill (C-FILL-3 improvement allowed: every request served) |
| V-STAT | C-Q-1..4, 4.3 | 255 accepted and 45 dropped of 300 pushes into a stalled memory; free counting down and back; overflow saturation (66,000 dropped pushes) |
| V-RST | C-RST-1..3 | reset with a full queue, during a fill, during each command class; sticky and memory contents unchanged across reset |
| V-VIEW | C-VIEW-1..5, C-DBL-1 | G6 frames: UI, then viewer at a vblank, with and without the doubler; a slow-memory case counting late lines; base and stride changed mid-frame (take effect at the next vblank only) |

### 2.3 Public-artefact replay (R3)
Replay the host-table call sequences of `sdk/sdk/conformance` and the lab `log` of each of the seven shipped packs through `fw2/gfx/gfx.c` semantics (the existing harnesses `sim/p2_gfx_harness.c`, `sim/test_p2_gfx.py`, `sim/test_p2_blit_check.py` compile `gfx.c` for the host; phase 2 adds a back end that turns its register writes into pushes for the RTL bench) and compare the visible frame with `canvas.py`'s picture. Text is excluded (the models draw blocks).

### 2.4 Monitors armed on every run
Memory-protocol monitor (C-MEM-1..5, 7, 8: one request at a time, lengths 1..512, `p0_data` held during a constant write, exactly `p0_rd_len` words taken, nothing issued before `sdram_init_complete`); write-rectangle monitor (every write lands inside the commanded rectangle of the command that wrote it, or its documented companions: for COPY/BLIT the destination, for a fill the line buffer); retirement monitor (one retirement per accepted push, in order, FENCE count = FENCEs retired); fill-wait monitor (C-FILL-2: worst wait recorded per run).

---

## 3. The classified differential (R1)
**Oracles.** The shipped `tau_talos.sv` frozen at its hash (to be copied to `sim/oracle/tau_talos2_oracle.sv` when the new file takes its name), which has FENCE, the 400-word fill and the pop fix; `sim/oracle/tau_talos1_oracle.sv` stays only for the existing cycle-exact `tb_tau_gfx_diff` of the old path. For the GALLERY set, Gallery's engine at `26d9b1c` in a separate tree (never in the author's tree).
**Method.** The same command stream, memory image and handshake stimulus go into the oracle bench and the new-engine bench (both on the functional stub, then both on the realistic model). Each bench logs every memory write with the index of the command that issued it. At the end, every word that differs between the two memories (and the expected model value, G1) is attributed to the last command that wrote it in either run, and classified:

| Class | Predicate | Expected value | Approval |
|---|---|---|---|
| IDENTICAL | default | old = new = model | |
| IMP-TEXT-DARK | the word was last written by CHAR with `text_light` = 0 and the coverage of that pixel is 5, 9, 12 or 14 | model with the derived table | D-T21 |
| IMP-RESERVED-NOOP | the old run's last writer is an opcode 10..15 that the new build does not implement | the value before that command | D-T19/D-T21 (contract Q3: confirm) |
| OOD | the command is outside its legal domain (contract per command) | not compared; the safety monitors decide | contract C-GEN-2 |
| IMP-FILL-NOLOSS | a fill request the old engine lost (C-FILL-3) | the line is filled | FREE in the contract |
| later: IMP-WIDE, IMP-OVERLAP, IMP-SBLIT-BURST (speed only, no pixel class needed) | | model | owner decision with a capability bit |
Anything else is **UNAPPROVED = REGRESSION = FAIL**, printed with the seed, the command index, its fields and a shrunk replay. Triage: new-engine bug / model bug (fix contract and model, re-review X1) / old-engine quirk outside the domain (tighten the generator, record in the contract) / proposed improvement (a new class, approved only by the owner in `docs/DECISIONS.md`). The new engine must also equal G1 on every word of every legal-domain vector.

---

## 4. Constrained-random stream generator (`tools/talos_fuzz/`, phase 2)
* **Seeds:** one 32-bit seed per run, printed; 200 seeds per gate run; any failing seed is kept as a regression vector.
* **Opcode mix:** two profiles. *Real*: weights from the meter logs and UI views (RECT 45 %, CHAR 30 %, BLIT 10 %, BAR 5 %, CBLIT 4 %, SBLIT 2 %, FENCE 4 %; refined by the per-opcode counts of the replay). *Uniform*: every opcode 0..15 equally, including reserved ones and feature opcodes in every build.
* **Sizes:** w and h drawn half from edge values (0, 1, 2, 126, 127, 128, 255, 256, 359, 360, 400, 511) and half uniform in the legal domain; blit-class w in 1..127 in legal mode, 1..511 in OOD mode.
* **Addresses:** visible rows, stash rows 360..1023, columns 400..511, straddles of row 359/360, rows that end at x = 511, page edges (word addresses near multiples of 1,024), near 2^19 for frame-offset commands, bases near 2^24 and near the top of 2^25 for blit-class; sources overlapping destinations in all four directions with probability 0.2.
* **Sticky and memory state:** changes of bases, strides (0..1023), key, blend (mode 0..7, alpha), reindex, CLUT entries, corner table, font words, `text_light`, CPU buffer: only after a FENCE has retired (legal mode); at random points in OOD mode (safety monitors only).
* **Hazards:** overlapping copies (left, right, up, down, with key and blend); queue back-pressure (memory `p0_available` held low for 0..20,000 cycles while 300 pushes arrive); fence storms (bursts of 1..50 FENCEs, FENCE after every command); mid-stream mode changes (flip requests at random times, viewer mode switches at vblank in VIEW builds, `text_light`); memory stalls (random gaps in `p0_data_available`, `p0_available` low between requests, `p0_ready` delayed 0..200 cycles); fill requests every scanline with jitter, plus late, doubled and missing ones; reset mid-run (1 run in 20).
* **Real streams in the corpus:** the seven packs' lab logs (window and fullscreen sizes), the UI views (Now Playing, Library, Settings, Info/Report, splash) through the `gfx.c` harness, Tau V/Tau Μ old-player sequences (G10), Gallery firmware sequences (grid, thumbnails, viewer start).

---

## 5. Mutations the new engine's benches must kill (at least 16; each with the check that kills it)
| # | Mutant | Killed by |
|---|---|---|
| M1 | rectangle one column short / one row long | V-RECT, write-rectangle monitor |
| M2 | BAR without the `lit <= h` clamp / lit rows on top | V-BAR |
| M3 | glyph index without bit 7 / the Latin-1 range one glyph off | V-CHAR codes 0x80..0xFF |
| M4 | weight table select ignored / tables swapped | V-CHAR both polarities |
| M5 | mix truncation changed (rounded instead of floored) | V-CHAR channel extremes |
| M6 | blend with weight 255 instead of 256 | V-BLIT alpha sweep |
| M7 | key ignored / key applied to SBLIT or CBLIT or DSBLIT | V-BLIT key, V-SBLIT, V-CBLIT, V-DSB |
| M8 | SBLIT without the row clamp / source column `i` instead of `floor(i/s)` | V-SBLIT |
| M9 | CBLIT without REINDEX / without the lookup | V-CBLIT |
| M10 | RRECT/CORNERS cut index `k` instead of `r - k` / corners in fg / CORNERS with the fill | V-RRECT, V-CORN |
| M11 | DSBLIT truncating average / wrong neighbour row / green summed from 5 bits / step capped at 1 | V-DSB (Gallery's own mutant set re-expressed) |
| M12 | double-buffer offset applied to blit-class, or not per row | V-DBUF |
| M13 | flip applied immediately instead of at vblank | V-DBUF, V-FILL |
| M14 | FENCE retires before the previous command's last write completes / fence counts every command | V-FENCE, retirement monitor |
| M15 | idle ignoring an in-flight command; a push not counted for one cycle | V-STAT, C-IDLE-1 |
| M16 | overflow not counted / a full queue accepting a push | V-STAT |
| M17 | fill of 399 words / wrong line-buffer half / fill not given priority | V-FILL, fill-wait monitor, scan-out late count |
| M18 | a zero-length or 513-word request | protocol monitor |
| M19 | row written before its source row is fully read (overlap) | V-BLIT case A |
| M20 | viewer chunk step 399 / stride ignored / doubler not halving the line | V-VIEW (Gallery's `BUG_VIEW` 1-6 re-expressed) |
The mutants are parameters of the new file, default 0, never set in a build, listed in its header and run by one Makefile target.

---

## 6. Feature-set matrix and structural checks
* **Simulation, 8 configurations** (D-T22 test matrix): all off; CORNERS; DSBLIT; ESC; VIEW; VIEW + DBL; GALLERY (all on); CORNERS + DSBLIT. Each runs the base directed sets, its feature's vectors, the random differential (200 seeds) and the mutant sweep.
* **All-off proof:** (1) the all-off build passes every base vector and the differential with zero UNAPPROVED differences; (2) a `quartus_map` probe (`tools/probes/talos_feat/run_probe.py`) of the all-off build gives the same ALUT, register, block-bit and DSP counts as a build of the same file with the feature code removed (B-1177's method).
* **Map-level resource gate (before any fit):** TAU set not above `ta-shipped` (1,489 ALMs estimated, 2,120 ALUTs, 1,219 registers, 221,696 block bits, 6 DSP; +5 % ALM allowed); GALLERY set not above `gal-view` (1,902 / 2,732 / 1,433 / 221,696 / 11 DSP); every memory inferred as RAM (grep of the map report: no register-file fallback for the row storage), the queue in 3 M10K.
* **Lint:** `make rtl-lint` (Verilator) clean for the new file; no latch; one write port per inferred RAM.

---

## 7. Performance benches and how cost is measured
1. **Functional stub** (the probe's `probe_sdram_stub`, given to the author as a stand-alone stub in the interface package): dispatch-to-retire cycles per command class, measured from the cycle the engine accepts a command to its retirement (the new engine exports, or the bench infers from the protocol, the dispatch and retire instants; for the old engine the probe reads two internal signals, oracle side only). Reference table: contract section 7.1.
2. **Realistic model** (`sim/sdram_sched_a/tb_a_gfx.sv` through `sched_a`, the PHY and the chip model, CPU traffic on): `cycles_to_idle` per stream and per class (`FIXOP`); reference for the mixed 150-command stream: 591,783 cycles, 0 late lines, 0 overflow (B-1178). Requirement R4: p50 and p95 per class within +5 %, no class above +10 %, on legal-domain streams.
3. **Fill-wait monitor:** worst cycles from a fill request being seen to its read request; shipped engine 131 on the probe's mixed stream; requirement at most 600 (contract C-FILL-2).
4. **Fence latency:** clk_sys cycles from a FENCE push to the counter step on an idle engine (shipped 6, requirement 12); push-to-idle for a 1 x 1 RECT (shipped 10, requirement 20).
5. **Meter-tick replay:** each pack's lab log through the `gfx.c` harness into both engines on the realistic model, engine-busy cycles per tick; compared with the oracle (R4) and, on a Pocket, with contract table 7.2.

---

## 8. Host verification gates before a fit (all must pass, in this order)
1. The new file elaborates in all 8 configurations; illegal combinations stop with an error.
2. Directed vector sets of section 2.2 on the stub: all pass against G1.
3. The ported Gallery viewer bench (G6) with and without the doubler.
4. The classified differential: 200 seeds legal mode, 50 seeds OOD mode (safety monitors only), the real-stream corpus; zero UNAPPROVED.
5. Existing benches unchanged and passing against the new engine: `make test-rtl-gfx-t3` (and its 5 mutants), `test-rtl-gfx-status-soc`, `test-rtl-helios-dbuf` (DBUF 1 and 0), `test-rtl-gfx-move`, `test-rtl-gfx-fill`, `test-rtl-gfx-cblit` (its burst-versus-per-pixel comparison becomes new-versus-model), `test-rtl-font-ram`, `test-rtl-font-soc`, `test-rtl-scanout-trace`, `sim/sdram_sched_a/run_gfx.sh` (both stacks pass), `test-rtl-wdog-soc` (a cut tick still drains, R10).
6. The mutant sweep: every mutant of section 5 killed.
7. Performance gates of section 7 (R4, C-FILL-2, C-IDLE-2).
8. Lint and the map-level resource gate (section 6).
9. The similarity audit and copy-aware blame (`TALOS_ENGINE_V2_CLEANROOM_PROCESS.md` section 5), against the old engine and Gallery's engine.
10. Review X3 (Cyclone V RTL/timing review: one writer per RAM, registered inputs to every RAM write port, no arithmetic chain into a write port in the same cycle, crossings as listed in contract 2.7).
Then one fit, two seeds (D-T21), with the timing figures of contract 7.5.

---

## 9. Pocket proof (the existing DEV pages and Report rows, and what is new)
**Existing, unchanged rows to compare with DEV 196 to 198 (B-900, B-902, B-904):** Check page all PASS; `GFX SCAN LATE` 0; `GFX Q OVF` 0; `GFX FENCE TMO` 0; `GFX HW FENCE` 2; `UNDER RING` 0 and `HW UNDER EV` not worse; `GFX FENCES`, `GFX FENCE CYC`, `GFX CMDS`, `GFX STALLS`, `GFX STALL CYC`; the per-meter `M ...` rows for the seven meters in the window and fullscreen (contract table 7.2); `SDA ENG BAD` 0, `SDA ENG REQ`/`WORDS`/`BUSY`, `SDA SCAN WAIT` not worse; the SDRAM soak (`SOAK FAILS` 0 over 30 minutes, warm); the blit check rows `BLIT OPS`/`BLIT BAD` (0 bad); the pack watchdog hang packs (a cut tick drains); screenshots of every UI view and every meter compared with the reference images (lossless, KB-103), where text in the dark theme is expected to differ only at the four coverage levels of IMP-TEXT-DARK.
**New rows (each a registered Diagnostic feature in `fw2/diag_features.json`, never a bare `#if TAU_DIAGNOSTIC`):** `GFX CAPS` (the capability field of `R_GFX_STATUS`, so a report shows which engine and set is running); `GFX ENGINE` (old or new, from a build constant, until the old engine is deleted); per-opcode command counters for the UI views (the open item of the scoping report; cheap: counted in `gfx.c` at the push).
**Gallery (at its import):** its M0 test page (viewer, mode switch, landing), the grid with rounded thumbnails at each grid size (DSBLIT, CORNERS), 2x and 4x zoom (doubler), its `make test-gallery-ci` and app mutants.

---

## 10. The hand-off package (allow-list) and the checklist
**The author receives (a fresh single-commit tree built by `git archive` of this list, contract process section 4):**
1. `docs/features/TALOS_ENGINE_V2_CONTRACT.md` and this plan (sections 1, 2, 4 to 9; section 3 names the oracle files, which the author does not get, only the class table).
2. The G1 model source and the vector files it generated; the derived weight tables and `tools/derive_text_weights.py`; the font images and the black-box `font_ram.v`/`font_rom.v` (port lists only matter).
3. Interface files: `docs/research/TALOS_SCANOUT_CONTRACT.md`; `docs/features/SDRAM_CONTROLLER_CONTRACT.md` section 4; the header comment of `tau_sdram_a_gfx_if.sv` (protocol only); `tau_cdc_gray_ctr.sv`, `tau_cdc_gray_bus.sv`; a stub `tau_gfx.sv` port shim; the stand-alone functional memory stub; `sdk/docs/HOST_TABLE.md`, `tau_meter.h`, `canvas.py`, `tau_fb.js`; `docs/MMIO_ALLOCATION.md` rows of the engine registers.
4. `docs/PROJECT_RULES.md` items 5, 6, 7, 9 and the Cyclone V guidance of the `analogue-pocket-dev` skill.
5. The bench requirements: sections 2.4, 5, 6, 7 and 8 of this plan.

**Checklist: nothing from the old engine's text is in the package** (the coordinator ticks each item before handing over; a reviewer who did not write the package re-checks):
- [ ] No file from `src/fpga/core/tau_talos.sv`, `sim/oracle/`, `src/fpga/core/mp3_*`, `legacy-harpmudd/`, `docs/archive/` engine copies, or Gallery's engine, in any form (copy, diff, excerpt).
- [ ] No old internal identifier in the contract, this plan or the model: grep the package for the old engine's state names, internal register and signal names and function names (the list is kept outside the package, in the coordinator's notes, and the grep must return nothing except port and MMIO names, which the interface fixes).
- [ ] No sentence or comment copied from the old engine or `tools/host/blit_reference.py`: run the similarity tool of the process document over the contract and the model against the old engine's comments (8-word shingles; flag any shared run of 8 words or more).
- [ ] Every vector was produced by G1, not dumped from an old-RTL run (the vector files carry the model's version and seed).
- [ ] The weight tables are the derived ones (dark) and, for light, the owner's Q2 choice, given as data with the method.
- [ ] Gallery material appears only as G1 vectors, G5 pictures and the ported G6 bench (port-level, no `dut.u_talos.*` reads).
- [ ] The scoping report, the Gallery features scoping, the consumer import plan, `docs/TALOS.md`, the two Talos reviews, the `TALOS_*` result notes, `TALOS2_REIMPLEMENTATION_PLAN.md`, the provenance measurements and `sim/tb_talos_v2_contract_probe.v` are absent.
- [ ] The tree's file list and hash are recorded in `docs/PROVENANCE.md` with the author's brief.
**Known residual exposure:** this plan and the contract were written by a session that read the old engine and Gallery's engine (contract author role); X1 reads them cold and lists every sentence that looks like an old-design description rather than behaviour.
