# Tau provenance and third-party notices

Tau is a derivative work based on
[HarpMudd MP3 Player](https://github.com/harpmudd/HarpMudd.mp3player) v1.4.0.
Development began from upstream commit
`7ef8f0fb84abd4ae5a6a187805fd910351ae57dc`.

The repository preserves the complete upstream Git history. HarpMudd's original
copyright notice remains in `LICENSE`, alongside the notice for Tau's later
modifications. The MIT license applies to HarpMudd's MIT-licensed original work
and to Tau's own MIT-licensed additions; it does not replace licenses attached
to third-party components.

## Original project

- Project: HarpMudd MP3 Player for Analogue Pocket
- Author: HarpMudd
- Source: <https://github.com/harpmudd/HarpMudd.mp3player>
- Tau baseline: v1.4.0 / commit `7ef8f0fb84abd4ae5a6a187805fd910351ae57dc`
- License: MIT, preserved in `LICENSE`

## Tau

- Project direction, identity, artwork, packaging, documentation, and
  modifications after the baseline: alfatreze
- Source: <https://github.com/alfatreze/Tau-Alpha>
- License for Tau-authored code and documentation: MIT

## Components with separate terms

- Helix MP3 decoder: RealNetworks Public Source License 1.0. Its notices and
  license remain under `third_party/libhelix-mp3/`. Tau has made small, macro-gated
  modifications (decoder stage-cost profiling in `mp3dec.c` and `pub/mp3_profile.h`,
  and the `TAU_POLY_FW` hook in `real/dct32.c`, `real/subband.c` and `real/coder.h`
  that hands the synthesis-window stage to the FPGA unit, and the same profiling
  macros in `real/imdct.c`); with those macros off the code is the unmodified decoder. The
  hardware window unit's coefficient table (`src/fpga/core/tau_mp3_poly_rom.svh`) is generated
  from Helix's `polyCoef` data. See `docs/ATTRIBUTIONS.md`.
- VexRiscv (`src/fpga/rtl/VexRiscv_Full.v`, SpinalHDL-generated): MIT, Copyright (c) 2016
  Spinal HDL contributors (Charles Papon and contributors); the licence text is
  `third_party/vexriscv/LICENSE`.
- agg23's `data_loader.sv`, `sound_i2s.v`, `sync_fifo.v` (`src/fpga/core/`): MIT, Copyright (c)
  2022 Adam Gastineau; the licence text is `third_party/agg23/LICENSE-analogue-pocket-utils`.
- picojpeg: public domain per its file header (Rich Geldreich, with changes from Chris Phoenix).
- Space Grotesk (`third_party/font/SpaceGrotesk-*.ttf`): SIL Open Font License 1.1, Copyright 2020
  The Space Grotesk Project Authors, retained at `third_party/font/OFL-SpaceGrotesk.txt`.
- Tamzen 8x16 (fork of Tamsyn): Copyright 2011 Suraj N. Kurapati and 2010 Scott Fial, own
  permissive licence text retained at `third_party/font/LICENSE-Tamzen`.
- Quartus-generated IP (`mf_*` files, PLLs, data-table RAMs): Intel licence terms in the file
  headers.
- `src/fpga/core/core_top.v`, `core_bridge_cmd.v` and `src/fpga/core/core_constraints.sdc` descend from
  Analogue's openFPGA core template and from the HarpMudd core scaffold (HarpMudd.mpatrol,
  whose own LICENSE states GPL-2.0-or-later; see `docs/research/ATTRIBUTION_OPEN_QUESTIONS_2026-11-25.md`).
- Inter font and the generated font ROM derived from it: SIL Open Font License
  1.1, retained at `third_party/font/OFL.txt`.
- Analogue Pocket Framework files: their embedded Analogue APF license notices
  and Pocket EULA terms remain controlling for those files.
- Other vendored components retain their file-level MIT, ISC, or public-domain
  notices as listed in `docs/ATTRIBUTIONS.md` and `docs/research/ATTRIBUTION_AUDIT_2026-11-25.md`.

No endorsement by HarpMudd, RealNetworks, Rasmus Andersson, Analogue, or other
upstream contributors is implied.
