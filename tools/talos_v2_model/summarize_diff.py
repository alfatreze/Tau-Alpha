#!/usr/bin/env python3
"""ORACLE-SIDE: tables for the differential results document from work/talos_v2_diff/results.json (audit B-1181).
    python3 tools/talos_v2_model/summarize_diff.py [results.json]
Prints markdown: vectors per verdict and per set, differing words per command class and classification, functional-stub
cost per command class over every vector, and the contract rules the vectors exercise against the real engine."""
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import talos_v2_model as M  # noqa: E402

CLS = ["IDENTICAL", "IMP-TEXT-DARK", "IMP-RESERVED-NOOP", "OOD", "IMP-FILL-NOLOSS", "UNAPPROVED", "TIMING", "PARAM-Q2"]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "work", "talos_v2_diff", "results.json")
    res = json.load(open(path))
    print("## vectors: %d (main engine %d, Gallery engine %d)" % (len(res), sum(r.get("engine") == "main" for r in res),
                                                                 sum(r.get("engine") == "gal" for r in res)))
    per_set = collections.defaultdict(collections.Counter)
    for r in res:
        s = r["name"].split("/")[0]
        v = r["verdict"]
        per_set[s]["n"] += 1
        if v == "IDENTICAL":
            per_set[s]["IDENTICAL"] += 1
        else:
            for c in v.split("+"):
                per_set[s][c] += 1
    print("\n| set | vectors | " + " | ".join(CLS) + " |\n|---|---|" + "---|" * len(CLS))
    tot = collections.Counter()
    for s in sorted(per_set):
        print("| %s | %d | %s |" % (s, per_set[s]["n"], " | ".join(str(per_set[s][c]) for c in CLS)))
        tot.update(per_set[s])
    print("| **all** | %d | %s |" % (tot["n"], " | ".join(str(tot[c]) for c in CLS)))
    # words per command class and class
    byop = collections.defaultdict(collections.Counter)
    cmds = collections.Counter()
    for r in res:
        for k, n in (r.get("by_op") or {}).items():
            op, c = k.split("/")
            byop[op][c] += n
        for op, n in (r.get("ops") or {}).items():
            cmds[op] += n
    print("\n| command class | commands run | differing words: " + " | ".join(c for c in CLS[1:]) + " |\n|---|---|" + "---|" * (len(CLS) - 1))
    for op in sorted(set(cmds) | set(byop), key=lambda o: [k for k, v in M.OP_NAMES.items() if v == o][:1] or [99]):
        print("| %s | %d | %s |" % (op, cmds[op], " | ".join(str(byop[op][c]) for c in CLS[1:])))
    # stub cost per class (single commands of all vectors)
    cost = collections.defaultdict(list)
    for r in res:
        for c in r.get("cost") or []:
            ci, op, busy = c[0], c[1], c[2]
            cost[M.OP_NAMES.get(op, str(op))].append(busy)
    print("\n| command class | retired | stub cycles p50 | p95 | max |\n|---|---|---|---|---|")
    for op, xs in sorted(cost.items()):
        xs.sort()
        print("| %s | %d | %d | %d | %d |" % (op, len(xs), xs[len(xs) // 2], xs[min(len(xs) - 1, (len(xs) * 95) // 100)], xs[-1]))
    # rules
    text = open(os.path.join(ROOT, "docs", "features", "TALOS_ENGINE_V2_CONTRACT.md")).read()
    ids = sorted(set(re.findall(r"\*\*(C-[A-Z]+-\d+)\s", text)))
    hit = collections.Counter()
    for r in res:
        for rule in r.get("rules") or []:
            hit[rule] += 1
    reached = [i for i in ids if hit[i]]
    print("\nrule identifiers defined: %d; exercised by at least one vector run on the old engine: %d" % (len(ids), len(reached)))
    print("not reached:", ", ".join(i for i in ids if not hit[i]))
    un = [r for r in res if r["verdict"] in ("UNAPPROVED", "ERROR")]
    print("\nUNAPPROVED/ERROR vectors: %d" % len(un))
    for r in un:
        print("-", r["name"], json.dumps(r.get("unapproved") or r.get("error"))[:400])


if __name__ == "__main__":
    main()
