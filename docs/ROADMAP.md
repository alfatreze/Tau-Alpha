# Roadmap: the one ordered list of what is next

**Status: DRAFT, 2026-09-26.** Proposed by Claude from the docs of every session; **the order is the owner's to set.** Items marked *(owner)* need a decision
before work starts. This is the only document that says what comes next. Every other plan or spec is a design reference for one item here
and must not carry its own "next" list (see section 6).

Current facts (card, release, open defects): `docs/CURRENT_STATUS.md`. History and evidence: `docs/AUDIT_TRAIL.md`.

## 1. Now (ready, no owner decision needed)

| # | Item | Status | Gate / next action | Reference |
|---|---|---|---|---|
| **0** | **Theme system, all 4 steps: the 0.5 gate** (owner, 2026-09-26: finish it, do not ship 0.5 with it half done) | Design only (`THEME_SPEC`), nothing built | 0a role table with today's look as the default theme, no visible change; 0b Dark/Light switch plus a second built-in theme, with the text gamma check (`tools/gen_text_gamma.py --check`); 0c VU ladder, meters and thumbnails onto roles and CLUT-from-roles; 0d theme file (`TTHM`, new data slot) loader plus Omega exporter. Theme index and polarity are **session-only** until persist is widened (owner decision, no RTL change). Each step ends with a host test and a card run | `THEME_SPEC`, `HELIOS_SPEC` 7.4 |
| 1 | Close alpha.30 | Hardware-confirmed good (404,712 slots, 0 BAD, 1.75x clean) | Optional like-for-like unit-off run; find the speed where audio breaks; HarpMudd comparison later | B-308, B-309 |
| 2 | Batched drawing for the hardware wave/scope path | Compiled out since B-302 (256 columns cost ~21x a normal meter) | Design batching (few `fb_rect`/`OP_BAR`-style commands per frame), re-enable, prove with Meter Sweep | B-298..B-302, `HARDWARE_METER_IDEAS` #6 |
| 3 | Remove software paths made redundant by hardware | Spectrum cascade already removed | After 2: delete software level/scope in `meters_feed` and `wviz_scope_tick` | ALPHA22 handoff item 3 |
| 4 | Meter module M1 to M2 | M0 done (B-294) | Not before 0.5 (owner, 2026-09-26: theme first); M2 depends on theme roles (D-M05) | `METER_MODULE_SPEC` section 17 |

## 2. Next (needs an owner call first)

| # | Item | Why it needs a decision | Reference |
|---|---|---|---|
| 5 | **Release the MP3 window unit** (make `POLY_FW=1` and the poly bitstream the release build, cut v0.5.0, **after item 0**) | The bitstream leaves only 4 free M10K blocks (304/308). Decide before spending more block RAM on anything. *(owner)* | `DECISIONS` (new), B-308/B-309 |
| 6 | **192 KB RAM shrink** | Old premise "9 blocks free, so defer" is now 4 free. It frees 64 blocks but firmware is 8.5 KB short of linking. Re-decide: do it now, or keep deferring. *(owner)* | `RAM_SHRINK_192K_PLAN`, `PHASE_F_SPEC` section 4 |
| 7 | Helios H1: convert regions to beam-gated drawing, then the now-playing redesign | H1 core is inert and needs a first real region; the now-playing redesign (Figma node 194:1630) was explicitly parked for its own session; do it after item 0 so it is written against roles, not fixed colours | `HELIOS_SPEC` section 9, ALPHA29 handoff part 2.9 |
| 8 | Persist widening in RTL (theme index and polarity, meter Configure settings) | Needs a fit; theme ships session-only until then | `THEME_SPEC` section 5, `METER_CONFIG_SPEC` |
| 9 | Cover art: TIM1 firmware reader as the default (palette 256 at 128 px) | Offline tools done; reader exists behind `TAU_ART_TIMG`, off by default. Container is still unfrozen (D-I05). Decide when to freeze. *(owner)* | `IMAGE_FORMATS`, `DECISIONS` D-I01..D-I05, `COVER_TIMG_READER` |
| 10 | Audio kernels beyond the MP3 window: FLAC bit reader first | Was ordered after the blit engine and RAM shrink (owner, 2026-09-22); the first is done. Re-confirm the order. *(owner)* | `PHASE_F_SPEC` section 14 row 7, B-086..B-098 |

## 3. Later and parked

- Meter modules M3 to M6 (trace recorder, `tau-assets.bin`, Chladni as first new module, legacy meters); public preset gallery (parked, D-M12).
- Hardware meter helpers (beat detector, L/R correlation, Chladni field evaluator): saved ideas, none started (`HARDWARE_METER_IDEAS`).
- Blit ideas held on purpose: B5 alpha blend (timing never closed, B-243), B13 gradient bar (cost bigger than scoped, B-241), B19 flip flags, B10 RLE blit, H2 double buffering.
- Library items waiting for RAM (`MEDIA_LIBRARY_0.4_SPEC` section 14) and the migration-across-versions question (section 15).
- Firmware modularization (`FIRMWARE_MODULARIZATION_PLAN`, parked until key features land).
- Winamp on-device Configure page: parked, may be replaced by Tau Omega authoring; do not extend.
- Tracker/MOD support, CJK/UTF-8 fonts, Chladni presets beyond Lattice/Shimmer, per-channel waveform presets.
- Resolution 720: **last** by owner decision, everything earlier must stay resolution-agnostic.
- Defects: `Track changes` Check failure; boot-restore mismatch (`docs/issues/021`, waits for the UI redesign); heap-peak instrumentation for Check;
  BUG-001 accented names (`docs/issues/001`).

## 4. Standing decisions (still in force; ask before reversing)

Recorded in the docs named, not yet in one register (moving them into `docs/DECISIONS.md` is part of the consolidation):
no FFT (hardware octave/spectrum bank instead); no 3D GPU (2.5D on the 2D engine); licence stays MIT (GPL RTL may be studied, never copied);
720 is last; main RAM shrink is gated on meters going cold (met); Talos = blit engine, Helios = UI library over it; two seeds for every timing
claim; hot-to-cold calls need a `COLD_READY()` gate; card installs go through `tools/install_dev_core.py`; Tau and Tau Omega never share literal
files. Meter and image decisions: `docs/DECISIONS.md` D-M01..D-M13, D-I01..D-I05.

## 5. Sibling project (Tau Omega)

Owns card management, Check/QR decode, screenshots, presets authoring. Its own order is in `../Tau Omega/docs/STATUS_HANDOFF.md`. Items here that
create work there: TIM1 container freeze (item 9), meter presets and `tau-assets.bin` (item 4, M4), new persist ids or data slots (log in
`docs/CROSS_PROJECT_INTERFACE.md`).

## 6. Rules for keeping this true (several sessions and accounts write here)

1. **Only this file orders work.** A spec may say "status: built / not built" for its own design, never "next".
2. **One editor for this file** (the session doing the consolidation). Other sessions add ideas at the bottom under "Proposed" with their name and date;
   the owner accepts or rejects them.
3. Every session logs to `docs/AUDIT_TRAIL.md` (own id range, check the last id first) and updates the single latest handoff, not a new parallel one.
4. When a fact changes (card contents, release, an open defect), update `docs/CURRENT_STATUS.md` in the same turn.
5. Commit only your own files (hunk-stage mixed files); see `CLAUDE.md` section 3.

## Proposed (other sessions add here)

*(empty)*
