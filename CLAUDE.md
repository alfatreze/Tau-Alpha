# Multi-Agent Workspace Control Blueprint

## 1. System Personas
- **Architect & Auditor:** Claude Code (Local Loopback over Proxy)
- **Code Implementer:** Aider (Direct to LM Studio Qwen-14B)
- **Adversarial Code Reviewer:** Codex (Local Git Tracking)

## 2. Shared Development Workflow
1. **Claude (Auditor)** must outline system changes, file layouts, and security rules here before code changes begin.
2. **Aider (Qwen-14B)** must read this file, execute the code modifications line-by-line, and make a clean Git commit.
3. **Codex** must run background checks against modified files to detect bugs, race conditions, or performance flaws.

## 3. Strict Project Rules
- Never modify this repository's upstream tracking references.
- Always append an execution log entry to this file after finishing a coding turn.

## AUDIT COMPLIANCE TARGETS
- **Code Style:** Ensure consistent naming conventions and formatting.
- **Security:** Regularly review for potential vulnerabilities.
- **Performance:** Optimize code for efficiency.

## Execution Log
- 2026-09-19 (Claude): docs only. Added `docs/A088_UPSTREAM_CHECKS.md` (A-088 next-run checks derived from HarpMudd upstream). No code, no upstream refs touched; upstream cloned to session scratchpad only.
- 2026-09-19 (Claude): docs only. Added `docs/UPSTREAM_MEMORY_AUDIO_KNOWLEDGE.md` (upstream SDRAM/PSRAM/audio knowledge; all other upstream features deferred until SDRAM, PSRAM and FPGA audio are settled). Font-base address verified in upstream `mp3_fb.sv` (`FONT_BASE = 25'h0100000`, word address = byte 2 MiB).
- 2026-09-19 (Claude): firmware/packaging. A-087 (source-word display), A-088 (datatable bridge base 0xF8000000 -> 0xF8002000; slot write confirmed on Pocket), A-089 (slot 5 parameters 0x22 -> 0x86; no effect). All firmware-only or packaging-only, same Quartus-verified A-080 RBF; each installed on the SD card with cache backup. Flush (0188) still times out and the save file is still zero. Details in `docs/AUDIT_TRAIL.md`.
- 2026-09-19 (Claude): firmware/packaging. A-090 (`TAU_LOG_TABLE_PROBE`): slot-5 table size/integrity, 10 s `0188` flush with timing, post-flush re-read; slot parameters back to 0x22. Same A-080 RBF; installed on the card with cache backup. Result pending. See `docs/AUDIT_TRAIL.md`.
- 2026-09-19 (Claude): docs only. Recorded A-090 Pocket result (slot table OK, `0188` flush unanswered at 10 s, second read timed out, file zero) and corrected `docs/A088_UPSTREAM_CHECKS.md` against upstream ROADMAP (0184 is not a proven persistence path; interact.json is).
- 2026-09-19 (Claude): firmware/packaging/tooling. Drafted A-091 (`TAU_LOG_INTERACT_PROBE`): diagnostic result published through interact.json persist (16 words, 31-bit encoding), no 0184/0188/slot 5; packager `--probe-a091`; `tools/decode_tau_diag_log.py --interact`. Built and packaged only; NOT installed on the card. See `docs/AUDIT_TRAIL.md`.
- 2026-09-19 (Claude): card. Installed A-091 on the Pocket SD card in place of A-090 (cache backup, SHA-256 verified). Result pending; Quit the core before removing the card so APF writes interact_persist.json.
- 2026-09-19 (Claude): docs. Recorded A-091 Pocket result: interact.json persist channel works; the screen (`W0`,`W7`,`WC`,`WF`) matches the decoded persist file (checksum 0x478E986A). Result-log path closed; SDRAM return-path fault unchanged.
- 2026-09-19 (Claude): docs only. Closed issue 019 (resolved by A-088 address fix + A-091 interact.json persistence); updated PROJECT_REGISTER, CURRENT_STATUS next gate, and marked DIAGNOSTIC_RESULT_LOG transport as superseded.
- 2026-09-20 (Claude): firmware/packaging/tooling. Reviewed A-060..A-079 and issue 018; drafted A-092 (`TAU_DISCRIMINATOR_PROBE`): firmware-only mailbox-vs-CPU-window discriminator publishing 14 raw words via interact.json; `--probe-a092`, `decode_tau_diag_log.py --interact --raw`. Built/packaged only; NOT installed on the card.
- 2026-09-20 (Claude): card. Installed A-092 in place of A-091 (cache backup, SHA-256 verified). Result pending; Quit the core before removing the card.
- 2026-09-20 (Claude): RTL/sim/docs. A-092 Pocket result decoded (writes land; reads lag one beat back-to-back). Added `sim/tb_tau_sdram_wb_return_regression.v` (`make test-rtl-sdram-wb-return`), which fails on the old adapter and reproduces the Pocket symptoms; fixed `src/fpga/core/tau_sdram_wb_adapter.sv` with a second release cycle (A-093). `make test-rtl` and `make test-host` pass. Quartus build and Pocket run pending; issue 018 root cause recorded.
- 2026-09-20 (Claude): docs/skill only. Added project skill `.claude/skills/analogue-pocket-dev/`: SKILL.md router + references (json-files, host-target-commands, hardware/video/audio, chip32, sd-packaging, changelog, template-and-examples) + `docs-snapshot/` (full text of all analogue.co developer docs pages) + `repo-src/` (template core_top/core_bridge_cmd, chip32 example/arch). Finding: template target FSM never issues 0188/0181/0185/0152. No code or upstream refs touched; open-fpga repos cloned to session scratchpad only.
- 2026-09-20 (Claude): VM. Staged `/home/taualpha/tau-local/phase2-adapter-fix-a093-20260920` (A-093 adapter fix; macros TAU_PHASE2_WINDOW=1 and TAU_PHASE2_MUX_PROBE=1, as A-080) and launched `make fpga` at 00:17:54 WEST (PID 28654, log quartus-a093.log). Result pending; recorded in AUDIT_TRAIL A-093.
- 2026-09-20 (Claude): docs/skill only. Extended `.claude/skills/analogue-pocket-dev/` with an evidence-graded knowledge base (`references/knowledge-base/`: 21 entries, approaches, open questions, resources, sources.json) and `scripts/kb.py` (validate/index/new/promote/stale) + `scripts/refresh.py` (docs/repo drift). Community repos (agg23 utils+wiki, NES, LiteX, janisc NGPC, Paprium, Gateman) cloned to session scratchpad only. Key lead: nonvolatile flush size comes from the core size table at slot POSITION (KB-001); template never issues 0188 (KB-007). ~/.claude/skills entry is now a symlink to the repo copy. No code or upstream refs touched.
- 2026-09-20 (Claude): Quartus/packaging. A-093 build finished Successful (44m03s, 0 errors, +1.106 ns setup, +0.111 ns hold, RBF sha256 e16ffe9d...4d9d); copied to work/diagnostics/sdram-cpu-probe-a093/fpga; added packager `--probe-a093` (pairs it with the A-091 matrix ROM); bundle built, NOT installed on the card.
- 2026-09-20 (Claude): card. Installed A-093 in place of A-092 (cache backup, SHA-256 verified). Result pending; Quit the core before removing the card.
- 2026-09-20 (Claude): docs. Recorded A-093 Pocket PASS (183 checks, 0 failures; screen and interact.json record agree, checksum 0x18511A0D); issue 018 resolved; scope limited to the uncached CPU-window data path.
- 2026-09-20 (Claude): docs/KB/firmware. Committed A-093 (three commits). Used the project skill `analogue-pocket-dev`: added KB-022..KB-025 (hardware-validated from A-088/A-090/A-091/A-093), appended notes to KB-001/004/007/008/021 and OQ-1/2/6, `kb.py validate`/`index` pass. Drafted A-094 (`TAU_LATENCY_PROBE`, uncached SDRAM window cost, firmware-only, same A-093 RBF); built/packaged, NOT installed. Promotion gates listed in docs/CURRENT_STATUS.md.
- 2026-09-20 (Claude): card. Installed A-094 in place of A-093 (cache backup, SHA-256 verified). Later A-093 persist file (00:51) decoded: 183 checks, 0 failures, checksum 0x1851CA53 (independent confirmation of a repeat run). Could not register the analogue-pocket-dev skill into the running session (needs a new session). A-094 result pending; Quit the core before removing the card.
- 2026-09-20 (Claude): docs. Recorded A-094 Pocket result: uncached SDRAM access ~48-50 cycles net (0.8 us) at 60 MHz, worst single access ~360 cycles, 0 mismatches; stride-write speed-up unexplained. Next: count accesses per candidate cold buffer.
- 2026-09-20 (Claude): docs. Recorded A-094 Pocket result: uncached SDRAM access ~48-50 cycles net (0.8 us) at 60 MHz, worst single access ~360 cycles, 0 mismatches; stride-write speed-up unexplained. Next: count accesses per candidate cold buffer.
- 2026-09-20 (Claude): docs. Committed A-094 (firmware+docs; `.claude/` left untracked, it holds verbatim Analogue doc snapshots). Added A-095 static access-count analysis: pl_text/pl_off/pl_order (13 KiB) cost tens of ms per playlist load; art_acc would add ~0.8-0.9 s to a full cover decode. Estimates only; user decision pending on 13 KiB first step vs reaching 24 KiB.
- 2026-09-20 (Claude): firmware measurement, no product change. A-096: built a throwaway 7-row settings menu (TAU_SETTINGS_PROTO; kept in work/diagnostics/settings-size-proto/, tracked files restored). It adds 2,996 B and fails the link by 608 B (reproduces the old rejection); with the 13 KiB playlist move it would leave ~12.7 KiB spare, so the 24 KiB target is not needed for it. An accidental rebuild of dist/Assets/tau/common/tau.rom was reverted with git checkout.
