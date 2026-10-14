#!/usr/bin/env python3
"""CLAUDE.md must stay small (B-742/B-744): it is loaded in full into every Claude Code session, and it had grown to 442 KB of dated log lines.
Checks (exit 1 on any failure):
  - CLAUDE.md is at most MAX_LINES lines and has no dated log lines ("- 2026-10-14 ..."): a record of a change belongs in docs/AUDIT_TRAIL.md;
  - CLAUDE.md imports docs/PROJECT_RULES.md (`@docs/PROJECT_RULES.md`), that file exists and carries the eight numbered rules;
  - AGENTS.md (read by Codex) points at docs/PROJECT_RULES.md and has no dated log lines either.
  python3 tools/check_claude_md.py [--root DIR]"""
import re, sys
from pathlib import Path
MAX_LINES = 150
DATED = re.compile(r"^\s*[-*]\s+(\*\*)?20\d\d-\d\d-\d\d")
def check(root: Path):
    errs = []
    cm = root / "CLAUDE.md"; rules = root / "docs" / "PROJECT_RULES.md"; ag = root / "AGENTS.md"
    if not cm.exists(): return ["CLAUDE.md is missing"]
    lines = cm.read_text().splitlines()
    if len(lines) > MAX_LINES: errs.append(f"CLAUDE.md has {len(lines)} lines (limit {MAX_LINES}): move detail to docs/")
    dated = [i + 1 for i, l in enumerate(lines) if DATED.match(l)]
    if dated: errs.append(f"CLAUDE.md has dated log lines (first at line {dated[0]}, {len(dated)} in all): record changes in docs/AUDIT_TRAIL.md")
    if "@docs/PROJECT_RULES.md" not in cm.read_text(): errs.append("CLAUDE.md does not import @docs/PROJECT_RULES.md")
    if not rules.exists(): errs.append("docs/PROJECT_RULES.md is missing")
    else:
        n = len(re.findall(r"^\d+\. \*\*", rules.read_text(), re.M))
        if n != 8: errs.append(f"docs/PROJECT_RULES.md has {n} numbered rules, expected 8")
    if ag.exists():
        t = ag.read_text()
        if "docs/PROJECT_RULES.md" not in t: errs.append("AGENTS.md does not point at docs/PROJECT_RULES.md")
        if any(DATED.match(l) for l in t.splitlines()): errs.append("AGENTS.md has dated log lines")
    return errs
def main():
    root = Path(sys.argv[sys.argv.index("--root") + 1]) if "--root" in sys.argv else Path(__file__).resolve().parent.parent
    errs = check(root)
    for e in errs: print("FAIL:", e)
    if not errs: print("check_claude_md: OK")
    sys.exit(1 if errs else 0)
if __name__ == "__main__": main()
