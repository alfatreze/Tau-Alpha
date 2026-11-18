"""Builds (once, cached) and drives runner/rv32run.c: the RV32IM interpreter with the player's memory map and host table. One Runner = one loaded pack = one process; ticks are sent as text lines."""
import hashlib, os, subprocess, sys, tempfile
from pathlib import Path
from . import SDK_ROOT, toolchain

SRC = SDK_ROOT / "runner" / "rv32run.c"; HDR = SDK_ROOT / "runner" / "pack_strike_core.h"


class RunnerError(RuntimeError):
    pass


def cache_dir():
    base = os.environ.get("TAU_METER_CACHE") or (Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "tau-meter")
    Path(base).mkdir(parents=True, exist_ok=True)
    return Path(base)


def build_runner(tc=None):
    tc = tc or toolchain.find()
    h = hashlib.sha256(SRC.read_bytes() + HDR.read_bytes() + tc["line"].encode() + sys.platform.encode()).hexdigest()[:16]
    exe = cache_dir() / ("rv32run-%s%s" % (h, ".exe" if os.name == "nt" else ""))
    if exe.exists(): return exe
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / exe.name
        cmd = [tc["clang"], "-O2", "-std=gnu11", "-Wall", "-I", str(SDK_ROOT / "runner")] + toolchain.host_cc_flags(tc) + [str(SRC), "-o", str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode: raise RunnerError("the SDK's host runner did not build with %s:\n%s" % (tc["line"], r.stderr))
        out.replace(exe) if out.exists() else None
    return exe


class Tick:
    def __init__(self):
        self.calls = []; self.status = "ok"; self.cycles = 0; self.instr = 0; self.cmds = 0; self.ret = 0; self.stack = 0; self.fault = None; self.wd = 0; self.strikes = 0; self.clean = 0; self.judged = False; self.struck = False
        self.palettes = []; self.in_fields_new = 0; self.pstate_subword = 0


class Runner:
    def __init__(self, pack_path, profile="author", abi_minor=2, limit=0, blend=1, params=(), roles=None, stackfill=0, tc=None):
        exe = build_runner(tc)
        self.p = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self._send("profile %s" % profile); self._send("abi %d" % abi_minor); self._send("blend %d" % blend); self._send("limit %d" % limit); self._send("stackfill %d" % stackfill)
        self._send("params %d %s" % (len(params), " ".join(str(int(v)) for v in params)))
        self._send("roles " + " ".join(str(int(v)) for v in (roles or [0x1234 + i * 0x421 for i in range(21)])))
        self._send("load %s" % pack_path)
        ln = self.p.stdout.readline().strip()
        if ln != "L ok": raise RunnerError("the runner could not load the pack: %s" % ln)

    def _send(self, s):
        self.p.stdin.write(s + "\n"); self.p.stdin.flush()

    def tick(self, frame, dt_ms, box, bg, force, paused, peak_l, peak_r, degrade, spec, stats=None):
        x, y, w, h = box
        st = stats or (0, 0, 0, 0, 0, 0, 0)
        self._send("tick %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s" % (frame, dt_ms, x, y, w, h, bg, force, paused, peak_l, peak_r, degrade, st[0], st[1], st[2], st[3], st[4], st[5], st[6], 1 if stats else 0, bytes(spec).hex()))
        t = Tick()
        while True:
            ln = self.p.stdout.readline()
            if not ln: raise RunnerError("the runner stopped unexpectedly")
            ln = ln.rstrip("\n")
            if ln.startswith("c "):
                f = ln.split(" ")
                t.calls.append((f[1],) + tuple(f[2:]))
            elif ln.startswith("p "): t.palettes.append([int(x) for x in ln.split()[1:]])
            elif ln.startswith("f "):
                parts = ln.split(" ", 3); t.fault = {"pc": int(parts[1]), "addr": int(parts[2]), "msg": parts[3]}
            elif ln.startswith("n in-fields-1.1"): t.in_fields_new = int(ln.split()[2])
            elif ln.startswith("n pstate-subword"): t.pstate_subword = int(ln.split()[2])
            elif ln.startswith("r "):
                f = ln.split(" ")
                if f[1] == "bad": raise RunnerError("the runner refused the tick line")
                t.status = f[1]; t.cycles, t.instr, t.cmds, t.ret, t.stack = (int(v) for v in f[2:7])
                kv = dict(x.split("=") for x in f[7:])
                t.wd, t.strikes, t.clean, t.judged, t.struck = int(kv["wd"]), int(kv["strikes"]), int(kv["clean"]), kv["judged"] == "1", kv["struck"] == "1"
                return t

    def close(self):
        try: self._send("quit"); self.p.wait(timeout=5)
        except Exception: self.p.kill()

    def __enter__(self): return self
    def __exit__(self, *a): self.close()
