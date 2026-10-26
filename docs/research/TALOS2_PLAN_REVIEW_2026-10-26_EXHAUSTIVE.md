# Talos 2: exhaustive plan review (Analogue Pocket / Cyclone V / 2D-graphics lens), 2026-10-26

Supersedes the first review (`TALOS2_EXPERT_REVIEW_2026-10-26.md`) where they differ (section 5 lists the corrections). Method: the Talos 2 plan (`docs/features/TALOS2_REIMPLEMENTATION_PLAN.md`, incl. the 2026-10-26 revision), `docs/TALOS.md`, `docs/research/TALOS_REVIEW_2026-09-28.md`, the RTL as it is today (`mp3_fb.sv`, `sdram_fb.sv`, `tau_sdram_arbiter.sv`, `core_game.vh`: facts extracted by a read-only pass, file:line in section 1) and the `analogue-pocket-dev` knowledge base. Nothing was simulated or fitted. Labels: [RTL] read in the source today, [KB-nnn] knowledge-base entry (with its own evidence grade), [DOC] a project document, [HYP] my engineering judgement, to be confirmed by T0 before anyone relies on it.

## 1. Ground truth: what the hardware does today
| Fact | Where |
|---|---|
| SDRAM: 64 MB (`512Mb x16`), 4 banks, row 13 bits, column 10 bits; CAS latency 2, 100 MHz, sequential burst, mode register full-page, manual termination; SDRAM clock is the negative edge through a DDIO output; fast input register on (+1 read cycle) | `sdram_fb.sv:96,166-172,234-238,749-776`; instance `core_game.vh:799` |
| **Closed-page**: every access is ACTIVE, READ/WRITE, PRECHARGE-ALL, tRP. Rows are never kept open. Address split: bank = addr[24:23], row = addr[22:10], column = addr[9:0] (16-bit-word addresses) | `sdram_fb.sv:339-344,351-354,639,670-676,704-727` |
| Cost of one write op about `len + 10` clocks from IDLE to ready (1 IDLE + 1 DELAY + 1 WRITE + len + 3 tWR + 1 PRECHARGE + 3 tRP) | `sdram_fb.sv` [HYP arithmetic] |
| **Refresh before every read** that arrives while idle: `needs_burst_refresh = refresh_counter + 1023 + 16 >= 751` is always true, so each such read pays an AUTO REFRESH (tRFC about 9 cycles). The mid-burst refresh branch is unreachable only because of this | `sdram_fb.sv:232,458,599,729-743` |
| Timing in cycles comes from a `CEIL` macro that passes a real to an integer function: an exact integer ratio gets +1 (tRFC 9, tRC 7, tRP 2, tRCD 2, tWR 2, refresh interval 751) | `sdram_fb.sv:25,106-164` [RTL, not simulated] |
| **Streaming-write data source `wsrc_cnt` free-runs from the ACTIVE cycle**, with a comment that it "assumes the 1-cycle tRCD DELAY at this clock" | `sdram_fb.sv:585-591` |
| Arbiter: two masters (the framebuffer engine and the CPU bridge), fb wins when idle, **no preemption**; a comment forbids gating acceptance on `p0_available` (combinational deadlock) | `tau_sdram_arbiter.sv:83-130` |
| Scanline = 4,167 clk_sdram cycles; the fill is a 512-word read (about 530 cycles incl. overhead); a worst blit row about 520; so slack is about 3,100 cycles | `mp3_fb.sv:55-57,1403` [HYP arithmetic] |
| Scanout: line buffer M10K 1024 x 16 (parity x 512), fill request is a toggle plus a 3-flop shifter with **the line number sampled with no handshake**; single pending slot; **no underflow detection or counter** | `mp3_fb.sv:587-591,1036-1041,1714-1718` |
| Command FIFO 256 x 88 bits M10K, Gray pointers, **255 usable; a push when full is silently dropped**; firmware polls R_FB_GO bit 0 ("full") before each command; per command 3-4 MMIO writes (RECT/BLIT) or 2 (CHAR) plus the poll read | `mp3_fb.sv:436-495`, `mp3_soc.v:176,1532`, `fw/player.c:498,541-548` |
| Engine: one 128 x 16 row buffer `glyphbuf` (two identical MLAB copies, single write port, T2-00); `OP_COPY`/`BLIT` burst into it, **`SBLIT` and `CBLIT` do one SDRAM transaction per pixel** | `mp3_fb.sv:601-630,1099-1200` |
| Sticky blit/CLUT config registers cross clk_sys to clk_sdram **with no synchroniser or handshake** (quasi-static) | `mp3_fb.sv:188-213` |
| Framebuffer 400 of 512 words per row, rows 0-359; stash rows above 360 (art 360, TIMG planes to row 984, Chladni plane at 984); **second display buffer at word 1,048,576, which lies inside the CPU's uncached window (starts at byte 1 MiB)**; CPU bridge turns each 32-bit access into two 16-bit controller ops | `mp3_fb.sv:276`, `tau_sdram_addr_decode.sv:27-55` |
| Measured: CPU window access about 50 cycles net, worst 360-373 under scanout+drawing; 30-minute soak 264 M write/read checks, 0 failures, **one Pocket, room temperature, one core** | [KB-030, hardware-validated] |

## 2. Findings, most important first
Each: what, why it matters, the risk, the proposed change, confidence.

**R1. The plan treats two goals as one: performance and provenance. They need different engineering.**
* Why: today's row operations are already near-optimal: a 128-word copy row costs about 140 (read) + 138 (write) = 278 cycles against a floor of 256, an 8% overhead [HYP, from section 1]. The inefficient operations are only the per-pixel ones (`SBLIT`, `CBLIT`: about 20 cycles per pixel, a 10x overhead), the refresh on every short read, and the destination pre-read for keyed blits [DOC review 3.1-3.4]. All three can be fixed inside the existing structure: `OP_COPY` already bursts a source row into `glyphbuf`; `SBLIT`/`CBLIT` can do the same and resample from the buffer; keying becomes a DQM mask. **The ping-pong back end buys little**: compose and CLUT work are already pipelined and the SDRAM port is shared, so SRC and DST still alternate [HYP].
* Risk if ignored: spending a 3.5-hour-per-attempt fit cycle and a hardware-proven engine's stability on a pipeline whose measurable gain is small.
* Proposal: split the work. **Performance:** targeted in-place changes (burst-gather `SBLIT`/`CBLIT`, DQM keying, 400-word fill) on the existing engine, each with the differential test. **Provenance (D-T01):** re-implement the same architecture as new files, behaviour-identical first ("strangler"), enhancements second. Choose the row-job/ping-pong structure only if T0 measurements show an engine-bound frame.
* Confidence: high on the arithmetic, medium on the conclusion until T0.

**R2. There is no measurement baseline, and the Pocket build has no Blit Test.** The plan's hardware acceptance (Blit Test page, blit storm, Check STANDARD) is written for the old player; **Player 2 does not have Blit Test, Meter Sweep or stress** (CHANGELOG/ROADMAP: "not yet back"). Without them there is no hardware regression gate for any Talos change.
* Risk: a Talos regression ships because nothing on the Pocket draws every opcode and compares the result.
* Proposal (T0, before any RTL): (a) per-frame counters in the shipped build (commands issued, MMIO cycles spent issuing, `fb_wait` polls, FIFO-full stalls, engine busy split SDRAM-busy/idle, scanout fill late/underflow); (b) port Blit Test to Player 2 as a known-answer scene covering every opcode and the sticky-state hazards; (c) **compare against the host reference renderer pixel-exactly using a screenshot**: Pocket screenshots are lossless exact RGB565 [KB-103, hardware-validated], and `tools/host/blit_reference.py` already produces the golden image. This gives a real hardware differential test for the old and new engines.
* Confidence: high.

**R3. No scanout underflow detection exists, so the central safety claim ("scanout always wins") cannot be observed.** A single pending fill slot, no counter [RTL].
* Why it matters: every Talos change is judged against "scanout never starves"; today that is argued from arithmetic (3,100 cycles of slack) and never counted. Under a controller rewrite this is the main thing that can silently break.
* Proposal: add a sticky "line not ready at the start of display" flag plus a counter (a few flops, `tau_cdc_gray_ctr` for the crossing), visible in the Report. Ship it first, in the v0.7 bundle; it costs almost nothing and is the instrument that judges the rest.
* Also: the slack is about 3,100 cycles, so **the plan's "a fill may wait at most one burst" constraint is far stricter than the hardware needs** [HYP]. A 128-word row split for scanout's sake is not required; it is required only by the 128-entry row buffer. Do not let the scanout-wait bound drive the row width.

**R4. Informal clock-domain crossings, and the plan would add more.** The fill request's line number and the sticky config registers cross without handshake or synchroniser; they work because they are quasi-static and set at least one line ahead [RTL]. The plan's idle bit (two flops) and a multi-bit token are the classic failure [KB-009: per-bit chains give stable but wrong values].
* Risk: a latent intermittent that a one-Pocket soak will not show (the same class as the I2S pair-tearing bug, KB-123/KB-129).
* Proposal: in the new `tau_scanout` cross `fill_line_req` with the existing `tau_cdc_gray_bus` or a held-data handshake; make the sticky config part of the in-queue stream (the plan's `SET_FIELD`) so no register crosses; fence as a **monotonic Gray fence counter** (`tau_cdc_gray_ctr`), idle as a single bit. Declare every crossing in the SDC and check the SDC is not silently dropped (KB-027: a multicycle on a merged-away register is ignored).
* Confidence: high.

**R5. Silent command loss when the FIFO is full.** A push when full is dropped (`mp3_soc.v:176`); correctness depends on firmware always polling first [RTL]. Also one poll read per command is a full bus round trip.
* Proposal: a sticky overflow flag and a counter (Info/Report); expose **free slots** as a register (registered Gray-to-binary, not the combinational decode used now) so firmware polls once per N commands; this is the cheapest large CPU saving across every opcode [HYP until T0 confirms CPU issue dominates].
* Risk: low. It also catches the next firmware that forgets the poll.

**R6. The SDRAM controller is the riskiest module to replace, and it is only validated on one unit.** The DDIO negative-edge SDRAM clock, the fast input register and the capture phase are a source-synchronous interface whose setup/hold trade against the clock phase [KB-027]; the soak evidence (KB-030) is one Pocket at room temperature. It also contains a clock-dependent assumption (`wsrc_cnt` free-runs on "the 1-cycle tRCD DELAY") and a `CEIL` macro with off-by-one behaviour on exact ratios [RTL]; the protocol with the arbiter has a documented deadlock hazard [RTL]. A rewrite can change any of these without a failing test.
* Risk: a controller that passes on the development unit and fails on another unit, temperature or voltage; worst case data corruption that looks like random UI glitches or, because the CPU shares the SDRAM, crashes.
* Proposal: (a) keep it last (D-T01 order) and make the new controller a drop-in with the **same PLL phase, same pin/IOE constraints, same capture register structure**; (b) derive every timing from named parameters with `$error` checks (no macro tricks), and assert the streaming-write alignment instead of assuming it; (c) a simulation SDRAM model that checks tRCD/tRP/tRFC/tRAS/tWR and the refresh interval, run at the real client rates (KB-028: a bench that is slower than the real client hides the failure); (d) measure setup and hold slack at both corners and at two phase values (KB-027) and **run it on at least two Pocket units, including a warm one**, before shipping; (e) the A-094 latency probe and the 30-minute soak before and after.
* Do not fold the refresh-policy change into it (R8).

**R7. Memory map hazard: the second display buffer overlaps the CPU window.** `DBUF_BASE1` is at word 1,048,576 and the uncached CPU window starts at 1 MiB [RTL, I did not check how the firmware avoids writing it]. This is the class of bug already hit twice (KB-099, KB-107: a diagnostic range overlapping the back buffer).
* Risk: a stray CPU store corrupts the back buffer; invisible in host tests.
* Proposal: T0 writes down one authoritative SDRAM map (framebuffer, back buffer, stash planes, TIMG planes, probe rows, CPU window, PSRAM windows) as a generated header consumed by firmware and RTL parameters, with a test that no two ranges overlap. The new modules take their bases from it.

**R8. The refresh-policy change is global and has a hidden coupling.** The unconditional pre-read refresh is the only thing keeping the mid-burst refresh branch unreachable (the controller ends a burst without telling the client, and the engine then waits forever) [RTL, DOC review 3.1]. Changing the policy makes that branch reachable on every client.
* Proposal: make the controller signal an aborted burst to the client (a defined `burst_aborted` pulse the engine can retry) BEFORE removing the pre-read refresh; prove it with a mutation test that forces a refresh mid-burst. Ship it as its own commit and fit, never in the engine rewrite's fit. The gain is mostly for single-word CPU reads (KB-030: part of the 50-cycle cost).

**R9. Fit variance and the rename.** A Quartus fit varies about 1.2 ns by seed [KB-011] and is deterministic only for identical sources [KB-051]; a rename or file split changes hierarchy and can move results. Bundling the split/rename with new logic makes any timing regression unattributable.
* Proposal (amends D-T02): the **split and rename (T2) gets its own two-seed fit as a baseline** with the logic unchanged: expected to reproduce the ALM/M10K/DSP numbers within noise and to close timing. Only then T3/T4 on top. Cost: about 3.5 h of VM time, bought for attribution. Both still ship in v0.7.
* Also: use the standalone probe project for per-block ALM before each fit [KB-190, KB-141].

**R10. Inference traps in the new buffers.** The plan's two 128 x 17 ping-pong buffers plus blend plus mask bit plus CLUT stage repeat the exact structure that fell out of RAM before: a second read port or write site silently became 3,084 flops and about 6,500 ALMs [KB-070], and an asynchronous MLAB read feeding an unregistered DSP could not close at 10 ns [KB-065, KB-118]. MLAB is 32 x 20, simple dual-port, read address from ALMs, no mixed width [KB-073, KB-143, KB-145]; M10K cannot return new data on a mixed-port read [KB-144].
* Specific points: (a) the plan's "single registered write stage" must have the SRC-data / blend-result / mask mux BEFORE the register, or it becomes the old six-writer mux; (b) a blend DSTREAD pass reads the same buffer it will write at a different address: legal only as simple dual port with registered reads (KB-144 old-data behaviour; do not depend on read-during-write); (c) a 17th mask bit makes the word 17 wide: fine for MLAB (20), but check the map report for `altdpram`, never per-bit register names (the plan already says so: make it a failing script); (d) 256 x 16 or deeper structures in MLAB cost far more than their bits (KB-201: about 50 LABs per 1024 x 16 plus a read mux), keep deep RAMs in M10K.
* Resource reality: free ALM/M10K/DSP are unknown after the font memory (+12 M10K) and the statistics block [DOC]; the plan's "0 extra M10K" and "at most 1,800 ALMs" are against stale numbers. T0 re-measures from the `stats-b821` fit report with the standalone probe.

**R11. Clipping and glyph edge cases are under-specified.** The plan clips in the front end with `aux` carrying only the glyph row and Bresenham state [DOC]; a clip that cuts a glyph at the left edge needs a column start offset in the AA path, and clipped scaled and palette blits need source start offsets. These are exactly the cases `blit_reference.py` does not cover today [HYP].
* Proposal: extend the reference and the scenes first (red), then the RTL. Treat "partial glyph at every clip edge, every scale" and "clipped GATHER/CBLIT" as named acceptance items. Fuzz them (R13).

**R12. Font source is stale, and the glyph sources still to come must fit the design.** `OP_CHAR` reads `font_rom` or `font_ram` (13-bit address, registered 32-bit output) only in `A_ROWFETCH` [RTL]; the plan still says Inter ROM. Extended Latin/CJK (owner scope) need a source that is not the on-chip atlas.
* Proposal: define `src_kind` FONT on the writable atlas now; reserve `GLYPH_PSRAM` as an encoding and a back-end SRC slot; write down that a PSRAM glyph fetch is a client of the PSRAM bus (`tau_psram_bus.sv`), not of the SDRAM controller, so it needs its own arbitration with CPU instruction fetch and the cold code (the PSRAM serves them [DOC review section 5]). Prove the cache design with the audio-deadline measurement before building.

**R13. Verification: strong plan, three gaps.** (a) **Differential fuzzing** of random command streams (opcode, geometry, clip, sticky changes, fence points, FIFO pressure) between the old and the new engine, pixel-exact and cycle-bounded; the old engine under `TAU_TALOS1` is the oracle. (b) Mutation tests must cover each new stage and the DQM mask alignment with the streaming-data latency (the `wsrc_cnt` fragility, R6). (c) A host model of the register path must model the RTL as built, not as documented [KB-093]; the sticky-state semantics (read at execute, KB-187) and the double-buffer selector's partial coverage (KB-094) are the known traps; the new design must keep or deliberately change them, with a test for each.
* Also: two defences that overlap hide each other from mutation tests [KB-185]: when adding the in-queue control opcodes next to the old immediate registers, mutate each path alone.

**R14. The column-list primitive: the design is not yet specified; the demand is real but the cause must be confirmed.** The hardware wave/scope has been compiled out since B-302 because 256 columns cost about 21 times a normal meter [DOC]. Each column is an `OP_BAR` command: 3-4 MMIO writes plus a poll [RTL]. The engine time for 256 columns is small (two short bursts each, about 30-40 cycles, so about 10k cycles = 0.1 ms [HYP]); the cost is almost certainly **command issue**, which a column list fixes only for this one case.
* Proposal: a packed sample list: the CPU writes N samples (two 16-bit samples per 32-bit MMIO write, x implied by the index, baseline and colour sticky) to an auto-increment port into a small RAM (256 x 16 in M10K or MLAB; the plan must say which, R10), then one GO draws N connected vertical segments. 128 MMIO writes instead of about 1,000. Add the fence/overflow rules from R5. Prove the cause with T0's per-frame issue counters first; if issue cost dominates everywhere, the general fix is R15, and the column list becomes one more client of it.

**R15. The general fix for CPU issue cost is probably a command ring.** A softcore store to main RAM is one cycle; an MMIO write plus a poll is a bus transaction. A display list in RAM (PSRAM or SDRAM) fetched by the engine, with a kick register and the fence counter, turns N commands into N RAM stores plus one kick, and gives `graphics/talos` the transaction semantics the architecture plan asks for [DOC]. Risk: a new SDRAM client (the fetch) inside the framebuffer master, more arbiter load and a new place for ordering bugs; the fetch must not starve scanout (counted by R3). Proposal: a T6 item, gated on T0 showing issue cost dominates; keep the FIFO path as is.

**R16. Resolution 720 must stay free ("last, but everything resolution-agnostic").** The new scanout, controller and engine must take `H_ACT`, `V_ACT`, `STRIDE` and the fill length from parameters; stride 1024 = one SDRAM page per line means the page-split logic and the full-page read termination matter [DOC: VIDEO_720_PHASED_SPEC, review 4.3]. The existing `test/720` branch already worked around the page hazard in firmware.
* Proposal: parameterise from the first line of the new scanout; add one elaboration of the testbench at 720 geometry.

**R17. Scanout fill length.** Fill reads 512 words to show 400 (2.4% of the bus) [RTL]. Ending at `H_ACT - 1` is a small change but sits at the burst-end alignment of a full-page read (the controller ends a burst after a request with a CAS-latency-dependent tail) [RTL]: an off-by-one leaves the last pixel stale.
* Proposal: do it in the new scanout with a testbench that checks all 400 words and the first uncovered word.

**R18. The clone-first rule and clean provenance.** D-T01 says write new modules from the contract, not edit the old text. Practically that means a reviewer who has not just read the old file writes from the written contract (this document's section 1, the register map and the reference renderer) and the old file is then used only as a test oracle; record the process in `docs/PROVENANCE.md`. A rename or `git mv` preserves blame; **only new, validated code lowers the inherited footprint** [DOC discontinuation report]. Expect the origin number to move only when each old file is deleted after hardware confirmation.

**R19. Things the plan should NOT include.** Bank interleaving and address swizzle do nothing with a closed-page controller (my first review's suggestion was wrong, section 5); a second SDRAM lane; B19 flip (timing-critical write network, [DOC]); RLE (low value, [DOC]). Keep them out so the fits stay about the engine.

## 3. Risk register (summary)
| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| 1 | Controller rewrite passes on one unit, fails on another (capture phase, temperature) | medium | high (corruption, crashes) | R6: same PLL/IOE, two units incl. warm, both-corner slack, soak |
| 2 | Regression ships because Player 2 has no Blit Test | high without R2 | high | R2: port it, screenshot-vs-reference |
| 3 | RAM inference falls back to registers (6,500 ALMs) | medium | high (fit fails at 94%+) | R10: map-report grep as a gate, standalone probe first |
| 4 | Informal CDC causes an intermittent | low-medium | medium | R4: proper crossings, counters |
| 5 | Mid-burst refresh abort hangs the engine when the pre-read refresh goes | certain if done naively | high | R8: abort signal first, mutation test |
| 6 | Rename plus logic in one fit makes timing regressions unattributable | medium | medium | R9: separate baseline fit |
| 7 | Memory map overlap (DBUF1 vs CPU window) | low | high | R7: one generated map, overlap test |
| 8 | Effort goes into ping-pong, gain unmeasured | medium | medium | R1, R2: measure first |
| 9 | Command ring adds ordering bugs | medium | medium | gate on T0, fence counter, fuzz |
| 10 | Extended/CJK glyph fetch starves audio/CPU on PSRAM | medium | high | R12: deadline measurement before build |

## 4. Revised phases (changes against the plan's T0-T6 are bold)
* **T0 (no fit):** counters (issue cost, polls, stalls, busy split, **scanout underflow flag**), **one generated SDRAM map with overlap test**, **port Blit Test to Player 2 + screenshot-vs-reference check**, re-measure free ALM/M10K/DSP. Exit: a baseline table in the audit trail.
* **T1 (no fit):** stacked-bar split, firmware clipping, width/page guards, **FIFO free-slots polling once per batch (needs T3 RTL, else firmware-side shadow count)**.
* **T2:** split `mp3_fb` into `tau_gfx`/`tau_scanout`/`tau_talos` and rename, logic unchanged, differential test; **its own two-seed baseline fit** (amends D-T02).
* **T3:** in-queue control opcodes, idle bit, **Gray fence counter, FIFO overflow flag and free-slots register, underflow counter**, 9-bit bar.
* **T3b (new):** **in-place performance fixes on the existing engine**: burst-gather `SBLIT`/`CBLIT`, DQM keying, 400-word fill; each behind the differential test.
* **T4:** column-list primitive (packed samples), after T0 confirms the cause.
* **T5:** new scanout (with proper crossings), then the new engine as a behaviour-identical clone, then **the controller with the burst-abort signal first and the refresh policy separately**, the controller last and two-unit validated; old files deleted only after hardware confirmation.
* **T6:** command ring (if T0 says so), gradient/pattern `src_kind`s, `GLYPH_PSRAM` after its audio-deadline study.

## 5. Corrections to my first review and to the plan
1. **Bank placement does nothing** (first review F6a): the controller is closed-page and keeps all rows precharged between operations; overlapping banks is a controller rewrite, lowest value per effort [RTL, DOC review 3.6].
2. **"Ping-pong hides pixel work" overstated the gain** (R1): a copy row is already within 8% of the bus floor.
3. **The scanout-wait bound in the plan is far stricter than needed** (R3): slack is about 3,100 cycles per line [HYP].
4. The plan's "scanout must wait at most one burst" is a design convenience, not a hardware limit; do not shape the row width around it.
5. **Not found:** an underflow counter, a Blit Test in Player 2, any record of the controller being run on a second unit.

## 6. Owner decisions this review needs
1. **Amend D-T02:** give T2 (split + rename) its own baseline fit (R9)? Recommended yes.
2. **Approve T0 as the first deliverable and a hard gate** (counters, Blit Test port, SDRAM map, underflow flag) before any RTL beyond the counters.
3. **Approve a second Pocket unit and a warm-soak run** as the acceptance for the controller rewrite (R6). Does the owner have a second unit?
4. **Confirm the clone-first rule** for provenance (R18): a reviewer writes the new module from the contract, the old file is only the test oracle.
