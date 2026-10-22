#!/usr/bin/env python3
"""Run the `make test-host` recipe in parallel.

Reads the recipe lines of the `test-host` target from the Makefile and runs them on a worker pool.
Lines that run a firmware build (`build.sh`) write to shared output directories, so they run one at a time
after the parallel part. Output of a passing line is dropped; a failing line prints its output and the
run exits 1. `make test-host` stays the serial reference.

  python3 tools/run_host_tests.py [-j 8] [--only SUBSTRING]
"""
import argparse, os, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def recipe(target="test-host"):
    lines, on = [], False
    with open(os.path.join(ROOT, "Makefile")) as f:
        for ln in f:
            if ln.startswith(target + ":"):
                on = True
                continue
            if on:
                if not ln.startswith("\t"):
                    break
                lines.append(ln[1:].rstrip("\n").lstrip("@"))
    return [l.replace("$(PYTHON)", sys.executable) for l in lines if l.strip()]


def run(cmd):
    t = time.time()
    p = subprocess.run(cmd, shell=True, cwd=ROOT, stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return cmd, p.returncode, time.time() - t, p.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-j", type=int, default=min(8, os.cpu_count() or 4))
    ap.add_argument("--only", help="run only recipe lines containing this text")
    a = ap.parse_args()
    cmds = [c for c in recipe() if not a.only or a.only in c]
    par = [c for c in cmds if "build.sh" not in c]
    ser = [c for c in cmds if "build.sh" in c]
    t0, results = time.time(), []
    with ThreadPoolExecutor(a.j) as ex:
        results += list(ex.map(run, par))
    results += [run(c) for c in ser]
    bad = [r for r in results if r[1]]
    for cmd, rc, _, out in bad:
        print(f"\n=== FAIL (rc {rc}): {cmd}\n{out[-4000:]}")
    slow = sorted(results, key=lambda r: -r[2])[:5]
    print("\nslowest:", *[f"{r[2]:.0f}s {r[0][:70]}" for r in slow], sep="\n  ")
    print(f"\n{len(results)} lines, {len(bad)} failed, {time.time() - t0:.0f}s wall (-j {a.j})")
    sys.exit(1 if bad else 0)


main()
