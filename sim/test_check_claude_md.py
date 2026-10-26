#!/usr/bin/env python3
"""tools/check_claude_md.py: the real files pass, and each rule it enforces fails on a deliberately broken copy (the checker's own mutants)."""
import shutil, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import check_claude_md as c
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name); fails += not ok
check("the repository's own CLAUDE.md, rules file and AGENTS.md pass", c.check(ROOT) == [])
def broken(edit, name, want):
    global fails
    with tempfile.TemporaryDirectory() as d:
        d = Path(d); (d / "docs").mkdir()
        for f in ("CLAUDE.md", "AGENTS.md"): shutil.copy(ROOT / f, d / f)
        shutil.copy(ROOT / "docs/PROJECT_RULES.md", d / "docs/PROJECT_RULES.md")
        edit(d)
        errs = c.check(d)
        check(f"mutant: {name} is caught", any(want in e for e in errs))
broken(lambda d: (d / "CLAUDE.md").write_text((d / "CLAUDE.md").read_text() + "\n".join("filler" for _ in range(200))), "an over-long CLAUDE.md", "limit")
broken(lambda d: (d / "CLAUDE.md").write_text((d / "CLAUDE.md").read_text() + "\n- 2026-10-14 (Claude): a log line\n"), "a dated log line in CLAUDE.md", "dated log")
broken(lambda d: (d / "CLAUDE.md").write_text((d / "CLAUDE.md").read_text().replace("@docs/PROJECT_RULES.md", "")), "the rules import removed", "does not import")
broken(lambda d: (d / "docs/PROJECT_RULES.md").write_text("1. **only one**\n"), "a rules file with one rule", "expected 8")
broken(lambda d: (d / "AGENTS.md").write_text((d / "AGENTS.md").read_text().replace("docs/PROJECT_RULES.md", "x")), "AGENTS.md without the pointer", "AGENTS.md does not point")
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
