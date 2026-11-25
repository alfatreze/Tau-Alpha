# Relicensing plan: Tau Alpha from MIT to Apache License 2.0 (2026-11-25)

**Evidence label: HOST-ONLY (static reading of the repository, of `git blame`/`git log`, of sibling folders read-only, and of public GitHub and licence pages fetched on 2026-11-25).** Nothing in this change alters `LICENSE`, `NOTICE.md`, `docs/ATTRIBUTIONS.md`, `docs/PROVENANCE.md`, any header, any rule file or any source. **This is not legal advice and draws no legal conclusion.** Where a licence text says something, the section is cited and the text was read (Apache-2.0 text read in `Tau Reader/LICENSE`, byte-for-byte the standard text; RPSL in `third_party/libhelix-mp3/RPSL.txt`; MIT in `LICENSE`; the ASF and FSF compatibility pages were fetched on 2026-11-25). Everything that needs a decision is in section 9 for the owner or a lawyer.

Owner intent: decision D-L01 (2026-11-25), move to Apache-2.0, "not applied yet; written and approved first". Note: `docs/DECISIONS.md` on this branch (cut from main before D-L01 and D-C01/D-C02 were recorded there) does not contain D-L01, D-C01 or D-C02; their content here is taken from the task brief (minimp3 CC0 base adopted) and must be re-checked at merge.

Companion: `RELICENSING_HEADER_POLICY_2026-11-25.md` (exact header text and checker spec). Inputs: `ATTRIBUTION_AUDIT_2026-11-25.md` (A1 to A21), `ATTRIBUTION_OPEN_QUESTIONS_2026-11-25.md` (Q1 to Q11), `provenance-2026-11-25/harpmudd_manifest.csv`, `HARPMUDD_REMOVAL_ASSESSMENT_2026-11-25.md`.

---

## 1. Summary of facts that decide the plan

1. **HarpMudd's MIT-origin text cannot be relicensed by the owner and does not need to be.** It stays under HarpMudd's MIT notice. MIT's own condition is "The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software" (`LICENSE`). The Gallery repository already carries this pattern (see 4.3), so there is an in-family precedent.
2. **Only one thing blocks the switch outright as a matter of lineage: files that descend from a repository whose own LICENSE says GPL-2.0-or-later** (`HarpMudd.mpatrol`, A4). The ASF page (fetched) says GPLv3 software cannot be included in Apache projects, and the FSF licence list (fetched) says Apache-2.0 is not compatible with GPL version 2. The exact files are in 5.1. Whether any GPL-covered text really survives in them is **not measured** (audit Q1); the plan removes the question by rewriting them first.
3. **Helix (RPSL 1.0) is a second licence boundary, not a blocker:** RPSL Exhibit B lists "Apache Software License" and "MIT license" among its Compatible Source Licenses (list "as of October 25, 2002"; Apache 2.0 was published after that date, so whether the entry covers 2.0 is a lawyer question, 9 Q5). RPSL 4.1 requires a Derivative Work that "contains or is derived from the Covered Code" to be licensed under the RPSL; the project already keeps Helix pristine plus `tau.patch` (B-1168).
4. **The release zips contain no licence text** (audit F1). Apache-2.0 section 4(a) and (d) need a copy of the licence and the NOTICE attributions to travel with redistributed copies, so the first Apache release must fix this regardless.
5. **Published releases up to v0.7.5 stay MIT** (the licence granted to a copy already distributed is not changed by a later licence on the repository; stated as fact of how the project will proceed, not as a legal opinion).
6. **No outside contributor exists in git history or on GitHub**: two authors (Abel Santos, HarpMudd). So sign-off is owner plus HarpMudd's MIT portions (which are not relicensed).

---

## 2. (a) Who holds copyright in what

### 2.1 Git history (2,207 commits on this branch, 2026-08-01 to 2026-11-25; 34 tags)

| Author (name, email as recorded) | Commits | Period | Note |
|---|---|---|---|
| Abel Santos `alfatreze@gmail.com` | 1,687 (9 of them committed through GitHub web, 1 with committer name "Abel Santos (aider)", the `docs: add CLAUDE.md` commit) | whole range | the owner; tooling-assisted commits are recorded under his name |
| HarpMudd `mikekoons@gmail.com` | 520 | 2026-08-01 to 2026-09-07 | upstream history preserved (NOTICE.md); fork point `7ef8f0fb` |

`gh api repos/alfatreze/Tau-Alpha/contributors` lists the same two logins (alfatreze 1,687; harpmudd 520). Forks: 2 (not inspected).

### 2.2 Lines still attributed to HarpMudd by `git blame -w` (first-committer attribution, a **lower bound** of HarpMudd origin where a later move or copy re-attributed lines, and an **over-count of authorship** where HarpMudd merely committed third-party text)

| Directory | HarpMudd-blamed lines | Owner-blamed lines | Reading |
|---|---|---|---|
| `src/` | 15,444 | 25,976 | contains vendored/generated text first committed by HarpMudd: `VexRiscv_Full.v` 6,657 (MIT, Spinal HDL), `font_rom.v` 3,070 (OFL-derived data), `src/fpga/apf/*` about 1,900 (Analogue), `core_bridge_cmd.v` 579 (Analogue), agg23 files about 340, so HarpMudd-original RTL is on the order of 2,000 lines (`core_top.v` 315, `tau_talos.sv` 519, `tau_soc.v` 500, `core_game.vh` 391, `pcm_fifo.v` 142, `tgt_cmd.v` 133, `ap_core.qsf` 761 mostly template) |
| `third_party/` | 24,684 | 2,347 | vendoring commits by HarpMudd (Helix, picojpeg): not HarpMudd authorship |
| `legacy-harpmudd/` | 8,746 | 16,587 | not built, not shipped (D-T20) |
| `sim/` | 4,197 | 41,266 | `test_mp3*.h` 2,660 (CC0 vectors), `oracle/tau_talos1_oracle.sv` 627, `tb_*`, `common/*` |
| `tools/` | 2,074 | 33,669 | flac tools, `rv32sim.py`, `gen_text_gamma.py`, `gen_eq_coeffs.py`, `library_check.py` |
| repo root | 2,573 | 1,866 | `ROADMAP.md` 2,201 (HarpMudd's roadmap text; the root `ROADMAP.md` is HarpMudd's file, the working roadmap is `docs/ROADMAP.md`), `STAGE0_RESULTS.md` 184, `CHANGELOG.md` 79, `.gitignore`, `LICENSE` |
| `docs/` | 1,137 | 428,291 | `FLAC.md` 505, archived EQ/QA docs |
| `dist/` | 428 | 3,000 | JSON templates (`interact.json`, `input.json`, ...) and a binary (`tau.rom`; blame of a binary is meaningless) |
| `fw/`, `fw2/`, `meters/`, `sdk/`, `themes/` | 21 / 0 / 0 / 0 / 0 | 4,894 / 22,201 / 2,621 / 17,929 / 231 | `fw2/platform/mmio.h` about 12 inherited lines were re-attributed by moves (manifest says 12 to 20) |

Method: `git blame -w --line-porcelain -e` over all tracked text files (script kept in the session scratchpad; not committed). Blame does not follow copies across files by default, so it differs from the manifest (for example `core_constraints.sdc`: blame 13 HarpMudd lines, manifest 8).

### 2.3 The manifest (131 files, `harpmudd_manifest.csv`), by verdict and whether the file ships

| Verdict | Ships? | Files | Lines | HarpMudd lines (manifest) |
|---|---|---|---|---|
| ORIGINAL-HARPMUDD | yes | 3 (`core_top.v`, `tgt_cmd.v`, `pcm_fifo.v`) | 421 | 377 |
| MIXED | yes | 4 (`tau_talos.sv`, `tau_soc.v`, `core_game.vh`, `core_constraints.sdc`) | 3,006 | 861 |
| ORIGINAL-HARPMUDD / MIXED | no (legacy, tests, tools) | 45 / 13 | 5,411 / 9,090 | 5,146 / 4,777 |
| MIXED, builds the release (tool) | tool | 1 (`package.py`) | 124 | 77 |
| THIRD-PARTY | yes (+ headers only) | 35 + 6 | 26,262 | (blame artefact, not authorship) |
| THIRD-PARTY | no | 14 | 4,578 | same |
| TAU-OWN | yes / no | 4 / 9 | 628 / 636 | 53 / 68 inherited lines |

**What HarpMudd text ships today (both RTL, in the bitstream): seven files, about 1,238 manifest lines**, plus the 12 to 20 inherited firmware lines in `fw2/platform/mmio.h`. D-T20 item 3 rewrites these (engine, SoC, shell, constraints) once the Talos scoping is approved; the relicensing plan does not depend on that rewrite finishing (MIT text can be carried), except for the GPL-lineage files in 5.1.

### 2.4 By origin class

| Class | Where | Copyright holder / licence as stated by the project (audit) |
|---|---|---|
| Owner-authored | `fw2/`, `sdk/`, `meters/`, `themes/`, `tools/` (most), `sim/` (most), `docs/` (most), `src/fpga/core/tau_*.sv` (Tau-named modules: scanout, SDRAM PHY and schedulers, gfx, psram probe, mp3 poly), `docs/research` | Abel Santos (the LICENSE line says "alfatreze (Tau modifications)"; same person, see Q9). Today MIT |
| HarpMudd MIT-origin | the 7 shipped files above; non-shipped files in manifest; `legacy-harpmudd/`; root `ROADMAP.md`, `STAGE0_RESULTS.md`; `tools/gen_text_gamma.py`, `rv32sim.py`, flac tools; `sim/common/*` | HarpMudd, MIT, "Copyright (c) 2026 HarpMudd" (A1) |
| HarpMudd file of unstated or GPL-lineage | constraints, PLL, shell comments | A3, A4, see 5.1 |
| Third-party vendored, MIT | `VexRiscv_Full.v` (Spinal HDL contributors 2016; text `third_party/vexriscv/LICENSE`), agg23 `data_loader.sv`, `sound_i2s.v`, `sync_fifo.v` (2022; `third_party/agg23/LICENSE-analogue-pocket-utils`), agg23 sdram licence text (2023) | A7, A9, A10 |
| Third-party vendored, other | Helix (RPSL 1.0 / RCSL 1.0, 70 files pristine plus `tau.patch`), picojpeg (public domain per header), Analogue APF (`src/fpga/apf/*`, `core_bridge_cmd.v`; Software License Agreement and Pocket EULA in headers), Intel/Quartus IP (`mf_*`, Intel licence wording) | A5, A6, A11, A13 |
| Generated | `VexRiscv_Full.v` (SpinalHDL 1.9.4), `font_rom.v` / `font_ram.v` (`tools/gen_font_*.py` from Inter, Space Grotesk, Tamzen), `tau_mp3_poly_rom.svh` (own generator from the standard's window table, minimp3 CC0 as the value source, B-1168), `fw2/generated/*`, Quartus `mf_*` | origin of the input decides |
| Fonts | Inter, Space Grotesk (SIL OFL 1.1), Tamzen/Tamsyn (own permissive text) | A14 to A16 |
| Data tables, vectors | `sim/common/test_mp3*.h` (lieff/minimp3 vectors, CC0 1.0), `tools/data/mp3_synthesis_window.txt` (values from minimp3 CC0), cover_weight tables (HarpMudd lineage, A20) | A18, B-1168 |
| Test material | `sim/golden/*`, `sim/**` benches, conformance fixtures | owner-authored unless listed above |
| Docs, specs | `docs/`, `README.md`, `CHANGELOG.md` | owner, except HarpMudd-blamed docs in 2.2 |
| Not in the repository | LibriVox clip, test music/covers (siblings), Analogue Pocket SDK pages | n/a |

---

## 3. (b) What the move means mechanically, class by class

Facts about how the project is built that matter: only **14 tracked source/tool files carry any copyright or licence line** (plus the LICENSE copies); of 726 `.c/.h/.sv/.v/.py` files under `fw2`, `src/fpga/core`, `tools`, `sim`, `sdk` essentially none has a header, and no file contains an SPDX tag. So the licence today is "the root LICENSE", not per-file text.

| Class | Mechanical change if D-L01 is applied | Notes |
|---|---|---|
| Owner-authored files | Root `LICENSE` becomes the Apache-2.0 text; a root `NOTICE` (Apache style) carries "Tau Alpha, Copyright 2026 Abel Santos (alfatreze)". New files get the header from the policy note. Existing files are **not** touched by default (Apache-2.0's Appendix says to attach the boilerplate; the policy note proposes a script-added SPDX line only for new files and for files touched afterwards, plus a one-time header sweep as an optional phase). | Which name is the holder: LICENSE says "alfatreze"; the siblings say "Abel Santos". Open Q9. |
| HarpMudd-origin MIT text | Keeps its MIT notice and "Copyright (c) 2026 HarpMudd". Move the MIT text from `LICENSE` to **`LICENSE-MIT`** (Gallery's layout), verbatim, both copyright lines. Mark the files either (i) by a per-file `SPDX-License-Identifier: MIT` line and the HarpMudd copyright line where the file is HarpMudd-origin, or (ii) by a list in `NOTICE` and `docs/PROVENANCE.md` (file paths and the manifest commit). | Apache-2.0 section 4 last paragraph (verified): "You may add Your own copyright statement to Your modifications and may provide additional or different license terms and conditions for use, reproduction, or distribution of Your modifications, or for any such Derivative Works as a whole". MIT permits sublicensing; the MIT text only requires the notice to stay with copies/substantial portions. How a lawyer words "mixed file" (HarpMudd lines plus Apache lines in one file) is Q3. |
| Vendored third-party | Unchanged: licence text beside the code (`third_party/`), no relicensing. `NOTICE` lists them. | Apache-2.0 section 4(c) concerns Work/Derivative Works under Apache and does not touch what a third party licenses to us. |
| Generated files: Quartus IP | Keep Intel headers; list in `NOTICE` as "Intel Quartus generated". If regenerated (5.1), keep the generator parameters in the repo. | Intel terms read only as header text (A6). |
| Generated files: SpinalHDL `VexRiscv_Full.v` | MIT of VexRiscv stays; the generation configuration is not recorded (audit F8); record it. | |
| Generated: own generators (fonts, ROMs) | Output carries the licence of the input data (OFL for font data; CC0 values for the window table) plus Tau's generator licence. Generated headers say so (policy note). | OFL 1.1 has terms on derivatives of the font (reserved names, same licence for the font software itself); a ROM of rendered glyph data is audit A14 "none found", not re-examined. |
| Assets, data | Icons, images, `assets/`, `themes/`: owner-authored content under Apache-2.0 (or CC-BY if the owner prefers a content licence; owner choice, Q10). Fonts and vectors unchanged. | |
| Docs | Apache-2.0 for text, or a separate doc licence (Q10). Third-party quotations unchanged. | |
| Test vectors | `test_mp3*.h`: CC0 1.0, header already names the source; keep. | |

---

## 4. (c) Compatibility facts to verify against the licence texts

All quotes are from texts read on 2026-11-25. "Fact" is what the text says; "not decided" is what a lawyer or the owner must decide.

### 4.1 Apache-2.0 itself (text of `Tau Reader/LICENSE`)

- **Section 3 (patent grant and termination):** "each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable (except as stated in this section) patent license ... where such license applies only to those patent claims licensable by such Contributor that are necessarily infringed by their Contribution(s) alone or by combination of their Contribution(s) with the Work". Termination clause: "If You institute patent litigation against any entity ... alleging that the Work or a Contribution incorporated within the Work constitutes direct or contributory patent infringement, then any patent licenses granted to You under this License for that Work shall terminate as of the date such litigation is filed." Fact for the owner: Apache-2.0 adds an express patent grant and a patent-retaliation clause that MIT does not state.
- **Section 4 (Redistribution), conditions (a) to (d):** give recipients a copy of the License; modified files "carry prominent notices stating that You changed the files"; retain copyright/patent/trademark/attribution notices in Source of Derivative Works; if the Work has a NOTICE file, Derivative Works "must include a readable copy of the attribution notices contained within such NOTICE file". NOTICE contents "are for informational purposes only and do not modify the License".
- **Section 4 last paragraph:** additional or different terms for one's own modifications are allowed provided the use of the Work otherwise complies.
- **Section 5 (Contributions):** "Unless You explicitly state otherwise, any Contribution intentionally submitted for inclusion in the Work by You to the Licensor shall be under the terms and conditions of this License". Relevant to outside contributions (none exist yet, 8).
- **Section 6:** no trademark licence (Tau, Tau Alpha names stay owner's).
- **Sections 7 and 8:** warranty disclaimer and liability limit.
- **Practical consequence for the release zip:** the zip is "Object form" distribution of firmware and bitstream. 4(a) needs the licence; 4(d) needs the NOTICE attributions. Nothing in the zip does today (F1).

### 4.2 Apache-2.0 with MIT inputs (agg23, VexRiscv, HarpMudd)
Fact: MIT text (read in `LICENSE`) grants use, copy, modify, merge, publish, distribute, sublicense and sell, on condition that the copyright and permission notice is included in all copies or substantial portions. Not decided: how to word a file that has both MIT-origin and Apache-origin lines (Q3); whether a per-file SPDX `MIT` plus a root `LICENSE-MIT` is enough as "permission notice included" in a binary-only zip (it is the same question as for RPSL and OFL: the zip needs the texts; see 6).

### 4.3 Precedent inside the family: Tau Gallery
Public repo `alfatreze/Tau-Gallery` (fetched): `LICENSE` is Apache-2.0, `LICENSE-MIT` holds HarpMudd's and the earlier Tau MIT notice, `NOTICE` (Apache style: "This product is a derivative of the HarpMudd MP3 Player ... used under the MIT License (see LICENSE-MIT)") and `NOTICE.md` ("Tau Gallery is distributed under the Apache License 2.0 (`LICENSE`); the MIT license (`LICENSE-MIT`) continues to apply to HarpMudd's MIT-licensed original work and to the earlier MIT-licensed Tau files, and its copyright and permission notice must stay with them."). Gallery is a fork of Alpha (pin `12418aab`) with the same HarpMudd lineage. Tau Reader and `Tau codecs` use the same pattern (`LICENSES/MIT-HarpMudd.txt`, `NOTICE` saying SDRAM files "are reused from Tau Alpha under MIT. Their source headers and the complete MIT license are preserved alongside them in `LICENSE-MIT.txt`"). Fact: the Alpha plan would copy a layout the owner already applies; whether a lawyer has reviewed Gallery's wording is unknown.

### 4.4 Apache-2.0 with RPSL components in one release zip
- RPSL 4.1 (read): "You must cause any Derivative Work that you distribute ... that in whole or in part contains or is derived from the Covered Code ... to be licensed as a whole at no charge to all third parties under the terms of this License and no other license except as provided in Section 4.2."
- RPSL 4.2 (read): independently developed modules that "contain no portion of the Covered Code" are exempt from 4.1 "only to the extent that" the module "including any software that is linked to, integrated with, or part of the same applications as, the software module by any method must be wholly subject to one of the Compatible Source Licenses", and "the entire Derivative Work must be licensed under a combination of the RPSL (for Covered Code) and a Compatible Source License". Exhibit B lists "Apache Software License" and "MIT license"; GPL and LGPL carry footnote (1) ("Any attempt to apply non RPSL license terms, including without limitation the GPL, to Covered Code is expressly forbidden").
- RPSL 4.3: mere aggregation on one distribution medium does not extend the licence; but "If You deliver the Covered Code for combination and/or integration with an application previously provided by You ... such combination and/or integration constitutes a Derivative Work".
- RPSL 2.1(c): each modified file of Covered Code carries the Exhibit A notice and a statement of change with its date. Fact: `tau.patch` changes five Helix files; Exhibit A notices are not added by the patch (not checked in the patch text: **open, Q5**).
- **Fact for the release**: Helix is compiled into `tau.rom`; the RPSL text is not in any zip. The Apache switch does not change the RPSL position (the project's MIT licence was equally "a Compatible Source License" in Exhibit B); it changes the entry that the lawyer reads (Apache vs MIT in Exhibit B). The plan therefore **does not rely on the switch to solve the Helix question**. D-C01 (minimp3 CC0 as the decoder base) removes the question for future releases; until then the first Apache release carries the RPSL text and the Helix notices like any earlier one should have.

### 4.5 Apache-2.0 with GPL-2.0-or-later and GPL-3.0 lineage (what would block)
- ASF page `GPL-compatibility.html` (fetched 2026-11-25): "Apache 2 software can therefore be included in GPLv3 projects ... However, GPLv3 software cannot be included in Apache projects." and "the FSF has never considered the Apache License to be compatible with GPL version 2, citing the patent termination and indemnification provisions".
- FSF `license-list.html` (fetched 2026-11-25): Apache-2.0 "is a free software license, compatible with version 3 of the GNU GPL ... not compatible with GPL version 2".
- `HarpMudd.mpatrol` LICENSE (read in the audit, A4): "licensed under the GNU General Public License, version 2 or (at your option) any later version, because it incorporates GPL-licensed HDL". "Or any later version" means GPLv3 is selectable, but that is a lawyer's reading of what applies to a derived file (Q1). The plan avoids relying on it.
- `opengateware/arcade-galaga` (A8) has a GPL-3.0 LICENSE, while the one file the project's `data_loader.sv` says it was ported from carries agg23's MIT header at that path (audit). That is a provenance question, not a settled GPL lineage; the plan replaces the file anyway (5.1).
- Optional tool `pykakasi` (GPL-3.0-or-later) is imported at run time, never bundled (PROVENANCE 2026-11-07): not part of the tree, no change.

### 4.6 Analogue APF terms
Header text (audit A5, read in `apf_top.v`, `common.v`, `io_pad_controller.v`): licensed "solely for use in designing, testing and creating applications for use with Company's Products or Services", EULA by URL (text not in the repository), and "To the extent any use of the APF requires application of the MIT License or the GNU General Public License and terms of this APF Software License Agreement and EULA are inconsistent with such license, the applicable terms of the MIT License or the GNU General Public License ... will prevail". Facts: this clause names MIT and GPL, **not Apache-2.0**. The APF files are third-party and keep their licence in any case; the question for a lawyer is whether the "MIT or GPL prevails" language gives any rule when the project's own licence is Apache (Q4). `core_top.v`, `core_bridge_cmd.v`, `ap_core.qsf/.qpf`, `build_id_gen.tcl` are template-shaped; template repository `open-fpga/core-template` has no licence file (GitHub: null).

### 4.7 CC0 and public domain into Apache
CC0 1.0 (minimp3, vectors): a public-domain dedication with a fallback licence; no condition travels with it. Fact: the window-table values in `tools/data/mp3_synthesis_window.txt` come from minimp3 (CC0), and D-C01 would adopt minimp3 code as the decoder base (brief). Under Apache-2.0 the files taken from minimp3 would keep their CC0 statement and be listed in `NOTICE` (a courtesy; CC0 requires nothing). picojpeg is "public domain" by file header only (no licence file upstream; audit A13): same handling, listed in `NOTICE`. Not decided: whether an ASF-style "category" analysis is wanted (not fetched; not needed for a non-ASF project).

---

## 5. GPL-lineage files and the removal plan (do this FIRST)

### 5.1 Exactly which files carry the lineage

Measured from `git blame`, headers and the manifest. Scaffold origin = `HarpMudd.mpatrol` (A4), which states GPL-2.0-or-later.

| File | Ships | Lineage evidence | HarpMudd lines (blame / manifest) | Plan |
|---|---|---|---|---|
| `src/fpga/core/core_constraints.sdc` | yes (Quartus constraints; not in the zip as text, but in the fit) | line 2 "user core constraints -- Moon Patrol Pocket core"; header lines 1 to 4 and the nine-line `set_clock_groups -asynchronous` block (lines 11 to 19) are from the first commit `68649161` (HarpMudd, 2026-08-01); lines 5 to 9 and 19 and the whole SDRAM section (about 33 lines) are the owner's | 13 / 8 | Rewrite the header and the clock-group block from the PLL instance names and the clock list in `core_top.v` and the owner's own description (the group set is dictated by the design: which clocks are asynchronous). Equivalence gate below. |
| `src/fpga/core/mf_pllbase.v`, `mf_pllbase.qip`, `mf_pllbase.sip` | yes | header says "inherited from the Moon Patrol scaffold" (line 7) | 30 / 7 / n | Replace with a **fresh Quartus IP wizard generation** (altera_pll) with the owner's parameters (74.25 MHz in; 60, 12, 12 at 90 degrees, 100 MHz), own wrapper comment. Parameters are facts of the design, the text is Intel's generator output. |
| `src/fpga/core/mf_pllbase/mf_pllbase_0002.v` | yes | line 1 "comment inherited from the Moon Patrol scaffold"; line 3 refers to "HarpMudd upstream's release/1.5.1" | 78 (blame) | same regeneration |
| `src/fpga/core/core_top.v` | yes | line 2 "FROZEN APF shell for MiSTer->Pocket ports"; manifest: ORIGINAL-HARPMUDD 238 of 249 lines, "proven equivalent ... across the HarpMudd port set"; mpatrol's LICENSE describes its `core_top.v` as the Analogue reference "adapted from" | 315 / 238 | Not proven GPL-lineage; the mpatrol LICENSE (as read) treats it as Analogue's. **Treat as the same family**: compare against Analogue's template text and against the mpatrol file if the owner can read it; rewrite as the Talos/SoC rewrite (D-T20 item 3) with the shell from the template. Open Q1/Q4. |
| `src/fpga/ap_core.qsf`, `.qpf` | yes (fit input) | template shaped, 761 HarpMudd-blamed lines, comment at line 763 about "not an arcade port" | 761 | Not GPL-evidenced; part of the same decision as `core_top.v` (template). |
| `src/fpga/core/data_loader.sv` | yes | header "Ported from opengateware/arcade-galaga ..." (a GPL-3.0 repository, A8); MIT header "Copyright (c) 2022 Adam Gastineau" | 159 (HarpMudd-blamed commit) | Replace with agg23's file from `agg23/analogue-pocket-utils` `ip/data_loader.sv` (MIT, commit `69a715e4`, 2022-09-25; **223 lines with the full MIT text; the local file is 159 lines and differs in 166 lines**, so it is a changed port, not a copy; the replacement needs a behaviour check by sim and a fit) or rewrite from the written bridge-write contract (D-T07 clean-room style). |
| `src/fpga/core/tau_talos.sv`, `sim/oracle/tau_talos1_oracle.sv` | yes / no | header "Scanout half adapted from HarpMudd.starwars' pocket_vector_fb.sv" (repository not public, A3) | 519 / 627 | covered by the D-T20 item 3 rewrite (D-T07 clean-room); licence of starwars unknown (Q3 audit). Gate: the rewrite lands before the switch, or the file stays under HarpMudd's MIT notice with the open question recorded. |
| `src/fpga/core/core_game.vh` | yes | comment "confirmed against HarpMudd.starwars'" (line 30) | 391 / 211 | rewrite is part of D-T20 item 3 |
| `package.py` | tool | builds the release; the manifest lists it as MIXED; its origin includes the scaffold ("`package.py` descends from mpatrol", audit A4) | n/a / 77 | rewrite or re-derive from Analogue's documented package layout (the SDK already vendors `meter_package.py` from it) |

Everything else HarpMudd-origin is **from `HarpMudd.mp3player`** (MIT; A1) and is not GPL-lineage.

### 5.2 Removal gates (all before any LICENSE change)
G1. Ask HarpMudd in writing (owner's call, not mine) which licence applies to the scaffold-derived files, **or** remove the question: items below. Either result is recorded in `docs/PROVENANCE.md` and `docs/DECISIONS.md`.
G2. `core_constraints.sdc` header and clock-group block rewritten; the clock-group set verified equal (see 8.1 for the test); one Quartus fit that shows the same constraint report set (batch with the next planned fit, 1h40 to 1h45 per fit by rule 3).
G3. PLL IP regenerated through the Quartus wizard, with the parameter list recorded in a text file; the fit shows the same `outclk` frequencies and phases (timing report and `core_top.v` unchanged).
G4. `data_loader.sv` replaced (agg23 file or clean-room), host sim of the bridge-write path equal, fit.
G5. `core_top.v`, `ap_core.qsf`, `core_game.vh`, `tau_talos.sv`, `tau_soc.v`, `tgt_cmd.v`, `pcm_fifo.v`: decision recorded (rewrite per D-T20 item 3, or keep under MIT with HarpMudd's notice and a recorded legal opinion). Not a blocker if the files are carried as MIT-origin text and not GPL-derived (G1).
G6. `package.py` re-derived or kept with the same decision.

---

## 6. (d) Repositories

| Repository | Today (read from the repo or GitHub) | Plan |
|---|---|---|
| **Tau Alpha** `alfatreze/Tau-Alpha` (this one) | MIT; LICENSE has two copyright lines (HarpMudd, alfatreze) | move, per this plan |
| `tau-alpha-meter-sdk` (public) | MIT, "Copyright (c) 2026 alfatreze (Tau Alpha project)" | follows Alpha: Apache-2.0 for owner-authored; the SDK vendors Alpha's `mmio.h` (12 inherited lines) and `meter_package.py`; keep the MIT notice only where HarpMudd-origin text is vendored |
| `tau-alpha-meters` (public) | MIT, "alfatreze (Tau Alpha project)"; `meter.json` says `"licence": "MIT"` and the 7 SDK folders hold copies of the root LICENSE with HarpMudd lines (audit Q8) | follows Alpha; fix the per-meter notices first (Q8); a meter's `meter.json` `licence` field is the **author's** choice, not a statement the SDK should hard-code |
| `tau-alpha-pocket-sync` (public, plugin) | MIT, "Copyright (c) 2026 Abel Santos"; NOTICE lists permissive crates and MPL-2.0 Symphonia (via `tau-core`) | can move on its own schedule (no HarpMudd text); Symphonia MPL-2.0 is file-level, unchanged; **brief said Apache; it is MIT today** |
| `analogue-pocket-dev-skill` (public) | MIT, "Abel Santos" (a knowledge base; decision 2026-11: AI mention fine) | independent; leave unless the owner wants uniformity |
| **Tau Gallery** (public) | **Apache-2.0** plus `LICENSE-MIT` for HarpMudd/early Tau files (4.3) | already moved |
| **Tau Reader** | Apache-2.0 (`NOTICE`: SDRAM files reused from Alpha under MIT) | already Apache |
| **pocket-testkit** | Apache-2.0 | already Apache |
| **Tau codecs / Tau V** | `Tau codecs`: Apache-2.0 since 2026-11-08 with `MIT-HarpMudd.txt`; **`tau-nu` (Tau V) LICENSE is still MIT with HarpMudd + alfatreze lines (GitHub `Tau-v`: mit)** | the 2026-11-08 note says Apache applied "across Tau V and Tau codecs", but `tau-nu/LICENSE` is MIT: a stale inconsistency to settle; Tau V carries the old `mp3_fb.sv` |
| **Tau Μ** | MIT with HarpMudd + alfatreze lines (old engine and old player) | its own decision; its HarpMudd text is larger |
| **Tau Omega** | `LICENSE` MIT "Abel Santos", `Cargo.toml` `license = "MIT"`, GitHub `mit` | **inconsistent with the docs**: `docs/CURRENT_STATUS.md` says "MIT OR Apache-2.0" and `docs/ATTRIBUTIONS.md` speaks of "a stricter host such as an AGPL project"; fix the text, not the licence |
| `tau-web`, `tau-alpha-diagrams`, `tau-alpha-sv12s2` | MIT (Abel Santos) / MIT with HarpMudd lines | out of scope |
| `pocket-sync` (not the owner's plugin; GitHub shows AGPL-3.0) | AGPL-3.0 | host program of the plugin; no code flows |

### 6.1 Code flows between the family by hash, and what each combination needs
(Direction names are the project's own: D-T08, D-T19, audit S1 to S5; whether the owner copies other files by hash is not recorded in one place: Q8.)

| Flow | From (licence now to licence after) | To | What the combination requires (as facts of the texts) |
|---|---|---|---|
| SDRAM PHY and scheduler (`tau_sdram_*`, tagged commits) | Alpha MIT to **Apache-2.0** (owner-authored files) | Reader (Apache), Tau V (MIT) | Reader: already Apache, so a copy of Apache-2.0 files needs nothing new; the Reader NOTICE sentence "reused from Tau Alpha under MIT" becomes "under Apache-2.0" for files copied after the switch and stays MIT for the files copied before. Tau V (MIT): receiving Apache-2.0 files into an MIT repo means Tau V's LICENSE/NOTICE must carry the Apache text for those files (Apache 4(a), 4(d)); Tau V's own MIT then does not "relicense" them. |
| Engine features back from Gallery (D-T19) | Gallery Apache-2.0 | Alpha (Apache after switch) | same licence, no new term; Gallery files that contain HarpMudd text keep `LICENSE-MIT` |
| Gallery forks Alpha | Alpha (MIT now; Apache later) | Gallery (Apache) | already Apache with `LICENSE-MIT` for the MIT-origin parts |
| Meters (`tau-alpha-meters`, SDK) | Alpha owner-authored to SDK/meters | the public meter repos | follow Alpha; meters written by other authors keep their own licence (the SDK says meters are independent) |
| Omega (MIT) to/from Alpha | contract data and fonts only (S1); no code | none | the "MIT into a stricter host, never the reverse" sentence in ATTRIBUTIONS stays true for Apache; note that Apache-2.0 files **into** an MIT repo bring the Apache terms with them (4(a), 4(d)), the opposite of the old rule, which assumed MIT sources |
| Tau V, Tau Μ (MIT, old HarpMudd engine) to Alpha | MIT (HarpMudd lineage) | Alpha | MIT-origin text arriving in Alpha keeps its MIT notice (same as today) |
| Reader (Apache) to Alpha | Apache | Alpha (Apache) | none |
| Anything GPL to anywhere | | | rule 7 stays: read, never copy |

---

## 7. (e) Releases

- Published releases up to v0.7.5 were MIT; they stay MIT (no re-publication, no edits to old assets).
- `tools/make_release.py` `make_zip()` keeps only `Cores/`, `Assets/`, `Platforms/` (line 59) and `check_zip` asserts "unexpected base folders" (line 115): a root-level `LICENSE` would be **dropped**. Change: put the licence texts under the core's own folder in the zip, `Cores/alfatreze.TAU/LICENSES/` (`LICENSE`, `LICENSE-MIT`, `NOTICE`, `LICENSE-RPSL.txt`, `LICENSE-RCSL.txt`, VexRiscv, agg23, OFL x2, Tamzen, `THIRD_PARTY_NOTICES.txt`), copied from the repository by the packager and checked by `check_zip` (every file hashes equal to the repository's; none missing). The Pocket ignores unknown files in the core folder (to be confirmed on a card before the release; `Cores/<id>/README.txt` is already shipped this way per the audit).
- The `tau-compat.json` and `dist/Cores/alfatreze.TAU/core.json` carry no licence field today (checked: `core.json` has `author` "alfatreze", `url`, `description`; `updaters.json` only lists the plugin URL). Nothing there needs to change; do **not** add a licence key to `core.json` (APF schema is Analogue's; unknown keys are untested).
- Meter packs: `meter.json` has `"licence": "MIT"` for all seven meters and the SDK template; `tools/sdk_collect_meters.py` copies the root `LICENSE` into each meter folder and writes "MIT" into the generated README (`sdk_collect_meters.py` line 52); `tools/export_sdk.py` copies `sdk/sdk/LICENSE` and writes each meter's `licence` into the collection index. Changes after the switch: the collector copies the Apache LICENSE and `LICENSE-MIT` only for meters whose source has MIT-origin text (none identified), and writes `Apache-2.0`; the template placeholder "Your Name" is fixed. Omega-facing: `docs/interop/to-omega/034...` says Omega shows "author, licence and hashes" from the pack: the licence string changes from "MIT" to "Apache-2.0" for new packs; Omega's code must accept any string (not checked; the interop docs do not constrain it). A note to Omega on `docs/interop/to-omega/` is the vehicle, owner decides.
- First Apache release: LICENSES folder in both zips (normal and Diagnostic), `README.txt` in the core folder names the licence and the folder, GitHub release notes name Apache-2.0 and say earlier releases remain MIT, `tools/make_release.py` prints the asset list to compare with `release/SHA256SUMS.txt` (rule 8), pass `--repo alfatreze/Tau-Alpha` (rule 8).
- The README line and the `docs/ATTRIBUTIONS.md` header are changed in the same commit (section 9).

---

## 8. (f) Contributions and sign-off

- Authors in git: Abel Santos 1,687 commits, HarpMudd 520. GitHub contributor list: the same two. No pull requests from outside were looked for beyond that; 2 forks exist (not inspected). So there is **no third-party contribution to ask consent from**. HarpMudd's code is not relicensed.
- **AI-assisted commits, stated factually.** The owner's commits (1,687) carry **no** `Co-Authored-By` trailer (project rule 9, 2026-11-12; `git log` finds 0 after 2026-11-12). **All 432 commits whose message contains `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>` are authored by HarpMudd** (2026-08-01 to 2026-09-07), i.e. part of the upstream history the repository preserves. The owner's tooling-assisted commits (Claude, Codex, a local Qwen workflow, one commit with committer "Abel Santos (aider)") are recorded under the owner's name, as the owner intends. What this means for who owns the copyright of AI-assisted output is a legal question that depends on the jurisdiction and on human creative contribution (Q7); no fact in the repository settles it. The AI service's own terms were not read.
- Sign-off needed before the switch (owner decides): (1) the owner's own approval (D-L01 recorded); (2) optional, recommended by the audit's Q1: HarpMudd's written statement on the scaffold files; (3) no CLA/DCO file exists: add a CONTRIBUTING note (Apache-2.0 section 5 applies by default) before accepting any outside change.

---

## 9. (g) Rule and document changes at apply time (exact lines, not applied now)

| File | Current line | New line (proposal) |
|---|---|---|
| `docs/PROJECT_RULES.md` rule 7 | `7. **Never copy copyleft code into this MIT project.** Read it for ideas only and record the inspiration in docs/PROVENANCE.md (decision D-P01).` | `7. **Never copy copyleft code into this Apache-2.0 project.** Read it for ideas only and record the inspiration in docs/PROVENANCE.md (decisions D-P01, D-L01). Third-party code is vendored only with its licence text and a NOTICE entry; MIT, BSD, CC0, OFL and public-domain pieces keep their own terms.` |
| `docs/DECISIONS.md` D-P01 | "Tau is MIT; GPL, AGPL and LGPL code ... stay out of the tree" | append a dated line, not a rewrite: "Since D-L01 applies: Tau Alpha is Apache-2.0; the rule is unchanged (copyleft code, including the constants and tables in it, stays out; permissively licensed code only with in-file attribution and a PROVENANCE entry)." |
| `docs/DECISIONS.md` | (new) D-L02 | record the plan outcome, the files kept MIT, the date and the first Apache tag |
| `README.md` lines 123 to 126 | "HarpMudd's original code and Tau's own code and modifications are MIT licensed, with both copyright notices preserved. Everything under `third_party/` keeps its own license, and MIT here relicenses none of it: Helix is RPSL 1.0 and Inter is SIL OFL 1.1." | "Tau's own code and modifications are Apache-2.0 licensed (LICENSE, NOTICE). HarpMudd's MIT-licensed original work keeps the MIT terms (LICENSE-MIT) with its copyright notice. Releases up to v0.7.5 were published under MIT. Everything under `third_party/` keeps its own license; neither licence here relicenses any of it ..." |
| `NOTICE.md` | lines 10 to 11, 20, 27: "The MIT license applies to HarpMudd's MIT-licensed original work and to Tau's own MIT-licensed additions"; "License for Tau-authored code and documentation: MIT" | copy Gallery's wording (4.3) |
| `LICENSE` | MIT, two lines | Apache-2.0 text, Appendix with `Copyright 2026 Abel Santos` (holder: Q9) |
| (new) `LICENSE-MIT`, `NOTICE` | none | as in Gallery |
| `docs/ATTRIBUTIONS.md` | lines 11 to 14, 87, 89, 105 | "What is MIT and what is not" becomes "What is Apache-2.0, what is MIT, what keeps its own licence"; line 87 "Tau's own code is MIT, so GPL RTL must never be copied in" becomes "... is Apache-2.0, so ..." |
| `docs/PROVENANCE.md` | header "Tau is MIT" (line 3) | dated note, history not rewritten |
| `CLAUDE.md`, `AGENTS.md` | no licence statement (checked) | none; point to the policy note under the rules list |
| `docs/DEVELOPERS.md` line 166 | "never copy GPL code into the MIT tree" | "into the Apache-2.0 tree" |
| `docs/ROADMAP.md` line 79 | "licence stays MIT (GPL RTL may be studied, never copied)" | "licence is Apache-2.0 (...)" |
| `docs/CURRENT_STATUS.md` line 197 | "Tau Omega (... MIT OR Apache-2.0 ...)" | correct to Omega's real licence (MIT) |
| `sdk/*/LICENSE`, `tools/sdk_collect_meters.py` line 52, `tools/export_sdk.py` line 35 | MIT copies | Apache copies (see 7) |
| Headers | none today | the policy note |

---

## 10. (h) Phased, reversible plan with gates

| Phase | Content | Gate to pass before the next | Reversible by | Effort (owner time mostly VM waiting) |
|---|---|---|---|---|
| **0. Decide** | Owner answers the questions in 11 (at least Q1 to Q4, Q9); optionally a lawyer reads RPSL, the Gallery wording and the HarpMudd question | recorded in `docs/DECISIONS.md` | nothing changed | 1 to 3 days owner time; lawyer optional |
| **1. GPL-lineage removal first** (5.2 G1 to G6) | rewrite SDC header and clock groups; regenerate PLL; replace `data_loader.sv`; `core_top`/QSF/shell decision | host tests green; ONE Quartus fit (1h40 to 1h45) shows equal clocks, constraints and timing; Pocket smoke on the DEV core by the owner | git revert of the commits; MIT unchanged throughout | about 1 to 2 working days of session time plus one to two fits; part overlaps D-T20 item 3 |
| **2. Prepare (still MIT)** | add `tools/check_licence_headers.py` per the policy note with `sim/test_licence_headers.py` (red/green cases), run it in `make test-host`; fix notices that are wrong today: Omega licence text, Q8 meter notices, `sdk/template` "Your Name", Tau V LICENSE note | checker passes on the tree **as a report** (no failing policy yet) | remove the tool | 0.5 to 1 day |
| **3. Release engineering** | `make_release.py` LICENSES folder and `check_zip` assertions with a test; `sdk_collect_meters.py` / `export_sdk.py` licence handling; one dry-run zip checked by listing | `sim/test_make_release*` green; a zip listing shows every licence text; hash-equals-repo check | revert | 1 day; test on a card once (owner) |
| **4. The switch commit** (one commit, one branch, owner approves) | `LICENSE` (Apache-2.0), `LICENSE-MIT`, `NOTICE`; `NOTICE.md`, README, ATTRIBUTIONS, PROVENANCE note, PROJECT_RULES rule 7, DECISIONS D-L02, DEVELOPERS, ROADMAP, CURRENT_STATUS fixes; policy note moves from research to `docs/`; the checker's strict mode on for new files | full `make test-host` exit 0; `git log -1 --format=%B` checked (rule 9); no `Co-Authored-By` | `git revert` of the one commit (until a release is cut) | 0.5 day |
| **5. First Apache release** | version bump, zips with LICENSES, release notes naming the licence change, rule 8 asset comparison | asset list equals `SHA256SUMS`; zip listing checked | releases are published and cannot be un-licensed: **the first point of no return** | normal release effort (about 1 day plus fit if RTL changed) |
| **6. Public sibling repos** | meter SDK, meters, pocket-sync, skill: each its own commit and decision | each repo's own tests | per repo | 0.5 day each |
| **7. Optional header sweep** | add SPDX lines to existing files by script in batches (not one giant commit) | checker passes in strict mode | revert per batch | 1 day, low value |

**What is NOT changed by this plan:** third-party licences and their files; HarpMudd's MIT notice and text; published releases and tags; APF, Intel, Helix, picojpeg, OFL and CC0 texts; the Analogue EULA position (stays "controlling for those files"); the rule that GPL code is only read; `core.json`/`updaters.json` fields; Omega's code; Tau V and Tau M until their owners' own decisions.

---

## 11. (i) Open questions for the owner or a lawyer

| # | Question | Facts | Options |
|---|---|---|---|
| Q1 | Do the scaffold-derived files (SDC, PLL wrappers, perhaps `core_top.v`) carry GPL-2.0-or-later terms? | `HarpMudd.mpatrol` LICENSE states GPL-2.0-or-later (incorporates GPL HDL); the same author's MP3 player repo states MIT; the files in this repo are comment lines, a nine-line clock-group block and Quartus output; how much text of the mpatrol file survives is not measured (neither the mpatrol file text nor a diff was read here) | (a) ask HarpMudd; (b) rewrite first (5.2, the plan); (c) lawyer reads |
| Q2 | Is `data_loader.sv` (MIT header, ported from a GPL-3.0 repo) clean? | agg23's own MIT repo holds a 223-line version; ours is 159 lines, 166 lines differ | replace with agg23's file; or rewrite; or ask agg23 |
| Q3 | How to mark a file that mixes HarpMudd MIT lines and Apache lines? | `tau_talos.sv`, `tau_soc.v`, `core_game.vh`, `pcm_fifo.v` (MIXED), HarpMudd lines 211 to 360 each; Apache-2.0 allows "additional or different license terms" for one's modifications and the Derivative Work as a whole (4 last paragraph); MIT needs the notice with substantial portions | (a) keep the file under MIT in full (SPDX `MIT`) until the rewrite; (b) dual notice in the header; (c) rewrite the files first (D-T20 item 3) |
| Q4 | APF header says MIT or GPL terms "prevail" if inconsistent; no mention of Apache | audit A5, Q4; EULA text not in the repository | lawyer; ask Analogue; keep APF files as-is under their own text |
| Q5 | Helix RPSL: obligations for the compiled `tau.rom` and for the modified files (Exhibit A notice, RPSL 2.1(c)); is "Apache Software License" in Exhibit B (2002) read as Apache 2.0? | texts read in 4.4; Exhibit A notices not found in `tau.patch` | add Exhibit A notices to the patched files; lawyer reads; D-C01 (minimp3) retires the question |
| Q6 | Is the licence change allowed to proceed while Helix is still in the shipped ROM? | RPSL 4.1 says Derivative Works containing Covered Code are licensed under the RPSL; the same sentence applies today with MIT | wait for D-C01; or ship Helix as the documented RPSL component (as today) |
| Q7 | AI-assisted code: does it affect who can license it? | owner's commits carry no trailer (rule 9); 432 upstream HarpMudd commits carry the Claude trailer; tooling-assisted commits are under the owner's name | lawyer; keep a statement in `NOTICE.md` that the owner is the author of record |
| Q8 | Which files does the owner copy between repositories by hash, in which direction? | audit S2 to S5 cover SDRAM files and engine files; no complete list | the checker in phase 2 can list each repo's headers; a per-repo "UPSTREAM.md" pin as in Gallery |
| Q9 | Copyright holder name in the new LICENSE/NOTICE | root LICENSE says "alfatreze (Tau modifications)"; Reader, Gallery, Omega, pocket-sync say "alfatreze" or "Abel Santos" | one holder line everywhere; the owner chooses |
| Q10 | Licence of docs, images, themes, data | the repo mixes code, docs and art; no separate content licence now | Apache-2.0 for everything; or CC-BY-4.0 for docs and art |
| Q11 | Tau V's `LICENSE` is MIT although the codecs note says Apache across Tau V; Omega's docs say "MIT OR Apache-2.0" and "AGPL"; the three public Alpha-family repos use "alfatreze (Tau Alpha project)" | read in 6 | owner fixes the texts; no relicensing needed |
| Q12 | Is a CLA or DCO wanted when outside contributions arrive? | none exists; Apache-2.0 section 5 applies by default | add CONTRIBUTING with DCO |
| Q13 | The `tau-nu`/Tau M copies of Alpha-origin files: do they move with Alpha? | their LICENSE is MIT with HarpMudd | their own decision; Apache-2.0 files can be received into an MIT repo only with the Apache text shipped (4.1, section 6.1) |
| Q14 | Unverified items from the audit still open | HarpMudd.starwars licence, the Analogue EULA, the VexRiscv generator configuration, libgcc in `tau.rom`, Intel IP terms | as in `ATTRIBUTION_OPEN_QUESTIONS_2026-11-25.md` |

---

## 12. What was verified and what was not

Verified (read or fetched 2026-11-25): root `LICENSE` text; README license paragraph; NOTICE.md; `git log` author counts; all 432 `Co-Authored-By` commits are HarpMudd's; `git blame` totals (script in scratchpad); the manifest totals; the Apache-2.0 text (Tau Reader copy); RPSL sections 1.2, 2.1, 4.1 to 4.3, 11, Exhibit B; the ASF and FSF compatibility pages; the sibling LICENSE files (local) and GitHub licence data for 11 owner repositories; Gallery's LICENSE/NOTICE/NOTICE.md; agg23's `data_loader.sv` and its diff against ours; `make_release.py` `make_zip` filter; `core.json`/`updaters.json` (no licence key).
Not verified: any text of `HarpMudd.mpatrol` beyond the audit's reading; the Analogue EULA; whether the Pocket tolerates extra files in the core folder; Omega's handling of the `licence` string; GPL-2.0 and GPL-3.0 texts themselves (only the ASF and FSF statements); the SPDX identifiers list (used from memory: `Apache-2.0`, `MIT`, `CC0-1.0`, `OFL-1.1`, `LicenseRef-...`); the content of D-L01 itself on this branch.
