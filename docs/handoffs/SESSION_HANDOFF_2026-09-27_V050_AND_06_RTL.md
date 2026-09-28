# Session handoff, 2026-09-27: v0.5.0 shipped, five 0.6.0 RTL changes fit individually, none combined or on hardware

Read this first. Supersedes `docs/handoffs/SESSION_HANDOFF_2026-09-26_ALPHA29_MPOLY.md` for current state (that doc's own
open items — the wave-meter saga, the MP3 window unit — are all done and shipped in v0.5.0, see below).

## 1. Where things stand

**v0.5.0 is released**: tagged, pushed to `main`, GitHub release published with both zips
(https://github.com/alfatreze/Tau-Alpha/releases/tag/v0.5.0). It carries the theme system, meter modules
(Winamp Bars/Scope, Chladni, Configure page), the TIM1 fast-cover reader, and the hardware MP3 window unit
(`tau_mp3_poly.sv`) — all hardware-confirmed before release.

**Five 0.6.0 RTL changes exist, each proven on its own, none combined, none on a card:**

| # | What | RTL/sim | Fit | Card |
|---|---|---|---|---|
| 1 | Pipelined alpha blend (B-327) | done | closed, both seeds (best +0.755 ns setup) | no |
| 2 | 192 KB RAM shrink (B-333/339) | done, firmware links at 192 KB | closed, both seeds (240/308 M10K, 68 free) | no |
| 3 | `clk_sys` 66.667 MHz (B-338/344) | done, firmware `CLK_HZ` macro-gated | closed, both seeds (+0.978 ns worst setup) | no |
| 4 | Helios H2 double buffering (B-340) | done, `sim/tb_helios_dbuf.v` passes | **launched (`dbuf-b340`), check `python3 tools/vm_fit.py status dbuf-b340` — may already be done** | no |
| 5 | Persist widening 16→32 words (B-346) | done, no dedicated RTL testbench (see B-346's own note on why) | **not launched** | no |

None combine yet. #2 and #3 are mutually exclusive today (`TAU_RAM_192K`/`TAU_CLK66` share one `CORE_VERSION`
chain in `mp3_soc.v`, deliberately not yet given a combined rev). #1/#4/#5 are each independent and additive.

**Next real step**: once the VM is free, stage a combined fit with as many of 1/4/5 as make sense together
(consider whether to also attempt 2+3 combined, a NEW rev value, not yet allocated), then package and install
for the first real hardware test of any of this. `tools/vm_fit.py launch <name> --append <qsf-file> --seed 1 --seed 2`
is the tool; qsf append files for each piece already exist (`tools/dbuf_qsf_append.txt`, `tools/clk66_qsf_append.txt`,
`tools/ram_shrink_qsf_append.txt` — check names before combining, and diff any new combined append file against
each piece's own to confirm nothing is dropped).

## 2. MP3/FLAC decode kernel work — profiler splits done, no hardware reading yet

Owner asked to start the deferred hardware MP3 decoding. IMDCT was scoped (`docs/research/MP3_IMDCT_KERNEL_SCOPING.md`)
and found materially harder than the already-shipped window unit (two block-type-dependent transforms, not one
fixed shape) — **correctly not started**. Owner then chose the FLAC bit reader instead (matches the standing
roadmap order). Both are profiler-split, not designed as RTL yet:

- FLAC (B-343): `unary()` (Rice-code scan) confirmed by disassembly to call a real `__clzdi2` libgcc subroutine
  (this CPU has no hardware CLZ) — plausible dominant cost. `flac_unary_calls` counter + a one-time boot
  calibration (`clz_cal_cyc`) give an estimate without live per-call timing (would perturb the hot path).
- MP3 (B-345): the old single "I" bucket (Dequantize+AntiAlias+IMDCT+windowing) split into
  `mp3_dequant_cyc`/`mp3_alias_cyc`/`mp3_xform_cyc` — these run only tens of times/frame, cheap enough for a
  plain tick()-around-the-call split, no calibration trick needed.
- Bench row now reads `H I S D A X R U` (`fw/player.c`, `UI_SHOW_DECODE_PROFILE`). **Neither split has a real
  hardware reading yet** — the next step is playing a FLAC track and an MP3 track on a packaged alpha build and
  reading that row, before designing any hardware kernel for either codec.

## 3. Docs

`docs/TALOS.md` and `docs/HELIOS.md` published (dedicated pages, Mermaid diagrams), linked from README.md and
`docs/README.md`. `docs/features/ALPHA_BLEND_ANALYSIS.md`, `docs/research/MP3_IMDCT_KERNEL_SCOPING.md`,
`docs/research/FLAC_BITREADER_KERNEL_SCOPING.md`, `docs/features/RAM_SHRINK_192K_PLAN.md` (rewritten with the B-333 prep) are all
current. `docs/ROADMAP.md` item 7 corrected (H1 is built and hardware-confirmed, H2's precondition is met).

## 4. Packaged, uninstalled 0.6.0-alpha builds (numbering: post-release alphas are 0.6.0-alpha.N, not 0.5.0-*
— see the feedback memory on this)

`alfatreze.TAU_0_6_0_A_1`/`_2` (on the card, installed) — 192 KB firmware on the old bitstream, and the blend
bitstream with trail/fade/rrect-probe-fix, respectively. `_3` (on the card) — the REAL RAM-shrink bitstream +
matching firmware, first genuine hardware test of the shrink, **not yet run by the owner**. `_4`/`_5`/`_6`
(packaged, NOT installed) — FLAC profiler split, MP3 profiler split, persist widening, each on the blend
bitstream (b327 s1) since none of these needed their own new fit.

## 5. Traps, unchanged

Other sessions/accounts write in this repo concurrently — `git status` before committing; this session's own
untouched-on-purpose untracked files: `docs/DECISIONS.md`, `docs/features/THEME_SPEC.md`, `docs/features/meters/CHLADNI_METER_SPEC.md`,
`docs/features/meters/METER_MODULE_SPEC.md`, `docs/features/IMAGE_FORMATS.md`, `docs/research/OPENFPGAOS_REVIEW.md`, `docs/research/CODE_ORIGIN_ANALYSIS.md`,
`docs/vendor/`, `.claude/`, `work/`, `release/`, `UniClaudeProxy/` — all belong to other sessions, do not commit
them. Card installs go through `tools/install_dev_core.py` only. `gh release create` without `--repo` targets the
upstream HarpMudd fork parent by default — always pass `--repo alfatreze/Tau-Alpha`.
