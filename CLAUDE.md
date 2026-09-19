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
