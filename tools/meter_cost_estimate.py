#!/usr/bin/env python3
"""Static, source-level draw-command budget check for the visualiser tick functions in fw/player.c.

WHY THIS EXISTS (B-298/B-300): the Winamp Oscilloscope's hardware wave-block path issues up to
768 fb_rect() calls per redraw (256 columns x up to 3 calls each) against wviz_bars_tick()'s ~36-49
-- a real audio-affecting cost regression that was never checked before it reached a Quartus fit and
a card install. This is NOT a hardware measurement (see the planned meter-cost Check sweep, B-301,
for that) -- it is a conservative WORST-CASE estimate from the actual current source text: it finds
each tick function's real draw-call sites, multiplies the ones inside a loop by the loop's resolved
bound, and asserts the total against a declared budget. Deliberately over-estimates (it does not try
to resolve which `if` branch runs at a given moment) rather than under-estimate a real hazard.

Usage: python3 tools/meter_cost_estimate.py [--check]  (both modes report and exit 1 on any budget
miss; --check is accepted for symmetry with this project's other generators, same behaviour either way)
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER = os.path.join(ROOT, "fw", "player.c")

DRAW_CALLS = ("fb_rect(", "fb_bar(", "fb_blit(", "fb_sblit(", "fb_rrect(", "fb_cblit(", "ui_bg_restore(")

# name -> (declared worst-case command budget, one-line description of the represented cost).
# Budgets are set relative to this project's own documented baseline (VIZ_BARS, ~36-49 commands per
# redraw) -- see docs/METER_MODULE_SPEC.md section 3's cost_class idea, which this enforces today
# rather than only at some future M1+ rewrite.
BUDGETS = {
    "wviz_bars_tick": 100,
    # Higher than wviz_bars_tick's: a column-per-pixel scope structurally needs about one draw call
    # per column, unlike a ~16-band bar meter -- reusing bars' own budget here would be comparing
    # unlike shapes. 250 gives headroom above the software path's real, owner-confirmed-safe cost
    # (195, measured by this same tool after the B-298/B-300 hardware-path stopgap) while still
    # catching a real regression (the disabled hardware path was 966).
    "wviz_scope_tick": 250,
}


def extract_body(name):
    """Return the full text of function `name`'s body (including its own signature line), found by
    brace-matching from its opening `{`."""
    m = re.search(r"^\S.*?\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", SRC, re.M)
    if not m:
        sys.exit(f"could not find function {name}() in {PLAYER}")
    depth = 0
    i = m.end() - 1     # the opening '{'
    start = i
    for j in range(i, len(SRC)):
        if SRC[j] == '{':
            depth += 1
        elif SRC[j] == '}':
            depth -= 1
            if depth == 0:
                return SRC[m.start():j + 1]
    sys.exit(f"unbalanced braces in {name}()")


def resolve_const(expr):
    """Resolve a loop-bound identifier to an integer via this file's own #define/enum, falling back
    to a small table of known runtime-configurable bounds (worst case: their own declared maximum)."""
    known = {"WVIZ_BANDS_MAX": 16, "SPEC_BANDS": 16, "WAVE_HW_COLS": 256, "WAVE_COLS": 64}
    expr = expr.strip()
    if expr in known:
        return known[expr]
    if expr.isdigit():
        return int(expr)
    m = re.search(r"#define\s+" + re.escape(expr) + r"\s+(\d+)u?\b", SRC)
    if m:
        return int(m.group(1))
    # A runtime variable this project bounds explicitly (bands <= WVIZ_BANDS_MAX, etc.): the loop
    # variable itself, so the caller's own clamp is what makes 'bands' safe -- use the same known table.
    if expr in ("bands",):
        return known["WVIZ_BANDS_MAX"]
    return None


def strip_comments(text):
    """Blank out /* */ and // comment bodies (keep length/positions intact for the brace-matching
    that runs on this same text) so an explanatory comment mentioning a draw-call name in prose
    (e.g. this file's own generated ones) is never counted as a real call site."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        if text[i:i + 2] == "/*":
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            for k in range(i, j):
                if out[k] not in "\n":
                    out[k] = ' '
            i = j
        elif text[i:i + 2] == "//":
            j = text.find("\n", i)
            j = n if j == -1 else j
            for k in range(i, j):
                out[k] = ' '
            i = j
        else:
            i += 1
    return "".join(out)


def strip_dead_branches(text):
    """Blank out the body of any `if (0 ...) { ... }` / `if (0) { ... }` block (the project's own
    idiom for a deliberately compiled-out stopgap, e.g. wviz_scope_tick()'s B-298/B-300 disable) so
    a static worst-case estimate reflects what can actually run, not dead source text."""
    out = list(text)
    for m in re.finditer(r"if\s*\(\s*0\b[^)]*\)\s*\{", text):
        depth, i = 1, m.end()
        for j in range(i, len(text)):
            if text[j] == '{':
                depth += 1
            elif text[j] == '}':
                depth -= 1
                if depth == 0:
                    for k in range(i, j):
                        if out[k] not in "\n":
                            out[k] = ' '
                    break
    return "".join(out)


def estimate(name):
    body = extract_body(name)
    body = strip_comments(body)
    body = strip_dead_branches(body)
    # Every top-level `for (...)` loop header in this body (not attempting nested-loop bound
    # multiplication beyond one level -- none of today's meters need it).
    total = 0
    covered = [False] * len(body)
    loops = []
    for fm in re.finditer(r"for\s*\(([^;]*);([^;]*);([^)]*)\)\s*\{", body):
        cond = fm.group(2)
        depth, i = 1, fm.end()
        j_end = None
        for j in range(i, len(body)):
            if body[j] == '{':
                depth += 1
            elif body[j] == '}':
                depth -= 1
                if depth == 0:
                    j_end = j
                    break
        if j_end is None:
            sys.exit(f"{name}(): unbalanced braces in a for-loop")
        seg = body[i:j_end]
        if not any(c in seg for c in DRAW_CALLS):
            continue    # no draw calls in this loop (e.g. a data-averaging inner loop): its bound is irrelevant
        bm = re.search(r"<\s*([A-Za-z_][A-Za-z0-9_]*|\d+)", cond)
        if not bm:
            sys.exit(f"{name}(): a loop with draw calls has no readable '< bound' condition: {cond!r}")
        bound = resolve_const(bm.group(1))
        if bound is None:
            sys.exit(f"{name}(): could not resolve loop bound {bm.group(1)!r} -- extend resolve_const()")
        loops.append((i, j_end, bound))
    for (i, j, bound) in loops:
        for k in range(i, j):
            covered[k] = True
        seg = body[i:j]
        n_calls = sum(seg.count(c) for c in DRAW_CALLS)
        total += n_calls * bound
    # flat (outside any loop) draw calls
    flat_seg = "".join(ch if not covered[k] else " " for k, ch in enumerate(body))
    total += sum(flat_seg.count(c) for c in DRAW_CALLS)
    return total, loops


def main():
    global SRC
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.parse_args()

    with open(PLAYER) as f:
        SRC = f.read()

    ok = True
    for name, budget in BUDGETS.items():
        total, loops = estimate(name)
        loop_desc = ", ".join(f"bound={b}" for (_, _, b) in loops) or "no loop found"
        status = "ok  " if total <= budget else "OVER"
        print(f"{status} {name}: estimated worst case {total} draw commands/redraw "
              f"(budget {budget}, {loop_desc})")
        if total > budget:
            ok = False
    if not ok:
        print("\nOne or more meters exceed their declared draw-command budget (estimate, not a "
              "hardware measurement -- see the meter-cost Check sweep, B-301, for real numbers).")
        print("This is expected right now for wviz_scope_tick's hardware wave path (B-298/B-300): "
              "fix by batching its per-column fb_rect() calls, or keep it on the software fallback "
              "path until that redesign lands.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
