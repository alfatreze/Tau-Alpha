# Talos engine v2: Tau Gallery's features as build-parameter features (scoping, 2026-11-25)

**Extends** `TALOS_ENGINE_V2_SCOPING_2026-11-25.md` after owner decision **D-T19** (`docs/DECISIONS.md`): `OP_CORNERS` (10), `OP_DSBLIT` (11), the viewer fill (`VIEW_ENABLE`) and the pixel doubler become features of the one reusable engine, each behind a build parameter, default OFF. Companion: `TALOS_ENGINE_V2_CONSUMER_IMPORT_PLAN_2026-11-25.md`.

**Evidence label: HOST-ONLY, READ-AND-ARITHMETIC.** Nothing was simulated, synthesised, fitted, run on a Pocket, installed or pushed. Sources: a read-only clone of Tau Gallery's archived bundle (`Tau-Gallery_gallery-publish_2026-11-23.bundle`, HEAD `26d9b1c`, in the session scratch directory, never inside this repository), its design notes, benches and audit entries, a diff of its engine against the Tau Alpha pin `12418aab`, and copy-aware blame (`-w -M -C -C`) run read-only. Tau V, Tau Reader and Tau Μ folders were only read. Scripts used are listed in section 9 so the numbers can be redone.

**Audience: the owner and the coordinator. This document names old internals (state names, signal names, line counts). It must NOT be given to the clean-room author** (`TALOS_ENGINE_V2_CLEANROOM_PROCESS.md`, section 8 says what the author gets instead).

## 0. Summary

| Item | Finding |
|---|---|
| What Gallery added to the engine file | `tau_talos.sv`: 188 added lines (160 code) over the pin, in Gallery commits `99f884e` (viewer fill), `16dd28a` (CORNERS), `739bc15` (DSBLIT), `0452b09` (doubler); also `tau_gfx.sv` +103, `tau_scanout.sv` +15, `tau_soc.v` +125 (registers), and the new files `tau_scanout_v.sv` (235), `tau_vidmode.sv` (63), `tau_pixfx.sv` (144, not part of D-T19). |
| Feature contracts | Written below from Gallery's design notes, benches and audit. CORNERS and DSBLIT are small and well pinned. The viewer fill and doubler are not engine features alone: they are engine + scan-out + mode sequencer + SoC registers + a 40 MHz video clock + `video.json`. The engine owns only the "fill the viewer's line buffer" half. |
| Gallery's test coverage of the pins | Good for DSBLIT and CORNERS pixels (synthetic vectors, 7 + 7 cases, 7 mutants), good for the viewer and doubler (whole-frame pixel compare, 16 scan-out mutants, 6 viewer-fill mutants). **Gaps:** no Python or C reference model for DSBLIT (only the Verilog bench's inline model), no committed pixel golden from a real picture, the sticky blend/key interaction of DSBLIT is unspecified (section 1.2). |
| Parameters | `FEAT_CORNERS`, `FEAT_DSBLIT`, `FEAT_ESC`, `VIEW_ENABLE` (+ `VIEW_DBL_EN`, `VIEW_LINE_WORDS`), all default 0; capability bits read from one word; `$error` on illegal combinations. Named sets: TAU (all off), GALLERY (CORNERS + DSBLIT + VIEW + VIEW_DBL), V (all off at first), READER (proposal only). |
| Fit cost | **Per-feature ALM cost is NOT measured anywhere** (Gallery fitted whole cores only; its draw engine has no probe). Measured: one before/after pair on the same base (the viewer bundle, +13 M10K and +1 DSP). Everything else below is an estimate from the diff, with ranges and the probe that replaces it (a `quartus_map` run per configuration). Whole Gallery set estimate: +0.9 to 1.7 k ALMs, +11 to 13 M10K, +1 DSP, none of it in Tau Alpha's own fit (all off). |
| **Collision found** | Gallery's viewer registers `0x1EC` to `0x1F8` collide with Tau Alpha's save-RAM registers `SAVE_ST/IDX/DATA` at `0x1EC`, `0x1F0`, `0x1F4` (added after Gallery's pin). Gallery's `CORE_VERSION` revisions 28 to 31 (`4D50331C` to `4D50331F`) also reuse the number space Tau Alpha counts in. A register remap and a capability-bit handshake are required (section 2.5). |
| Scan-out lineage | `tau_scanout_v.sv`, `tau_vidmode.sv` and Gallery's additions to the engine and to `tau_scanout.sv` are Tau-authored: 0 code lines equal a HarpMudd-origin line (one trivial `always @(posedge ...) begin` coincidence in the engine additions and in `tau_pixfx.sv`). 34 lines of `tau_scanout_v.sv` descend from the clean-room `tau_scanout.sv` (D-T07). So the viewer scan-out needs **no provenance rewrite**; the rewrite need is only in the engine file, where the additions are interleaved with HarpMudd-origin structure (section 5). |
| Order | Opcodes first (CORNERS, DSBLIT, ESC), scan-out features second (VIEW, DBL), as D-T19. |

## 1. Behavioural contract outline per feature

Each feature is stated as observable behaviour. "PINNED" means Gallery 1.0.0's published output must not change. The conformance input column names Gallery files that become vectors or checks (never engine text to the author, see section 7).

### 1.1 `OP_CORNERS` (opcode 10) — PINNED (pixels)

Purpose: round a picture that is already drawn by painting only the four corner cuts of a rounded rectangle in one colour; the interior is not touched.

* **Command:** opcode 10 = R_FB_GO write `0x4002` (op[3] at bit 14, op[2:0] = 2) with `R_FB_ADDR` = `y*512 + x` of the rectangle's top-left, `R_FB_SIZE` = `{h, w}` (9 bits each), radius `r` in the glyph field (bits 9:3; only `r[3:0]` is used, 0 to 15; the firmware clamps), colour = the **background** field of `R_FB_COLOR` (the foreground field is ignored; Gallery's `gfx_corners` writes the colour to both).
* **Cut table:** the same 16-entry table as `OP_RRECT` (`R_RC_IDX`/`R_RC_DATA`; resets to zero; the cut of corner row `k` is `table[r - k]`). The table is read when the command executes, so a firmware that changes it must fence first (Gallery's `rc_lut_ensure` does). With an all-zero table nothing is painted.
* **Effect:** for each corner row `k = 0 .. r-1` whose cut `c` is greater than 0: paint `c` words at the left and `c` words at the right end of row `k` of the rectangle, and the same at row `h-1-k` (bottom), all in the colour. No other word is written. `w = 0`, `h = 0` or `r = 0` retires with no memory access.
* **Addressing:** the drawing-buffer offset of the double-buffered frame applies exactly as for RECT and RRECT (the engine reuses the rectangle address register for corner segments).
* **Ordering:** a command queued after CORNERS starts after the last corner segment (Gallery's bench checks a RECT queued right behind it).
* **Dependency:** needs the RRECT cut table and sequencer (kept: D-T18). A build with CORNERS and without RRECT is illegal.
* **Not contractual:** the segment write order and the number of memory requests (Gallery's bench counts `rows_written`, a white-box check; re-express as final memory contents). Overlapping segments (small `h` or `w`) write the same colour, so order never shows.
* **Conformance input:** `sim/tb_mp3_fb.v` lines 541 to 580 (corner geometry for a first case and for `r` = 3, `r = 0` and `w = 0`, ordering behind a RECT), mutants `BUG_CORNERS_FILL`, `BUG_CORNERS_BG`, and the base `BUG_IGNORE_RC_CUT`; `sim/p2_engine_model.h` (C model of ops 8 and 10 used by `sim/test_p2_gfx.py`), `sim/test_rc_lut.py` (the cut table); firmware use in `fw2/gfx/gfx.c` (`gfx_corners`) and the thumbnail rounding in `fw2/gallery/gallery_app.c`.
* **Evidence:** SIMULATED (benches), part of the fitted Gallery core (m1f seed 2 closed). Pocket: the owner has run builds that round thumbnails (DEV 025 onward, B-1127); no dedicated pixel measurement is recorded.

### 1.2 `OP_DSBLIT` (opcode 11) — PINNED (pixels)

Purpose: scaled blit **down**, each output pixel the rounded average of a 2 x 2 source box.

* **Command:** opcode 11 = `0x4003 | (rx << 10) | (ry << 12)` where `rx` and `ry` are the 2-bit ratio fields (the `sx` and `sy` fields): 0 = 3:4, 1 = 2:3, 2 = 1:2, 3 = 1:3 (output : source) per axis. Source width and height in the size fields; source origin = `SRC_BASE + {fg[2:0], bg}` exactly as `OP_SBLIT`; destination = `DST_BASE + addr`; source row pitch = the sticky `SRC_STRIDE` (default 512).
* **Output size per axis** (`s` = source extent): `floor(3s/4)`, `floor(2s/3)`, `floor(s/2)`, `floor(s/3)`, each limited to 127. Zero output in either axis retires with no memory access.
* **Pixels:** output pixel `(i, j)` takes the source position `X = floor(i * den / num)`, `Y = floor(j * den / num)` (the axis' own ratio) and is, per channel of the 5-6-5 word, `(a + b + c + d + 2) >> 2` of the four words at `(X, Y)`, `(X+1, Y)`, `(X, Y+1)`, `(X+1, Y+1)`. Red and blue are summed from 5 bits, green from 6. The neighbours always exist because the ratio is at least 4:3, so there is no edge clamp.
* **Rounding rule:** half up on the four-word sum (`+2` before `>> 2`). Gallery's mutants cover truncation, a second row read from the wrong neighbour, a step capped at 1, and 5-bit green.
* **Limits:** output width and height at most 127 (the row buffer); larger sources are out of domain (a build may lift this later behind a capability bit: improvement, not pinned).
* **Not contractual:** the four single-word reads per pixel and their order (Gallery chose single-word transactions so the scan-out fill can pre-empt between any two; a new engine may burst-gather if the pixels are identical and the scan-out wait bound holds).
* **Unspecified, to be fixed by the contract author from the old engine (cheap, a 3-line bench):** whether the sticky blend enable and colour key apply to the destination write of DSBLIT, as they might for BLIT. Recommended contract: DSBLIT ignores both (plain write); Gallery's firmware never sets blend or key while drawing a grid.
* **Conformance input:** `sim/tb_mp3_fb.v` lines 202 to 240 (the model: a word at address `a` reads back `a + 1`; the averaging and position formula) and 420 to 427 (seven cases: each ratio on both axes, two mixed ratios, 96 wide at 2:3 by 3:4), mutants `BUG_DSB` 3 to 7 (the Makefile's `test-rtl-fb-mutation`); firmware `fw2/gfx/gfx.c` (`gfx_dsblit`) and the grid sizes in `fw2/gallery/gallery_app.c`, `sim/test_gallery_app.py`.
* **Gaps to close before a contract is final:** there is no Python or C reference for DSBLIT (`sim/p2_engine_model.h` models ops 8 and 10 but not 11), and the B-1112 note says the golden frame was not rerun. The new model must be written from the contract, then cross-checked against Gallery's bench on a real picture (a thumbnail set) to give a pixel golden.
* **Evidence:** SIMULATED (benches, 5 mutants killed); fitted as part of m1f (seed 2 closed, seed 1 failed routing and was stopped); Pocket: owner-observed grid sizes in DEV 025 onward (B-1127, B-1138); no isolated measurement.

### 1.3 Viewer fill (`VIEW_ENABLE`) — PINNED (whole-frame pixels and timing)

What the engine owns: when the viewer is the mode on the video bus, serve the viewer scan-out's line requests from a packed plane in SDRAM instead of the 400 x 360 UI rows.

* **Plane format (frozen, `M0_DESIGN.md` section 3):** a line is 800 pixels of 3 bytes in memory order R, G, B, no padding, 1,200 sixteen-bit words, word `i = {byte(2i+1), byte(2i)}`. Base = 25-bit word address of plane line 0 (reaches all 64 MB); stride = 16-bit words per plane line (reset 1,200).
* **Fill:** on the scan-out's request for line `n` (0 to 719) the engine reads `1,200` words from `base + n * stride` as **three requests of 400 words** at consecutive addresses and writes them to the viewer line buffer at `{parity(n), word index 0..1199}` (12-bit address), then reports `n` as the finished line after the third chunk. Plane address = `base + n * stride` (a registered two-stage multiply; the start waits three clocks for it).
* **Latching:** mode, base, stride and the doubler bit are latched at the vertical-blank edge, never mid-frame.
* **Arbitration against the normal queue:** the scan-out fill has priority at every unit boundary of the engine (the same rule as the UI fill); engine units are not pre-empted; the three chunks of a line are consecutive (no queued command runs between chunks, because the fill stays pending across them); a request whose product is not yet valid waits. In viewer mode the engine keeps executing queued commands in the time left. Budget: a viewer line is 900 clocks at 40 MHz = 2,250 SDRAM cycles; 1,200 words plus overhead is more than half of them, so the engine gets under half the bandwidth in viewer mode (arithmetic, not measured).
* **Known limits (Gallery's own note):** a mode change while a fill is in flight glitches that one line; the UI scan-out keeps running while the viewer is shown.
* **System parts outside the engine** (they are why this feature is not an engine parameter alone): the viewer scan-out `tau_scanout_v` (800 x 720, 900 x 740 total at 40 MHz = 60.06 Hz, active window `hc` 16..815 and `vc` 8..727, sync `hc` 840..879 and `vc` 734..737), the mode sequencer `tau_vidmode` (request noticed at a frame wrap, pending frame carries the APF scaler-slot word on every line, other scan-out takes over from its first clock), the UI scan-out re-timed to 1,666 x 400 (`H_TOT`, `MODE_EN`), a 40 MHz video PLL output (`TAU_VID40`), the video mux in `tau_gfx`, the registers, `video.json` with a second scaler slot.
* **Registers (Gallery 1.0.0):** `VID_MODE 0x1EC`, `VIEW_BASE 0x1F0`, `VIEW_STRIDE 0x1F4`, `VIEW_CAPS 0x1F8` (reads `0x8000000F`), the doubler at `R_PIX_CTRL 0x250` bit 3. See section 2.5 for the collision.
* **Conformance input:** `sim/tb_tau_gfx_view.v` (real `tau_gfx`, base 4,096 and stride 1,208 so the stride is used, UI first then the switch at a vblank, one frame of 576,000 pixels compared, frame = 666,000 clocks, no late line; slow-memory case counts late lines), `sim/tb_tau_scanout_v.v` (mutants `BUG` 1 to 16), `sim/tb_tau_vidmode.v` (4 mutants), `sim/tb_view_soc.v` with `sim/fw_view`, Talos mutants `BUG_VIEW` 1 to 6.
* **Evidence:** M0 viewer, mode switch and interrupts HARDWARE-CONFIRMED on a Pocket in handheld mode (DEV 002 to 004, measured, `M0_DESIGN.md` 6d and 6e: 40 of 40 switches, worst 33 ms; geometry and channel order right; plane exact in SDRAM). The Dock is untried. Whole-core fit m1f closed in all corners (seed 2).

### 1.4 Pixel doubler (`VIEW_DBL_EN`) — PINNED (pixels)

* **Where it lives:** in **both** the scan-out and the engine. The scan-out (`tau_scanout_v`) shows each plane pixel on two consecutive pixel clocks and each plane line on two consecutive screen lines: a 400 x 360 plane appears as 800 x 720. The engine's part is one line number: with the bit latched, the plane line fetched for screen line `n` is `floor(n/2)`. (Gallery fetches the full 1,200 words per screen line although a doubled line uses only 600; see improvements below.) The SoC part is one register bit (`R_PIX_CTRL` bit 3, which sits in the pixel-effects register for history, not for a reason), and `VIEW_CAPS` bit 3 reports it.
* **Latch point:** the scan-out takes the bit (through a three-flop synchroniser) at the first clock of a frame, so a frame is wholly one mode; the engine takes it at the vblank edge with base and stride. Both are quasi-static. A write that lands between the two latch points can show one wrong frame. That window is a Gallery limitation, not pinned; a new design should latch one registered value at one point.
* **Interaction with UI mode (400 x 360):** none. The bit is ignored while the UI is on the bus; it applies to the viewer mode only. The UI mode itself runs at 1,666 x 400 clocks of the 40 MHz clock in Gallery's build (so the 12 MHz Tau Alpha timing contract is one profile, Gallery's the second, section 5).
* **Interaction with 800 x 720:** the doubler is how Gallery makes 2x and 4x zoom views from a quarter-size plane (B-1139); the plane is 400 x 360 pixels of its 1,200-word lines, so a doubled frame has nearest-neighbour pixels exactly `plane(x/2, y/2)`.
* **Conformance input:** `DBL=1` runs of `sim/tb_tau_scanout_v.v` and `sim/tb_tau_gfx_view.v` (bit-exact against `pix(x/2, y/2)`), mutants `BUG` 12 to 16 (never doubles, odd and even fetch clocks, pick index not halved, no capture delay) and `BUG_VIEW` 6.
* **Evidence:** SIMULATED bit-exact; Gallery 1.0.0 ships it (bitstream m1g seed 1, B-1145); owner reported 2x and 4x panning "fairly smooth" on a Pocket from DEV 033 (B-1140). No dedicated Pocket pixel measurement. The m1g fit report is not in the archive's git (reports are git-ignored); B-1145 says it closed in all corners.

### 1.5 Improvements allowed behind capability bits (not pinned)

Same pixels with less work: fetch only 600 words per doubled line, and fetch each plane line once for two screen lines; burst-gather DSBLIT reads; single latch point for the doubler; lifting the 127-word output limit of DSBLIT. None is required; each needs a capability bit or an ABI revision (owner's standing "no regressions, improvements welcome").

## 2. Parameterisation design

### 2.1 Names and defaults

| Parameter | Default | Meaning | Rules |
|---|---|---|---|
| `FEAT_CORNERS` | 0 | opcode 10 | requires RRECT (always present) |
| `FEAT_DSBLIT` | 0 | opcode 11 | none |
| `FEAT_ESC` | 0 | opcode 15 and the sub-opcode field (section 3) | none |
| `VIEW_ENABLE` | 0 | viewer fill in the engine, `tau_scanout_v`, `tau_vidmode`, the video mux, the viewer registers | needs the 40 MHz video clock (`TAU_VID40`); `$error` at elaboration if missing, as Gallery does |
| `VIEW_DBL_EN` | 0 | doubler (engine line select, scan-out repeat, register bit) | requires `VIEW_ENABLE` |
| `VIEW_LINE_WORDS` | 1200 | words per viewer plane line; a multiple of the 400-word chunk | 1200 = Gallery RGB888; 800 would serve an RGB565 plane (Reader's draft needs that, see the import plan) |

Names keep Gallery's `VIEW_ENABLE` so its wrappers still elaborate. Opcode features get a `FEAT_` prefix because there is no existing name to keep. All parameters are constants in the engine; no runtime enable is added (a runtime gate would cost logic in cores that never use the feature).

### 2.2 Capability bits

One read-only word reports what was built: proposed bits of `R_GFX_STATUS [30:17]` (the scoping report's route; owner approval pending, section 8): 17 CORNERS, 18 DSBLIT, 19 ESC, 20 VIEW, 21 VIEW_DBL. They replace `CORE_VERSION` probing for these features (the firmware asks the hardware what exists). Gallery's own `VIEW_CAPS` (viewer block) stays, as the viewer's detailed capability word (`0x8000000F`: present, sequencer, 800 x 720, effects, doubling).

### 2.3 What "no footprint when off" means, and how it is proved

* **Logic:** every opcode arm, state, register and adder of a feature is inside a `generate if (FEAT_x)`; with 0 the opcode retires as a no-op (the v2 rule for reserved opcodes); ports of the module that exist only for a feature (`view_*`, `vlb_*`) are tied or left open and removed by synthesis.
* **Registers and status bits:** with a feature off, its registers are not in the read mux (the address reads what the base map returned, 0 or free), writes are ignored, its capability bit reads 0, and the queue word keeps its 88-bit width (ESC adds bits only when on).
* **Proof, cheap:** (1) the existing oracle differential and all base benches pass with every feature off, **cycle for cycle** (as Gallery did with `VIEW_ENABLE = 0`: "the engine is cycle for cycle what it was"); (2) a `quartus_map` run of the all-off configuration reports the same ALUT, register, M10K and DSP counts as the engine without any feature code (section 4, probe level 1). Item 2 is what catches a feature that leaks into a core that did not ask.

### 2.4 Combinations, supported sets and test matrix

Legal combinations: CORNERS (2) x DSBLIT (2) x ESC (2) x viewer (off, on, on with doubler: 3) = 24. **Only named sets are supported** (fitted and Pocket-tested):

| Set | CORNERS | DSBLIT | ESC | VIEW | VIEW_DBL | For |
|---|---|---|---|---|---|---|
| TAU | 0 | 0 | 0 | 0 | 0 | Tau Alpha Player 2 (budget 84 % ALMs, nothing may be added) |
| GALLERY | 1 | 1 | 0 | 1 | 1 | Tau Gallery 1.0 (its pins) |
| V | 0 | 0 | 0 | 0 | 0 | Tau V first import (UI path through Helios); CORNERS may be added if its UI wants rounded boxes |
| READER | proposal only | proposal only | 0 | 1, `VIEW_LINE_WORDS` 800 (RGB565) | 0 | Tau Reader's 800-wide reading mode is its own scan-out today; whether it adopts this feature is open (import plan) |
| MU | 0 | 0 | 0 | 0 | 0 | Tau Μ stays on its own old engine unless the owner decides otherwise |

**Test matrix:** simulation of 8 configurations: all off (the regression set), CORNERS alone, DSBLIT alone, ESC alone (no-op sub-opcode and retire), VIEW alone, VIEW with DBL, GALLERY (all on, the pins), and the legal combination CORNERS + DSBLIT for the shared sequencer registers (the two features share the rectangle and scale state in the old engine; a clean engine should not, but the test proves it). Each runs the base benches plus its feature's conformance vectors plus a mutant sweep. Fits: **two** (TAU, which must reproduce the shipped fit family, and GALLERY). The other 16 combinations are unsupported and say so in the build's `$error` or `$warning`.

### 2.5 Register and version collisions (found while reading Tau Alpha main)

* **MMIO:** Tau Alpha main uses `0x1EC SAVE_ST`, `0x1F0 SAVE_IDX`, `0x1F4 SAVE_DATA` (save RAM, added after the pin). Gallery's viewer block is `0x1EC` to `0x1F8`. Only `0x1F8` to `0x1FC` is free in Tau Alpha's 9-bit page. **Proposal:** move the viewer block (`VID_MODE`, `VIEW_BASE`, `VIEW_STRIDE`, `VIEW_CAPS`, the doubler control) to the 1 KB page's new space (`0x200` upward, which Gallery already widened the decode for: offsets below `0x200` keep their meaning exactly), in a block reserved for the viewer, and carry the doubler bit in the viewer block, not in `R_PIX_CTRL`. Gallery rebuilds its firmware from the header at the tag; its published 1.0.0 bitstream and ROM pair is untouched. A page-widening change is itself a SoC change in Tau Alpha (small, behind the same parameter).
* **`CORE_VERSION`:** Gallery used `4D50331C` to `4D50331F` (revisions 28 to 31); Tau Alpha main is at `4D50331B` and counts upward. The two will assign the same numbers to different cores. Proposal: the engine's capability bits are the feature handshake; each product keeps its own `CORE_VERSION` namespace (a product id in the high bits is the owner's call).
* **Queue word and `R_FB_GO`:** unchanged unless ESC is on (section 3).

## 3. Opcode map and the escape opcode

| Opcode | Today | After D-T19 |
|---|---|---|
| 0 RUN, 1 RECT, 2 CHAR, 3 COPY, 4 BLIT, 5 BAR, 6 SBLIT, 7 CBLIT, 8 RRECT | engine | unchanged, kept (D-T18) |
| 9 FENCE | engine | unchanged |
| 10 | free (Gallery: CORNERS) | **CORNERS**, `FEAT_CORNERS` |
| 11 | free (Gallery: DSBLIT) | **DSBLIT**, `FEAT_DSBLIT` |
| 12, 13, 14 | free | reserved, retire as no-op (still count for idle); the old engine draws a row, a change the v2 contract already makes |
| 15 | free | **escape**, `FEAT_ESC`: sub-opcode field selects the real command; with ESC off it retires as a no-op |

* **Field:** the opcode stays 4 bits (`R_FB_GO` bit 14 is `op[3]`, bits 2:0 are `op[2:0]`; escape = `0x4007`). The sub-opcode is a new 6-bit field in `R_FB_GO` bits `[22:17]` (bits 31:17 of the write are unused today), stored in 6 new bits of the queue word: **88 to 94 bits, still 3 M10K** (256 entries x 120 bits available in three blocks). 0 = NOP (retires), 1 to 63 allocated through the contract; an unknown sub-opcode retires as a no-op and counts for idle. The existing operand fields (address, size, colours, glyph, `sx`, `sy`) keep their meanings as operands; a sub-opcode needing more arguments uses sticky registers added with that sub-opcode, not before.
* **Why a field and not the glyph bits:** reusing `{glyph_hi, glyph}` would steal operand bits from the new command; a separate field keeps all nine operand fields.
* **MMIO effect:** none for ESC itself (no register); sub-opcodes bring their own, each behind its own parameter. Capability bit 19 reports ESC; a second bitmap of implemented sub-opcodes is added when the first sub-opcode exists.
* **With ESC off:** the GO bits above 16 are ignored, the queue word stays 88 bits, opcode 15 retires as a no-op. A firmware that never sets bits 22:17 is unaffected in every build.
* Only 12 to 14 stay available as direct opcodes; after ESC exists, new commands go through it.

## 4. Fit cost

### 4.1 What is measured, what is not

| Datum | Value | Label | Source |
|---|---|---|---|
| Tau Alpha shipped bitstream `wdog-w3` | 15,507 ALMs (84 %), 274 RAM blocks, 23 DSP, two seeds closed; LABs 93 % at T0 (the real ceiling is below the ALM count); worst setup +0.655 ns on the SDRAM read capture path | MEASURED | scoping report section 4 |
| Tau Alpha `stats-b821` (base of Gallery's first fit) | 272 RAM blocks, 23 DSP | MEASURED | Tau Alpha B-821 |
| Gallery M0 first fit (`stats-b821` bundle verbatim + `TAU_VID40 TAU_VIEW TAU_LAND TAU_IRQ`) | 285 RAM blocks, 24 DSP, worst setup +0.631 ns, hold +0.075 ns, both seeds | MEASURED | Gallery B-1005 |
| **Delta of the viewer bundle (viewer fill + scan-out + sequencer + landing path + interrupts)** | **+13 M10K, +1 DSP** (the viewer line buffer was written for 8 M10K; the landing FIFO is estimated at 2). ALMs not recorded. | MEASURED (whole bundle, not the viewer alone) | B-1005 against B-821 |
| Gallery m1d (decoder, no PSRAM) | 89 % ALMs, 79 % M10K, 71 % DSP | MEASURED | B-1112 note |
| Gallery m1f (Gallery 1.0 RTL batch: CORNERS, DSBLIT, pixel effects, histogram, 4:4:4, scatter writer, PSRAM restored) | 16,725 ALMs (91 %), 289 RAM blocks (94 %), 53 DSP (80 %); seed 2 closed: slow setup +0.669, hold +0.082; seed 1 died in the routing retry; run 4 h 55 min | MEASURED (whole core; the features cannot be separated) | B-1123 |
| Gallery m1g (adds the doubler; shipped 1.0.0) | numbers not in the archive git | NOT AVAILABLE | B-1145 says closed |
| `tau_pixfx` probe | 3 M10K, 0 DSP, +10.3 ns, 133 MHz at 40 MHz need | MEASURED (not in D-T19) | B-1112 |
| Draw engine alone | **no probe exists**; "its timing is only visible in the whole-core fit" | NOT MEASURED | `GALLERY_RTL_1_0_SPEC.md` |
| Per-feature ALM, either project | none | NOT MEASURED | |

Gallery's own estimate for the batch was "the downscale a few hundred ALMs and no DSP", flagged by it as an estimate.

### 4.2 Per-feature estimates (from the diff; ranges, not measurements)

| Feature | ALMs | M10K | DSP | Basis | Timing risk |
|---|---|---|---|---|---|
| CORNERS | +20 to 60 | 0 | 0 | one opcode arm; 10 register loads; reuses the rounded-rectangle sequencer and cut table; two registered values computed in the dispatch cycle | low to medium: the dispatch cycle (`f_rrect_cut`, `f_rrect_seg_addr` from the queue output) is the shape that failed five times earlier; closed in m1f |
| DSBLIT | +250 to 450 | 0 | 0 expected (the `* 171` and `* 3` are constants; a probe confirms) | tap accumulators (22 flops), step and subtract logic for two axes, two registered output sizes, a three-term read address (`row + col + neighbour + stride`), row stride adder | **medium to high**: two arithmetic stages sit directly behind the SDRAM read capture (`p0_q` into the tap sums) and one extra term in the read address, on the path that already has the worst slack (+0.655 ns). A clean engine registers the capture before summing. |
| ESC | +10 to 30 | 0 (94 of 120 bits) | 0 | 6 more queue bits, one decode | low |
| VIEW (engine half) | +150 to 300 | 0 | 1 (`line * stride`, 10 x 16) | latched base, stride, line registers, chunk counter, 25-bit address adders, 12-bit line-buffer write port | medium |
| VIEW (scan-out half: `tau_scanout_v`, `tau_vidmode`, video mux, SoC registers) | +400 to 800 | about 8 to 11 | 0 | 96-bit group register, 4:1 pixel pick, two line banks, sequencer, 24-bit video mux | medium: a second 40 MHz domain; Tau V needed a second seed for the same clock |
| DBL | +10 to 40 (engine 1 flop and a mux; scan-out three flops, window and pick muxes) | 0 | 0 | | low |
| **GALLERY set total** | **+0.9 to 1.7 k** (5 to 9 % of the device) | **+11 to 13** | **+1** | | |
| TAU set | **0** (by construction; proved by the map-only equality in 2.3) | 0 | 0 | | none |

**Verdict:** the TAU set pays nothing, which is the point of the parameters. The GALLERY set is what costs, and Gallery has very little room: m1f is at 91 % ALMs, 94 % M10K, 80 % DSP, with hold slack +0.082 ns and a seed that failed to route. A rewritten engine that is larger than today's in the GALLERY set can fail Gallery's fit. The scoping report's "B-lean 10 to 20 % below today" is still an estimate; the clean engine must be measured against the old one in the GALLERY set before Gallery imports it.

### 4.3 Cheapest probes (none run now)

1. **Level 1, map only:** `quartus_map` (synthesis, no fit) of `tau_gfx` as top for the 8 configurations of 2.4, on the VM; minutes each; read the Analysis and Synthesis resource summary (ALUTs, registers, M10K bits, DSP). The difference to all-off is the feature cost to within synthesis accuracy (mapped ALUTs differ from fitted ALMs by packing, ranking is reliable). Also equality of all-off against the pre-feature engine. This answers every ALM, M10K and DSP row above except fit packing.
2. **Level 2, probe fit:** the engine plus a stub SDRAM master as a stand-alone design at 100 MHz (Gallery's `tools/block_probe.py` pattern), one seed, for DSBLIT and VIEW; about 15 to 30 minutes each; gives the worst path of the feature in isolation (optimistic).
3. **Level 3, the real thing:** the two named sets, both seeds (about 1 h 40 for the TAU family; Gallery's m1f took 4 h 55).

## 5. The scan-out question and lineage

### 5.1 Method

Tau Alpha's own `tau_talos.sv`/`tau_scanout.sv` at the pin `12418aab` were blamed copy-aware (`-w -M -C -C`) and a line counts as HarpMudd-origin when its origin commit is in the base history, the upstream history or the HarpMudd port commit (`measure.py` rule): 899 non-trivial lines (at least 25 characters after removing whitespace and comments) across `src/fpga/core`. In the Gallery clone the baseline import commit hides history (the files came by `git archive`), so every Gallery-added line (origin not the import commit) was compared with that set by normalised text; lines copied from another baseline file were tracked with `-C -C`. Textual origin only; not creative ownership; not a legal conclusion.

### 5.2 Result

| File | Lines | Origin | HarpMudd-origin text in Gallery-added lines |
|---|---|---|---|
| `tau_scanout_v.sv` (new 2026-11-07, 5 commits) | 235 | 201 Gallery-written (M0.1, doubler); **34 lines copy-matched to the clean-room `tau_scanout.sv`** | **0** (and the 34 matched lines are not HarpMudd text) |
| `tau_vidmode.sv` | 63 | all Gallery-written (design from Tau Alpha's unmerged `test/720`, B3) | 0 |
| `tau_pixfx.sv` | 144 | all Gallery-written (not D-T19) | 1 trivial (`always @(posedge clk_sys) begin`) |
| `tau_scanout.sv` (Gallery's copy) | 190 | 175 baseline (the clean-room file), 15 added (`H_TOT`, `MODE_EN`, `hold`, `slot_word`, `frame_end`) | 0 |
| `tau_gfx.sv` | 420 | 317 baseline, 103 added (viewer ports, video mux, mode wiring) | 0 |
| `tau_talos.sv` | 2,057 | 1,869 baseline (the Tau Alpha engine, 37 % HarpMudd-origin by the assessment), **188 added** (160 code): viewer fill 61, DSBLIT 104, CORNERS 18, doubler 5 by origin commit | 1 trivial (`always @(posedge clk_sdram) begin`) |
| `tau_soc.v` registers | +125 | Gallery-written | not run |

* **Prior art inside Tau Alpha:** the branch `test/720` (T1, commit message cites B-374, 2026-09-29) added a `VID720` doubling mode to `mp3_fb.sv` and a 37.5 MHz video clock; Gallery's timing positions come from Tau V's `tau_tvc_video` 800 x 720 build. Line overlap between `test/720`'s T1 additions (14 non-trivial lines) and Gallery's scan-out and engine files: **0** (one `parameter DBUF_ENABLE = 0,` line). Ideas, not text.
* **Where the additions sit:** every Gallery addition to `tau_talos.sv` is Tau-authored, but each is woven into HarpMudd-origin structure: DSBLIT reuses the scaled-blit state and counters, CORNERS reuses the rounded-rectangle registers, the viewer fill edits the idle arm and the fill states. The text cannot be moved into a clean-room engine without importing the old state names. So the engine side is contract plus vectors (D-T19, section 7), and the scan-out side can be imported as is.
* **Licence note:** the Gallery repository is Apache-2.0 with MIT preserved for the inherited files (`NOTICE.md`, B-1128). Its added code is the owner's own; moving it into this MIT project needs the owner's statement and a `NOTICE` line, not a rewrite.

### 5.3 Recommendation: a separate viewer contract, second step

* **Step 1 (opcode features):** contract and vectors for CORNERS, DSBLIT, ESC, written now (section 7), engine rewritten with the base set.
* **Step 2 (scan-out features):** a **separate** `TALOS_SCANOUT_VIEWER_CONTRACT.md`, behavioural, three profiles that keep the existing 12 MHz contract (`TALOS_SCANOUT_CONTRACT.md`) untouched: (a) the UI timing at the 40 MHz clock (1,666 x 400, same 400 x 360 content, plus `hold`, `slot_word`, `frame_end`); (b) the 800 x 720 viewer scan-out with the doubler and the slot word; (c) the mode sequencer. The oracle for its differential bench is Gallery's `tau_scanout_v` and `tau_vidmode` (the T5 precedent: old module as black box, mutants, three frames).
* **Provenance does not force a rewrite** of (a) to (c): they have no HarpMudd text. Two options for the owner: **import by hash** (about a session, Gallery's benches come with them), or **rewrite from the contract** (2 to 3 sessions, buys a second implementation and a contract that Tau Reader can build its own RGB565 scan-out against). Recommended: import by hash plus write the contract (the contract is what Reader and Tau V need; the rewrite is optional).
* **The engine's half of the viewer** (fill of the line buffer, doubler line select) is the only part that sits in HarpMudd-origin text, and is re-implemented from the contract in the engine rewrite, as `VIEW_ENABLE`.

## 6. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| 1 | MMIO and `CORE_VERSION` collision with Tau Alpha's save RAM and revision counter (2.5) | remap the viewer block above `0x200`; capability bits; product namespace in `CORE_VERSION` |
| 2 | Gallery has no fit headroom (91 / 94 / 80 %, hold +0.082, a seed failed routing; m1f 4 h 55) | measure the new engine in the GALLERY set (probe level 1, then 2) before Gallery imports it; the rewrite must not be larger than today's engine plus its features |
| 3 | DSBLIT puts arithmetic behind the SDRAM capture path (worst slack +0.655) | contract allows any read pattern; the clean engine registers the capture first; probe level 2 |
| 4 | Pins have gaps: no DSBLIT reference model, no committed real-picture golden, golden frame "not rerun" (B-1112) | write the Python model and a thumbnail golden before the engine rewrite starts; cross-check against Gallery's bench |
| 5 | DSBLIT interplay with sticky blend and colour key is unspecified | read once, 3-line bench, state it in the contract |
| 6 | The viewer is not an engine-only feature: PLL (`TAU_VID40`), `core_game.vh`, SoC registers, `video.json` all change; Tau Alpha main has none of it | scan-out profile step 2; engine parameter alone is documented as insufficient; PLL change is a SoC-level import owned by the importing project |
| 7 | Hard-coded 1,200-word, three-chunk line ties the feature to RGB888 | `VIEW_LINE_WORDS`; Reader's RGB565 800-word plane is a one-parameter case |
| 8 | Doubler latch mismatch can show one wrong frame; mode change in flight glitches a line | contract fixes one latch point; not pinned, listed as an improvement |
| 9 | White-box checks in Gallery's benches (they read `dut.u_talos.*` and count writes) cannot go to the author | extract memory-level vectors from a model, section 7 |
| 10 | Legal combinations explode | named sets only, `$error` on the rest, test matrix of 8 |
| 11 | Hardware evidence thin for the doubler and CORNERS/DSBLIT (owner-observed, no isolated measurement); Dock untried | record each result with a label in the importing project; the Gallery owner confirms m1g data |
| 12 | Licence mixing (Apache-2.0 repository, MIT inherited files) | owner statement; NOTICE line; no copy of unrelated Apache files |
| 13 | Pack ABI is frozen at 1.2: new opcodes must not leak to meter packs | wrappers (`gfx_corners`, `gfx_dsblit`) added to Tau Alpha's `fw2/gfx` only for firmware that has the capability; no pack ABI change |

## 7. Effect on the clean-room process and the similarity audit

* **What the author receives for these features:** the behavioural contract sections 1.1 to 1.4 rewritten without identifiers from section 0 to 6 (no state names, no register names of the old engine; port names only), and **vectors**: command stream, source memory image, expected destination image, generated by a **new** Python model written from the contract (never from the old RTL). Gallery's tests become vectors in two steps: the contract author runs Gallery's scenarios through its RTL as an oracle in a separate tree and through the new model, resolves every difference, and hands over only the model's vectors. The Gallery benches themselves are **not** given as they are: `tb_mp3_fb.v` reads `dut.u_talos.*` and counts writes (white-box); `tb_tau_gfx_view.v` and `tb_tau_scanout_v.v` instantiate modules by port and are usable once ported to the new tree.
* **Not given:** Gallery's `tau_talos.sv` (the extended copy), its diff, `GALLERY_RTL_1_0_SPEC.md` text that quotes engine internals, this document, the Gallery audit entries.
* **Exposure:** this session read the Gallery engine diff, so it is a contract author, not an RTL author. Reviewer X1 (no old text, no scoping report) must read the new sections cold.
* **Similarity audit additions:** compare the new engine also against Gallery's `tau_talos.sv` (160 added code lines are the relevant ones), `tau_scanout_v.sv` and `tau_vidmode.sv` with the same thresholds; calibration baseline: the one trivial line of about 30 characters shared by Gallery's additions and old text. Copy-aware blame of the new file must show nothing from the Gallery clone either.
* **Provenance record:** add to `docs/PROVENANCE.md` when the engine exists: the Gallery features were specified from Gallery's design notes and tests, authored by the owner, not copied.

## 8. Owner decisions and unknowns

| # | Decision or unknown | Cheap way to resolve |
|---|---|---|
| 1 | Approve the parameter names and the named sets (2.1, 2.4) | read section 2 |
| 2 | Approve the register remap above `0x200` and a product namespace in `CORE_VERSION` (2.5) | read `MMIO_ALLOCATION.md` row `0x1EC` to `0x1F4` against Gallery's header |
| 3 | Claim `R_GFX_STATUS [30:17]` as the capability bits (same item as the scoping report's decision 6) | check nothing else reads them (grep the firmware) |
| 4 | ESC sub-opcode field: `R_FB_GO [22:17]`, 6 bits | approve or choose another width |
| 5 | Scan-out viewer: import by hash or rewrite from the contract (5.3) | owner choice; recommended import plus contract |
| 6 | Whether Gallery's published pixels are pinned bit-exact only for the synthetic vectors or also for a real-picture golden | build the golden (a thumbnail set through the old engine) in a Gallery clone; the Gallery owner (the owner) supplies the pictures |
| 7 | DSBLIT and blend/key | 3-line bench on the old engine |
| 8 | Fit numbers per feature | probe level 1 (section 4.3); needs the owner's yes for VM time |
| 9 | m1g fit report and the doubler's Pocket evidence | ask for `work/reports` of the Gallery working folder (not in the archive git) or the 15 GB tar; do not unpack without a yes |
| 10 | Does Tau Reader adopt VIEW with an 800-word RGB565 plane? | its owner (the owner) at its R milestone; no engine work before |
| 11 | Apache-2.0 / MIT statement for Gallery-authored code moved into Tau Alpha | owner statement, one line in `NOTICE.md` |
| 12 | Unknown: Dock behaviour of the viewer and of the 1,666 x 400 UI line | Gallery's own open test G1 |

## 9. Reproduction

Scripts (read-only, in the session scratch directory, not committed): `lineage.py` (Gallery-added lines against Tau Alpha HarpMudd-origin text), `overlap720.py` (Gallery files against the `test/720` T1 diff), `blame.sh`, `blame2.sh` (copy-aware blame by origin commit and file), `tv_lineage.py` (Tau V, used in the import plan). The Gallery diff: `git diff 75d8891 HEAD -- src/fpga/core/tau_talos.sv` in the clone (75d8891 is the untouched import of `12418aab`).
