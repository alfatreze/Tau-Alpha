#!/usr/bin/env python3
"""Player 2 dependency rule (docs/features/PLAYER_V2_SPEC.md section 4): every `#include "..."` between fw2 modules must follow the allowed-dependency table, and the module
graph must have no cycle. `app` may call everything; nothing calls `app`; `cores`, `platform` and `counters` call only `cores`; diagnostics is a leaf that nothing
else includes. Usage: check_deps.py [fw2-dir]  (default ../fw2); exit 1 and a list of violations if the rule is broken."""
import re, sys
from pathlib import Path

ALLOWED = {
    "app":         {"transport", "queue", "ui", "settings", "decode", "stream", "aout", "gfx", "art", "diagnostics", "platform", "counters", "cores"},
    "transport":   {"decode", "stream", "aout", "queue", "platform", "counters", "cores"},
    "queue":       {"platform", "counters", "cores"},
    "ui":          {"transport", "settings", "aout", "gfx", "art", "queue", "platform", "counters", "cores"},
    "settings":    {"platform", "counters", "cores"},
    "decode":      {"stream", "platform", "counters", "cores"},
    "stream":      {"platform", "counters", "cores"},
    "aout":        {"platform", "counters", "cores"},
    "gfx":         {"platform", "counters", "cores"},
    "art":         {"gfx", "stream", "platform", "counters", "cores"},
    "diagnostics": {"transport", "queue", "ui", "settings", "decode", "stream", "aout", "gfx", "art", "platform", "counters", "cores"},
    "platform":    {"cores"},
    "counters":    {"cores"},
    "cores":       {"cores"},
}

# The meter module rule (docs/features/meters/METERS_PLAYER2_PLAN.md): a meter (fw2/ui/meter_<key>.c) draws through gfx, reads its inputs from meter.h and the theme roles, and takes its logic from cores. It may include
# only these; it must not reach transport, settings, aout, art, queue, stream, diagnostics, the platform beyond the register map, or another meter. (The old player's portable cores under fw/ are allowed until they move into fw2/cores.)
METER_OK_FILES = {"ui/meter.h", "ui/theme.h", "platform/mmio.h"}
METER_OK_DIRS = {"gfx", "cores", "generated"}
def meter_violations(f, root, text):
    out = []
    for line_no, line in enumerate(text.splitlines(), 1):
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
        if not m: continue
        inc = m.group(1); tgt = (f.parent / inc).resolve()
        try: rel = tgt.relative_to(root.resolve()).as_posix()
        except ValueError:
            if "/fw/" in "/" + Path(inc).as_posix() or inc.startswith("../../fw/") or "fw/" in inc: continue        # an old-player core (transitional)
            continue                                                                                           # system or vendor header
        if rel in METER_OK_FILES or rel.split("/")[0] in METER_OK_DIRS: continue
        out.append(f"{f.relative_to(root)}:{line_no}: a meter module may include only meter.h, theme.h, gfx/, platform/mmio.h, cores/ and generated/ ({inc})")
    return out

def module_of(path, root):
    rel = path.relative_to(root)
    return rel.parts[0] if len(rel.parts) > 1 else None

def scan(root):
    edges, viol, unknown = {}, [], set()
    for f in sorted(root.rglob("*")):
        if f.suffix not in (".c", ".h", ".inc", ".S") or not f.is_file(): continue
        src = module_of(f, root)
        if src is None or src == "generated": continue
        if src not in ALLOWED: unknown.add(src); continue
        text = re.sub(r"/\*.*?\*/", "", f.read_text(), flags=re.S)
        if src == "ui" and f.name.startswith("meter_") and f.suffix == ".c": viol.extend(meter_violations(f, root, text))
        for line_no, line in enumerate(text.splitlines(), 1):
            m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
            if not m: continue
            inc = m.group(1)
            cand = [(f.parent / inc), (root / inc)] + [root / mod / Path(inc).name for mod in ALLOWED]
            tgt = next((c for c in cand if c.exists()), None)
            if tgt is None: continue                       # a vendor or system header outside fw2
            try: dst = module_of(tgt.resolve(), root.resolve())
            except ValueError: continue
            if dst is None or dst == "generated" or dst == src: continue
            edges.setdefault(src, set()).add(dst)
            if dst not in ALLOWED.get(src, set()):
                viol.append(f"{f.relative_to(root)}:{line_no}: {src} may not include {dst} ({inc})")
    # cycles
    state = {}
    def dfs(n, stack):
        state[n] = 1
        for m in sorted(edges.get(n, ())):
            if state.get(m) == 1: viol.append("cycle: " + " -> ".join(stack + [n, m]))
            elif m not in state: dfs(m, stack + [n])
        state[n] = 2
    for n in sorted(edges):
        if n not in state: dfs(n, [])
    for u in sorted(unknown): viol.append(f"unknown module directory fw2/{u} (add it to the table in tools/check_deps.py and to the spec)")
    return edges, viol

def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "fw2"
    edges, viol = scan(root)
    for v in viol: print("VIOLATION:", v)
    print(f"check_deps: {'FAIL' if viol else 'OK'} ({sum(len(v) for v in edges.values())} module edges)")
    return 1 if viol else 0

if __name__ == "__main__": sys.exit(main())
