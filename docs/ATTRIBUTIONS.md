# Attributions and licences

Tau stands on other people's work. Where code or data is included, the name and licence below come from that source file's own
header or licence file, checked against the repository. The short, authoritative provenance statement is [NOTICE.md](../NOTICE.md);
this page expands it and must stay consistent with it. If they ever differ, NOTICE.md and the licence files win and this page is wrong.

Contents: [What is MIT and what is not](#what-is-mit-and-what-is-not) · [Included in the repository](#included-in-the-repository) ·
[Studied or evaluated, not included](#studied-or-evaluated-not-included) · [Tooling and test dependencies](#tooling-and-test-dependencies) ·
[The one-way GPL rule](#the-one-way-gpl-rule) · [Tau's own work](#taus-own-work) · [Licence text locations](#licence-text-locations)

## What is MIT and what is not

**MIT ([LICENSE](../LICENSE)):** HarpMudd's original code and Tau's own code, tools, firmware, RTL, documentation and modifications,
with both copyright notices preserved (`Copyright (c) 2026 HarpMudd`, `Copyright (c) 2026 alfatreze (Tau modifications)`). MIT was
kept because it is the upstream project's licence and keeps reuse simple without trying to relicense the original work.

**Keeps its own licence, and MIT here relicenses none of it:**

| Component | Licence | Where |
|---|---|---|
| Helix MP3 decoder | RealNetworks Public Source License 1.0 (RPSL), with RCSL and a licence summary alongside | `third_party/libhelix-mp3/docs/` |
| Inter typeface, and the font ROM generated from it | SIL Open Font License 1.1 | `third_party/font/OFL.txt` |
| Analogue Pocket Framework (APF) files | Analogue's own licence notice and the Pocket EULA | headers in `src/fpga/apf/`; the notice is embedded in each file |

Two of these carry real obligations: Helix is a per-file source-disclosure licence (so it is vendored in full), and the font ROM is a derivative of
Inter under the same OFL terms. The rest are MIT, ISC or public domain.

## Included in the repository

Where code is included, the name comes from that source file's copyright header.

| Component | Author | Licence | Where in the repo | Notes |
|---|---|---|---|---|
| **[HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player)** | HarpMudd | MIT | the whole project's baseline (v1.4.0, upstream commit `7ef8f0fb84abd4ae5a6a187805fd910351ae57dc`) | Original core, firmware, UI, integration. The complete upstream git history is retained. `mp3_fb.sv`'s scanout half is adapted from HarpMudd's `pocket_vector_fb.sv` (from the HarpMudd.starwars core), per its own header. |
| **[Helix MP3 decoder](https://github.com/ultraembedded/libhelix-mp3)** | (c) 1995-2002 [RealNetworks, Inc.](https://www.realnetworks.com), released to the [Helix Community](https://helixcommunity.org) | RPSL 1.0 | `third_party/libhelix-mp3/` | See the modifications note below. |
| **[VexRiscv](https://github.com/SpinalHDL/VexRiscv)** soft CPU | Charles Papon ([Dolu1990](https://github.com/Dolu1990)) | MIT (as stated by the project) | `src/fpga/rtl/VexRiscv_Full.v` | The generated file is a SpinalHDL 1.9.4 output that carries no licence header of its own, so the MIT statement rests on the upstream project's licence. See the note below. |
| **[picojpeg](https://github.com/richgel999/picojpeg)** | Rich Geldreich ([richgel999](https://github.com/richgel999)), with changes from Chris Phoenix | public domain (per the file header) | `third_party/picojpeg/` | Baseline JPEG decoder for album art. |
| **SDRAM controller and I2S audio bridge** | Adam Gastineau ([agg23](https://github.com/agg23)) | MIT | `src/fpga/rtl/mem/sdram_fb.sv` (licence: `src/fpga/rtl/mem/sdram_LICENSE`), `src/fpga/core/sound_i2s.v`, `src/fpga/core/sync_fifo.v` | Headers state `MIT License, Copyright (c) 2022` (2023 for the SDRAM licence file). |
| **APF data loader** | Adam Gastineau, ported from opengateware/arcade-galaga | MIT | `src/fpga/core/data_loader.sv` | Header: "Ported from opengateware/arcade-galaga modules/dataloader-pocket". |
| **[openFPGA framework](https://www.analogue.co/developer)** | [Analogue](https://www.analogue.co) | Analogue APF licence / Pocket EULA | `src/fpga/apf/` | Its embedded notices remain controlling for those files. |
| **[Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/)** | Robert Bristow-Johnson | published formulas | `tools/gen_eq_coeffs.py` and the generated `eq_coefs.vh` | The equalizer's shelf and peaking filter formulas are his; the coefficients are generated from them. |
| **[Inter typeface](https://rsms.me/inter/)** | Rasmus Andersson ([rsms](https://github.com/rsms)) | SIL OFL 1.1 | `third_party/font/` (`Inter-SemiBold.ttf`, `OFL.txt`) | The font ROM the core draws with (`font_rom.v`, `tools/gen_font_rom.py`) is generated from it and is a derivative under the same licence. |
| **QR code encoder** (`fw/qrcode.h`) | Written for Tau; algorithm per ISO/IEC 18004, structure after Nayuki's public-domain-style reference layout (per its own header) | MIT (Tau) | `fw/qrcode.h`, `fw/qr_tables.h` | Verified against the `segno` reference; see below. |

**Helix modifications.** The RPSL requires modifications to covered code to be identifiable. Since v0.5.0 the vendored Helix is **not entirely unmodified**: the
repository history shows Tau changes to `mp3dec.c` and a new `pub/mp3_profile.h` (decoder stage-cost profiling, off unless the profile macros are set), and to
`real/dct32.c`, `real/subband.c` and `real/coder.h` (the `TAU_POLY_FW` hook that captures FDCT32's outputs so the MP3 window unit can take over the synthesis
window stage). With the macros off the build is intended to be byte-identical to the unmodified decoder. The older statement "Included in full, under its own
license, unmodified" in the previous README credits therefore no longer holds literally; NOTICE.md now records the same modifications.
(If this needs correcting in NOTICE.md, that is the owner's call; this page does not edit it.)

**VexRiscv note.** [DECISIONS.md](DECISIONS.md) and [PHASE_F_SPEC.md](PHASE_F_SPEC.md) record that the generated `VexRiscv_Full.v` has no licence header and that
the project's MIT statement should be verified properly rather than assumed; treat the licence of the generated file as "MIT per the upstream project".

## Studied or evaluated, not included

Two projects shaped the design without ending up in it; both decided something, which is why they are credited:

- **[minimp3](https://github.com/lieff/minimp3)** by [lieff](https://github.com/lieff) (CC0). Measured against Helix and rejected: over three times slower, because it is floating
  point and the CPU has no FPU. Its test vectors were the measurement input either way.
- **[PicoRV32](https://github.com/YosysHQ/picorv32)** by Claire Xenia Wolf ([clairexen](https://github.com/clairexen)) (ISC). The first CPU tried. It needed 114-351 MHz to decode in real
  time depending on configuration, which is what sent the design to VexRiscv.

Also read for ideas, never copied: HarpMudd's later v1.5.x work ([HARPMUDD_UPSTREAM_1.5_REVIEW.md](HARPMUDD_UPSTREAM_1.5_REVIEW.md); a few small items were ported, MIT to MIT),
other openFPGA cores and open-source graphics and blitter designs listed in [PHASE_F_SPEC.md](PHASE_F_SPEC.md) and [OPENFPGAOS_REVIEW.md](OPENFPGAOS_REVIEW.md). The Winamp Bars and Winamp Scope
meters are styles named after the classic player's visualisers; they contain no Winamp code or assets. The name "Winamp" belongs to its owners.

## Tooling and test dependencies

Not shipped in the core, used to build or check it:

| Tool | Used by | Note |
|---|---|---|
| Quartus Prime Lite (Cyclone V), RISC-V GCC (`riscv-none-elf`), Icarus Verilog | building and simulating | not redistributed |
| Pillow, numpy | image and thumbnail tooling (`tools/tau_image.py`, `tools/sync_media.py --art-variants`, `tools/gen_meter_thumbs.py`) | installed by the user |
| segno, OpenCV (`opencv-python`) | QR tests and decoding (`tools/gen_qr_tables.py`, `sim/test_qr.py`, `tools/decode_tau_suite.py`) | test and decode tools only; not part of any firmware or bitstream |
| LAME (via Homebrew) | generating test MP3s | test assets only |
| Test audio | LibriVox speech clip (public domain) and generated tracks | assets under `work/`, not shipped |

## The one-way GPL rule

Tau's own code is MIT, so **GPL RTL must never be copied in**. GPL and unlicensed graphics-hardware designs (for example Minimig's blitter, PSX_MiSTer's GPU) were studied for
technique only; the techniques are old and safe to reimplement independently, but their code would pull the GPL in. The blit engine, blend pipeline and other
Tau RTL are written from the technique, not from those sources. The same one-way rule applies between Tau and its companion Tau Omega: code may flow from MIT into a
stricter host such as an AGPL project, never the reverse. Sources: [PHASE_F_SPEC.md](PHASE_F_SPEC.md) (licence table and decision), [DECISIONS.md](DECISIONS.md), AUDIT_TRAIL B-085.

## Tau's own work

- **Original MP3 Player core, firmware, UI, integration and the v1.4.0 baseline:** [HarpMudd](https://github.com/harpmudd), from
  [HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player).
- **Tau project direction, identity, artwork, packaging, documentation and modifications after the v1.4.0 baseline:** [alfatreze](https://github.com/alfatreze)
  ([Tau-Alpha](https://github.com/alfatreze/Tau-Alpha)). Development uses AI coding assistants; every result is logged with its evidence in [AUDIT_TRAIL.md](AUDIT_TRAIL.md).

No endorsement by HarpMudd, RealNetworks, Rasmus Andersson, Analogue, or other upstream contributors is implied.

## Licence text locations

| Licence | File |
|---|---|
| Tau and HarpMudd (MIT) | [LICENSE](../LICENSE) |
| Helix RPSL 1.0, RCSL, summary | `third_party/libhelix-mp3/docs/RPSL.txt`, `RCSL.txt`, `LICENSE.txt` |
| Inter (OFL 1.1) | `third_party/font/OFL.txt` |
| agg23 SDRAM (MIT) | `src/fpga/rtl/mem/sdram_LICENSE` |
| Analogue APF | the header of each file in `src/fpga/apf/` |
| Provenance summary | [NOTICE.md](../NOTICE.md) |
