#!/usr/bin/env python3
"""ORACLE-SIDE classified differential: G1 model vectors replayed through the OLD Talos engine (audit B-1181).

NEVER hand this file, its outputs or sim/tb_talos_v2_diff.v to the clean-room RTL author: it drives the shipped
engine (src/fpga/core/tau_talos.sv) and Tau Gallery's copy (26d9b1c, copied read-only into the git-ignored
work/talos_v2_diff/gal/) and compares them with the contract model (tools/talos_v2_model/talos_v2_model.py).

    python3 tools/talos_v2_model/run_diff.py [--vectors DIR] [--sets V-RECT,V-BAR] [--jobs N] [--quick] [--out DIR]
    python3 tools/talos_v2_model/run_diff.py --selftest          # a handful of vectors (the make test-host gate)

Per vector: the SoC registers are emulated (tau_soc.v semantics) and turned into port-level stimulus for
sim/tb_talos_v2_diff.v; the bench's write log, fills, register samples and final state are compared with the model,
and every differing word is classified with the conformance plan's section 3 classes:
  IDENTICAL, IMP-TEXT-DARK, IMP-RESERVED-NOOP, OOD, IMP-FILL-NOLOSS, UNAPPROVED
plus two bookkeeping classes that are NOT engine findings: TIMING (a register read or fill taken while the
transaction-level model still held unexecuted commands: the model is "the slowest legal engine", plan 2.1/GAP-26)
and PARAM-Q2 (a vector built with the derived light table, which the old engine cannot express).
Which engine: a vector whose build needs CORNERS, DSBLIT or the viewer runs on Gallery's engine (which builds
CORNERS and DSBLIT always), every other vector on main's.
MIT licence (Tau Alpha).
"""
import argparse
import collections
import json
import multiprocessing as mp
import os
import re
import subprocess
import sys
import time
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import talos_v2_model as M  # noqa: E402

WORK = os.path.join(ROOT, "work", "talos_v2_diff")
GAL_SRC = os.environ.get("TALOS_GAL_ENGINE", os.path.join(WORK, "gal", "tau_talos_gal.sv"))
RTL = os.path.join(ROOT, "src", "fpga", "core")
TB = os.path.join(ROOT, "sim", "tb_talos_v2_diff.v")
DEFAULT_VECTORS = os.path.join(ROOT, "work", "talos_v2_model", "vectors")

CLASSES = ["IDENTICAL", "IMP-TEXT-DARK", "IMP-RESERVED-NOOP", "OOD", "IMP-FILL-NOLOSS", "UNAPPROVED", "TIMING", "PARAM-Q2"]
STATUS_CAPS_MASK = ~(0x3FFF << 17) & 0xFFFFFFFF          # GFX_STATUS[30:17] is new in v2 (capability bits): not compared


# ---- the oracle build ---------------------------------------------------------------------------------------------------
def oracle_of(p):
    """-> (engine, key) for the resolved model parameters p."""
    gal = bool(p["FEAT_CORNERS"] or p["FEAT_DSBLIT"] or p["VIEW_ENABLE"])
    return ("gal" if gal else "main"), (("gal" if gal else "main"), p["BLIT_BLEND_ENABLE"], p["DBUF_ENABLE"], p["FONT_RAM_ENABLE"],
                                        p["FILL_WORDS"], p["VIEW_ENABLE"] if gal else 0)


def build(key, outdir):
    eng, blend, dbuf, font, fw, view = key
    name = "%s_b%d_d%d_f%d_w%d_v%d.vvp" % key
    vvp = os.path.join(outdir, name)
    if os.path.exists(vvp) and os.path.getmtime(vvp) >= max(os.path.getmtime(TB), os.path.getmtime(GAL_SRC if eng == "gal" else os.path.join(RTL, "tau_talos.sv"))):
        return vvp
    src = GAL_SRC if eng == "gal" else os.path.join(RTL, "tau_talos.sv")
    if not os.path.exists(src):
        raise SystemExit("missing %s (copy Gallery's src/fpga/core/tau_talos.sv at 26d9b1c there, read-only)" % src)
    cmd = ["iverilog", "-g2012", "-I", RTL, "-o", vvp] + (["-DGAL"] if eng == "gal" else []) + \
          ["-Ptb_talos_v2_diff.%s=%d" % (k, v) for k, v in (("BLEND", blend), ("DBUF", dbuf), ("FONTRAM", font), ("FILLW", fw), ("VIEW", view))] + \
          [TB, src, os.path.join(RTL, "tau_cdc_gray_ctr.sv"), os.path.join(RTL, "font_rom.v"), os.path.join(RTL, "font_ram.v")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("iverilog failed:\n" + r.stderr[-2000:])
    return vvp


# ---- the recording model -------------------------------------------------------------------------------------------------
class Rec(M.Talos):
    """The model with a little bookkeeping: queue state at reads and fills, fill words, commands discarded by reset."""
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.read_state = {}
        self.fill_vals = []
        self.discarded = set()
        self.push_map = []            # per push (accepted or not, in order): the model's command index, or None if dropped
        self.step_target = {}         # action index of a "step" -> commands retired since the last reset after it
        self.stall_queue = []         # queue length each time a stall starts
        self.since_reset = 0
        self.exec_state = {}

    def execute(self, ci, c):
        self.exec_state[ci] = {"src_base": self.src_base, "src_stride": self.src_stride, "dst_base": self.dst_base,
                               "dst_stride": self.dst_stride, "key_en": self.key_en, "key": self.key, "blend_en": self.blend_en,
                               "blend_mode": self.blend_mode, "alpha": self.blend_alpha, "reindex": self.reindex,
                               "cpu": self.dbuf_cpu, "text_light": self.text_light}
        super().execute(ci, c)

    force = None                      # a list of accept flags per push (the old engine's), or None: the model decides

    def push(self, cmd):
        n0 = len(self.commands)
        if self.force is not None:
            k = len(self.push_map)
            cmd = M.mask_cmd(cmd)
            if k < len(self.force) and not self.force[k]:
                self.ovf = min(M.OVF_MAX, self.ovf + 1)
                self._ev("drop", op=cmd["op"], ovf=self.ovf)
                self.push_map.append(None)
                return False
            dom, notes = M.classify(cmd, self)
            self.commands.append({"cmd": cmd, "domain": dom, "notes": notes})
            self.queue.append(n0)
            self.accepted += 1
            self._ev("push", ci=n0, op=cmd["op"], free=self.free(), full=int(self.full()), idle=self.idle())
            self.push_map.append(n0)
            return True
        ok = super().push(cmd)
        self.push_map.append(n0 if ok else None)
        return ok

    def act(self, a):
        if a["a"] in ("mmio_r", "snapshot", "fill"):
            self.read_state[self.t] = (len(self.queue), self.stalled)
        if a["a"] == "stall" and a["v"]:
            self.stall_queue.append(len(self.queue))
        super().act(a)
        if a["a"] == "step":
            self.step_target[self.t] = self.retired

    def fill(self, n):
        vals = super().fill(n)
        self.fill_vals.append((self.t, n & 0x3FF, np.asarray(vals, dtype=np.int64)))
        return vals

    def reset(self):
        self.discarded.update(self.queue)
        super().reset()


def run_model(params, init, actions, mem=None, force=None):
    eng = Rec(params, init, memory=mem)
    eng.force = force
    for i, a in enumerate(actions):
        eng.t = i
        eng.act(a)
    eng.t = len(actions)
    if not eng.stalled:
        eng.drain()
    return eng


def last_writer(write_log, rtl=False):
    """-> dict addr -> (ci, op) of the last write."""
    d = {}
    for ci, op, addrs, _ in write_log:
        for x in np.asarray(addrs).ravel().tolist():
            d[x] = (ci, op)
    return d


# ---- SoC emulation: actions -> stimulus ----------------------------------------------------------------------------------
STICKY_IDX = {"src_base": 0, "src_stride": 1, "dst_base": 2, "dst_stride": 3, "key_en": 4, "key": 5, "blend_en": 6,
              "blend_mode": 7, "blend_alpha": 8, "reindex": 9}


class Soc:
    """tau_soc.v's engine registers (read from the RTL): held FB_*, BLT_IDX/DATA (steps 6 -> 0, 7 writes REINDEX and
    wraps to 0), CLUT_IDX/DATA (steps, wraps), RC_IDX/DATA (steps, wraps), TEXT_MODE, DBUF_CPU (stored always, reads 0
    without DBUF), DBUF_DISP write = flip toggle, FONT_IDX/DATA/CTRL (FONT_RAM only), the proposed viewer block."""
    def __init__(self, p, out):
        self.p = p
        self.o = out
        self.fb_addr = self.fb_size = self.fb_color = 0
        self.blt_idx = 0
        self.clut_idx = 0
        self.rc_idx = 0
        self.text_light = 0
        self.cpu = 0
        self.font_idx = 0
        self.font_gen = 0
        self.view = {"en": 0, "base": 0, "stride": 1200, "dbl": 0}
        self.vid_mode_req = 0

    def S(self, f, v):
        self.o.append("S %d %d" % (f, v))

    def push(self, c):
        c = M.mask_cmd(c)
        self.o.append("P %d %d %d %d %d %d %d %d %d %d" % (c["op"], c["addr"], c["w"], c["h"], c["fg"], c["bg"], c["glyph"],
                                                         c["sx"], c["sy"], c["glyph_hi"]))

    def sticky(self, field, v):
        field &= 7
        if field == 0: self.S(0, v & M.MASK25)
        elif field == 1: self.S(1, v & 0x3FF)
        elif field == 2: self.S(2, v & M.MASK25)
        elif field == 3: self.S(3, v & 0x3FF)
        elif field == 4: self.S(4, (v >> 16) & 1); self.S(5, v & 0xFFFF)
        elif field == 5: self.S(6, v & 1); self.S(7, (v >> 1) & 7); self.S(8, (v >> 8) & 0xFF)
        else: self.S(9, v & 0xFF)
        self.blt_idx = 0 if field >= 6 else field + 1

    def set_view(self):
        self.S(13, self.view["en"] & 1)
        self.S(14, self.view["base"] & M.MASK25)
        self.S(15, self.view["stride"] & 0xFFFF)
        self.S(16, (self.view["dbl"] & 1) if self.p["VIEW_DBL_EN"] else 0)       # a VIEW build without the doubler gates it

    def decode(self, off):
        return off & ((1 << self.p["SOC_DECODE_BITS"]) - 1) & ~3

    def mmio_w(self, off, v):
        off = self.decode(off); v &= 0xFFFFFFFF; p = self.p
        if off == M.R_FB_ADDR: self.fb_addr = v & M.MASK19
        elif off == M.R_FB_SIZE: self.fb_size = v & 0x3FFFF
        elif off == M.R_FB_COLOR: self.fb_color = v
        elif off == M.R_FB_GO: self.push(M.decode_fb_go(v, self.fb_addr, self.fb_size, self.fb_color, 0))
        elif off == M.R_BLT_IDX: self.blt_idx = v & 7
        elif off == M.R_BLT_DATA: self.sticky(self.blt_idx, v)
        elif off == M.R_CLUT_IDX: self.clut_idx = v & 0xFF
        elif off == M.R_CLUT_DATA: self.o.append("C %d %d" % (self.clut_idx, v & 0xFFFF)); self.clut_idx = (self.clut_idx + 1) & 0xFF
        elif off == M.R_RC_IDX: self.rc_idx = v & 15
        elif off == M.R_RC_DATA: self.S(12, (self.rc_idx << 8) | (v & 31)); self.rc_idx = (self.rc_idx + 1) & 15
        elif off == M.R_TEXT_MODE: self.text_light = v & 1; self.S(10, v & 1)
        elif off == M.R_DBUF_CPU: self.cpu = v & 1; self.S(11, v & 1)
        elif off == M.R_DBUF_DISP:
            if v & 1: self.o.append("X")
        elif off == M.R_FONT_CTRL:
            if p["FONT_RAM_ENABLE"] and (v & 1): self.font_idx = 0
        elif off == M.R_FONT_IDX:
            if p["FONT_RAM_ENABLE"]: self.font_idx = 0 if (v & 0x1FFF) > 6111 else (v & 0x1FFF)
        elif off == M.R_FONT_DATA:
            if p["FONT_RAM_ENABLE"]:
                self.o.append("F %d %d" % (self.font_idx, v))
                if self.font_idx == 6111: self.font_idx = 0; self.font_gen = (self.font_gen + 1) & 0xFFF
                else: self.font_idx += 1
        elif p["VIEW_ENABLE"] and p["SOC_DECODE_BITS"] >= 10:
            if off == M.R_VID_MODE: self.vid_mode_req = v & 1
            elif off == M.R_VIEW_BASE: self.view["base"] = v & M.MASK25; self.set_view()
            elif off == M.R_VIEW_STRIDE: self.view["stride"] = v & 0xFFFF; self.set_view()
            elif off == M.R_VIEW_CTRL: self.view["dbl"] = v & 1; self.set_view()

    def mmio_r(self, off, q):
        """q: the bench's engine outputs (full, idle, free, fence, ovf, disp, pend, vact) at the read."""
        off = self.decode(off); p = self.p
        full, idle, free, fence, ovf, disp, pend, vact = q
        if off == M.R_FB_GO: return full | (idle << 1)
        if off == M.R_FB_STALL: return None
        if off == M.R_TEXT_MODE: return (1 << 31) | self.text_light
        if off == M.R_DBUF_CPU: return ((1 << 31) | self.cpu) if p["DBUF_ENABLE"] else 0
        if off == M.R_DBUF_DISP: return ((1 << 31) | (pend << 1) | disp) if p["DBUF_ENABLE"] else 0
        if off == M.R_FONT_CTRL: return ((1 << 31) | (self.font_gen << 16) | 6112) if p["FONT_RAM_ENABLE"] else 0
        if off == M.R_FONT_IDX: return self.font_idx if p["FONT_RAM_ENABLE"] else 0
        if off == M.R_GFX_FENCE: return fence
        if off == M.R_GFX_STATUS: return (1 << 31) | (idle << 16) | free          # the old SoC: bits 30:17 read 0
        if off == M.R_GFX_COUNTS: return ovf
        if p["VIEW_ENABLE"] and p["SOC_DECODE_BITS"] >= 10:
            if off == M.R_VIEW_BASE: return self.view["base"]
            if off == M.R_VIEW_STRIDE: return self.view["stride"]
            if off == M.R_VIEW_CTRL: return self.view["dbl"]
            if off == M.R_VIEW_CAPS: return (1 << 31) | (p["VIEW_DBL_EN"] << 3) | (1 << 1) | 1
            if off == M.R_VID_MODE: return (self.vid_mode_req << 2) | (int(self.vid_mode_req != self.view["en"]) << 1) | self.view["en"]
        return 0


def stimulus(p, init, actions, model=None):
    """-> (lines, soc, read_offsets {t: off}). Also the init image (backdoor). model: the recorded model run (a
    "step" becomes a wait until the old engine has retired as many commands as the model had after that step)."""
    o = []
    soc = Soc(p, o)
    for start, words in init.get("regions", []):
        for i, w in enumerate(words):
            o.append("B %d %d" % ((start + i) & M.MASK25, w & 0xFFFF))
    for start, n, v in init.get("const_regions", []):
        for i in range(n):
            o.append("B %d %d" % ((start + i) & M.MASK25, v & 0xFFFF))
    if "clut" in init:
        for i, v in enumerate(init["clut"]):
            o.append("U %d %d" % (i, v & 0xFFFF))
    for i, v in enumerate(list(init.get("rc", [])) + [0] * (16 - len(init.get("rc", [])))):
        o.append("S 12 %d" % ((i << 8) | (v & 31)))
    # the font memories: the model powers up with zeros (the real RAM holds the default font, the ROM the Inter atlas):
    # load what the vector gives, zeros otherwise, so CHAR compares engine arithmetic, not font data
    if p["FONT_RAM_ENABLE"]:
        f = init.get("font_ram")
        words = (M.synthetic_font(M.FONT_RAM_WORDS, f.get("salt", 1)) if isinstance(f, dict) else list(f)) if f is not None else []
        words = words + [0] * (M.FONT_RAM_WORDS - len(words))
        o += ["N %d %d" % (i, w & 0xFFFFFFFF) for i, w in enumerate(words)]
    else:
        f = init.get("font_rom")
        words = (M.synthetic_font(M.FONT_ROM_WORDS, f.get("salt", 2)) if isinstance(f, dict) else list(f)) if f is not None else []
        words = words + [0] * (M.FONT_ROM_WORDS - len(words))
        o += ["O %d %d" % (i, w & 0xFFFFFFFF) for i, w in enumerate(words)]
    for k, v in init.get("sticky", {}).items():
        if k in STICKY_IDX:
            o.append("S %d %d" % (STICKY_IDX[k], v))
    if init.get("text_light"):
        soc.text_light = 1; o.append("S 10 1")
    if init.get("dbuf_cpu"):
        soc.cpu = 1; o.append("S 11 1")
    soc.set_view()
    reads = {}
    for t, a in enumerate(actions):
        k = a["a"]
        if k == "push":
            for _ in range(a.get("repeat", 1)):
                soc.push(a)
        elif k == "mmio_w":
            soc.mmio_w(a["off"], a["v"])
        elif k in ("mmio_r", "snapshot"):
            reads[t] = a.get("off")
            o.append("Q %d" % t)
        elif k == "sticky":
            soc.blt_idx = a["field"] & 7
            soc.sticky(a["field"], a["v"])
        elif k == "clut":
            soc.mmio_w(M.R_CLUT_IDX, a["idx"])
            for v in a["v"]:
                soc.mmio_w(M.R_CLUT_DATA, v)
        elif k == "rc":
            soc.mmio_w(M.R_RC_IDX, a["idx"])
            for v in a["v"]:
                soc.mmio_w(M.R_RC_DATA, v)
        elif k == "font":
            soc.mmio_w(M.R_FONT_IDX, a["idx"])
            for v in a["v"]:
                soc.mmio_w(M.R_FONT_DATA, v)
        elif k == "text_light":
            soc.text_light = a["v"] & 1; o.append("S 10 %d" % (a["v"] & 1))
        elif k == "dbuf_cpu":
            soc.mmio_w(M.R_DBUF_CPU, a["v"])
        elif k == "flip_req":
            o.append("X")
        elif k == "vblank":
            o.append("V")
        elif k == "fill":
            o.append("L %d %d" % (t, a["line"] & 0x3FF))
        elif k == "view":
            for f in ("en", "base", "stride", "dbl"):
                if f in a:
                    soc.view[f] = a[f]
            soc.set_view()
        elif k == "wait_idle":
            o.append("I %d" % t)
        elif k == "fence":
            soc.push({"op": M.OP_FENCE}); o.append("I %d" % t)
        elif k == "step":
            o.append("K %d" % model.step_target[t] if model is not None and t in model.step_target else "D 20")
        elif k == "stall":
            o.append("T %d" % (1 if a["v"] else 0))
        elif k == "reset":
            o.append("R")
            o.append("T 0")                                                   # the model's reset ends a stall (the scheduler resets too)
            soc.fb_addr = soc.fb_size = soc.fb_color = 0                    # the model's reading (GAP-08); see the results doc
        elif k == "cpu_w":
            for i, v in enumerate(a["v"]):
                o.append("B %d %d" % ((a["addr"] + i) & M.MASK25, v & 0xFFFF))
        elif k == "delay":                                                   # oracle-side experiments only
            o.append("D %d" % a["n"])
        elif k == "fillreq":                                                 # oracle-side: a fill request, no wait
            o.append("J %d" % (a["line"] & 0x3FF))
        elif k == "ystat":                                                   # oracle-side: fills finished so far
            o.append("Y %d" % t)
        else:
            raise ValueError("unknown action %r" % k)
    o.append("E")
    return o, soc, reads


# ---- running the bench ---------------------------------------------------------------------------------------------------
def parse_log(path):
    r = {"acc": [], "disp": [], "ret": [], "writes": [], "prot": [], "fills": [], "lost": [], "q": {}, "hang": [], "z": None}
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            k = t[0]
            if k == "W":
                d = t[3]
                r["writes"].append((int(t[1]), int(t[2]), -1 if not d.isdigit() else int(d)))
            elif k == "A":
                r["acc"].append((int(t[1]), int(t[2])))
            elif k == "D":
                r["disp"].append((int(t[1]), int(t[2])))
            elif k == "C":
                r["ret"].append(tuple(int(x) for x in t[1:8]))
            elif k == "P":
                r["prot"].append((t[1], int(t[2]), int(t[3])))
            elif k == "F":
                hx = t[4] if len(t) > 4 else ""
                r["fills"].append((int(t[1]), int(t[2]), int(t[3]), [int(hx[i:i + 4], 16) if "x" not in hx[i:i + 4] else -1 for i in range(0, len(hx), 4)]))
            elif k == "G":
                r["lost"].append((int(t[1]), int(t[2])))
            elif k == "Q":
                r["q"][int(t[1])] = tuple(int(x) if x.isdigit() else -1 for x in t[2:10])
            elif k == "Y":
                r.setdefault("y", []).append(tuple(int(x) for x in t[1:4]))
            elif k == "H":
                r["hang"].append(t[1])
            elif k == "Z":
                r["z"] = tuple(int(x) if x.isdigit() else -1 for x in t[1:10])
    return r


def run_bench(vvp, lines, tag):
    os.makedirs(os.path.join(WORK, "runs"), exist_ok=True)
    stim = os.path.join(WORK, "runs", tag + ".stim")
    out = os.path.join(WORK, "runs", tag + ".log")
    with open(stim, "w") as f:
        f.write("\n".join(lines) + "\n")
    t0 = time.time()
    r = subprocess.run(["vvp", "-n", vvp, "+stim=" + stim, "+out=" + out], capture_output=True, text=True, timeout=3600)
    dt = time.time() - t0
    if not os.path.exists(out):
        raise RuntimeError("bench produced no log: %s" % (r.stdout[-500:] + r.stderr[-500:]))
    res = parse_log(out)
    res["sim_s"] = dt
    if not os.environ.get("TALOS_DIFF_KEEP"):
        os.remove(stim); os.remove(out)
    return res


# ---- comparison and classification ---------------------------------------------------------------------------------------
def oracle_variants(p, eng):
    """The model reshaped to what the old engine builds, one approved change at a time."""
    base = {k: v for k, v in p.items()}
    dark = dict(base, TEXT_W_DARK=M.TEXT_W_DARK_SHIPPED)
    shape = dict(dark, TEXT_W_LIGHT=M.TEXT_W_LIGHT_IN_USE, FEAT_ESC=0, MUTANT="M21_RSV_DRAWS")
    if eng == "gal":
        shape.update(FEAT_CORNERS=1, FEAT_DSBLIT=1)
    return dark, shape


def mem_value(rtl_mem, addrs):
    base = M.Memory.base()
    return np.array([rtl_mem.get(a, int(base[a])) for a in addrs], dtype=np.int64)


def compare(v, res, p, eng, m=None):
    """-> dict with the classification of every difference."""
    acts = v["actions"]
    params = dict(v["params"])
    if m is None:
        m = run_model(params, v["init"], acts)
    # acceptance: where the model (the slowest legal engine) dropped pushes the old engine accepted, or the reverse,
    # because the model still held commands the old engine had already executed, the model is replayed with the old
    # engine's accept decisions (TIMING), so that everything after is compared like for like
    rtl_accept = [bool(x) for _, x in res["acc"]]
    acc_timing = False
    if rtl_accept != [c is not None for c in m.push_map] and not (m.stall_queue and not any(m.stall_queue)):
        acc_timing = True
        m = run_model(params, v["init"], acts, force=rtl_accept)
    force = rtl_accept if acc_timing else None
    pm = m.push_map

    def mci_of(push_idx):                         # the bench numbers every push; the model numbers accepted ones
        if push_idx < 0:
            return push_idx
        c = pm[push_idx] if push_idx < len(pm) else None
        return c if c is not None else -1000 - push_idx
    out = {"name": v["set"] + "/" + v["name"], "engine": eng, "classes": collections.Counter(), "words": collections.Counter(),
           "by_op": collections.Counter(), "notes": [], "unapproved": [], "sim_s": round(res["sim_s"], 2)}
    ood_ci = {i for i, c in enumerate(m.commands) if c["domain"] == "ood"}
    nondet = bool(m.nondeterministic)
    discarded = m.discarded
    # RTL final memory
    rtl_mem = {}
    rtl_last = {}
    for ci, a, d in res["writes"]:
        ci = mci_of(ci)
        rtl_mem[a] = d
        if ci >= -1:
            rtl_last[a] = ci
        else:
            rtl_last[a] = ci                       # -3: written by a command abandoned by reset
    model_last = last_writer(m.write_log)
    addrs = sorted(set(rtl_mem) | set(model_last) | set(int(x) for d in m.mem.dirty for x in np.asarray(d).ravel()))
    addrs_np = np.array(addrs, dtype=np.int64)
    mv = m.mem.a[addrs_np].astype(np.int64) if addrs else np.zeros(0, dtype=np.int64)
    rv = mem_value(rtl_mem, addrs) if addrs else np.zeros(0, dtype=np.int64)
    diff_idx = np.nonzero(mv != rv)[0]
    ops_of = {i: c["cmd"]["op"] for i, c in enumerate(m.commands)}
    variants = None
    if len(diff_idx):
        dark_p, shape_p = oracle_variants(params, eng)
        md = run_model(dark_p, v["init"], acts, force=force)
        ms = run_model(shape_p, v["init"], acts, force=force)
        has_rsv = any(c["cmd"]["op"] >= 10 and not ((c["cmd"]["op"] == 10 and M.resolve_params(params)["FEAT_CORNERS"]) or
                                                    (c["cmd"]["op"] == 11 and M.resolve_params(params)["FEAT_DSBLIT"]))
                      for c in m.commands)
        has_char = any(c["cmd"]["op"] == M.OP_CHAR for c in m.commands)
        light_q2 = tuple(M.resolve_params(params)["TEXT_W_LIGHT"]) != M.TEXT_W_LIGHT_IN_USE
        variants = (md, ms)
        dv = md.mem.a[addrs_np[diff_idx]].astype(np.int64)
        sv = ms.mem.a[addrs_np[diff_idx]].astype(np.int64)
        for j, ix in enumerate(diff_idx.tolist()):
            a = addrs[ix]
            mci = model_last.get(a, (None, None))[0]
            rci = rtl_last.get(a)
            op = ops_of.get(rci if rci is not None and rci >= 0 else mci, -1)
            if rci is not None and rci <= -1000:
                cls = "TIMING"                    # a push the old engine accepted and the (slower) model dropped
            elif (mci in ood_ci) or (rci in ood_ci) or nondet or (rci is not None and (rci in discarded or rci == -3)) or (mci in discarded):
                cls = "OOD"
            # a word equal to the model reshaped to the old engine carries an approved change, written by the command
            # itself or copied onward by a later BLIT/COPY/CBLIT/SBLIT/DSBLIT (propagated: same class)
            elif rv[ix] == dv[j]:
                cls = "IMP-TEXT-DARK"
            elif rv[ix] == sv[j] and (op >= 10 or has_rsv):
                cls = "IMP-RESERVED-NOOP"
            elif rv[ix] == sv[j] and light_q2:
                cls = "PARAM-Q2"
            elif rv[ix] == sv[j] and has_char:
                cls = "IMP-TEXT-DARK"
            else:
                cls = "UNAPPROVED"
            out["words"][cls] += 1
            out["by_op"]["%s/%s" % (M.OP_NAMES.get(op, str(op)), cls)] += 1
            if cls == "UNAPPROVED" and len(out["unapproved"]) < 6:
                out["unapproved"].append({"kind": "memory", "addr": a, "model": int(mv[ix]), "rtl": int(rv[ix]),
                                          "model_writer": mci, "rtl_writer": rci, "op": M.OP_NAMES.get(op, str(op))})
    # the first command whose own writes differ (address set or values): the place to start a shrink
    if len(diff_idx):
        mw = collections.defaultdict(dict)
        for ci_, op_, a_, v_ in m.write_log:
            for x, y in zip(np.asarray(a_).ravel().tolist(), np.asarray(v_).ravel().tolist()):
                mw[ci_][x] = y
        rw = collections.defaultdict(dict)
        for ci_, a_, d_ in res["writes"]:
            rw[mci_of(ci_)][a_] = d_
        for ci_ in sorted(set(mw) | set(k for k in rw if k >= 0)):
            if mw.get(ci_, {}) != rw.get(ci_, {}):
                A_, B_ = mw.get(ci_, {}), rw.get(ci_, {})
                bad = [x for x in sorted(set(A_) | set(B_)) if A_.get(x) != B_.get(x)]
                out["first_divergent"] = {"ci": ci_, "cmd": m.commands[ci_]["cmd"] if ci_ < len(m.commands) else None,
                                          "domain": m.commands[ci_]["domain"] if ci_ < len(m.commands) else None,
                                          "state": m.exec_state.get(ci_), "n_bad": len(bad), "model_words": len(A_), "rtl_words": len(B_),
                                          "examples": [(x, A_.get(x), B_.get(x)) for x in bad[:4]]}
                break
    # protocol violations
    for kind, ci, ln in res["prot"]:
        ci = mci_of(ci)
        cls = "OOD" if (ci in ood_ci or ci < 0) else "UNAPPROVED"
        out["classes"][cls] += 1
        out["notes"].append("protocol %s len %d by command %d (%s)" % (kind, ln, ci, cls))
        if cls == "UNAPPROVED":
            out["unapproved"].append({"kind": "protocol", "what": kind, "len": ln, "ci": ci})
    for h in res["hang"]:
        out["unapproved"].append({"kind": "hang", "at": h}); out["classes"]["UNAPPROVED"] += 1
    # fills
    mf = list(m.fill_vals)
    lost = {t for t, _ in res["lost"]}
    rf = {t: (line, vact, words) for t, line, vact, words in res["fills"]}
    fw = p["FILL_WORDS"]
    for t, n, vals in mf:
        q, stalled = m.read_state.get(t, (0, False))
        if t in lost:
            out["classes"]["IMP-FILL-NOLOSS"] += 1; continue
        if t not in rf:
            out["classes"]["UNAPPROVED"] += 1; out["unapproved"].append({"kind": "fill-missing", "t": t}); continue
        line, vact, words = rf[t]
        nw = len(vals)
        got = np.array(words[:nw], dtype=np.int64)
        if line != n or len(got) != nw or not np.array_equal(got, vals):
            if q and not stalled:
                out["classes"]["TIMING"] += 1
            elif variants is not None and any(t2 == t and len(v2) == nw and np.array_equal(got, v2) for t2, _, v2 in variants[1].fill_vals):
                out["classes"]["IMP-TEXT-DARK" if any(c["cmd"]["op"] == M.OP_CHAR for c in m.commands) else "IMP-RESERVED-NOOP"] += 1
            elif nondet:
                out["classes"]["OOD"] += 1
            else:
                out["classes"]["UNAPPROVED"] += 1
                bad = int(np.count_nonzero(got != vals)) if len(got) == nw else -1
                out["unapproved"].append({"kind": "fill", "t": t, "line": n, "rtl_line": line, "words_differ": bad})
        else:
            out["classes"]["IDENTICAL"] += 1
    # register reads
    soc = replay_soc(M.resolve_params(params), v["init"], acts)
    mreads = {e["t"]: e for e in m.trace if e["ev"] == "read"}
    for t, e in sorted(mreads.items()):
        off = e["off"]
        mvv = e["v"]
        if t not in res["q"]:
            out["unapproved"].append({"kind": "read-missing", "t": t}); out["classes"]["UNAPPROVED"] += 1; continue
        rvv = soc[t](res["q"][t]) if t in soc else None
        if mvv is None or rvv is None:
            continue
        o2 = off & 0x1FC
        if o2 == M.R_GFX_STATUS:
            mvv &= STATUS_CAPS_MASK; rvv &= STATUS_CAPS_MASK
        if mvv == rvv:
            out["classes"]["IDENTICAL"] += 1
            continue
        q, stalled = m.read_state.get(t, (0, False))
        if (q and not stalled) or (acc_timing and o2 in (M.R_GFX_COUNTS, M.R_GFX_STATUS, M.R_FB_GO)):
            out["classes"]["TIMING"] += 1
        elif nondet or any(c["domain"] == "ood" for c in m.commands):
            out["classes"]["OOD"] += 1
        else:
            out["classes"]["UNAPPROVED"] += 1
            out["unapproved"].append({"kind": "read", "t": t, "off": "0x%03X" % off, "model": "0x%08X" % mvv, "rtl": "0x%08X" % rvv})
    # final state (when not stalled at the end)
    if res["z"] and not m.stalled:
        full, idle, free, fence, ovf, disp, pend, fdl, fseen = res["z"]
        st = m.status()
        for name, a, b in (("fence", fence, st["fence"]), ("ovf", ovf, st["ovf"]), ("idle", idle, st["idle"]), ("free", free, st["free"]),
                           ("disp", disp, st["disp"]), ("pending", pend, st["pending"]), ("fill_seen", fseen, st["fill_seen"]),
                           ("fill_done_line", fdl, st["fill_done_line"])):
            if a != b:
                cls = "OOD" if nondet else ("TIMING" if acc_timing and name in ("ovf", "free") else "UNAPPROVED")
                out["classes"][cls] += 1
                if cls == "UNAPPROVED":
                    out["unapproved"].append({"kind": "final", "field": name, "model": b, "rtl": a})
    # retirement order (C-RET-1): the ops the old engine retired, in order, vs the model's
    rtl_ret = [(mci_of(ci), op) for ci, op, *_ in res["ret"] if ci >= 0]
    rtl_ret = [x for x in rtl_ret if x[0] not in discarded]      # retired by the old engine before a reset the model reached first
    mod_ret = [(e["ci"], e["op"]) for e in m.trace if e["ev"] == "retire"]
    if rtl_ret != mod_ret and not nondet:
        out["classes"]["UNAPPROVED"] += 1
        k = next((i for i, (x, y) in enumerate(zip(rtl_ret, mod_ret)) if x != y), min(len(rtl_ret), len(mod_ret)))
        out["unapproved"].append({"kind": "retire-order", "first": k, "rtl": rtl_ret[k:k + 3], "model": mod_ret[k:k + 3],
                                  "n_rtl": len(rtl_ret), "n_model": len(mod_ret)})
    # acceptance (C-Q-2/3): pushes accepted by the engine vs the model
    rtl_acc = sum(a for _, a in res["acc"])
    if acc_timing:
        out["classes"]["TIMING"] += 1
    elif rtl_acc != len(m.commands):
        # exact only when every stall began with the model's queue empty (otherwise the model still held commands the old
        # engine had already executed: the transaction-level model is the slowest legal engine)
        q_reason = "UNAPPROVED" if (m.stall_queue and not any(m.stall_queue)) else "TIMING"
        out["classes"][q_reason] += 1
        if q_reason == "UNAPPROVED":
            out["unapproved"].append({"kind": "accepted", "model": len(m.commands), "rtl": rtl_acc})
    for c in out["words"]:
        out["classes"][c] += 1 if out["words"][c] else 0
    out["n_cmds"] = len(m.commands)
    out["ops"] = collections.Counter(M.OP_NAMES[c["cmd"]["op"]] for c in m.commands)
    out["cost"] = [r for r in res["ret"]]
    out["verdict"] = "UNAPPROVED" if out["unapproved"] else ("IDENTICAL" if set(out["classes"]) <= {"IDENTICAL"} else
                                                            "+".join(sorted(c for c in out["classes"] if c != "IDENTICAL")))
    out["rules"] = v.get("rules", [])
    return out


def replay_soc(p, init, actions):
    """-> {t: fn(q) -> register value} with the SoC state as it is at each read."""
    o = []
    soc = Soc(p, o)
    if init.get("text_light"):
        soc.text_light = 1
    if init.get("dbuf_cpu"):
        soc.cpu = 1
    fns = {}
    for t, a in enumerate(actions):
        k = a["a"]
        if k == "mmio_w":
            soc.mmio_w(a["off"], a["v"])
        elif k == "mmio_r":
            snap = Soc(p, [])
            snap.__dict__.update({kk: (dict(vv) if isinstance(vv, dict) else vv) for kk, vv in soc.__dict__.items() if kk != "o"})
            off = a["off"]
            fns[t] = (lambda s, off: (lambda q: s.mmio_r(off, q)))(snap, off)
        elif k == "sticky":
            soc.blt_idx = 0 if (a["field"] & 7) >= 6 else (a["field"] & 7) + 1
        elif k == "clut":
            soc.clut_idx = (a["idx"] + len(a["v"])) & 0xFF
        elif k == "rc":
            soc.rc_idx = (a["idx"] + len(a["v"])) & 15
        elif k == "font":
            soc.mmio_w(M.R_FONT_IDX, a["idx"])
            for v in a["v"]:
                soc.mmio_w(M.R_FONT_DATA, v)
        elif k == "text_light":
            soc.text_light = a["v"] & 1
        elif k == "dbuf_cpu":
            soc.mmio_w(M.R_DBUF_CPU, a["v"])
        elif k == "view":
            for f in ("en", "base", "stride", "dbl"):
                if f in a:
                    soc.view[f] = a[f]
    return fns


def one(args):
    path, idx, line = args
    v = json.loads(line)
    p = M.resolve_params(dict(v["params"]))
    eng, key = oracle_of(p)
    vvp = build(key, os.path.join(WORK, "build"))
    m = run_model(dict(v["params"]), v["init"], v["actions"])
    lines, _, _ = stimulus(p, v["init"], v["actions"], model=m)
    res = run_bench(vvp, lines, "%s_%04d_%d" % (os.path.basename(path).split(".")[0], idx, os.getpid()))
    try:
        r = compare(v, res, p, eng, m)
    except Exception as e:  # noqa: BLE001
        r = {"name": v["set"] + "/" + v["name"], "verdict": "ERROR", "error": repr(e), "classes": {}, "unapproved": [], "rules": v.get("rules", [])}
    r["file"] = os.path.basename(path); r["index"] = idx
    return r


def jsonable(o):
    if isinstance(o, collections.Counter):
        return {(" ".join(k) if isinstance(k, tuple) else k): v for k, v in o.items()}
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, tuple):
        return list(o)
    raise TypeError(type(o))


SELFTEST = [  # (set, name, the expected verdict) built in-process from the generator: no vector files needed
    ("V-RECT", "rect_w400_h360_buf1", "IDENTICAL"), ("V-RECT", "addr_straddle_359_360_buf1", "IDENTICAL"),
    ("V-BAR", "bar_h10_lit11_w33", "IDENTICAL"), ("V-BLIT", "overlap_B_down_one_cmd", "IDENTICAL"),
    ("V-CHAR", "ram_codes_40_light0", "IMP-TEXT-DARK"), ("V-CHAR", "ram_codes_40_light1", "IDENTICAL"), ("V-BLIT", "key_and_blend", "IDENTICAL"),
    ("V-ESC", "ALL_OFF_op12_sub0", "IMP-RESERVED-NOOP"), ("V-STAT", "ovf_saturates", "IDENTICAL"),
    ("V-FENCE", "fence_alone", "IDENTICAL"), ("V-DBUF", "flips_2_before_vblank", "IDENTICAL"),
]


def selftest():
    import gen_vectors as G
    want = {(s, n): e for s, n, e in SELFTEST}
    cases = [c for c in G.directed_cases(sorted({s for s, _, _ in SELFTEST})) if (c[0], c[1]) in want]
    bad = 0
    for s, name, rules, params, init, acts in cases:
        v = {"set": s, "name": name, "rules": rules, "params": params, "init": init, "actions": acts}
        line = json.dumps(v)
        r = one(("selftest", 0, line))
        ok = r["verdict"] == want[(s, name)]
        bad += not ok
        print("%-5s %-44s %s" % ("ok" if ok else "FAIL", s + "/" + name, r["verdict"]))
    # the comparator must notice a wrong model: the RECT vector against a model that draws one column short
    s, name, rules, params, init, acts = next(c for c in cases if c[1] == "rect_w400_h360_buf1")
    p = M.resolve_params(dict(params)); eng, key = oracle_of(p)
    m = run_model(dict(params, MUTANT="M1_RECT_SHORT"), init, acts)
    res = run_bench(build(key, os.path.join(WORK, "build")), stimulus(p, init, acts, model=m)[0], "selftest_mut")
    r = compare({"set": s, "name": name, "actions": acts, "params": dict(params, MUTANT="M1_RECT_SHORT"), "init": init}, res, p, eng, m)
    ok = r["verdict"] == "UNAPPROVED"
    bad += not ok
    print("%-5s %-44s %s" % ("ok" if ok else "FAIL", "comparator notices a one-column-short model", r["verdict"]))
    missing = len(SELFTEST) - len(cases)
    print("selftest: %d checks, %d failed%s" % (len(cases) + 1, bad, (", %d cases not generated" % missing) if missing else ""))
    return 1 if bad or missing else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vectors", default=DEFAULT_VECTORS)
    ap.add_argument("--sets", default="")
    ap.add_argument("--names", default="", help="comma-separated substrings of vector names")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--quick", action="store_true", help="every 7th vector of each set")
    ap.add_argument("--selftest", action="store_true", help="a few vectors of each kind: the make test-host gate")
    ap.add_argument("--out", default=os.path.join(WORK, "results.json"))
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not os.path.isdir(a.vectors):
        raise SystemExit("no vectors in %s: run tools/talos_v2_model/gen_vectors.py first" % a.vectors)
    sets = [s for s in a.sets.split(",") if s]
    names = [s for s in a.names.split(",") if s]
    work = []
    for fn in sorted(os.listdir(a.vectors)):
        if not fn.endswith(".jsonl") or (sets and fn[:-6] not in sets):
            continue
        for i, line in enumerate(open(os.path.join(a.vectors, fn))):
            if a.quick and i % 7:
                continue
            if names and not any(n in line[:400] for n in names):
                continue
            work.append((os.path.join(a.vectors, fn), i, line))
    # build every needed configuration once, serially
    for _, _, line in work:
        v = json.loads(line)
        build(oracle_of(M.resolve_params(dict(v["params"])))[1], os.path.join(WORK, "build"))
    os.makedirs(WORK, exist_ok=True)
    t0 = time.time()
    with mp.Pool(a.jobs) as pool:
        results = pool.map(one, work, chunksize=1)
    verd = collections.Counter(r["verdict"] for r in results)
    print("%d vectors in %.0f s: %s" % (len(results), time.time() - t0, dict(verd)))
    for r in results:
        if r["verdict"] in ("UNAPPROVED", "ERROR"):
            print("  %-52s %s %s" % (r["name"], r["verdict"], json.dumps(r.get("unapproved") or r.get("error"), default=jsonable)[:300]))
    with open(a.out, "w") as f:
        json.dump(results, f, default=jsonable)
    print("results:", os.path.relpath(a.out, ROOT))
    return 0 if not any(r["verdict"] == "ERROR" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
