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
