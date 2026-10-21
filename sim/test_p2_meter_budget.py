#!/usr/bin/env python3
"""The meter budget checker (tools/player2/meter_budget.py) on synthetic section tables: a meter within its ceilings passes; one over a ceiling, one with a size in a class that has no ceiling, a missing object and
a manifest without a budget are each refused. The real objects are checked by `make test-host` after the firmware build."""
import sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_budget as mb
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def run(sec_by_obj, budgets, missing=()):
    with tempfile.TemporaryDirectory() as td:
        for obj in sec_by_obj:
            if obj not in missing: (Path(td) / (obj + ".o")).write_text("x")
        mb.sections = lambda f: sec_by_obj[Path(f).stem]
        mb.targets = lambda: [("m_a", "a", budgets)]
        sys.argv = ["x", td]
        import io, contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf): rc = mb.main()
        return rc, buf.getvalue()
S = {".text": 1000, ".rodata": 200, ".data": 8, ".bss": 40}
B = {"cold": 1300, "hot_ram": 64}
rc, _ = run({"m_a": S}, B); check("within the ceilings passes", rc == 0)
rc, out = run({"m_a": dict(S, **{".bss": 80})}, B); check("hot RAM over its ceiling is refused", rc == 1 and "hot_ram is 88 B" in out)
rc, out = run({"m_a": dict(S, **{".text.foo": 400})}, B); check("cold code over its ceiling is refused (a .text.* section counts)", rc == 1 and "cold is 1600 B" in out)
rc, out = run({"m_a": dict(S, **{".psram_state": 16})}, B); check("a size in a class without a ceiling is refused", rc == 1 and "psram_state is 16 B but has no ceiling" in out)
rc, out = run({"m_a": S}, B, missing=("m_a",)); check("a missing object is refused", rc == 1 and "is missing" in out)
rc, out = run({"m_a": S}, None); check("a manifest without a budget is refused", rc == 1 and "no p2.budget" in out)
rc, out = run({"m_a": S}, {"cold": 1300, "hot_ram": 64, "flash": 1}); check("an unknown class is refused", rc == 1 and "unknown budget class" in out)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
