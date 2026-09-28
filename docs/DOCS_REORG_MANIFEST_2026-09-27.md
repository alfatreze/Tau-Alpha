# Docs reorganization manifest (2026-09-27)

Reorganized `docs/` (94 loose files at the top level) into subdirectories by purpose. No file was
deleted; every `.md` under `docs/` before this pass (119 total, including pre-existing `archive/`,
`guide/`, `issues/`, `vendor/`) still exists after it (119 total). Nothing was merged away.

## New top-level layout under `docs/`

- `docs/` (root) — standing reference docs and the public-facing pages the top-level `README.md`
  links directly: `README.md`, `CURRENT_STATUS.md`, `ROADMAP.md`, `ROADMAP_PUBLIC.md`, `DECISIONS.md`,
  `AUDIT_TRAIL.md`, `MMIO_ALLOCATION.md`, `PROJECT_REGISTER.md`, `ATTRIBUTIONS.md`, `HOW_IT_WORKS.md`,
  `PERFORMANCE.md`, `DEVELOPERS.md`, `TECHNICAL_SPEC.md`, `TALOS.md`, `HELIOS.md`, `FLAC.md`,
  `EQ_DESIGN.md`, plus `METER_CAPABILITIES.md`/`METER_REGISTRY.md` (kept here, see note below).
- `docs/features/` — feature specs and design docs (21 files: `HELIOS_SPEC.md`, `PHASE_F_SPEC.md`,
  `PHASE_G_SPEC.md`, `THEME_SPEC.md`, `THEME_FILE_FORMAT.md`, `IMAGE_FORMATS.md`,
  `MEDIA_LIBRARY_0.4_SPEC.md`, `ARCHITECTURE_ROADMAP.md`, `CROSS_PROJECT_INTERFACE.md`, etc.)
- `docs/features/meters/` — meter-specific specs (7 files: `METER_MODULE_SPEC.md`,
  `METER_CONFIG_SPEC.md`, `METER_VU_MASTERING_SPEC.md`, `CHLADNI_METER_SPEC.md`,
  `HARDWARE_METER_IDEAS.md`, etc.)
- `docs/memory/` — SDRAM/PSRAM architecture and diagnostics (8 files)
- `docs/research/` — `*_RESEARCH.md`, `*_REVIEW.md`, `*_ANALYSIS.md`, `*_SCOPING.md`, upstream reviews,
  the full-audit doc (12 files)
- `docs/tests/` — `TEST_SCRIPT_*.md`, `TEST_SUITE_SPEC.md`, `LIBRARY_PHASE0_TEST.md`, `QA_PLAN.md`
  (12 files)
- `docs/handoffs/` — every `SESSION_HANDOFF_*.md` (13 files), unchanged content and unchanged
  supersession chain (see CLAUDE.md section 2b, which still names the chain correctly)
- `docs/procedures/` — `CARD_INSTALL_PROCEDURE.md`, `FPGA_BUILD.md`, `JTAG_DEBUG_ACCESS.md`,
  `DEVELOPMENT_WORKFLOW.md` (4 files)
- `docs/archive/`, `docs/guide/`, `docs/issues/`, `docs/vendor/` — pre-existing, reused as-is (3 new
  archive additions, see below)

## Exception: `METER_CAPABILITIES.md` / `METER_REGISTRY.md` stayed at `docs/` root

These are generated outputs of `tools/gen_meters.py`, which hardcodes their output path as
`docs/METER_CAPABILITIES.md` / `docs/METER_REGISTRY.md` (`CAPS_DOC`/`REGISTRY_DOC` constants). The task
instructions say not to touch tools code, so rather than break `make test-host`'s
`gen_meters.py --check` step, I moved these two back to `docs/` root after initially placing them in
`docs/features/meters/`. All other meter docs (hand-written, not generated) are in
`docs/features/meters/`.

## Archived (3 files, to `docs/archive/`)

- `MEDIA_LIBRARY_0.4_BRIEF.md`, `MEDIA_LIBRARY_0.4_PROMPT.md` — pure planning/kickoff docs for the 0.4
  media library, which shipped in v0.4.0 (now superseded by v0.5.0); `MEDIA_LIBRARY_0.4_SPEC.md` (the
  actual spec, still actively referenced, e.g. section 15's 2026-09-23 update) stays in
  `docs/features/`.
- `DIAGNOSTIC_RESULT_LOG.md` — self-marked "Superseded 2026-09-19 (A-091)"; the 0184/0188 mechanism it
  describes never worked and was replaced by the `interact.json` persist channel.

No merges were performed — I read the "supersedes" language across the corpus (CLAUDE.md section 2b,
`PHASE_F_SPEC.md` section 15 vs `HELIOS_SPEC.md`, `FLAC.md`'s internal superseded section, etc.) and in
every case found the older doc already explicitly self-references or is cross-referenced by the newer
one rather than being genuinely duplicated content, so I left those pairs as separate files per the
task's own guidance not to merge docs of different scope.

## References fixed

Used a script to rewrite `docs/OLDNAME.md`-style (repo-root-relative) prose references and
`](OLDNAME.md)`-style bare markdown-link hrefs across every `.md`/`.py`/`.sh`/`.txt` file in the repo,
computing correct relative paths per referencing file's new location. Verified zero remaining
references to any old path anywhere in the repo except `docs/AUDIT_TRAIL.md` (left untouched, per
instructions — its entries are a historical record) and `CLAUDE.md`'s Execution Log (also untouched,
historical). `CLAUDE.md`'s only edit is in section 2b (lines 14-20): updated the path references for
`SESSION_HANDOFF_2026-09-27_V050_AND_06_RTL.md`, `SESSION_HANDOFF_2026-09-26_ALPHA29_MPOLY.md`,
`SESSION_HANDOFF_2026-09-26_ALPHA22.md`, `SESSION_HANDOFF_2026-09-26_ALPHA17.md`, `HELIOS_SPEC.md`,
`PHASE_F_SPEC.md`, `SESSION_HANDOFF_2026-09-25_PHASE_F_CONTINUED.md`,
`SESSION_HANDOFF_2026-09-25_BLIT_TEST_ROOT_CAUSE.md`, `SESSION_HANDOFF_2026-09-24_BLIT_TEST.md`,
`SESSION_HANDOFF_2026-09-24.md`, `SESSION_HANDOFF_2026-09-22_RELEASE_0.4.md`,
`SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md`, `SESSION_HANDOFF_2026-09-21.md`,
`CARD_INSTALL_PROCEDURE.md`, each now pointing at `docs/handoffs/...` or `docs/features/...` or
`docs/procedures/...` as appropriate. No other line in `CLAUDE.md` was changed.

Also incidentally caught and reverted: the reference-fixing script initially also rewrote 10 files
under `toolchain/isolation-v1..v5/` (gitignored, self-contained build-isolation snapshots each with
their own `docs/` copy). Those were out of scope and restored from their own `.tar` snapshots
(`toolchain/isolation-vN-source.tar`), byte-for-byte, before finishing.

## Left alone, flagged for the owner

- Several currently-untracked files from other live sessions in this repo (`docs/CHLADNI_METER_SPEC.md`,
  `docs/CODE_ORIGIN_ANALYSIS.md`, `docs/DECISIONS.md`, `docs/IMAGE_FORMATS.md`,
  `docs/METER_MODULE_SPEC.md`, `docs/OPENFPGAOS_REVIEW.md`, `docs/THEME_SPEC.md`, `docs/vendor/`) were
  moved (filesystem-level only) along with everything else per their content/purpose. `DECISIONS.md`
  was left at `docs/` root since it is a standing register like `AUDIT_TRAIL.md`. None of their content
  was edited beyond reference-path fixes.
- `tools/gen_meters.py --check` and other `make test-host` steps were already failing before this
  reorg (confirmed via `git stash`/`git stash pop`) due to another session's in-progress, uncommitted
  meter-module work (`fw/meter_gen_*.h`, `meters/chladni/meter.json` out of sync). This is unrelated to
  the docs reorg and was left as-is.
- I did not second-guess CLAUDE.md section 2b's own supersession chain for the `SESSION_HANDOFF_*`
  series; all of them stay in `docs/handoffs/` (not archived), matching its "still correct for what it
  covers" framing.
