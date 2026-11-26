# Talos v2 G1 model: the model author's gap list (raw, 2026-11-25)

**What this is:** the unedited `GAPS.md` written by the G1 model author, who wrote `tools/talos_v2_model/` from the contract v0.1 alone, in a separate tree (source SHA-256 `10433e46105ab24af2210a17a0e60d1fe4a26bce811c9923015d10228fd1b751`). Audit B-1181. The paths in it (`model/`, `public/`, `text_weights/`, `docs/`) are the author tree's; in this repository they are `tools/talos_v2_model/`, `sdk/sdk/conformance/canvas.py`, `tools/meters/preview/tau_fb.js`, `sdk/docs/HOST_TABLE.md`, `tools/derive_text_weights.py` and `docs/features/`. The oracle-side answers to every gap are in `TALOS_ENGINE_V2_DIFFERENTIAL_RESULTS_2026-11-25.md` (oracle side: not for the RTL author). Everything below the line is the author's text, unchanged.

---

# Talos v2 contract: gaps found while writing the G1 model from the contract alone

Reviewed: `docs/TALOS_ENGINE_V2_CONTRACT.md` (v0.1, 2026-11-26), `docs/TALOS_ENGINE_V2_CONFORMANCE_PLAN_sections_1_2_4-9.md`, the interface files, the public artefacts and the text-weight package in this folder. Nothing outside this folder was read. Each gap gives the quoted text, why it is open, the readings, the reading the model takes (tag `GAP-nn` in `model/talos_v2_model.py`), a vector that separates the readings, and a severity:
**B** = blocks implementation (two conforming engines could legitimately differ on a pinned or match result, or a required artefact is missing); **R** = could cause a regression; **C** = cosmetic.

Totals: 32 gaps (B 5, R 10, C 17), 5 internal contradictions, 12 old-design passages, 7 unverifiable performance or fit requirements, 0 pinned rules without a named artefact (but 13 whose artefact is not in the author's package).

## 1. Gaps

| # | Where | Quoted text | Why open / competing readings | Model's reading | Separating vector | Sev |
|---|---|---|---|---|---|---|
| GAP-01 | 1.3 | `VIEW_LINE_WORDS` default 1200, "a multiple of `FILL_WORDS`"; `FILL_WORDS` "512 is accepted for the old A/B" | 1200 is not a multiple of 512. (a) the check is unconditional: every `FILL_WORDS = 512` build fails to elaborate; (b) it applies only with `VIEW_ENABLE = 1` | (b) | elaborate `FILL_WORDS=512`, `VIEW_ENABLE=0` (`V-FILL/lines_fw512_*`) | B |
| GAP-02 | 1.4, plan 6 | named sets list only CORNERS, DSBLIT, ESC, VIEW, VIEW_DBL; matrix "all off" | `BLIT_BLEND_ENABLE`, `DBUF_ENABLE`, `FONT_RAM_ENABLE` for TAU, V, GALLERY and for "all off" are unstated (defaults 0, "shipped: 1") | every set and matrix entry uses the shipped 1/1/1; an extra `DEFAULTS` config uses all defaults | any BLIT with blend on, any CHAR code >= 0xA0 under set V | R |
| GAP-03 | 1.3 | `VIEW_ENABLE` "needs the 40 MHz video build (`TAU_VID40`) in the SoC; error otherwise" | the engine has no parameter that tells it about the SoC; who raises the error | model parameter `SOC_VID40` (defaults to follow VIEW) | elaborate VIEW with `SOC_VID40=0` | C |
| GAP-04 | 2.2, 2.2.3 | "The shipped SoC decodes 9 bits"; viewer block "read 0 and ignore writes when `VIEW_ENABLE` = 0 (in a 10-bit-decode SoC)" | `interfaces/MMIO_ALLOCATION.md` says 8 bits with aliasing at `0x100` (and later "B-287 widened"); what a VIEW build with 9-bit decode does is only "needs" | `SOC_DECODE_BITS` = 10 with VIEW, else 9; offsets alias to `off & mask & ~3` | read `0x3BC` (aliases to `0x1BC` at 9 bits, not at 10) | C |
| GAP-05 | C-DBUF-1 | "a computed word address below 184,320 ... gets 1,048,576 added ... the decision is made per row" | a row can cross 184,320 inside itself (start column > 0 and `x + w > 512`, or a `2^19` wrap): (a) decided per word; (b) per row on the row's first word; (c) on the row's x = 0 word | (c) `(A + 512y) mod 2^19 < 184,320` | RECT `A = 359*512 + 500`, `w = 30`, CPU buffer 1 | R |
| GAP-06 | 2.2.2 | "A selector of 7 writes REINDEX (C-REG-1 FREE)" | where the selector steps after 7 (0 or stays 7) | steps to 0 | BLT_IDX=7, two BLT_DATA writes | C |
| GAP-07 | C-Q-2 vs C-Q-3 | "`cmd_full` is 1 when 255 are held (not yet dispatched)"; pinned test "300 pushes into a stalled queue: 255 kept, 45 counted" | if the engine has dispatched the first command (stalled in memory), 256 are accepted and 44 dropped; "stalled" is undefined (no dispatch, or a dispatched command waiting for `p0_available`) | "stall" = nothing dispatches; 255 kept, 45 dropped | 300 pushes with `p0_available` low after the first dispatch | B |
| GAP-08 | 2.2.1 vs C-RST-1 | `FB_*` "0 at system reset"; C-RST-3 "registers ... not changed by the system reset" | the engine's `reset` and the SoC's "system reset" are not related; `FB_STALL` reset unstated | model `reset` = engine reset + held `FB_*` and `FB_STALL` cleared; sticky kept | `FB_GO` push after reset without rewriting `FB_ADDR` | R |
| GAP-09 | C-RST-1, C-FILL-1 | reset "clears the push, retire, fence and overflow counts" | `fill_seen` (sticky) and `fill_done_line` across reset unstated; the scan-out's late-line check depends on them | cleared | reset, read the late count of the next frame | R |
| GAP-10 | 3.14 | "With `DBUF_ENABLE` = 0 the CPU buffer and flip inputs are ignored ... `DBUF_*` read 0" | whether the SoC register still stores a write (it does not reach the engine) | write ignored | write DBUF_CPU=1 in a no-DBUF build, read it back | C |
| GAP-11 | 2.2.2 | `FONT_CTRL [27:16]` "completed loads" | 12-bit field: wraps or saturates | wraps mod 4096 | 4097 full loads | C |
| GAP-12 | C-COPY-1, C-BLIT-6 | "Legal domain: `w` 1..127" (COPY gives no `h` or address domain) | out-of-domain result not compared, but the model must produce something | full `w`, flagged OOD in the vector | `V-COPY/copy_ood_w200` | C |
| GAP-13 | C-BLIT-2, C-ORD-1, 7.3 | "a source word equal to KEY ... leaves the destination word unchanged"; 7.3 "the same words read and written per command" | (a) the keyed word is not written; (b) it is written back with its pre-read value; changes write traffic and the write-rectangle monitor's write count, not memory | (b) logged as written | `V-BLIT/key_every` (write counts) | C |
| GAP-14 | 2.2.2 | CLUT "contents not initialised" | vectors must carry a full CLUT; the reset/power-up content is undefined, so any CBLIT through an unloaded entry is not comparable | model flags `nondeterministic` when CBLIT reads an unwritten entry; vectors load all 256 | CBLIT before any CLUT write | C |
| GAP-15 | 3.7, 3.12, C-ORD-1 | C-ORD-1 names "BLIT, COPY or CBLIT" only; SBLIT and DSBLIT state no legal domain | row order and read-before-write for SBLIT/DSBLIT with overlapping source and destination are unstated; SBLIT re-reads a source row for several output rows; DSBLIT reads two source rows per output row | each output row: all its source words read, then written; rows top-down | SBLIT 2x with destination = source + 1 row | R |
| GAP-16 | 3.2, 3.4, 3.9 | legal domain stated for RECT only (`A + 512(h-1) + w - 1 < 2^19`) | BAR, CHAR, RRECT, CORNERS address domain unstated (wrap mod 2^19 or OOD) | RECT's rule applied, OOD otherwise | CHAR 3x at `A = 2^19 - 100` | C |
| GAP-17 | C-RRECT-1 | "Out of domain FREE with C-GEN-2" | which corner words a cut > `w` or `r > h` writes | cut clamped to `w`, rows outside `0..h-1` skipped | `V-RRECT/edge_ood_*` | C |
| GAP-18 | plan 2.2 V-RRECT, V-CHAR, V-DSB, G4/G5/G10 | "tables: zero, monotone, Helios' default"; "the theme palettes of `themes/*.json`"; "Gallery's seven cases" | data the vector author needs is not in the package (Helios table, theme palettes, Gallery bench parameters, G5 thumbnails, G10 streams, real font atlases) | substitutes: a "round" table, eight fixed colour pairs, seven self-chosen DSBLIT cases, synthetic fonts | n/a: supply the data | B |
| GAP-19 | C-VIEW-2 | "reads ... from `view_base + p * view_stride`" | modulo of the viewer address (25 bits?) unstated | mod 2^25 | base near 2^25 | C |
| GAP-20 | 2.2.3 | `VID_MODE`: "write [0] requested mode; read [31:16] completed switches, [2] requested, [1] pending, [0] on the bus" | these fields belong to the mode sequencer (out of scope, 1.2); what drives `view_en` into the engine is not said | `view_en` is a direct input; VID_MODE modelled minimally, `[31:16]` = 0 | n/a (second-step contract) | C |
| GAP-21 | 2.2.3 | `VIEW_CAPS [1]` "800 x 720 viewer", `[0]` "mode sequencer" | values not defined by the engine contract | both 1 with VIEW | read VIEW_CAPS | C |
| GAP-22 | C-VIEW-2 | viewer line-buffer address `{n[0], i}` "(12-bit address)" | implies `VIEW_LINE_WORDS <= 2048`; not a stated parameter rule | elaboration refuses > 2048 | `VIEW_LINE_WORDS = 2400` | C |
| GAP-23 | 1.3, C-CHAR-4 | "16 weights, 5 bits each" | a 5-bit weight can be 17..31: the formula then has a negative `16 - wt` | refused at elaboration | `TEXT_W_DARK[7] = 20` | C |
| GAP-24 | C-CHAR-4, G9 | "Derived by `tools/derive_text_weights.py` ... the RGB565 result of the formula above" | (1) the themes are not in the package: the tables cannot be re-derived, only copied; (2) **finding:** the tool's engine model mixes 8-bit bit-replicated channels and truncates to 565, not the contract's 5/6-bit formula: 31,620 of 104,448 (channel, weight) cases differ (30.3 %), so the fit optimised a different arithmetic | tables used as data (identical in contract and redo doc, checked) | re-run the fit with C-CHAR-3's arithmetic | R |
| GAP-25 | C-FILL-1, C-VIEW-2 | "requests line n (0..359)"; viewer "n (0..719)" | behaviour for `fill_line_req` outside those ranges (10-bit field) | formula applied as is | fill line 600 in UI mode | C |
| GAP-26 | 2.6, plan 2.1 | fills are asynchronous to commands | which retired commands a fill sees depends on timing; the vector format gives no rule for the "fill" action's position | a fill sees exactly the commands retired at its position in the action list; benches must wait for idle before a fill whose content is compared | `V-FILL/fill_sees_retired_only` | R |
| GAP-27 | plan 2.1 | "a CRC of the whole memory" | algorithm, width, byte order and range not defined | CRC-32 (zlib), little-endian words, addresses 0..2^25-1 | any vector | B |
| GAP-28 | 2.2.1, 2.2.3 | `FB_STALL` "clk_sys cycles during which the queue was full"; `GFX_COUNTS[31:16]` late lines | not expressible at transaction level; FB_STALL has no reset rule in the engine | not modelled (read returns null / 0) | cycle bench only | C |
| GAP-29 | C-ORD-4 vs 2.7 | C-ORD-4 lists "a sticky field, the CLUT, the corner table or the font"; 2.7 also calls `text_light`, `dbuf_cpu_buf`, viewer base/stride quasi-static | whether firmware must fence before `text_light`/`dbuf_cpu` changes (a frame begin that flips the CPU buffer without a fence would race) | the model flags any such change while busy as nondeterministic | change `DBUF_CPU` between two queued RECTs | R |
| GAP-30 | 2.2.1 | "the push happens on the clock after the write"; C-IDLE-1 "a push followed by an idle read one clk_sys cycle later reads busy" | read of `FB_GO[0]` in the same cycle as the push; which is the first busy cycle | transaction level: busy from the push action | cycle bench | R |
| GAP-31 | plan 2.2 V-DSB | "strides 512 and 1,208" | `SRC_STRIDE` is 10 bits (0..1023): 1,208 is not representable (it is the viewer stride of G6). The model masks it to 184 | 1208 & 1023 = 184 (vectors named `*_stride1208` exercise the mask) | supply a legal second stride | B |
| GAP-32 | host mapping, C-SBLIT-1 | `canvas.py` takes `sc` = 1, 2, 3; the engine takes a selector 0..3 | the host-to-selector map (1 -> 0, 2 -> 2, 3 -> 3) is not stated in the contract | that map | none (host side) | R |

Also R-severity (engine-observable) facts the contract states only as probe results, which the model reproduces but cannot confirm: C-DBUF-3 (displayed buffer survives reset, UNCONFIRMED, Q7), C-RRECT-1's clamp of the 7-bit field, the CORNERS clamp "finding" in 3.11.

## 2. Internal contradictions

1. **C-Q-2 vs its pinned test (GAP-07):** "full when 255 are held (not yet dispatched)" plus a dispatched-and-stalled command gives 256 accepted, not the pinned "255 kept, 45 counted", unless "stalled" means nothing is dispatched.
2. **1.3 defaults (GAP-01):** `VIEW_LINE_WORDS` 1200 is not a multiple of the allowed `FILL_WORDS` 512.
3. **Plan V-DSB stride 1,208 vs the 10-bit `SRC_STRIDE` (GAP-31).**
4. **Decode width:** contract 2.2 "9 bits" vs `interfaces/MMIO_ALLOCATION.md` "decodes only 8 bits of offset; offsets at 0x100 and above alias" (the same file later lists registers up to `0x1F4`).
5. **Plan M2 vs C-BAR-1:** "BAR without the `lit <= h` clamp" is unobservable inside the rectangle (rows `y >= h - L` are all rows when `L > h`); the clamp is only visible as writes above the rectangle, i.e. only the write-rectangle monitor kills that mutant. The model's M2 mutant therefore paints above, as `canvas.py` does.

## 3. Passages that describe the old design instead of behaviour

1. 1.3 `CBLIT_BURST` "accepted and ignored ... kept only so `tau_gfx` elaborates unchanged" (and Q5).
2. 1.3 "Mutation hooks of the old engine (`BUG_*`)".
3. C-MEM-5 "(The shipped engine does ... a blit-class width that is a multiple of 128 produces a zero-length write)".
4. C-MEM-6 "(The old controller, now removed, wrapped writes and hung on reads at a page edge ...)".
5. C-FILL-3 "The shipped engine can lose a request that arrives in the last cycle of a fill, and overwrites the line number".
6. C-OP-1 "The shipped Tau Alpha engine draws one row as RUN for opcodes 10..15 (probe: 5 words in fg at the address)".
7. 3.6 / C-BLIT-6 "the shipped engine uses `w mod 128`".
8. C-RRECT-1 "the shipped engine wraps the mirrored row and can write far outside the rectangle".
9. C-VIEW-4 "The shipped engine waits three cycles after a line change for its address product".
10. C-Q-5 and 2.5 resource sentences ("3 M10K blocks", "94 of the 120 bits", "One M10K") are implementation, not behaviour.
11. 2.1 port table and C-FILL-4 ("sampled with at least two flip-flops") specify structure; fine for a drop-in, but not testable at the behavioural level.
12. 7.1 table: the old engine's stub cycles per command are the performance contract.

## 4. Performance or fit requirements that cannot be checked as written (by the author)

1. 7.1 R4 "+5 % of the old engine at p50 and p95": needs the old engine and the realistic model (oracle side); the per-class corpus of plan section 4 does not exist yet.
2. C-FILL-2 "at most 600 cycles on the probe's functional stub and stream": the stub and the stream are not in the package.
3. C-IDLE-2 / 7.4 "12 clk_sys", "20 cycles", "4 clk_sdram": same stub, not in the package; "on the probe's stub at 66.667/100 MHz" defines no phase relation.
4. 7.2 per-meter-tick Pocket figures: hardware only.
5. 7.3 "the engine's share of memory bandwidth not above the old engine's": no measurement method is given (GAP-13 changes write counts).
6. 7.5 fit: two seeds, slack and resource ceilings: a fit, not a host check; "ALM not above the replaced code +5 %" needs the old map.
7. 1.3 / plan 6 "A feature that is off must leave no logic" (map-level proof): requires Quartus; the model can only show the all-off configuration behaves as specified.

## 5. Pinned rules whose artefact is not in the author package

Every PINNED rule names an artefact and a test (section 8 table is complete: 38 identifiers). These cannot be checked by the author with what was handed over: C-IF-1 (`fw2/gfx/gfx.c`), C-FONT-1 (`FONT_SPEC.md`, `font.c`), C-CHAR-2/-3 (shipped screens), C-CORN-1 and C-DSB-1 (Gallery bench; for DSBLIT not one worked number is given), C-DBUF-1/-2 (`tb_helios_dbuf`), C-ORD-1 (`scroll.c`, `tb_tau_gfx_move`), C-ORD-4 (`gfx.c`), C-Q-2/-3, C-RET-1, C-IDLE-1 (T3 spec, `tb_tau_gfx_t3`), C-VIEW-1/-2 and C-DBL-1 (`tb_tau_gfx_view`), C-FILL-2 (Pocket). The public artefacts that ARE here pin C-RECT-1, C-BAR-1, C-BLIT-1/-3/-6, C-SBLIT-1, C-CBLIT-1, C-MAP-1, C-FENCE-1 (documentary) and C-BLIT-4 (lab), and all agree with the model except the three divergences the contract states.

## 6. Divergences found between the public artefacts and the contract (beyond the three it states)

1. `canvas.py` `blit`/`blit_abs` and `tau_fb.js` `blit`/`copy`/`blitAbs`/`scrollDown` read the whole rectangle before writing (memmove); C-ORD-2 says one engine command is not a memmove for a downward overlap: 22 of 25 (canvas) and 19 of 20 (tau_fb) random downward-overlap cases differ. Firmware that relies on the models for an in-place downward scroll in one command would be wrong on hardware (the HOST_TABLE's scroll_down is "a few copies", so the shipped player is safe).
2. `canvas.py`/`tau_fb.js` `blend_blit` without `blend_on` blend at a = 255 (`(f*255 + b) >> 8`, not a copy); the engine with the BLEND enable 0 copies. Host-table semantics; no engine rule covers it.
3. `tau_fb.js` `blit` works on the visible 400 x 360 plane only (pitch 400) and throws outside it; `canvas.py` `blit` addresses the whole memory. The two models disagree with each other on the domain of `blit`.
4. The three stated divergences (BAR lit > h, SBLIT rows > 127, CBLIT clipping) were confirmed present in both models and shown to be the only difference in those cases (the model with the clamp removed equals `canvas.py`).
