# Session handoff, 2026-09-21 (SDRAM window shipped, settings menu, v0.2.2)

Supersedes `docs/SESSION_HANDOFF_2026-09-20.md`. Read in this order in a fresh session: this file, `CLAUDE.md` (rules and
execution log), `docs/CURRENT_STATUS.md`, then the `docs/AUDIT_TRAIL.md` entries you need (index in section 5). Do not re-derive what they record.

## 1. State in three lines
- **Shipped and installed:** Tau **v0.2.2** is the base `TAU` core on the Pocket card: the probe-free SDRAM-window RBF (seed 2), firmware with the in-app settings menu,
  Info page, full-screen playlist, the playlist buffers in SDRAM and greyscale meter previews. `dist/` holds exactly what is on the card.
- **SDRAM CPU window: done and validated on Pocket** (A-093 root cause and fix, A-097/A-128 soak, A-100/A-128 whole-window coverage, A-102/A-129 contention with playback,
  A-105..A-108 playlist, A-126 tests). Remaining SDRAM ideas (artwork buffers, cached window, code in SDRAM) are optional and unspecified.
- **Parallel PSRAM session** owns the B-NNN audit series, `docs/PSRAM_*.md`, `docs/MMIO_ALLOCATION.md` and uncommitted RTL/sim edits (`core_game.vh`, `core_top.v`, `mp3_soc.v`, `ap_core.qsf`,
  `tau_psram_*.sv`, `sim/`). Never stage those; see section 7 before touching RTL.

## 2. Working rules
- Append an execution-log line to `CLAUDE.md` after each coding turn; log every hardware result in `docs/AUDIT_TRAIL.md` with an evidence label (code-review, host, simulation, Quartus, Pocket).
  Audit IDs: **A-NNN** for SDRAM/UI/firmware (next free: check `grep '^### A-' docs/AUDIT_TRAIL.md`, last used A-137), **B-NNN** for PSRAM. Re-read shared files before editing them; several sessions append to them.
- Never modify upstream tracking references; push with plain `git push origin main`. Commit only your own files (`git add <file>`; for a shared file with foreign hunks use a filtered patch and
  `git apply --cached`, as done for `fw/build.sh`). Do not commit the PSRAM session's files.
- **Ask before writing to the SD card.** Volume `Pock`. Every install: back up the files being replaced (and `System/*.bin` catalog indexes: `core_viewby_platform`, `corelist_cache`, `cores_cache`,
  `platform_viewby_category`, `platforms_cache`) into `work/diagnostics/<topic>/...`, copy, delete the five indexes, verify SHA-256 of every file against the bundle, `sync`, `diskutil eject`.
  Quote every path (the project path has spaces; an unquoted `$P` once aborted an install half-way). The card can drop off the Mac mid-copy: check state before retrying. Copy, never move, user media.
  Pocket results are read back from the card: screenshots in `Memories/Screenshots` (the Pocket clock runs behind the Mac's), `Settings/<core>/Interact/_core/interact_persist.json` (written only on Quit).
- Screenshot combination stops playback (KB-031). A HUD `E` counter is *early* underruns after a restart, not errors; failures print `FAIL n`. Read `E L M S K` as: early/late underruns, worst window access
  (cycles), draw stall (ms), achieved k ops/s.
- Use the `analogue-pocket-dev` skill and its knowledge base (`~/.claude/skills/analogue-pocket-dev`, symlinked from `.claude/skills/`) for Pocket/APF questions; local entries live in git-ignored `local-entries/`.

## 3. Build tiers, targets, budgets (`fw/build.sh <target>`)
| Target | Flags | Output | Heap gap (floor) |
|---|---|---|---|
| `release` | settings + Info + SDRAM playlist + meter previews | `dist/` (the shipped product) | 7,824 B (6,144) |
| `player-settings` | same flags | `work/diagnostics/settings-ui/` | same |
| `player-diagnostic` (the **Diagnostic Build**) | release flags minus previews, plus tests page, stress pump, soak | `work/diagnostics/diagnostic-build/` | 4,224 B (4,096) |
| `player` | legacy product (no settings, no SDRAM playlist) | `dist/` (overwrites it!) | large |
| `player-sdram-pl` / `player-sdram-pl-fault` | SDRAM playlist only / with a deliberately failing preflight | `work/diagnostics/playlist-sdram*/` | n/a |
| `sdram-cpu-soak`, `sdram-cpu-full`, `player-stress-window`, ... | old standalone probes (removed from the card; results archived) | `work/diagnostics/...` | n/a |
Building `player` or `release` writes the tracked `dist/Assets/tau/common/tau.rom`; after experiments restore it with `git checkout -- dist/Assets/tau/common/tau.rom` or rebuild `release` last.
The release check in `build.sh` requires `APP_VER` (player.c), `dist/Cores/alfatreze.TAU/core.json` and README "Current version" to agree. Package the RBF with
`python3 package.py --rbf <raw> --rbf-sha256 <hash>` (audited); test bundles with `tools/package_sdram_stress.py --playlist-sdram --settings|--diagnostic --rbf ... --rbf-sha256 ...`.
Flags: `TAU_SETTINGS_UI`, `TAU_PL_SDRAM` (needs a window RBF), `TAU_DIAG_INFO`, `TAU_DIAG_TESTS`, `TAU_METER_THUMBS`, `TAU_SDRAM_STRESS[_WINDOW]`, `TAU_STRESS_HUD`.
Memory is the constraint: the 256 KiB block RAM holds image + BSS + heap gap + 24 KiB DMA ring + 4 KiB tag + 16 KiB stack; the release is at 89% of the image budget. `arena` (24,576 B decoder) and `pcm`
must stay in BRAM. Cold data can go behind the uncached window (about 50 cycles per access, worst about 370) only after the window is proven at runtime (fail-safe = feature off).

## 4. RBF, VM and gates
- Product RBF: `work/diagnostics/sdram-probefree-a114/s2/ap_core.rbf` (raw SHA-256 `551e5a76...718b`; bit-reversed `cb15310a...a3c9`), macros `TAU_PHASE2_WINDOW=1` only, `SEED 2`, 300/308 RAM blocks,
  setup +0.664 ns, hold +0.124 ns. `TAU_PHASE2_PROBE` (probe, top-edge red/green overlay, controller debug taps) is a separate opt-in since A-113.
- RTL interlock: `EXPECT_VERSION` in firmware must equal `CORE_VERSION` in RTL (now `0x4D503317`, rev 23). Any RTL register or behaviour change bumps both, or the core paints the AAAA/5555 screen.
- VM: `ssh -i ~/.ssh/taualpha_vm_ed25519 -p 2222 taualpha@127.0.0.1`, Quartus `/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh`. Stage a fresh copy under `/home/taualpha/tau-local/`
  (see `probefree-a114-s1-20260920` for the recipe: tree copy without db/incremental_db/output_files/logs, `src/fpga`+`Makefile` from the checkout overlaid, macros and `SEED n` appended to `ap_core.qsf`,
  `make check-fpga`, `nohup make fpga`). 4 vCPUs; two parallel fits took about 52 min each, one no-window fit alone took 2 h 20 min. Stage from a **clean** checkout or state exactly which uncommitted RTL you used.
- Gates after any RTL change (all now inside the Diagnostic Build): Diagnostics > Tests > WINDOW TEST (`PASS 89`), READ/WRITE CYCLES (about 48/56/330), PLAYLIST CHECK; Stress > Level R1-R3 with music, Status page
  failures 0, worst access 365-375, late underruns 0; a 30-minute Soak (PASS); Playlist check again. The standalone soak/coverage/stress cores were retired after passing (backups in `work/diagnostics/gates-a128/`).

## 5. Evidence index
A-088..A-091 result-log channel (interact.json persist); A-092/A-093 adapter double-issue root cause and fix; A-094 cost; A-097 soak; A-100 coverage; A-101/A-114 product-candidate fits;
A-102/A-129 contention; A-105..A-108 playlist in SDRAM and large lists; A-109..A-112 fail-safe proofs and the probe-overlay finding; A-113 macro split; A-115..A-122 settings, overlays, Info, overlap fix, 1.2x gesture removal;
A-124..A-129 gates on the seed-2 RBF; A-125..A-127 Diagnostic Build Phases 2-3; A-130/A-131/A-135/A-137 releases v0.2.0/0.2.1/0.2.2; A-132..A-136 meter previews and speeds; card cleanups in A-125, A-128, A-137 addenda.

## 6. Facts worth knowing (not obvious from the code)
- The SDRAM read bug was the Wishbone adapter accepting a finished beat twice (KB-024); regression `make test-rtl-sdram-wb-return`. Any new bus adapter (cached window, PSRAM window) must pass an equivalent test.
- The persisted-settings register file has 16 words (`SW_N` = 12 used); a 16-word diagnostic record does not fit in the product, so results go on screen or via a diagnostic core's own `interact.json`.
- Speed is `speed_idx` over a ten-entry rational table but the menu offers 0.85-1.20x: 1.25x micro-stutters and 1.30x+ underrun (decoder needs about 45.7 of 60 MHz at 1.0x on 320 kbps); above 48 kHz / file rate the I2S
  zero-order hold also drops samples. A resampler / pitch correction is the real fix. The list is `SET_SPEED_SHOWN` in `fw/settingsui.inc`.
- Full-screen overlays hold every drawing primitive (`FB_HELD()` in `fb_rect`, `fb_copy_span`, `fb_char`); overlay code must paint through `ov_draw`. Closing repaints via `pl_ui_restore`.
- Meter previews: `tools/gen_meter_thumbs.py` -> `fw/meter_thumbs.h` (8-colour palette per image, 3-bit index + 5-bit run byte). Inputs are the greyscale Figma exports in `assets/ui/meter/`.
- Snapshot fixtures (`tools/ui_snapshot_renderer.py`, `make visual-review`) read geometry and labels from firmware sources; every new UI state needs a named fixture (AGENTS.md).

## 7. For the PSRAM work (P4 and later)
1. **Do not mix your RTL edits with a shipping build.** The working tree has uncommitted RTL (`core_game.vh`, `core_top.v`, `mp3_soc.v`, `ap_core.qsf`). A release RBF must come from committed, macro-explicit sources
   (`TAU_PHASE2_WINDOW` only). Anything PSRAM must sit under its own macro, off by default, exactly as the SDRAM probe lesson (A-112/A-113): the probe overlay shipped inside a "probe-free" product because the window macro also enabled it.
2. **Any RTL change means a new fit and a rerun of the gates.** Placement and timing move (setup/hold differ by hundreds of ps per seed); use several seeds, keep the seed-2 RBF as the fallback product.
3. **Window adapter:** reuse the KB-024 lesson and regression, make the address decode explicit (the macro-off decode aliases `0x4000_0000` into BRAM), keep the first window uncached, and prove the window at runtime before any store
   (mailbox write, window read; failure = feature off). The SDRAM playlist code (`pl_sdram_prove`, `PL_ERR_SDRAM`) and the fault-injection variant are the pattern.
4. **Interlock and MMIO:** register changes need `CORE_VERSION`/`EXPECT_VERSION` bumped together; your `docs/MMIO_ALLOCATION.md` note that `mp3_soc` decodes only 8 offset bits (0x100+ aliases) is right; the persist register file is full.
5. **Where tests go:** add PSRAM checks to the Diagnostic Build tests page rather than new standalone cores. Its heap gap is only 4,224 B (floor 4,096): budget code size first, or gate the PSRAM tests behind their own macro
   or a separate diagnostic build like `psram-diag`. Card cores of yours (`TAU_PSRAM`) are separate from `TAU`/`TAU_DIAGNOSTIC`.
6. **Value of PSRAM for memory:** the release has 7.8 KiB of heap gap; candidates to move behind a window are `art_acc` (11 KiB) and its maps (2 KiB), which cost about 0.8-0.9 s per cover decode through the SDRAM window (A-095). Compare PSRAM's
   access cost to SDRAM's (about 0.8 us) before choosing; a hardware scaling blit engine is the alternative that removes `art_acc` without a slower decode.
7. **Cartridge-port safety:** never enable both chip selects of a PSRAM chip; keep the cartridge translator tie-offs from the template. *(Corrected 2026-09-21 by the PSRAM session: PSRAM is on its own `cram0_*`/`cram1_*` pins, not on the cartridge pins; the Analogue docs list them separately, and the PSRAM design drives only `cram*` pins.)*
8. **Housekeeping:** your uncommitted files are safe in the tree, but before pushing coordinate what is staged; commit with explicit `git add` paths. Status of the PSRAM items (updated by the PSRAM session, 2026-09-21; the PSRAM RTL, firmware and tests are now committed, `25ae8b6` and `82f5f90`, and `25ae8b6` also repairs `fw/build.sh`, which had not parsed since `af28127`): the margin-experiment builds were installed and measured (B-014, B-015); P4 is built and simulated with two Quartus builds running (B-018). See `docs/SESSION_HANDOFF_PSRAM_2026-09-21.md`.

## 8. Open items (SDRAM/UI side)
Temperature test; a saturating stress run on other tracks; FLAC (labelled unverified by decision); Diagnostic Build save report (only 4 free persist words) and SD read speed test; resampler and pitch correction (then raise
`SET_SPEED_SHOWN`); artwork buffers (blit engine versus cached window; write a spec first); real reset/Advanced pages and EQ curve preview in settings; CAS latency / clock phase (KB-021, OQ-6); the platform name on the card is
`TAU` + superscript alpha while `dist/` has plain `TAU`; `docs/ARCHITECTURE_ROADMAP.md` and PSRAM plan edits are the other session's.
