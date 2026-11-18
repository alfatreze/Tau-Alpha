"""The conformance suite: every rule the player enforces on a meter, checked on the host (`tau-meter test FILE.tmeter`).

Groups (each result has a stable id; docs/TESTING.md lists them with what to do about a failure):
  PKG-*     the package rules of spec section 1 (tools/meter_package.py verify: zip, size caps, paths, manifest, pins, the pack's own header)
  LOAD-*    the loader's refusals (fw2/cores/meter_pack_core.h E21..E29) and the ABI and tau_min rules
  DESC-*    the descriptor the pack carries (name, parameters, limits, preview) and PRESET-*, PARAMS-REV
  STATIC-*  what the code could do: system calls, absolute addresses (recursive descent, conformance/rv.py)
  RUN-*     what it does when run on the host runner (tau_meter/runner.py): faults, calls outside the host table, draws outside the box, the draw-command and cycle budgets, the hang and strike verdicts,
            determinism, stack, the declared ABI
Exit codes: 0 every check passed (warnings allowed), 1 a check failed, 2 the input cannot be tested at all (not a file, no toolchain)."""
import hashlib, json, os, re, struct, sys, tempfile, zipfile
from pathlib import Path
from tau_meter import runner as rn, toolchain, png
from . import rv, canvas
import meter_package as mpk, meter_bin as mb, meter_desc as md, mpreset

PASS, FAIL, WARN, SKIP, INFO = "pass", "fail", "warn", "skip", "info"
ROLES_DARK = [0x1082, 0x0000, 0x2945, 0x18E3, 0xFFFF, 0x94B2, 0xFD20, 0x0000, 0x07FF, 0x07E0, 0xFFE0, 0xF800, 0x0841, 0x2124, 0x39E7, 0xF800, 0x4208, 0x0000, 0xFD20, 0xF800, 0x2104]
BG = 0x0841
BOXES = {"main": (16, 154, 368, 122), "cfg": (20, 26, 360, 100), "full": (0, 0, 400, 323), "small": (40, 200, 120, 40), "tiny": (100, 100, 16, 8)}   # the player's three boxes: Now Playing, the Configure preview, fullscreen (fw2/ui/regions.h, configure_view.c, fullscreen_view.c); small and tiny are robustness boxes
ROBUST = ("small", "tiny")
STACK_WARN, STACK_FAIL = 2048, 3584          # policy: the player's stack is 5 KiB (0x1400) and its own peak was measured at about 1.7 KiB, so a meter should stay well under 3.5 KiB (Layered Wave, the biggest, uses 3.0 to 3.3 KiB)
WINDOW_CODE_ORG = 0x24970000
SCRATCH_ORG, SCRATCH_CAP = 0x26C00, 0x1000
FW_ABI_MINOR, FW_ABI_MAJOR = 2, 1


class Result:
    def __init__(self, cid, status, title, detail=""):
        self.id, self.status, self.title, self.detail = cid, status, title, detail

    def line(self):
        mark = {PASS: "PASS", FAIL: "FAIL", WARN: "warn", SKIP: "skip", INFO: "info"}[self.status]
        s = "[%s] %-14s %s" % (mark, self.id, self.title)
        return s + ("\n         " + self.detail.replace("\n", "\n         ") if self.detail else "")

    def as_dict(self): return {"id": self.id, "status": self.status, "title": self.title, "detail": self.detail}


class Report:
    def __init__(self, path):
        self.path = str(path); self.results = []; self.exit_override = None; self.preview = None; self.measured = {}

    def add(self, cid, status, title, detail=""):
        self.results.append(Result(cid, status, title, detail)); return status

    @property
    def failed(self): return [r for r in self.results if r.status == FAIL]

    def exit_code(self):
        if self.exit_override is not None: return self.exit_override
        return 1 if self.failed else 0

    def ids(self, status=FAIL): return [r.id for r in self.results if r.status == status]

    def text(self):
        out = ["tau-meter test %s" % Path(self.path).name]
        out += [r.line() for r in self.results]
        n = {s: sum(1 for r in self.results if r.status == s) for s in (PASS, FAIL, WARN, SKIP)}
        out.append("")
        out.append("%d passed, %d failed, %d warnings, %d skipped: %s" % (n[PASS], n[FAIL], n[WARN], n[SKIP], "NOT CONFORMANT" if n[FAIL] else "conformant"))
        return "\n".join(out)

    def json(self):
        return json.dumps({"file": self.path, "exit": self.exit_code(), "results": [r.as_dict() for r in self.results], "measured": self.measured}, indent=2)


# ------------------------------------------------------------------------------------------------ the package and the loader rules
PKG_RULES = [   # (regex on the reason from tools/meter_package.py verify, check id)
    (r"is not a zip|cannot be read from the zip|does not exist", "PKG-ZIP"),
    (r"the package is .* bytes|declare .* bytes in total", "PKG-SIZE"),
    (r"appears twice|path .* (is absolute|goes outside|is not a plain)", "PKG-PATH"),
    (r"is not part of a meter package|is missing from the package|is not pinned|meter.json pins", "PKG-FILES"),
    (r"does not match its SHA-256 pin", "PKG-PIN"),
    (r"signature", "PKG-SIG"),
    (r"meter.bin", "PKG-PACK"),
    (r"meter.json|the id |the name |version|abi |tau_min|params_rev|files must|author|licence|homepage|source", "PKG-MANIFEST"),
]


def classify(reason):
    for rx, cid in PKG_RULES:
        if re.search(rx, reason): return cid
    return "PKG-OTHER"


def loader_checks(rep, pack, man):
    """the refusals of mpk_install (E21..E29) and of the window loader, in the firmware's order; returns the header dict or None"""
    if len(pack) < 48 or pack[:4] != b"TMPK":
        rep.add("LOAD-E21", FAIL, "E21: the pack has no TMPK header", "meter.bin must start with the 48-byte TMPK header the SDK writes."); return None
    abi, mid, size, origin, entry, crc0, d_off, d_size, s_org, bss, ps_off, ps_size = struct.unpack_from("<HHIIIIIIIIII", pack, 4)
    h = dict(abi=abi, mid=mid, size=size, origin=origin, entry=entry, data_off=d_off, data_size=d_size, scr_org=s_org, bss=bss, ps_off=ps_off, ps_size=ps_size)
    ok = True
    if (abi >> 8) != FW_ABI_MAJOR or (abi & 255) > FW_ABI_MINOR:
        ok = False; rep.add("LOAD-E22", FAIL, "E22: the meter needs ABI %d.%d; this player speaks ABI %d.0 to %d.%d" % (abi >> 8, abi & 255, FW_ABI_MAJOR, FW_ABI_MAJOR, FW_ABI_MINOR), "Rebuild with the current SDK, or lower \"abi\" in meter.json if the meter does not use the newer entries.")
    if origin != WINDOW_CODE_ORG:
        ok = False; rep.add("LOAD-E24", FAIL, "E24: the pack is linked for 0x%08X, not for the window at 0x%08X" % (origin, WINDOW_CODE_ORG), "Use `tau-meter build`: it links with the SDK's linker script.")
    if size == 0 or size > 0x10000 or ps_off < size or ps_off > 0x10000 or ps_size > 0x10000 - ps_off or d_off > size or d_size > size - d_off:
        ok = False; rep.add("LOAD-E25", FAIL, "E25: the image (%d bytes) and its state (%d bytes at +%d) do not fit the 64 KiB window" % (size, ps_size, ps_off), "Reduce code size, constants or TAU_PSTATE arrays.")
    if 48 + size <= len(pack) and (zlib_crc(pack[48:48 + size]) != crc0):
        ok = False; rep.add("LOAD-E26", FAIL, "E26: the image does not match the CRC in the pack header", "The pack file is damaged or was edited after it was built.")
    if entry >= size:
        ok = False; rep.add("LOAD-E27", FAIL, "E27: the entry point (+%d) is outside the image (%d bytes)" % (entry, size))
    if s_org != SCRATCH_ORG:
        ok = False; rep.add("LOAD-E28", FAIL, "E28: the pack's scratch origin is 0x%X, the player's is 0x%X" % (s_org, SCRATCH_ORG), "Use the SDK's linker script.")
    bss_off = (d_size + 3) & ~3
    if bss_off > SCRATCH_CAP or bss > SCRATCH_CAP - bss_off:
        ok = False; rep.add("LOAD-E29", FAIL, "E29: .data (%d bytes) and .bss (%d bytes) need more than the %d bytes of scratch memory that all meters share" % (d_size, bss, SCRATCH_CAP), "Shrink static arrays, or move a history into TAU_PSTATE (PSRAM).")
    if ok: rep.add("LOAD", PASS, "the loader accepts the pack (header, ABI %d.%d, window, state, CRC, entry, scratch %d of %d bytes)" % (abi >> 8, abi & 255, bss_off + bss, SCRATCH_CAP))
    return h if ok else None


def zlib_crc(b):
    import zlib
    return zlib.crc32(b) & 0xFFFFFFFF


def tau_min_check(rep, man):
    t = man.get("tau_min")
    if t is None or str(t).split(".")[:2] == ["0", "0"]: rep.add("LOAD-TAU", PASS, "tau_min: no minimum (any Tau that can load meters)"); return
    a, b = (int(x) for x in str(t).split(".")[:2])
    if (a, b) < (0, 7): rep.add("LOAD-TAU", WARN, "tau_min %s is older than 0.7, the first Tau with loadable meters: a lower number changes nothing" % t)
    else: rep.add("LOAD-TAU", PASS, "tau_min %s" % t)


# ------------------------------------------------------------------------------------------------ descriptor, presets
def descriptor(pack):
    """-> the parsed descriptor dict (tools/player2/meter_desc.py parse_desc) of a pack"""
    image = pack[48:]
    tot, mg = struct.unpack_from("<II", image, len(image) - 8)
    if mg != 0x31435344 or tot > len(image) or tot < 48 or tot & 3: raise ValueError("the descriptor trailer is missing or damaged")
    return md.parse_desc(bytes(image[len(image) - tot:]))


def desc_checks(rep, pack, man, files):
    try: d = descriptor(pack)
    except Exception as e:
        rep.add("DESC-PARSE", FAIL, "the pack's descriptor cannot be read (%s)" % e, "The descriptor is built from meter.json and thumb.json by the SDK; do not edit meter.bin by hand."); return None
    rep.add("DESC-PARSE", PASS, "descriptor: %r, %d parameter(s)" % (d["name"], len(d["params"])))
    bad = []
    for i, p in enumerate(d["params"]):
        if p["type"] == 1 and not 2 <= len(p["vals"]) <= md.MAX_NVAL: bad.append("parameter %d (%s) is a choice with %d values (2 to %d allowed)" % (i, p["label"], len(p["vals"]), md.MAX_NVAL))
        if p["type"] in (0, 3) and p["min"] > p["max"]: bad.append("parameter %d (%s): min %d is above max %d" % (i, p["label"], p["min"], p["max"]))
        if p["type"] == 0 and p["step"] == 0: bad.append("parameter %d (%s): step is 0" % (i, p["label"]))
        if p.get("def") is not None and not p["min"] <= p["def"] <= p["max"]: bad.append("parameter %d (%s): default %d is outside %d..%d" % (i, p["label"], p["def"], p["min"], p["max"]))
        if p["when_idx"] != 255 and p["when_idx"] >= len(d["params"]): bad.append("parameter %d (%s): 'when' names parameter %d, which does not exist" % (i, p["label"], p["when_idx"]))
    hashes = [p.get("key") for p in d["params"]]
    if len(set(hashes)) != len(hashes): bad.append("two parameter keys have the same 16-bit hash: rename one")
    if len(d["params"]) > md.MAX_PARAMS: bad.append("%d parameters (the limit is %d)" % (len(d["params"]), md.MAX_PARAMS))
    rep.add("DESC-PARAMS", FAIL if bad else PASS, "parameters: ranges, defaults, choices" if not bad else "the parameter descriptors are not valid", "\n".join(bad))
    if d["cmds"] == 0 or d["max_cycles"] == 0:
        rep.add("DESC-LIMITS", WARN, "the descriptor declares no %s: the player cannot protect itself from this meter" % ("command ceiling (cmds)" if d["cmds"] == 0 else "cycle limit (max_cycles)"), "Set p2.cmds and p2.max_cycles in meter.json from the measured run (`tau-meter test` prints them).")
    else: rep.add("DESC-LIMITS", PASS, "declared limits: %d draw commands, %s cycles per tick" % (d["cmds"], format(d["max_cycles"], ",")))
    try:
        di = mb.desc_info(pack[48:])
        rep.add("DESC-PREVIEW", PASS if mb.preview_ok(di["preview"]) else FAIL, "the METERS-list preview is 56 x 32 pixels" if mb.preview_ok(di["preview"]) else "the preview stream is not 56 x 32 pixels in 1 KiB")
    except mb.BinError as e: rep.add("DESC-PREVIEW", FAIL, "the preview cannot be read (%s)" % e)
    return d


def preset_checks(rep, files, man, d):
    names = sorted(n for n in files if n.startswith("presets/"))
    if not names:
        rep.add("PRESET", PASS, "no factory presets (allowed)"); return []
    want = int(man["id"][:8], 16) & 0xFFFF; bad = []; out = []
    if "presets/index.tpi" not in files: bad.append("presets/index.tpi is missing")
    else:
        try:
            mid, ent = mpreset.parse_index(files["presets/index.tpi"])
            if mid != want: bad.append("index.tpi carries meter id %d, the id hash's low 16 bits are %d" % (mid, want))
            for e in ent:
                f = "presets/p%d.tpr" % e["id"]
                if f not in files: bad.append("%s is listed in index.tpi but missing" % f); continue
                m2, _, nm, pairs = mpreset.parse_preset(files[f])
                if m2 != want: bad.append("%s carries meter id %d, expected %d" % (f, m2, want))
                hs = {p.get("key"): p for p in d["params"]}
                vals = {}
                for h, v in pairs:
                    if h not in hs: bad.append("%s sets a parameter (hash %04x) the descriptor does not have" % (f, h)); continue
                    p = hs[h]
                    if not p["min"] <= v <= p["max"]: bad.append("%s (%s): %s = %d is outside %d..%d" % (f, nm, p["label"], v, p["min"], p["max"]))
                    vals[h] = v
                out.append({"name": nm, "values": vals})
        except ValueError as ex: bad.append("a preset file is not valid: %s" % ex)
    rep.add("PRESET", FAIL if bad else PASS, "factory presets: %d, files, ids, values in range" % len(out) if not bad else "the factory presets are not valid", "\n".join(bad))
    return out


def params_rev_check(rep, man, d, previous):
    if not previous: return
    try:
        pm, pfiles = mpk.load(previous)
        pd = descriptor(pfiles["meter.bin"])
    except Exception as e:
        rep.add("PARAMS-REV", SKIP, "the previous package could not be read (%s)" % e); return
    sig = lambda dd: [(p.get("key"), p["type"], p["min"], p["max"], len(p["vals"])) for p in dd["params"]]
    changed = sig(d) != sig(pd)
    if changed and man["params_rev"] <= pm["params_rev"]:
        rep.add("PARAMS-REV", FAIL, "the parameter set changed since the previous package, but params_rev is still %d" % man["params_rev"], "Raise params_rev in meter.json: the player drops a saved tuning whose params_rev no longer matches.")
    elif not changed and man["params_rev"] != pm["params_rev"]:
        rep.add("PARAMS-REV", WARN, "params_rev changed (%d -> %d) but the parameters did not: every user's saved tuning of this meter is dropped for nothing" % (pm["params_rev"], man["params_rev"]))
    else: rep.add("PARAMS-REV", PASS, "params_rev %d is consistent with the previous package" % man["params_rev"])


# ------------------------------------------------------------------------------------------------ static analysis
def static_checks(rep, pack, h, profile):
    image = bytes(pack[48:48 + h["size"]])
    lo = h["scr_org"]; hi = lo + ((h["data_size"] + 3) & ~3) + h["bss"]
    r = rv.scan(image, h["entry"], lo, hi)
    insn = [(o, m) for o, m in r["findings"] if "system call" in m or "CSR" in m or "csr" in m or "fence.i" in m or "does not have" in m or "ecall" in m or "ebreak" in m]
    hw = [(o, m) for o, m in r["findings"] if "hardware register" in m]
    other = [(o, m) for o, m in r["findings"] if (o, m) not in insn and (o, m) not in hw]
    def fmt(lst): return "\n".join("+0x%04x: %s\n      %s" % (o, m, "\n      ".join(rv.disasm_at(image, o, 1))) for o, m in lst[:6])
    rep.add("STATIC-INSN", FAIL if insn else PASS, "no system calls, CSR or fence.i instructions in the code that can be reached" if not insn else "the code contains instructions a meter may not use", fmt(insn))
    if profile == "shipped" and hw: rep.add("STATIC-HW", INFO, "first-party profile: %d direct hardware-register access(es) allowed (Scope capture block, cycle counter, PSRAM status)" % len(hw))
    else: rep.add("STATIC-ADDR", FAIL if (hw or other) else PASS, "the code uses only the host table and its own memory (no absolute addresses)" if not (hw or other) else "the code uses an address that is not its own memory", fmt(hw + other))
    rep.add("STATIC-FLOW", INFO, "%d instructions reached by direct control flow; %d jumps through a register (the host table and function pointers) are left to the run" % (r["reached"], r["indirect"]))


# ------------------------------------------------------------------------------------------------ the runs
def spec_for(frame, kind="music"):
    if kind == "silence": return bytes(16)
    if kind == "loud": return bytes([255] * 16)
    out = bytearray()
    for b in range(16):
        ph = (frame * 7 + b * 29) % 120
        tri = ph if ph < 60 else 120 - ph                                  # a triangle wave, integer only
        base = 40 + (tri * 3) + ((frame * 13 + b * 17) % 23)
        out.append(min(255, base * (16 - b // 2) // 16))
    return bytes(out)


def stats_for(spec, paused):
    mx = max(spec); pk = 24575 * mx // 255
    return (pk * 6 // 10, pk * 55 // 100, 120 if mx else 0, sum(i * v for i, v in enumerate(spec)) * 255 // (15 * max(1, sum(spec))) if mx else 0, 24 if mx else 0, min(255, sum(abs(spec[i] - spec[i - 1]) for i in range(1, 16)) // 8), 1 if mx > 200 else 0)


def scenario(name, quick=False):
    """-> [dict(frame, box, force, paused, degrade, spec_kind)] for the named scenario"""
    t = []
    def add(n, box="main", force=0, paused=0, degrade=0, kind="music"):
        for i in range(n): t.append(dict(box=box, force=1 if (force and i == 0) else 0, paused=paused, degrade=degrade, kind=kind))
    if name == "music":
        add(1, force=1); add(18); add(4, kind="silence"); add(1, force=1); add(24)
    elif name == "pause":
        add(1, force=1); add(6); add(6, paused=1, kind="silence"); add(4)
    elif name == "move":
        add(1, force=1); add(6); add(1, "cfg", force=1); add(5, "cfg"); add(1, "full", force=1); add(5, "full"); add(1, "main", force=1); add(4)
    elif name == "loud":
        add(1, "full", force=1); add(10, "full", kind="loud")
    elif name == "degrade":
        add(1, force=1); add(4); add(4, degrade=1); add(4, degrade=2); add(2)
    elif name == "tiny":
        add(1, "small", force=1); add(3, "small"); add(1, "tiny", force=1); add(5, "tiny")
    return t


def run_scenario(path, rep_state, ticks, params, limit, abi_minor, profile, roles=ROLES_DARK, stackfill=0, want_frames=None, tc=None):
    """-> dict(ticks=[Tick], calls_hash, violations, faults, ...) for one fresh runner over `ticks`"""
    out = {"ticks": [], "hash": hashlib.sha256(), "bounds": [], "frames": {}}
    cv = canvas.Canvas() if want_frames else None
    palette_i = 0
    with rn.Runner(path, profile=profile, abi_minor=abi_minor, limit=limit, params=params, roles=roles, stackfill=stackfill, tc=tc) as r:
        for n, tk in enumerate(ticks):
            box = BOXES[tk["box"]]; spec = spec_for(n, tk["kind"]); pk = 24575 * max(spec) // 255
            if tk["paused"]: pk = 0
            st = stats_for(spec, tk["paused"])
            t = r.tick(n + 1, 26, box, BG, tk["force"], tk["paused"], pk, pk * 9 // 10, tk["degrade"], spec, stats=st if abi_minor >= 1 else None)
            t.box = box; t.forced = bool(tk["force"]); t.index = n
            out["ticks"].append(t)
            for c in t.calls: out["hash"].update((" ".join(c) + "\n").encode())
            out["hash"].update(("r %s %d\n" % (t.status, t.ret)).encode())
            for c in t.calls:
                for v in canvas.bounds_violations(c, box): out["bounds"].append((n, c, v, tk["box"]))
            if cv is not None:
                pi = 0
                for c in t.calls:
                    if c[0] == "clut_load":
                        start = int(c[1]); cols = t.palettes[pi] if pi < len(t.palettes) else []; pi += 1
                        for i, col in enumerate(cols): cv.clut[(start + i) & 255] = col
                    else: cv.apply(c)
                if n in want_frames:
                    x, y, w, h = box
                    out["frames"][n] = (box, cv.crop_rgb(x, y, w, h))
            if t.status != "ok" or t.struck and False: break
    out["hash"] = out["hash"].hexdigest()
    return out


FAULT_IDS = [("hardware register", "RUN-HW"), ("writes into the inputs", "RUN-INPUT-WRITE"), ("PSRAM state", "RUN-PSTATE-WORD"), ("misaligned", "RUN-MISALIGNED"),
             ("jumps to an address outside", "RUN-JUMP"), ("system call", "RUN-INSN"), ("CSR", "RUN-INSN"), ("fence.i", "RUN-INSN"), ("compressed", "RUN-INSN"), ("illegal", "RUN-INSN"), ("does not have", "RUN-INSN"),
             ("writes", "RUN-MEM-WRITE"), ("reads", "RUN-MEM-READ"), ("instruction alias", "RUN-MEM-READ")]


def fault_id(msg):
    for k, cid in FAULT_IDS:
        if k in msg: return cid
    return "RUN-FAULT"


def run_checks(rep, path, pack, man, h, d, presets, profile, quick, preview_path, tc):
    abi_minor = h["abi"] & 255
    params0 = [p.get("def", p["min"]) if p.get("def") is not None else p["min"] for p in d["params"]]
    limit, ceiling = d["max_cycles"], d["cmds"]
    with tempfile.TemporaryDirectory() as td:
        binf = Path(td) / "meter.bin"; binf.write_bytes(pack)
        names = ["music", "loud", "tiny"] if quick else ["music", "pause", "move", "loud", "degrade", "tiny"]
        results = {}
        max_cyc = max_cmds = max_cyc_f = max_cmds_f = max_stack = 0; fault_done = set(); bounds_done = set(); over_done = set()
        retmismatch = False
        for nm in names:
            try: res = run_scenario(str(binf), None, scenario(nm), params0, limit, abi_minor, profile, tc=tc)
            except rn.RunnerError as e:
                rep.add("RUN-" + nm.upper(), FAIL, "the host runner failed: %s" % e); continue
            results[nm] = res
            for t in res["ticks"]:
                if t.fault:
                    cid = fault_id(t.fault["msg"])
                    if t.fault["msg"].startswith("jumps") and t.fault["addr"] == 0 and abi_minor < 2: cid = "RUN-ABI-CALL"
                    if cid not in fault_done:
                        fault_done.add(cid)
                        msg = t.fault["msg"] if cid != "RUN-ABI-CALL" else "calls a host-table entry that does not exist at the ABI it declares (abi 1.%d): scroll_left needs \"abi\": \"1.2\"" % abi_minor
                        det = "scenario %r, tick %d: %s (code +0x%x, address 0x%08x)\n%s" % (nm, t.index, msg, t.fault["pc"], t.fault["addr"], "\n".join(rv.disasm_at(pack[48:], t.fault["pc"], 2)) if t.fault["pc"] < len(pack) else "")
                        rep.add(cid, FAIL, "the meter " + msg.split(" (")[0] if cid != "RUN-ABI-CALL" else msg, det)
                if t.status == "cut":
                    if "RUN-HANG" not in fault_done:
                        fault_done.add("RUN-HANG")
                        rep.add("RUN-HANG", FAIL, "a tick did not return: the hardware watchdog would cut it off after %s cycles and strike the meter out at once (D-T15)" % format(t.wd, ","), "scenario %r, tick %d. The soft limit is %s cycles; the watchdog window is 4 x that (at least 2,000,000, at most 20,000,000). A cut is final for the session: the player falls back to Winamp Bars." % (nm, t.index, format(limit, ",")))
                if t.status == "ok":
                    max_stack = max(max_stack, t.stack)
                    if t.forced: max_cyc_f = max(max_cyc_f, t.cycles); max_cmds_f = max(max_cmds_f, t.cmds)
                    else:
                        max_cyc = max(max_cyc, t.cycles); max_cmds = max(max_cmds, t.cmds)
                        if limit and t.cycles > limit and "cyc" not in over_done:
                            over_done.add("cyc")
                            rep.add("RUN-OVERRUN", FAIL, "a tick used an estimated %s cycles, over its declared limit of %s: the player counts a strike" % (format(t.cycles, ","), format(limit, ",")), "scenario %r, tick %d. Three strikes disable the meter for the session (one is forgiven per 64 clean ticks, D-M20). Make the tick cheaper or raise p2.max_cycles. (The cycle figure is an estimate: docs/PERFORMANCE.md.)" % (nm, t.index))
                        if ceiling and t.cmds > ceiling and "cmd" not in over_done:
                            over_done.add("cmd")
                            rep.add("RUN-CMDS", FAIL, "a tick issued %d draw commands, over its declared ceiling of %d: the player lowers the meter's detail level" % (t.cmds, ceiling), "scenario %r, tick %d. Each tick over the ceiling raises the meter's own degrade level; a meter that is always over it is drawn every 2nd or 4th window. Draw fewer, bigger commands or raise p2.cmds." % (nm, t.index))
                    if t.ret != t.cmds: retmismatch = True
                if t.pstate_subword and "RUN-PSTATE-WORD" not in fault_done:
                    fault_done.add("RUN-PSTATE-WORD")
                    rep.add("RUN-PSTATE-WORD", WARN, "the meter made %d byte or halfword access(es) to its PSRAM state" % t.pstate_subword, "scenario %r, tick %d. The player's own notes say PSRAM state takes whole 32-bit words (cores/histring_core.h); a compiler may turn a clearing loop into a byte-wise memset. Declare the state as uint32_t and clear it with a word loop." % (nm, t.index))
                if t.in_fields_new and "RUN-ABI-FIELDS" not in fault_done:
                    fault_done.add("RUN-ABI-FIELDS")
                    rep.add("RUN-ABI-FIELDS", FAIL, "the meter reads mtr_in_t fields added in ABI 1.1 but declares abi 1.%d" % abi_minor, "Set \"abi\": \"1.1\" in meter.json (the player then refuses the meter on a Tau that predates the fields instead of feeding it garbage).")
            for (n, c, v, bname) in res["bounds"]:
                cid = "RUN-BOX-SMALL" if bname in ROBUST else "RUN-BOX"
                if cid not in bounds_done:
                    bounds_done.add(cid)
                    nb = len(res["bounds"])
                    if cid == "RUN-BOX": rep.add(cid, FAIL, "the meter drew outside the rectangle it owns", "scenario %r, tick %d: %s\n(%d call(s) in this scenario; the meter owns only (in->x, in->y, in->w, in->h) and must never draw outside it)" % (nm, n, v, nb))
                    else: rep.add(cid, WARN, "the meter draws outside a box smaller than the player gives today (%dx%d)" % BOXES[bname][2:], "scenario %r, tick %d: %s\nThe player's boxes are 368x122, 360x100 and 400x323, so this works today; a meter that sizes itself from in->w and in->h survives a future box." % (nm, n, v))
            if nm in ("tiny",) and not res["bounds"] and not res["ticks"][-1].fault: pass
        for cid in ("RUN-HW", "RUN-INPUT-WRITE", "RUN-MEM-WRITE", "RUN-MEM-READ", "RUN-JUMP", "RUN-INSN"): pass
        if not (fault_done & {c for c in fault_done if c.startswith("RUN-") and c not in ("RUN-HANG", "RUN-ABI-FIELDS", "RUN-PSTATE-WORD")}) and results:
            rep.add("RUN-HOSTONLY", PASS, "every run stayed inside the host table, the meter's own memory and the inputs (%d scenarios)" % len(results))
        if "RUN-BOX" not in bounds_done and results: rep.add("RUN-BOX", PASS, "every draw call stayed inside the meter's rectangle (%d scenarios, boxes %s)" % (len(results), ", ".join(sorted({t.box and "%dx%d" % t.box[2:] for r in results.values() for t in r["ticks"]}))))
        if "RUN-HANG" not in fault_done and results: rep.add("RUN-HANG", PASS, "no tick came near the watchdog window")
        if "cyc" not in over_done and "RUN-HANG" not in fault_done and results:
            rep.add("RUN-OVERRUN", PASS if limit else SKIP, "slowest drawn tick: %s estimated cycles of the declared %s (forced repaint, which the player does not judge: %s)" % (format(max_cyc, ","), format(limit, ",") if limit else "none", format(max_cyc_f, ",")))
        if "cmd" not in over_done and results:
            rep.add("RUN-CMDS", PASS if ceiling else SKIP, "most draw commands in a drawn tick: %d of the declared %s (forced repaint: %d)" % (max_cmds, ceiling or "none", max_cmds_f))
        rep.measured.update({"max_cycles_est": max_cyc, "max_cycles_forced_est": max_cyc_f, "max_cmds": max_cmds, "max_cmds_forced": max_cmds_f, "stack": max_stack, "declared_cycles": limit, "declared_cmds": ceiling})
        if max_stack > STACK_FAIL: rep.add("RUN-STACK", FAIL, "the meter used %d bytes of stack; the policy limit is %d (the player's whole stack is 5 KiB and its own peak is about 1.7 KiB)" % (max_stack, STACK_FAIL), "Avoid large local arrays and deep recursion.")
        elif max_stack > STACK_WARN: rep.add("RUN-STACK", WARN, "the meter used %d bytes of stack (above %d; the failing limit is %d)" % (max_stack, STACK_WARN, STACK_FAIL))
        else: rep.add("RUN-STACK", PASS, "stack use %d bytes (limit %d)" % (max_stack, STACK_FAIL))
        if retmismatch: rep.add("RUN-COUNT", WARN, "the value the entry returns is not the number of draw commands it issued", "TAU_METER() counts them for you; the player trusts its own count, not this one.")
        # the strike model on the measured scenario: a pack that overruns three times is disabled for the session
        if "music" in results and not (fault_done - {"RUN-ABI-FIELDS"}):
            last = [t for t in results["music"]["ticks"] if t.status == "ok" and not t.forced][-1]
            rep.add("RUN-STRIKES", PASS if not last.struck else FAIL, "the player's strike model after the 'music' scenario: %d strike(s), meter %s" % (last.strikes, "STRUCK OUT" if last.struck else "still running"))
        # determinism: the same scenario twice, the second time with the stack filled with garbage (an uninitialised local shows up as a different picture)
        if "music" in results and not (fault_done - {"RUN-PSTATE-WORD"}):
            try:
                again = run_scenario(str(binf), None, scenario("music"), params0, limit, abi_minor, profile, stackfill=0xA5, tc=tc)
                same = again["hash"] == results["music"]["hash"]
                rep.add("RUN-DETERMINISM", PASS if same else FAIL, "the same inputs give the same draw-call trace (also with the stack pre-filled with garbage)" if same else "the same inputs gave a different draw-call trace the second time",
                        "" if same else "The meter's output depends on something other than its inputs and its own state: an uninitialised local variable, or a read of memory that is not its own. Initialise every variable.")
            except rn.RunnerError as e: rep.add("RUN-DETERMINISM", FAIL, "the second run failed: %s" % e)
        # parameters at their extremes and the factory presets
        if not quick and d["params"] and not fault_done:
            bad = []
            sets = []
            for i, p in enumerate(d["params"]):
                if p["type"] in (0, 1, 2, 3) and p.get("hidden", 0) == 0:
                    for v in {p["min"], p["max"]}:
                        pv = list(params0); pv[i] = v; sets.append(("%s=%d" % (p["label"], v), pv))
            for pr in presets:
                pv = list(params0)
                for hh, vv in pr["values"].items():
                    for i, p in enumerate(d["params"]):
                        if p.get("key") == hh: pv[i] = vv
                sets.append(("preset " + pr["name"], pv))
            tk = scenario("music")[:1] + scenario("music")[1:7]
            for label, pv in sets[:60]:
                try: res = run_scenario(str(binf), None, tk, pv, limit, abi_minor, profile, tc=tc)
                except rn.RunnerError as e: bad.append("%s: runner error %s" % (label, e)); continue
                for t in res["ticks"]:
                    if t.fault: bad.append("%s: %s" % (label, t.fault["msg"])); break
                    if t.status == "cut": bad.append("%s: the tick did not return (watchdog)" % label); break
                    if not t.forced and limit and t.cycles > limit: bad.append("%s: %s estimated cycles over the limit %s" % (label, format(t.cycles, ","), format(limit, ","))); break
                if res["bounds"]: bad.append("%s: draws outside the box (%s)" % (label, res["bounds"][0][2]))
            rep.add("RUN-PARAMS", FAIL if bad else PASS, "every parameter at its minimum and maximum, and every factory preset, runs clean (%d settings)" % len(sets[:60]) if not bad else "some parameter settings break the meter", "\n".join(bad[:8]))
        # the preview image
        if preview_path and results.get("music") and not fault_done:
            try:
                frames_at = {1: None, 10: None, 20: None, 40: None}
                res = run_scenario(str(binf), None, scenario("music"), params0, limit, abi_minor, profile, want_frames=set(frames_at), tc=tc)
                rep.preview = write_preview(preview_path, res["frames"], man["name"])
                rep.add("PREVIEW", PASS, "preview image: %s (%d frames of the 'music' scenario)" % (preview_path, len(res["frames"])))
            except Exception as e: rep.add("PREVIEW", WARN, "the preview image could not be made (%s)" % e)
        elif preview_path: rep.add("PREVIEW", SKIP, "no preview image: the meter did not run clean")
    return results


def write_preview(path, frames, title):
    """a grid of the captured frames, 2x scale, on a grey ground"""
    keys = sorted(frames); w = max(frames[k][0][2] for k in keys); h = max(frames[k][0][3] for k in keys); cols = 2; rows = (len(keys) + 1) // 2
    S = 2; pad = 8; W = pad + cols * (w * S + pad); H = pad + rows * (h * S + pad)
    img = bytearray(bytes([40, 40, 44]) * (W * H))
    for n, k in enumerate(keys):
        (bx, by, bw, bh), rgb = frames[k]; ox = pad + (n % cols) * (w * S + pad); oy = pad + (n // cols) * (h * S + pad)
        for y in range(bh):
            row = bytearray()
            for x in range(bw): row += rgb[(y * bw + x) * 3:(y * bw + x) * 3 + 3] * S
            for dy in range(S):
                o = ((oy + y * S + dy) * W + ox) * 3; img[o:o + len(row)] = row
    png.write_png(path, W, H, bytes(img)); return str(path)


# ------------------------------------------------------------------------------------------------ entry
def run(path, profile="author", quick=False, preview=None, previous=None, tc=None):
    path = Path(path); rep = Report(path)
    if not path.is_file():
        rep.add("INPUT", FAIL, "%s does not exist or is not a file" % path); rep.exit_override = 2; return rep
    why, man, files = mpk._verify(path)
    groups = {}
    for w in why: groups.setdefault(classify(w), []).append(w)
    for cid, ws in sorted(groups.items()):
        rep.add(cid, FAIL, {"PKG-ZIP": "the file is not a readable zip package", "PKG-SIZE": "the package is over a size cap", "PKG-PATH": "a file path inside the package is not allowed", "PKG-FILES": "the files in the package are not the files the rules allow",
                            "PKG-PIN": "a file does not match its SHA-256 pin: the package was changed after it was built", "PKG-SIG": "the package carries a signature this version cannot check", "PKG-PACK": "meter.bin is not a valid pack for the window",
                            "PKG-MANIFEST": "meter.json breaks a rule"}.get(cid, "the package breaks a rule"), "\n".join(ws))
    if not why: rep.add("PKG", PASS, "package rules: zip, size caps, paths, manifest fields, id and name, SHA-256 pins, pack header")
    if "meter.bin" not in files or man is None or not isinstance(man, dict):
        rep.add("RUN", SKIP, "nothing further can be checked without a readable meter.json and meter.bin"); return rep
    pack = files["meter.bin"]
    if groups.get("PKG-ZIP") or groups.get("PKG-PATH") or groups.get("PKG-SIZE"):
        rep.add("RUN", SKIP, "the package was refused before any of its files were read"); return rep
    tau_min_check(rep, man)
    ph = [n for n in ("LICENSE", "README.md") if n in files and b"Your Name" in files[n]] + (["meter.json author"] if man.get("author") == "Your Name" else [])
    if ph: rep.add("PKG-PLACEHOLDER", WARN, "the package still carries the template's placeholder text (\"Your Name\") in %s" % ", ".join(ph), "Put your own name in LICENSE and meter.json; edit README.md to describe your meter.")
    h = loader_checks(rep, pack, man)
    if h is None:
        rep.add("RUN", SKIP, "the loader would refuse this pack, so it is never run"); return rep
    d = desc_checks(rep, pack, man, files)
    if d is None: rep.add("RUN", SKIP, "no descriptor: the pack is not run"); return rep
    presets = preset_checks(rep, files, man, d)
    params_rev_check(rep, man, d, previous)
    static_checks(rep, pack, h, profile)
    try: tc = tc or toolchain.find()
    except toolchain.ToolchainError as e:
        rep.add("RUN", FAIL, "no toolchain to build the host runner: %s" % e); rep.exit_override = 2; return rep
    try: run_checks(rep, path, pack, man, h, d, presets, profile, quick, preview, tc)
    except rn.RunnerError as e:
        rep.add("RUN", FAIL, "the host runner could not run the meter: %s" % e); rep.exit_override = 2
    return rep
