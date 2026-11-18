"""A meter project: a folder with meter.json (+ thumb.json, presets.json, README.md, LICENSE, src/). meter.json is the same file the Tau Alpha repository keeps for its own meters, so a shipped meter is a valid project.

Fields (docs/MANIFEST.md): schema 1, id (32 hex digits, `tau-meter id new`), name (1..23 printable ASCII), version, author, licence, homepage, source (optional link), abi ("1.0": the minor the meter NEEDS), tau_min,
params_rev, params [{key, label, type u8|u16|bool|enum, min, max, step, default, values}], p2 {cmds, max_cycles, compose, label}, sources (default src/*.c)."""
import json, re, secrets, sys
from pathlib import Path
from . import SDK_ROOT
import meter_desc, meter_bin, mpreset

class ProjectError(ValueError):
    pass


def new_id():
    """128 random bits as 32 lower-case hex digits (spec: ids are random, nobody keeps a registry)"""
    return secrets.token_hex(16)


def slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_") or "meter"


def load(path):
    """-> normalized manifest dict (adds key, index, p2 defaults, presets); raises ProjectError in plain words"""
    root = Path(path)
    mf = root / "meter.json"
    if not mf.is_file(): raise ProjectError("%s has no meter.json" % root)
    try: d = json.loads(mf.read_text())
    except ValueError as e: raise ProjectError("meter.json is not valid JSON (%s)" % e)
    for k in ("id", "name", "version", "author", "licence", "params_rev"):
        if k not in d: raise ProjectError("meter.json has no %r" % k)
    if not (isinstance(d["id"], str) and meter_bin.valid_id(d["id"])): raise ProjectError("the id %r is not 32 lower-case hex digits: make one with `tau-meter id new`" % (d["id"],))
    d.setdefault("abi", "1.0"); d.setdefault("tau_min", "0.7"); d.setdefault("params", []); d.setdefault("homepage", "")
    d.setdefault("key", slug(d["name"]))
    h = int(d["id"][:8], 16)
    d.setdefault("index", h & 0xFFFF)
    p2 = d.setdefault("p2", {}); p2.setdefault("cmds", 0); p2.setdefault("max_cycles", 0); p2.setdefault("compose", False); p2.setdefault("slot", 0)
    pj = root / "presets.json"
    d["presets"] = json.loads(pj.read_text())["presets"] if pj.is_file() else []
    for i, p in enumerate(d["params"]):
        for k in ("key", "label", "type"):
            if k not in p: raise ProjectError("params[%d] has no %r" % (i, k))
        if p["type"] not in ("u8", "u16", "bool", "enum"): raise ProjectError("params[%d] (%s): type %r is not u8, u16, bool or enum" % (i, p["key"], p["type"]))
        if p["type"] == "enum" and not p.get("values"): raise ProjectError("params[%d] (%s) is an enum without values" % (i, p["key"]))
        if p["type"] in ("u8", "u16") and not all(k in p for k in ("min", "max", "step")): raise ProjectError("params[%d] (%s) needs min, max and step" % (i, p["key"]))
    return d


def thumb(path):
    tf = Path(path) / "thumb.json"
    if not tf.is_file(): raise ProjectError("%s has no thumb.json (the 56 x 32 preview shown in the METERS list; `tau-meter thumb` makes one)" % path)
    return json.loads(tf.read_text())


def sources(path, d):
    root = Path(path)
    names = d.get("sources") or sorted(str(p.relative_to(root)).replace("\\", "/") for p in (root / "src").glob("*.c"))
    if not names: raise ProjectError("no source files: put your C in src/*.c or list them under \"sources\" in meter.json")
    for n in names:
        if not (root / n).is_file(): raise ProjectError("source file %s does not exist" % n)
    return names
