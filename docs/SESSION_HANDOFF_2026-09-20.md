> **SUPERSEDED by `docs/SESSION_HANDOFF_2026-09-21.md` (state, rules and procedures are current there).** Kept for the history of that day.

# Session handoff, 2026-09-20 (SDRAM CPU window: from root cause to contention pass)

Read this first in a fresh session, then `docs/CURRENT_STATUS.md`, the A-088..A-102
entries in `docs/AUDIT_TRAIL.md`, and `CLAUDE.md` (project rules and execution log).

## Working rules (from CLAUDE.md and the user)
- Append an execution-log line to `CLAUDE.md` after each coding turn; record every hardware
  result in `docs/AUDIT_TRAIL.md` with an evidence label (code-review, host, simulation,
  Quartus, Pocket). Next audit ID: check `grep "^### A-" docs/AUDIT_TRAIL.md` first. A
  **parallel PSRAM session** edits the same files and took A-098 and A-099; IDs used by
  the SDRAM work are A-088..A-097 and A-100..A-102. Re-read shared files before editing.
- Never modify upstream tracking references. Push with plain `git push origin main`.
- **Ask before writing to the SD card.** The card is the volume `Pock`. Procedure every
  time: back up `System/*.bin` catalog indexes (core_viewby_platform, corelist_cache,
  cores_cache, platform_viewby_category, platforms_cache) into the `work/diagnostics/...`
  backup folder, remove the superseded package, copy the new one, delete the five indexes,
  verify SHA-256 on the card against the bundle, `sync`, unmount. Pocket results are
  read back from the card: screenshots in `Memories/Screenshots`, persisted records in
  `Settings/<core>/Interact/_core/interact_persist.json` (written only when the core is
  Quit to the menu).
- VM: `ssh -i ~/.ssh/taualpha_vm_ed25519 -p 2222 taualpha@127.0.0.1`, Quartus at
  `/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh`. Build from a fresh
  ext4 snapshot under `/home/taualpha/tau-local/` staged from
  `/home/taualpha/tau-workspace/tau-alpha` (exclude toolchain, work, .git, .claude,
  docs/vendor, the venv, Quartus db); append the macros to `src/fpga/ap_core.qsf`
  (`TAU_PHASE2_WINDOW=1`, optional `SEED n`), `make check-fpga`, `nohup make fpga`.
  Two builds in parallel fit the 4 vCPUs (about 50-55 min each). A stale idle A-067 session
  in the VM process list is harmless. Do not build `fw/build.sh player-stress` directly
  (it overwrites a staged artifact); the window stress target is `player-stress-window`.

## What was established (all Pocket-verified unless stated)
1. **Result log path (issue 019):** the 0184/0188 slot route never persisted a file. Two
   causes: a wrong bridge base (`0xF8000000` instead of `0xF8002000`, A-088) and an
   unanswered 0188 (A-090). The working channel is 16 `interact.json` persist words
   carrying a 64-byte record in 31-bit encoding (A-091). Decode with
   `tools/decode_tau_diag_log.py --interact [--raw|--soak|--full]`.
2. **SDRAM CPU-window failure (issue 018):** root cause was the Wishbone adapter
   re-accepting each completed beat (released to idle one cycle after ACK while `mp3_soc`
   registers ACK again), so every ACK carried the previous beat's data (A-092/A-093). Fix:
   `S_RELEASE2` in `src/fpga/core/tau_sdram_wb_adapter.sv`; regression
   `make test-rtl-sdram-wb-return` (fails on the old adapter). 183-check matrix: 0 failures
   on three cold boots.
3. **Cost and coverage:** an uncached window access is about 48-50 cycles (0.8 us at
   60 MHz), worst single access 360-373 cycles; 30 min soak, 264M checks, 0 failures (A-097);
   address lines 2..25 over 1-64 MiB and 3 x 1 MiB CRC under drawing, 0 failures (A-100).
4. **Product-candidate RTL (A-101):** `TAU_PHASE2_WINDOW=1` only, no probes, four seeds all
   closed timing, 300/308 RAM blocks, hold +0.105..+0.123 ns, setup +0.39..+0.52 ns
   (probe build had +1.106). **Seed 4 selected** (raw RBF sha256
   `ed34a6bcb8f0fac7aa1b7a20b4afed33ae8c7cc2e1ed6672b32d31c0c2ee90eb`, copies in
   `work/diagnostics/sdram-product-a101/s1..s4/`).
5. **Contention (A-102):** `fw/build.sh player-stress-window` (Select+X cycles stress off/
   levels 1-3; HUD `E L M S R K P`), packaged with
   `tools/package_sdram_stress.py --window --rbf ... --rbf-sha256 ...`. With the seed-4 RBF,
   tracks 1-4 at levels 0-3: 0 mismatches, 0 late underruns, draw stall 0, worst access
   366-373 cycles, achieved up to 22.6k ops/s (CPU-idle-limited, about 6% CPU).
6. **Cold-buffer economics (A-095/A-096):** the playlist arrays (`pl_text`+`pl_off`+
   `pl_order`, 13,312 B) cost tens of ms per playlist load through the window; `art_acc`
   (11 KiB) would add about 0.8-0.9 s to a full cover decode. A minimal 7-row settings
   menu costs about 3.0 KiB and misses the link by 608 B today; the 13 KiB playlist move
   alone would leave about 12.7 KiB spare, so the old 24 KiB target is not supported.
   A hardware scaling blit engine (design sketch in the session) would remove `art_acc`
   and its maps from CPU RAM without the cached window; no spec was written.

## Current state on the Pocket SD card (volume `Pock`)
- Core `alfatreze.TAU_SDRAM_WSTRESS` / platform `tau_sdram_wst`: bit-reversed RBF
  `2e9aaf0e66cebcb71d4f83e59ea2cd8e91b4c6d9e52a0ed430df2c158aaa3cd7` (A-101 seed 4),
  ROM `55384a5596eac32f189eaba073d31f25d6c0236c9e77202553fa7484cb9bc20a`
  (burst pump, E/L underrun split), test music under `Assets/tau_sdram_wst/common/`
  (`work/test-music/tau_sdram_wst/common/`, README there). Older cores (normal TAU, Phase 1
  `TAU_SDRAM_STRESS`, others) are untouched.
- Bundle: `work/diagnostics/sdram-stress-window/pocket` (all `work/` is untracked).

## Open gates and next steps (in order)
1. Repeat a short A-102 run from a cold boot; add a deliberate seek/pause/cover-change
   sequence under stress; optionally a saturating burst level (ambiguity: CPU starvation).
2. Media: FLAC stays labelled unverified by project decision.
3. First real data move: playlist buffers behind the uncached alias (linker NOLOAD section,
   explicit init after SDRAM init, window preflight as in the stress pump, fail-safe if the
   window errors, playback regression matrix on hardware). Product build must enable
   `TAU_PHASE2_WINDOW` without probe macros (the macro-off decode still aliases `0x4000_0000`
   into BRAM).
4. Decide on the scaling blit engine (offloads `art_acc`) versus a cached-window adapter
   (needed only for running code from SDRAM, Phase 3). Write a spec before RTL.
5. Unexplained/minor: 4 KiB-stride writes measured faster than sequential (A-094);
   worst window access 362 with seed 1 vs 373 with seed 4; setup margin is thinner than the
   probe build; temperature untested.
6. Real settings menu size report against the link gap (2.4 KiB slack at baseline).

## Skill and knowledge base
The project skill `analogue-pocket-dev` (symlinked from `.claude/skills/`, real files in
`~/.claude/skills/analogue-pocket-dev`) is untracked on purpose (it holds verbatim third-
party doc snapshots); so is `docs/vendor/` (a PSRAM datasheet PDF). Hardware-validated Tau
entries: KB-022 (0188 unanswered), KB-023 (datatable base 0xF8002000), KB-024 (adapter
must not re-accept a completed beat), KB-025 (interact.json persist record), and notes on
KB-001/004/007/008/011/021. Pending KB text (also the source of truth here until it is
written to the skill): **KB-030** hardware-validated: a bridged CPU window costs about 50
cycles per 32-bit access at 60 MHz, worst about 6 us, passed soak/coverage/contention at
CL2/100 MHz (A-094/A-097/A-100/A-102). **KB-031**: the Pocket screenshot key combination is
also seen by the core (stopped Tau's playback), so restart underruns must be classified
separately (frames since flush). **Damage to fix:** a Tau session wrote its text into
KB-026 (unloader must wait for read_avail, K3V GBA audit) and KB-027 (SDRAM capture phase
is a dial, plasticbugs METHODOLOGY 5.20) by number, overwriting those entries' bodies (both
created by the weekly refresh) and promoting KB-026 to hardware-validated in error. Reset
both to community-reported and re-derive the bodies from their cited sources; KB-030/031
exist as empty stubs in the same folder.
