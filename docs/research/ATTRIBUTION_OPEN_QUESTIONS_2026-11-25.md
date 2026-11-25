# Attribution: open questions for the owner or a lawyer (2026-11-25)

Companion to `ATTRIBUTION_AUDIT_2026-11-25.md` (item numbers A1... refer to its tables). **Nothing here is a legal conclusion.** Each point gives the facts found and the options; the decision (D-T20 item 5, R7 of the removal assessment) is the owner's, with a lawyer if he wishes.

## Q1. HarpMudd's code carries an MIT statement in one repository and a GPL-2.0-or-later statement in another (A1, A4)
Facts: `HarpMudd.mp3player` has an MIT `LICENSE` ("Copyright (c) 2026 HarpMudd"), identical to this repository's first lines. `HarpMudd.mpatrol` (Moon Patrol port) has a `LICENSE` that opens "licensed under the GNU General Public License, version 2 or (at your option) any later version, because it incorporates GPL-licensed HDL (listed below)" and lists T80, MiSTer support modules, agg23 files, Analogue APF/`core_top.v`, Intel PLL output as third-party parts. This repository's comments say `core_constraints.sdc`, `mf_pllbase.v` and `mf_pllbase_0002.v` were "inherited from the Moon Patrol scaffold", and `core_top.v` is "proven equivalent ... across the HarpMudd port set". Not measured: how much text of the mpatrol scaffold (as opposed to Analogue's template) is still in these files.
Options: (a) ask HarpMudd in writing which licence covers the scaffold files in the MP3 player and record the answer; (b) rewrite the scaffold files from Analogue's template and the Quartus wizard (they are small; `core_constraints.sdc` 24 lines) so the question disappears; (c) leave as is and credit HarpMudd.mpatrol (done in the notices) and let the lawyer say what that means.

## Q2. `data_loader.sv`: MIT header in a GPL-3.0 repository (A7, A8)
Facts: `src/fpga/core/data_loader.sv` says "Ported from opengateware/arcade-galaga modules/dataloader-pocket". The arcade-galaga repository's `LICENSE` is GPL-3.0; the file at that path carries "MIT License, Copyright (c) 2022 Adam Gastineau" and the same author's separate `analogue-pocket-utils` repository (MIT) contains `ip/data_loader.sv`. This repository's copy has an MIT header by agg23 and no permission text (now backed by `third_party/agg23/LICENSE-analogue-pocket-utils`).
Options: (a) treat the MIT header of the file's own author as the statement and leave it; (b) ask the author which licence applies to this file; (c) rewrite the loader (about 117 lines, one reader class: APF bridge writes to RAM).

## Q3. HarpMudd.starwars / sdramfbtest, source of the scanout and engine halves (A3)
Facts: not found through the public API (private, renamed or removed). The scanout half was replaced clean-room (D-T07), but the engine half of `tau_talos.sv` and the oracle still descend from the same `mp3_fb.sv`, which HarpMudd's MIT repository holds (A1).
Options: ask HarpMudd where `pocket_vector_fb.sv` is published and under what licence; or rely on A1's MIT statement for `mp3_fb.sv` lineage and record that the starwars repository was not checked.

## Q4. Analogue's APF terms, `core_top.v`, `core_bridge_cmd.v` and Intel IP (A5, A6)
Facts: the APF files carry a "Software License Agreement" that licenses the software "solely for use in designing, testing and creating applications for use with Company's Products or Services", incorporates the Pocket EULA (URL only, text not in the repository), and says that where an MIT or GPL licence applies and is inconsistent, that licence's terms prevail. `core_bridge_cmd.v` has "2022 Analogue" and no licence text; the template's `core_top.v` has none; the template repository has no licence file. `core_top.v` in this repository is 96% blamed on HarpMudd and is shaped like the template; the removal assessment proposes counting it as third-party. Intel-generated files carry Intel's licence wording.
Options: record the EULA terms as of a date (needs the owner to fetch it; not done here); list `core_top.v` and `core_bridge_cmd.v` with the APF files as Analogue-origin (done in the notices); ask Analogue whether template-derived top files may be distributed in an MIT-licensed repository; consider whether the repository's MIT statement should name the exception explicitly (the owner decides; LICENSE not edited).

## Q5. Helix under RPSL 1.0 (A11, A12)
Facts: files are vendored with their licence texts (RPSL, RCSL, summary byte-identical to upstream). Tau modified six files (profiling macros in `mp3dec.c`, `imdct.c`; the `TAU_POLY_FW` hook in `dct32.c`, `subband.c`, `coder.h`; new `mp3_profile.h`); `imdct.c` was missing from the notices (fixed). The shipped bitstream contains a ROM table generated from Helix's `polyCoef` and a tap map derived from running Helix's FDCT32 (`tau_mp3_poly_rom.svh`), and the shipped firmware contains Helix compiled. The core zip has no RPSL text and no source offer. This audit did not interpret the RPSL's terms for modified or binary distributions.
Options: lawyer reads RPSL sections on modifications and distribution of executables against the release zip; decide whether the generated table is covered; consider adding a notice file to the release zip (F1 in the audit); consider dropping the hardware window unit's dependence on the Helix table (own derivation from the MPEG-1 synthesis window in the standard) if the answer is unwelcome.

## Q6. libgcc linked into `tau.rom` (A19)
Facts: `fw2/build.sh` links `-lgcc` with `-nostdlib`, so GCC runtime routines (for example 64-bit helpers) may be in the ROM; the repository's own `libc_min.c` supplies the string functions. Not inspected: which routines, which toolchain build (xPack or other) and its licence wording.
Options: inspect the map file; record the toolchain version and licence; or replace the few helpers with Tau's own (the SDK runtime already has its own 64-bit divide and shifts).

## Q7. The 16-number text-weight table `cov_weight` (A20)
Facts: `src/fpga/core/tau_talos.sv`, function `cov_weight`, maps a 4-bit glyph coverage to a 5-bit weight (0, 4, 6, 7, 8, 10, 10, 11, 12, 13, 13, 14, 15, 15, 16, 16); the comments say it was fitted for light text on a dark ramp and descends from HarpMudd's `mp3_fb.sv`. The light-text table `cov_weight_light` (0,1,1,2,3,3,4,5,6,7,8,9,10,12,13,16 as currently in the RTL; its fitting is in the audit trail theme entries) was added by Tau. It is also contract data in the Talos scoping (D-T18, D-T19), and `tools/host/blit_reference.py` mirrors it. Whether a 16-value fitted table is protectable at all is a legal question not answered here.
Options: keep and credit under HarpMudd's MIT statement (already covered by the root `LICENSE`); or regenerate it independently from `tools/gen_text_gamma.py` against Tau's own palette and glyph set (the tool is HarpMudd lineage; it would need rewriting too) and record the new fit.

## Q8. Meter and SDK licence files (A21)
Facts: seven SDK meter folders contain a copy of the root `LICENSE` (HarpMudd and alfatreze lines) while `meter.json` says author alfatreze, MIT; exported meter collections use `sdk/sdk/LICENSE` (alfatreze only); the SDK template carries the placeholder "Your Name".
Options: decide per meter whether the HarpMudd line belongs (only if the meter's source descends from HarpMudd text; `sdk_collect_meters.py` copies it blindly); fix the template placeholder.

## Q9. No licence text in release zips (F1 in the audit)
Facts: core zips contain only `Cores/`, `Platforms/`, `Assets/`; the shipped bitstream and ROM include RPSL (Helix), MIT (VexRiscv, agg23), OFL (Inter, Space Grotesk) and Analogue-origin material.
Options: add a `LICENSES.txt` to the zips through `package.py`/`make_release.py` (a release-engineering change with a test; not done here); or link to the repository notices from `README.txt`.

## Q10. Sibling projects (S1 to S5)
Facts: Tau V and Tau M carry the old `mp3_fb.sv` (HarpMudd lineage); Gallery forks the current stack; the shared SDRAM PHY and scheduler flow to Tau V and Reader; D-T19 plans Gallery's engine features flowing back. Sibling licences were not read.
Options: record per project which licence its copy of Alpha-origin and HarpMudd-origin text is under; state the direction rule (the notices currently say "MIT into stricter host, never the reverse" only for Omega).

## Q11. Unverified items (A3, A10, A19, A5 EULA)
Not found or not read: HarpMudd.starwars licence; the Analogue EULA text; the VexRiscv generator configuration and the unpublished VexRiscv hash `8542a57` in the generated header; the toolchain licence text.
