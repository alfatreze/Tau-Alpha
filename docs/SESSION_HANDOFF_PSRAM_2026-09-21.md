# Session handoff, PSRAM workstream, 2026-09-21 (P0-P3 done, margin measured, P4 built in simulation, two Quartus builds running)

Read in this order in a fresh session: this file, `CLAUDE.md` (project rules and the execution log), `docs/PSRAM_IMPLEMENTATION_PLAN.md` (plan, corrections, section 8 = impact of the SDRAM work),
then the audit entries B-011 to B-020 in `docs/AUDIT_TRAIL.md`. The SDRAM/UI side has its own handoff, `docs/SESSION_HANDOFF_2026-09-21.md` (shipped product v0.2.2); read its sections 1, 2, 4 and 7 too.
**Use the `analogue-pocket-dev` skill for anything about the Pocket, APF, the core template, PSRAM/SDRAM/SRAM hardware, packaging or the card.** Do not re-derive what is recorded.

## 1. State in six lines
- **PSRAM works on the Pocket** (one unit, room temperature): asynchronous mode on power-up defaults, no register writes, all four dies (2 chips x 2 dies, 32 MiB), 1 MiB hash-verified fill per die, guard word refused. P3 (B-009..B-011): 10 of 10 starts PASS.
- **Read-timing margin measured** (B-012..B-015): reads pass from sample index 7, fail at 6 (partially); the shipped default index 9 has two clocks (33 ns) of margin. Recommendation: keep 9.
- **P4 (uncached CPU window at `0xA400_0000..0xA5FF_FFFF`) is built and simulated** (B-016, B-017): decode, bus path, owner mux with the mailbox, guard ACK, firmware modes L (window suite) and R (soak), PSW1 record.
  `make test` passes (27 suites), including the real VexRiscv running the ROM against the RTL with an injected fault caught.
- **Two Quartus builds are running on the VM** (B-018, launched 2026-09-21 02:12 WEST, both seed 2; ETA about 03:10-03:20 WEST): (a) the P4 diagnostic, (b) the product-configuration regression build.
- **Nothing for P4 is installed on the card.** The card currently has the B-008 `TAU_PSRAM` core and the two margin cores `TAU_PSRAM_T7`/`_T6` (B-014).
- **PSRAM RTL, firmware, tests and tools are committed** (`25ae8b6` repairs `fw/build.sh`, `82f5f90` is the PSRAM code; the documentation commit follows). Only `docs/vendor/` (datasheet PDF) is deliberately left out. The P4 builds were staged from exactly this RTL (manifest `b21a9c0a...`).

## 2. What to do first: the two builds (B-018)
| Build | Stage (VM) | Config appended to `src/fpga/ap_core.qsf` | Log |
|---|---|---|---|
| (a) P4 diagnostic | `/home/taualpha/tau-local/psram-p4-b018-diag-s2-20260921` | `tools/psram_window_qsf_append.txt` (`TAU_PHASE2_WINDOW`, `TAU_PSRAM_PROBE`, `TAU_PSRAM_WINDOW`, 22 `FAST_*` lines) + `SEED 2` | `quartus-b018-diag.log` |
| (b) product regression | `/home/taualpha/tau-local/psram-p4-b018-prod-s2-20260921` | `TAU_PHASE2_WINDOW=1` + `SEED 2` only (the A-114 appendix) | `quartus-b018-prod.log` |
Sources are the exact working tree; RTL manifest SHA-256 `b21a9c0a73379e415da850d936403c1365c3a5958a37475982d0028f5f92ea7c` (45 `src/` files; B-018 lists the key file prefixes). Synthesis passed for both at 02:21 (0 errors):
(a) has the PSRAM bus wrapper (instance `tau_psram_bus:g_phase2_window.g_psram.u_psram_bus`) and 8,044 registers; (b) has no PSRAM logic and 7,470 registers.

**When the owner says the builds are done** (the owner will ask; do not poll in a loop):
1. Extract results with the reusable tool. On the VM: `ssh -i ~/.ssh/taualpha_vm_ed25519 -p 2222 taualpha@127.0.0.1 'python3 - <output_files_dir> --log <log> [--expect-rbf-sha256 HEX]' < tools/quartus_fit_summary.py`
   (or copy `output_files/` down and run it locally). It prints resources, worst setup/hold, negative-slack count, the CRAM pad-register packing table, the 176279 packing warnings and the RBF SHA-256.
2. **Build (b): compare the raw RBF with the shipped product RBF** `551e5a7600fbf5c5e93a3d1f513a4b71c5603e3d890d26fa72dfcbfd4343718b` (`--expect-rbf-sha256`). Bit-identical proves the PSRAM changes are inert in the product configuration.
   If different: compare timing and resources with A-114 seed 2 (setup +0.664, hold +0.124, RAM 300/308) and rerun the Diagnostic Build gates before any of the shared files reach a release (SDRAM handoff section 4). The A-114 seed-2 RBF stays the fallback product.
3. **Build (a) acceptance:** "Successful", 0 errors, 0 negative slack, RAM blocks 300/308, DQ and control registers packed as in B-008 **and now `dq_out` / `cram_a` too** (`Output Register` yes for `cram*_dq` and `cram*_a`; the two chips' `clk`/`cre` pins are constants and read `no`).
   Seed 1 only if seed 2 misses timing. Previous PSRAM builds closed at about +0.5 to +1.0 ns setup and +0.07 to +0.12 ns hold.
4. Copy the RBF and reports to `work/diagnostics/psram-diag/` (naming as before: `fpga-b018-*`, `reports-b018-*`), verify the hash against the VM, package with hash lock:
   `python3 tools/package_psram_diagnostic.py --rbf <raw> --rbf-sha256 <hash> --rom work/diagnostics/psram-diag/tau.rom --rom-sha256 <hash> --variant w --output work/diagnostics/psram-diag/pocket-w`
   Rebuild the ROM first (`bash fw/build.sh psram-diag`); at the time of writing it is 14,056 B, SHA-256 `042bfa1faee749008710b3e1472f6ca2db35bce92245f89a6d214f51c1b0ea90`. Identity `alfatreze.TAU_PSRAM_W` / platform `tau_psram_w`. Check `rbf_r` equals the bit-reversed RBF.
5. Record everything as the next B-NNN entry (check `grep '^### B-' docs/AUDIT_TRAIL.md`; last used B-020) and a `CLAUDE.md` line. **Then ask before installing.**

## 3. The P4 Pocket run (after install; owner approval needed for the card write)
Install additively (procedure in section 5). Owner runs, with Pocket screenshots (they read from the card: `Memories/Screenshots`, the Pocket clock runs behind the Mac's; see B-015 for how the 16 screenshots were mapped):
- Start the core; the automatic mailbox run must PASS as in P3. Press **L** (window suite) and screenshot; press **R** (soak) and let it run **30 minutes** (each pass about seconds; any mode key stops it), screenshot near the end and after stopping; Quit before pulling the card
  (only the last Quit writes `interact_persist.json`).
- **Predictions (recorded in B-016, before any hardware):** window suite `PASS`, `CHECKS 1049216 FAIL 0`, `TO 0 CE 0 GHIT 1 X 0 GD OK`, `WIN OPS 2098432` for the first window run (each soak pass adds the same), CRC chain `0xAF0AA680`, per-die CRCs as in P3
  (`833D7446`, `4BF5A918`, `ECE394E1`, `8B21EF3F`), access cost about `RD 32/32 WR 26/26` cycles (avg/max, no spread). SDRAM baseline: read 48/56/330, write 31/38/313 (min/avg/max, A-126).
- Decode the saved record: `python3 tools/decode_tau_diag_log.py --interact --psram-window <interact_persist.json>` (the mailbox records use `--psram`). A failure reading: mailbox passes but window fails = bus wrapper or CPU return path (A-092 pattern), not the chip.
- **Then the SDRAM-unchanged gate:** the Diagnostic Build ROM on the P4 RBF (`tools/package_sdram_stress.py --playlist-sdram --diagnostic --rbf <raw> --rbf-sha256 <hash>`, replaces the `TAU_DIAGNOSTIC` core on the card, back it up first): Tests > WINDOW TEST `PASS 89`,
  READ/WRITE CYCLES about 48/56/330 and 31/38/313, PLAYLIST CHECK, Stress R1-R3 with music (0 **late** underruns; `E` is early underruns after a restart, not errors), 30-minute Soak PASS. See SDRAM handoff section 4.

## 4. Working rules (learned in this workstream)
- **Audit series B-NNN for PSRAM** (SDRAM/UI use A-NNN). Append an execution-log line to `CLAUDE.md` after each turn that changes something; record hardware results with an evidence label and **write predictions before running**.
- **The owner approves each VM launch and each card write explicitly** ("go ahead and launch", "install it"). The project workflow requires human approval before submitting a remote build. Stage, verify and prepare freely; stop before launching or installing and ask.
- **Card procedure** (volume `Pock`, quote every path): back up the files being replaced and the five `System/*.bin` catalog indexes (`core_viewby_platform`, `corelist_cache`, `cores_cache`, `platform_viewby_category`, `platforms_cache`) to `work/diagnostics/...`, verify the backup,
  copy with `cp -RX`, delete `._*` files you created in the new paths, delete the five indexes, verify SHA-256 of every file against the bundle, `sync`, then eject. Install PSRAM cores additively; never remove other cores (the other Tau cores were removed by the owner on purpose after releases 2.0-2.2).
- **Stage builds from the working tree and prove it:** hash every `src/` file locally and on the VM and diff (only `ap_core.qsf` may differ). VM: `ssh -i ~/.ssh/taualpha_vm_ed25519 -p 2222 taualpha@127.0.0.1`, Quartus `/home/taualpha/intelFPGA_lite/25.1std/quartus/bin/quartus_sh`; stream a file list with `tar ... | ssh ... tar -x`, exclude
  `work`, `toolchain`, `.git`, `.claude`, `docs/vendor`, Quartus `db`; `make check-fpga`; `setsid nohup make fpga > quartus-<name>.log`. Two parallel fits take about 52-60 min on 4 vCPUs.
- **Do not commit the SDRAM side's files or mix commits.** Commit only your own files with explicit `git add <path>`; for a shared file that contains foreign hunks use a filtered patch (`git apply --cached`). The owner asks for commits explicitly.
- **Shipping safety:** P4 edits shared, shipping RTL (`mp3_soc.v`, `tau_sdram_addr_decode.sv`, `core_game.vh`, `core_top.v`, `ap_core.qsf`). Everything PSRAM sits behind macros that are off by default; the regression build (b) is the proof. `CORE_VERSION` (`0x4D503317`) is deliberately not bumped until the product RTL itself gains PSRAM (P5).
- Skill rules: consult `INDEX.md` of the knowledge base, cite entry ids, promote entries only with evidence, log learnings. My local (git-ignored) entries: **KB-029** (AS1C8M16PL datasheet timing, docs-verified), **KB-036** (Quartus I/O-cell register packing rules),
  **KB-037** (Pocket PSRAM works in async mode on defaults, with the measured read margin; hardware-validated).

## 5. Map of artifacts
| Area | Files |
|---|---|
| Plans and contracts | `docs/PSRAM_EVALUATION_PLAN.md` (original contract P0-P5), `docs/PSRAM_IMPLEMENTATION_PLAN.md` (current plan; sections 7 and 8 = later considerations), `docs/PSRAM_TIMING_CONTRACT.md` (datasheet-checked timings, measured margin), `docs/MMIO_ALLOCATION.md` (register table, PSRAM window map), `docs/vendor/DOC012312972.pdf` (datasheet, not committed) |
| RTL | `src/fpga/core/tau_psram_async.sv` (controller), `tau_psram_bus.sv` (Wishbone wrapper), `tau_psram_probe.sv` (mailbox, owner mux, pins), `tau_sdram_addr_decode.sv` (+PSRAM window), `mp3_soc.v` (`xm_*`, `PSRAM_WINDOW_ENABLE`), `core_game.vh`, `core_top.v`, `src/fpga/ap_core.qsf` (lists the three PSRAM files) |
| Firmware | `fw/psram_diag.c` (modes A/X/Y/B mailbox, L window suite, R soak), `fw/build.sh` targets `psram-diag` and `psram-diag-sim` |
| Simulation | `sim/psram_chip_model.v` (strict four-die model), `tb_tau_psram_async.v`, `tb_tau_psram_wb_return_regression.v`, `tb_tau_psram_probe.v`, `tb_psram_fw.v` (real CPU), `make_soc_sim.py`, `check_psram_fw_record.py`, `test_psram_decode.py`, `tb_tau_sdram_addr_decode.v` |
| Tools | `tools/package_psram_diagnostic.py` (hash-locked, `--variant`), `decode_tau_diag_log.py` (`--interact --psram` / `--psram-window`), `check_psram_idle.py`, `psram_probe_qsf_append.txt`, `psram_window_qsf_append.txt`, `quartus_fit_summary.py` |
| Evidence and bundles (`work/diagnostics/psram-diag/`, git-ignored) | `p3/` (photos, records), `margin/` (16 screenshots, records), `fpga*/` and `reports*/` (RBFs and Quartus reports per build), `pocket`, `pocket-t7`, `pocket-t6`, `pocket-b006-unpacked` (superseded, never install), `pocket-cache-backup-*` |
| Records | audit entries B-001 to B-018, `CLAUDE.md` log |

## 6. Commands
`make test` (27 suites, about 2.6 min), or individually `make test-rtl-psram-{idle,async,wb-return,mutation,probe,fw}`, `make test-rtl-sdram-decode`, `python3 sim/test_psram_decode.py`. Firmware: `bash fw/build.sh psram-diag` (Pocket ROM), `psram-diag-sim` (short fill for simulation).
Icarus cannot elaborate `mp3_soc` as written (declaration order, already true at HEAD); tests use `sim/make_soc_sim.py`, which never touches the synthesised source. Read-sample index: macro `PSRAM_T_ACC` (default 9, read back in `PS_CFG[15:8]`).

## 7. Decisions still open for the owner
1. **P4 result gates the rest.** After the builds and the card run: does the window meet the exit (round trip proven, cost measured, default Tau unchanged)?
2. **Read-sample default:** keep 9 (recommended). Index 8 saves about 8% of read time but rests on one unit at room temperature.
3. **P5 (one cold-data move, separate approval):** candidates `art_acc` (11 KiB) plus maps (2 KiB). Estimated added decode time through PSRAM about 0.5-0.6 s [EST] versus 0.8-0.9 s through SDRAM; the SDRAM handoff also proposes a hardware scaling blit engine that removes `art_acc`
   without a slower decode. Decide after the P4 cost measurement. P5 needs: decouple `TAU_PSRAM_WINDOW` from `TAU_PSRAM_PROBE` (a minimal mailbox plus a presence flag in the product), the `pl_sdram_prove` fail-safe pattern (feature off on failure), a `CORE_VERSION`/`EXPECT_VERSION` bump, and a heap budget (release gap 7,824 B, Diagnostic Build gap 4,224 B).
4. Optional: a per-bit/per-die error map at a failing index (identifies the slowest DQ lines); a stress-pump variant aimed at `0xA4000000` for real-playback stress on the PSRAM window.

## 8. Gotchas
- The shell is **zsh**: an unquoted `$VAR` holding several words is not split (use arrays or explicit variables); `set -- $cfg` does not work; `====` at the start of an echo is parsed as a command.
- macOS `cp` and `tar` add `._*` files and extended-attribute warnings on the FAT card and in VM stages; strip what you create, ignore the tar warnings.
- Quartus names generate-block instances `g_phase2_window.g_psram.u_psram_bus`; a plain-name grep finds nothing. The first log line "Quartus Prime Shell was successful" is a false completion signal; wait for the stages to exit or use the summary tool.
- The VM can be **suspended** when the Mac sleeps: wall-clock times jump while `uptime` barely moves; judge progress by `uptime -p` and process elapsed time.
- I/O-cell register packing needs `FAST_*_REGISTER` assignments, single-load registers and one synchronous control per register (KB-036). Read the fitter pin tables after every fit.
- The Pocket's own screenshots are readable exactly from the card, are ordered by the card clock, and are the preferred evidence. The screenshot combination may reach the core as button presses (KB-031); screens label themselves (`RUN n`, `IDX n`) so an extra run is harmless.
- Never assert both chip selects of a PSRAM chip; never access the last CPU word of a die (the datasheet's software-access sequence at `3FFFFFh` lives there). The controller enforces both; the guard word ACKs with data 0 through the CPU window (no bus error, the firmware has no handler).
- Mapping names in `input.json` are limited to 19 characters; the packager now respects it. Stale statements in the SDRAM-side documents were corrected or flagged in B-017/B-019.

## 9. Commits and what is left out
- `2b69114` P0/P1 controller and timing contract; `25ae8b6` **repairs `fw/build.sh`** (see below); `82f5f90` the P2-P4 RTL, firmware, simulation and tools; then a documentation commit (plans, timing contract, MMIO table, this handoff, audit trail, log).
- **`fw/build.sh` finding:** since the v0.2.0 release commit `af28127` the committed script did not parse (`bash -n` failed at the `release)` label: `player-diagnostic)` sat directly before it with its body left after a `;;`). Builds kept working because the working tree held the intact file. `25ae8b6` moves that one label back;
  the diagnostic flags equal those of the last valid version `3cc562b`. Tell the SDRAM side if they still have a different copy.
- Not committed on purpose: `docs/vendor/DOC012312972.pdf` (third-party datasheet marked confidential; commit only if the owner says so), `.claude/` (skill, local KB entries), `work/` (bundles, RBFs, evidence), `UniClaudeProxy/`.
- Rules for further commits: only when the owner asks, explicit `git add` paths, filtered patches for files that contain another session's hunks, verify `bash -n fw/build.sh` and `make test` first.
