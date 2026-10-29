#!/usr/bin/env python3
"""Aggregate the chip model's [CHIPERR n] lines of one run (stdin, streamed) into a JSON summary. MIT licence.

  aggregate.py parse OUT.json LOGFILE < vvp-output     writes OUT.json, the non-error lines to LOGFILE
  aggregate.py table A.json B.json ...                 prints a class x run matrix (counts) in Markdown
"""
import json, re, sys

CHIPERR = re.compile(r"\[CHIPERR (\d+)\] t=(\d+) edge=(\d+) (\S+): (.*)")


def parse(out_json, log_path):
    classes = {}
    bench = []
    with open(log_path, "w") as log:
        for line in sys.stdin:
            m = CHIPERR.match(line)
            if m:
                _, t, edge, name, msg = m.groups()
                c = classes.setdefault(name, {"count": 0, "first_t_ps": int(t), "first_edge": int(edge), "first_msg": msg.strip(),
                                              "msgs": {}})
                c["count"] += 1
                c["msgs"][msg.strip()] = c["msgs"].get(msg.strip(), 0) + 1
                # keep the first time of each distinct message
                c.setdefault("msg_first", {}).setdefault(msg.strip(), int(t))
            else:
                log.write(line)
                if line.startswith("[BENCH]") or line.startswith("[CHIPMODEL]"):
                    bench.append(line.rstrip())
    json.dump({"classes": classes, "bench": bench}, open(out_json, "w"), indent=1)


def table(files):
    runs = [(f, json.load(open(f))) for f in files]
    names = sorted({n for _, r in runs for n in r["classes"]})
    print("| class | " + " | ".join(f.split("/")[-1].replace(".json", "") for f, _ in runs) + " |")
    print("|---|" + "---|" * len(runs))
    for n in names:
        print(f"| {n} | " + " | ".join(str(r["classes"].get(n, {}).get("count", 0)) for _, r in runs) + " |")


if __name__ == "__main__":
    if sys.argv[1] == "parse":
        parse(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "table":
        table(sys.argv[2:])
    else:
        sys.exit(__doc__)
