#!/usr/bin/env python3
"""MP3 v2 WP-05: I$ alias checker. Own code (Tau Alpha, MIT). Reads an ELF's symbol table (the link map) and, optionally, a per-PC profile written by rvprof
(tools/mp3_v2_measure/rvprof.c), and reports where hot code lands on the same sets of the CPU's instruction cache (4 KiB, direct-mapped, 32-byte lines: 128 sets,
set = address bits 11:5; expert review Q1) so two hot functions evict each other.

  alias_check.py <elf> [--prof prof.txt [--prof-elf other.elf]] [--min-count N] [--top N] [--cache-bytes 4096] [--line-bytes 32] [--json]

With a profile ("dynamic"): every 32-byte line that executed at least N instructions (default 1,000) is a hot line; a set holding two or more hot lines is a conflict set. Its
score is the executions on all its lines except the hottest one (the code that has to share the set with it), and the report's EXPOSURE is the sum of those scores over all
sets as a fraction of the instructions executed: an upper bound on the share of fetches that can miss because of aliasing (alternate execution of the lines would make every
entry miss; a line executed in one long run loses far less). Without a profile ("static"): functions of at least N bytes (default 256) whose address ranges share sets are
listed with the number of shared sets; every large program has some, so this is a list to read, not a verdict.
PROJECTION (--prof-elf): the profile of a run on ELF X (the profiler's harness) can be laid onto the function addresses of ELF Y (the shipped firmware): every profiled PC is
mapped to (function, offset) with X's symbols and placed at Y's address of that function plus the offset. Functions Y lacks are dropped; Y's other hot code (stream, UI, loop) is not
in the profile, so the result covers the decoder's own aliasing in Y's layout, not the whole firmware's. Same code is assumed (same sources and flags).
Library use: check(elf, prof=None, ..., prof_elf=None) -> dict. Exit status 0 always (it reports; the tests assert)."""
import bisect, json, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


def _bin():
    return Path(subprocess.check_output([sys.executable, str(ROOT / "tools/toolchain.py"), "bin"], text=True).strip().splitlines()[-1])


def functions(elf):
    """-> sorted [(addr, size, name)] of the text symbols"""
    o = subprocess.run([str(_bin() / "riscv-none-elf-nm"), "-S", "-n", str(elf)], capture_output=True, text=True, check=True).stdout
    out = []
    for l in o.splitlines():
        p = l.split()
        if len(p) == 4 and p[2] in "tTwW":
            out.append((int(p[0], 16), int(p[1], 16), re.sub(r"\.(constprop|isra|part)\.\d+", "", p[3])))
    return sorted(out)


def check(elf, prof=None, min_count=None, top=10, cache_bytes=4096, line_bytes=32, prof_elf=None):
    nsets = cache_bytes // line_bytes
    fns = functions(elf); starts = [f[0] for f in fns]
    def fn_of(addr):
        i = bisect.bisect_right(starts, addr) - 1
        return fns[i][2] if i >= 0 and addr < fns[i][0] + max(fns[i][1], 4) else "?"
    res = {"cache_bytes": cache_bytes, "line_bytes": line_bytes, "sets": nsets}
    if prof is None:
        mc = 256 if min_count is None else min_count
        by_set = {}
        for a, sz, n in fns:
            if sz < mc: continue
            for ln in range(a // line_bytes, (a + sz - 1) // line_bytes + 1): by_set.setdefault(ln % nsets, set()).add(n)
        pairs = {}
        for s, names in by_set.items():
            names = sorted(names)
            for i in range(len(names)):
                for j in range(i + 1, len(names)): pairs[(names[i], names[j])] = pairs.get((names[i], names[j]), 0) + 1
        res["mode"] = "static"
        res["pairs"] = [{"a": a, "b": b, "shared_sets": n} for (a, b), n in sorted(pairs.items(), key=lambda kv: -kv[1])[:top]]
        return res
    mc = 1000 if min_count is None else min_count
    line_exec, total = {}, 0
    src = functions(prof_elf) if prof_elf else None
    src_starts = [f[0] for f in src] if src else None
    dst = {n: a for a, _, n in fns} if prof_elf else None
    for l in Path(prof).read_text().splitlines():
        f = l.split(); pc, n = int(f[0], 16), int(f[2])
        if prof_elf:                                   # project the PC onto the other ELF's layout
            i = bisect.bisect_right(src_starts, pc) - 1
            if i < 0 or pc >= src[i][0] + max(src[i][1], 4) or src[i][2] not in dst: continue
            pc = dst[src[i][2]] + (pc - src[i][0])
        line_exec[pc // line_bytes] = line_exec.get(pc // line_bytes, 0) + n; total += n
    sets = {}
    for ln, n in line_exec.items():
        if n >= mc: sets.setdefault(ln % nsets, []).append((n, ln))
    conflicts, exposure = [], 0
    for s, lines in sets.items():
        if len(lines) < 2: continue
        lines.sort(reverse=True)
        score = sum(n for n, _ in lines[1:]); exposure += score
        conflicts.append({"set": s, "score": score, "lines": [{"addr": hex(ln * line_bytes), "exec": n, "function": fn_of(ln * line_bytes)} for n, ln in lines]})
    conflicts.sort(key=lambda c: -c["score"])
    # function pairs: by the score of the conflict sets they share
    pair = {}
    for c in conflicts:
        names = sorted({l["function"] for l in c["lines"]})
        for i in range(len(names)):
            for j in range(i + 1, len(names)): k = (names[i], names[j]); pair[k] = pair.get(k, 0) + c["score"]
    res.update({"mode": "dynamic", "instructions": total, "hot_lines": sum(len(v) for v in sets.values()), "conflict_sets": len(conflicts),
                "exposure": exposure / total if total else 0.0, "top_sets": conflicts[:top],
                "pairs": [{"a": a, "b": b, "score": s} for (a, b), s in sorted(pair.items(), key=lambda kv: -kv[1])[:top]]})
    return res


def main():
    a = sys.argv[1:]
    def opt(name, default=None):
        if name in a:
            i = a.index(name); v = a[i + 1]; del a[i:i + 2]; return v
        return default
    prof, mc, top, pelf = opt("--prof"), opt("--min-count"), int(opt("--top", 10)), opt("--prof-elf")
    cb, lb = int(opt("--cache-bytes", 4096)), int(opt("--line-bytes", 32))
    js = "--json" in a; a = [x for x in a if x != "--json"]
    r = check(a[0], prof, int(mc) if mc else None, top, cb, lb, pelf)
    if js: print(json.dumps(r, indent=1)); return 0
    if r["mode"] == "static":
        print(f"static: functions of at least {mc or 256} bytes sharing sets of the {r['sets']}-set I$ (a list to read, not a verdict)")
        for p in r["pairs"]: print(f"  {p['shared_sets']:3d} sets  {p['a']}  /  {p['b']}")
        return 0
    print(f"dynamic: {r['instructions']:,} instructions, {r['hot_lines']} hot lines, {r['conflict_sets']} conflict sets, EXPOSURE {100 * r['exposure']:.2f}% of fetches (upper bound)")
    for p in r["pairs"]: print(f"  score {p['score']:>12,}  {p['a']}  /  {p['b']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
