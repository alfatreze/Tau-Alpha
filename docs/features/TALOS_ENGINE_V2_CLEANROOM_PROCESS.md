# Talos engine v2: clean-room process (proposal, 2026-11-25)

Scope: how the new engine is written without copying the old `tau_talos.sv` (D-T01, D-T07, rule 7). Follows the scanout precedent (`docs/research/TALOS_SCANOUT_CONTRACT.md`, `docs/PROVENANCE.md`). **Weakness of the precedent, stated first:** the scanout separation was honour-based (the author was told not to read the old file; nothing stopped it). This process adds a technical separation and a tool audit, and still cannot prove a negative.

## 1. Roles
* **Contract author:** a session that has read the old engine (it must, to find behaviour the firmware relies on). It writes only behaviour. It must not author the RTL.
* **Contract reviewer (X1):** an agent that has read neither the old engine nor the scoping report; it tries to implement from the text and lists every guess.
* **Clean-room author:** a separate agent, on a separated tree.
* **Auditor:** a script plus a reviewer who did not write the RTL.
* **Owner:** approves classes and drops, runs the Pocket, approves deletion.

## 2. What the author RECEIVES
1. `docs/research/TALOS_ENGINE_V2_CONTRACT.md` (to be written from section 7 of the scoping report; behaviour only, no old identifiers, no state names, no line quotes).
2. The conformance vectors (files of command streams with expected memory results, generated from the new Python model, never from the old RTL).
3. The interface: the port list of the module (names, widths, clock domains), the MMIO packing and register semantics for `R_FB_*`, `R_BLT_*`, `R_CLUT_*`, `R_DBUF_*`, `R_GFX_FENCE`, `R_GFX_STATUS`, `R_GFX_COUNTS`, the `p0` protocol (`SDRAM_CONTROLLER_CONTRACT.md` section 4 and the adapter's header comment, which describes the protocol, not the engine), the scanout contract's handshake, the Gray counter and bus modules, `font_ram.v` as a black box (module and port list only), and the SDRAM chip model and `sched_a` benches.
4. The public artefacts: `sdk/docs/HOST_TABLE.md`, `tau_meter.h`, `canvas.py` (the SDK's idealised model, not an RTL transcription).
5. Repository rules (`PROJECT_RULES.md` items 5, 6, 7, 9), the Cyclone V guidance of the `analogue-pocket-dev` skill, the existing generic RTL idioms of the project except the old engine.
6. The mutant list requirement and the bench requirements (the scanout's section 7 and 8 pattern).

## 3. What the author must NOT receive or read
* `src/fpga/core/tau_talos.sv` and its git history, `sim/oracle/` (both oracles), `src/fpga/core/mp3_*`, any older copy of the engine in `docs/archive/`, `legacy-harpmudd/`.
* `tools/host/blit_reference.py` (its comments transcribe the old RTL construct by construct; the new model replaces it), `docs/TALOS.md` (describes old internals and timing lessons), `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md`, the two Talos reviews, `docs/research/TALOS_*` result notes that name old states, **and the scoping report `TALOS_ENGINE_V2_SCOPING_2026-11-25.md`** (it describes old internals and quirks).
* The provenance measurements and blame output (`docs/research/provenance-*`).
* The old engine's mutation hooks and benches that instantiate it (`tb_tau_gfx_diff`, `tb_mp3_fb`, `test-rtl-fb-mutation`); the author writes its own bench.
* Any chat or handoff that quotes the old engine.

## 4. Technical separation (the addition over the scanout precedent)
1. Build the author's tree with `git archive` of an **allow-list** of paths (the items in section 2) into a new directory with a fresh single-commit repository: no history, no old engine, no oracle, no excluded docs. The author works only there.
2. The tree contains a stub `tau_gfx.sv`/`tau_soc.v` instantiation shim so the module can be elaborated, and the new benches.
3. The author's report lists every file it opened (the harness logs file reads if available; otherwise self-reported as in the scanout record) and every sentence of the contract it found ambiguous.
4. After delivery the new file is imported into the main repo by the coordinator, and only then do the old file and the oracle become visible next to it.
Residual weakness: the author is a language model with general knowledge of common open designs (a blitter, a Gray FIFO); nothing here proves it did not recall a public design. Rule 7 applies: the author must say if any structure came from a named external source, and the inspiration is recorded in `PROVENANCE.md`. The contract author's own exposure to the old text is unavoidable; the mitigation is review X1 and the behavioural-only rule.

## 5. Similarity audit (after the code exists, before any fit)
Run by a script (`tools/provenance/similarity.py`, to be written; it does not exist yet) and read by a reviewer:
1. **Copy-aware blame of the new file** with the repo's `docs/research/provenance-2026-10-12/measure.py` method (`-w -M -C -C`): any line attributed to HarpMudd or to an old-engine commit fails.
2. **Token similarity:** tokenise both files (strip comments, whitespace; map identifiers to a class token, keep operators, numbers and keywords), then (a) Jaccard similarity of 8-token shingles over the whole file, (b) the longest common token run, (c) the list of all common runs of 30 tokens or more. **Proposed thresholds (to be calibrated on the scanout pair first, whose numbers are not recorded: run it and write them down):** whole-file 8-shingle Jaccard above 0.15, or any common run over 40 tokens, or more than 5 runs over 30 tokens, goes to the reviewer. Each flagged run needs a written reason: dictated by the contract (a formula the vectors pin), by an interface (port lists, `p0` protocol), or an industry idiom (Gray code, async FIFO pointers); otherwise the author rewrites it.
3. **Constants and tables:** list every numeric table in the new file (coverage weights, glyph tables) and mark its source: contract data, model-generated, or independently fitted. The dark text weight table is open (scoping report section 11).
4. **Structure review:** state names, module boundaries and register names are compared (they must not reuse the old ones except where the interface forces them, e.g. port names).

## 6. Evidence recorded
In `docs/PROVENANCE.md` (new section) and an `AUDIT_TRAIL.md` entry: the contract's commit; the reviewer's X1 report; the author's brief verbatim, tree allow-list and hash, files-read list, ambiguity report; bench statistics and mutant kill table; the similarity tool output (JSON) with thresholds and the reviewer's explanation of each flagged run; the copy-aware-blame result; the classified-differential report; fit and Pocket results. Evidence labels as usual (HOST-ONLY, FIT, HARDWARE-CONFIRMED).

## 7. What this does not establish
Legal originality (R7 of the assessment is the owner's and, if he wishes, a lawyer's); that the author did not recall public code; that behaviour equal to the old engine's is a substantive copy (the contract states behaviour the product needs, which is not text). The claim is "written from a behavioural contract by an author without access to the old text, audited by tools", not more.

## 8. Tau Gallery's features (D-T19, added 2026-11-25)

Scope: `OP_CORNERS` (10), `OP_DSBLIT` (11), the viewer fill, the pixel doubler and the escape opcode. Detail and sources: `TALOS_ENGINE_V2_GALLERY_FEATURES_SCOPING_2026-11-25.md` (**that document and `TALOS_ENGINE_V2_CONSUMER_IMPORT_PLAN_2026-11-25.md` describe old internals and must not be given to the author, like the scoping report**).

1. **What the author receives (adds to section 2):** the behavioural sections for these features (rewritten from the scoping document's section 1 without any old identifier, state name or old register name; port names only); vectors in the same form as the base (command stream, source image, expected destination image) generated by a **new** model; the parameter list and the named supported feature sets; the capability bits; the escape-opcode field layout. For the viewer: the plane format, the line-buffer write port, the request and done protocol of the scan-out (the interface, as for the base), and a bench adapted to the new tree that compares one whole frame.
2. **How Gallery's tests become vectors:** the contract author runs Gallery's scenarios (its `tb_mp3_fb.v` DSBLIT and CORNERS cases, `tb_tau_gfx_view.v` with and without the doubler) on Gallery's RTL as an oracle **in a separate tree the author never sees**, and on the new Python model; every difference is resolved in the contract; only the model's vectors go to the author. The Gallery benches themselves are not given: `tb_mp3_fb.v` reads old signals (`dut.u_talos.*`) and counts memory writes (white-box); the viewer benches instantiate modules by port and are usable once ported.
3. **Not given (adds to section 3):** Gallery's extended `tau_talos.sv` and every diff of it, its `GALLERY_RTL_1_0_SPEC.md` and design notes where they quote engine internals, Gallery's audit entries, the Gallery clone.
4. **Gaps to close before the author starts:** a Python model for DSBLIT (none exists), a real-picture golden for DSBLIT and CORNERS (Gallery's thumbnails through the old engine), the DSBLIT behaviour with the sticky blend and colour key, and the MMIO remap of the viewer registers (companion document section 2.5).
5. **Scan-out features are a second step with their own contract** (`TALOS_SCANOUT_VIEWER_CONTRACT.md`, to be written): Gallery's `tau_scanout_v`, `tau_vidmode` and its additions to `tau_scanout.sv` have no HarpMudd-origin text (measured, companion document section 5), so they may be imported by hash instead of rewritten; the contract is still written because Tau Reader and Tau V need an interface to build against. The engine's half of the viewer is rewritten in the engine from the contract.
6. **Similarity audit (adds to section 5):** compare the new engine also against Gallery's extended `tau_talos.sv`, `tau_scanout_v.sv` and `tau_vidmode.sv` with the same thresholds; copy-aware blame of the new file must show nothing from the Gallery clone; calibration baseline: one trivial line of about 30 characters is shared between Gallery's additions and old text.
7. **Recorded evidence (adds to section 6):** which Gallery documents the contract author read, the model-versus-oracle difference list, and the owner's licence statement for Gallery-authored code (Apache-2.0 repository, MIT here).
8. **Exposure of this session:** the scoping session read Gallery's engine diff, so it is a contract author and does not author RTL; reviewer X1 reads the new behavioural sections cold.
