#!/usr/bin/env python3
"""The meter descriptor a pack carries (B-812, roadmap item 4): everything the firmware needs to list, configure and preview a meter whose drawing code is only in its pack: the display name, the Configure page's
parameter descriptors, the presets, the preview (palette spec + run-length stream), the compose rule and the draw limits. Built from meters/<key>/meter.json + thumb.json, linked into the pack as section .mtr_desc
(fw2/ui/meter_pack.ld), read by fw2/cores/meter_desc_core.h. This module also has the manifest -> descriptor rows that tools/player2/gen_meters_p2.py uses for the built-in tables, so both come from one place.

Layout (little endian; every offset from the start of the descriptor; the whole is a multiple of 4 bytes):
  0 u32 'MDSC'   4 u16 version (1)   6 u16 total length (trailer included)   8 u8 nparams   9 u8 npresets   10 u16 cmds   12 u32 cmp (compose code)   16 u32 max_cycles
  20 u16 name offset   22 u16 params offset   24 u16 presets offset   26 u16 palette offset   28 u16 rle offset   30 u16 rle length
  32 u16 manifest id (the meter's id in tau-assets.bin METR and the reports)   34 u16 slot   36 u32 reserved (0)
  params:  nparams x 16 B { u16 label, u8 type, u8 hidden, u16 min, u16 max, u16 step, u8 nval, u8 when_idx, u16 when_val, u16 vals offset }   (vals: nval u16 string offsets)
  presets: npresets x stride { u16 name offset, u16 value[nparams] } (stride = 2 + 2 nparams rounded up to 4)
  palette: 8 x 8 B { u16 fixed, u8 a, u8 b, u8 t, u8 n, u16 0 }   (the preview's colours: a fixed RGB565 (a = 255), a theme role, or a ramp between two roles, as fw2/generated/meter_thumbs.h)
  rle:     the preview's 56 x 32 run-length stream ((palette index << 5) | (run - 1))
  strings: NUL-terminated;   trailer: u32 total length, u32 'DSC1' (the loader finds the descriptor from the end of the pack's image)"""
import json, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
MAGIC, TRAILER, VERSION = 0x4353444D, 0x31435344, 1
MAX_PARAMS, MAX_PRESETS, MAX_NVAL, MAX_STR = 48, 16, 12, 23
ROLES = ["bg_top", "bg_bottom", "surface", "surface_track", "text_primary", "text_secondary", "accent", "on_accent", "accent2", "ok", "warn", "danger", "base", "chrome", "pill", "error", "faint", "splash_bg", "splash_bar", "fs_red", "fs_track"]
HIDE_EXACT = {"info"}
def is_colour(k, p): return p["type"] == "u16" and (k.startswith("custom_") or k in ("color_green", "color_yellow", "color_red"))
def hidden(k, p): return 1 if k in HIDE_EXACT else 0
def param_rows(d):
    """one dict per parameter in the manifest's order: key, label (upper case), type (0 number, 1 choice, 2 on/off, 3 colour), hidden, min, max, step, vals (the choice names), when_idx, when_val"""
    keys = [p["key"] for p in d["params"]]; rows = []
    for p in d["params"]:
        k = p["key"]; vals = [v.replace("_", " ") for v in p["values"]] if p["type"] == "enum" else []
        w = p.get("when"); wi, wv = 255, 0
        if isinstance(w, dict) and len(w) >= 1:
            wk, wval = next(iter(w.items()))
            if wk in keys and isinstance(wval, int): wi, wv = keys.index(wk), wval
        ty = 3 if is_colour(k, p) else {"enum": 1, "bool": 2}.get(p["type"], 0)
        lo, hi, st = (0, len(vals) - 1, 1) if ty == 1 else (0, 1, 1) if ty == 2 else (0, 65535, 1) if ty == 3 else (p["min"], p["max"], p["step"])
        rows.append({"key": k, "label": p["label"].upper(), "type": ty, "hidden": hidden(k, p), "min": lo, "max": hi, "step": st, "vals": vals, "when_idx": wi, "when_val": wv})
    return rows
def gate_code_int(d, spec):
    """{"param": key, "in": [values]} -> bits 14..10 the parameter's index, 9..6 a mask of the values (each < 4) that enable it, bit 15 set"""
    keys = [p["key"] for p in d["params"]]; idx = keys.index(spec["param"]); mask = 0
    assert idx < 32 and all(0 <= v < 4 for v in spec["in"]), (d["key"], spec)
    for v in spec["in"]: mask |= 1 << v
    return 0x8000 | (idx << 10) | (mask << 6)
def cmp_code_int(d):
    """the compose code of the registry: 0 never, 1 always, or a gate (composed only while a parameter has one of the listed values)"""
    c = d["p2"].get("compose", False)
    return gate_code_int(d, c) if isinstance(c, dict) else (1 if c else 0)
def palette_entries(thumb):
    out = []
    for fixed, sp in zip(thumb["palette"], thumb.get("palette_roles", [None] * 8)):
        if sp is None: out.append((fixed, 255, 0, 0, 0))
        elif isinstance(sp, str): out.append((0, ROLES.index(sp), 0, 0, 0))
        else:
            ra, rb, tt, nn = sp["ramp"]; assert 0 <= tt <= nn and nn > 0
            out.append((0, ROLES.index(ra), ROLES.index(rb), tt, nn))
    return out
def build_desc(d, thumb):
    rows = param_rows(d); np_, npre = len(rows), len(d["presets"])
    assert np_ <= MAX_PARAMS and 0 < npre <= MAX_PRESETS and len(d["name"]) <= MAX_STR
    keys = [p["key"] for p in d["params"]]
    pool = bytearray(); offs = {}
    def S(text):
        assert len(text) <= MAX_STR, text
        if text not in offs: offs[text] = len(pool); pool.extend(text.encode("latin1") + b"\0")
        return offs[text]
    name_s = S(d["p2"].get("label", d["name"])); label_s = [S(r["label"]) for r in rows]; val_s = [[S(v) for v in r["vals"]] for r in rows]; pre_s = [S(p["name"]) for p in d["presets"]]
    rle = bytes.fromhex(thumb["rle"]); assert (thumb["w"], thumb["h"]) == (56, 32) and sum((b & 31) + 1 for b in rle) == 56 * 32 and len(thumb["palette"]) == 8
    stride = (2 + 2 * np_ + 3) & ~3
    off = 40; params_off = off; off += 16 * np_
    vals_off = []
    for r in rows: vals_off.append(off if r["vals"] else 0); off += 2 * len(r["vals"])
    off = (off + 3) & ~3; presets_off = off; off += stride * npre
    pal_off = off; off += 8 * 8; rle_off = off; off += len(rle); off = (off + 3) & ~3
    pool_off = off; total = ((pool_off + len(pool) + 3) & ~3) + 8
    b = bytearray(total)
    struct.pack_into("<IHHBBHII", b, 0, MAGIC, VERSION, total, np_, npre, d["p2"].get("cmds", 0), cmp_code_int(d), d["p2"].get("max_cycles", 0))
    struct.pack_into("<HHHHHH", b, 20, pool_off + name_s, params_off, presets_off, pal_off, rle_off, len(rle))
    struct.pack_into("<HHI", b, 32, d["index"], d["p2"]["slot"], 0)
    for i, r in enumerate(rows):
        struct.pack_into("<HBBHHHBBHH", b, params_off + 16 * i, pool_off + label_s[i], r["type"], r["hidden"], r["min"], r["max"], r["step"], len(r["vals"]), r["when_idx"], r["when_val"], vals_off[i])
        for j, so in enumerate(val_s[i]): struct.pack_into("<H", b, vals_off[i] + 2 * j, pool_off + so)
    for k, p in enumerate(d["presets"]):
        struct.pack_into("<H", b, presets_off + stride * k, pool_off + pre_s[k])
        for i, key in enumerate(keys): struct.pack_into("<H", b, presets_off + stride * k + 2 + 2 * i, p["values"][key])
    for i, (fx, a, bb, t, n) in enumerate(palette_entries(thumb)): struct.pack_into("<HBBBBH", b, pal_off + 8 * i, fx, a, bb, t, n, 0)
    b[rle_off:rle_off + len(rle)] = rle; b[pool_off:pool_off + len(pool)] = pool
    struct.pack_into("<II", b, total - 8, total, TRAILER)
    return bytes(b)
def parse_desc(b):
    """An independent reader: returns a dict, or raises ValueError (the checks fw2/cores/meter_desc_core.h makes too)."""
    def u16(o): return struct.unpack_from("<H", b, o)[0]
    if len(b) < 48 or len(b) % 4: raise ValueError("size")
    magic, ver, total, np_, npre, cmds, cmp_, maxc = struct.unpack_from("<IHHBBHII", b, 0)
    if magic != MAGIC or ver != VERSION or total != len(b): raise ValueError("header")
    if struct.unpack_from("<II", b, total - 8) != (total, TRAILER): raise ValueError("trailer")
    name_o, params_o, presets_o, pal_o, rle_o, rle_n = struct.unpack_from("<HHHHHH", b, 20)
    def string(o):
        if o >= total - 8: raise ValueError("string offset")
        e = b.index(b"\0", o, total - 8); return b[o:e].decode("latin1")
    mid, slot_, _ = struct.unpack_from("<HHI", b, 32)
    out = {"mid": mid, "slot": slot_, "name": string(name_o), "cmds": cmds, "cmp": cmp_, "max_cycles": maxc, "params": [], "presets": [], "palette": [], "rle": bytes(b[rle_o:rle_o + rle_n])}
    for i in range(np_):
        lab, ty, hid, lo, hi, st, nv, wi, wv, vo = struct.unpack_from("<HBBHHHBBHH", b, params_o + 16 * i)
        out["params"].append({"label": string(lab), "type": ty, "hidden": hid, "min": lo, "max": hi, "step": st, "vals": [string(u16(vo + 2 * j)) for j in range(nv)], "when_idx": wi, "when_val": wv})
    stride = (2 + 2 * np_ + 3) & ~3
    for k in range(npre): out["presets"].append({"name": string(u16(presets_o + stride * k)), "values": [u16(presets_o + stride * k + 2 + 2 * i) for i in range(np_)]})
    for i in range(8): out["palette"].append(struct.unpack_from("<HBBBB", b, pal_o + 8 * i))
    return out
def for_meter(key):
    d = json.loads((ROOT / "meters" / key / "meter.json").read_text()); t = json.loads((ROOT / "meters" / key / "thumb.json").read_text())
    return build_desc(d, t)
def c_array(blob, name="mtr_pack_desc"):
    lines = ["/* generated by tools/player2/meter_desc.py from meters/<key>/meter.json and thumb.json: do not edit. The descriptor a pack carries (section .mtr_desc, last in the pack image). */",
             "__attribute__((section(\".mtr_desc\"), used, aligned(4))) static const unsigned char %s[%d] = {" % (name, len(blob))]
    for i in range(0, len(blob), 16): lines.append("    " + ", ".join("0x%02X" % x for x in blob[i:i + 16]) + ",")
    return "\n".join(lines + ["};", ""])
if __name__ == "__main__":
    for k in sys.argv[1:]:
        blob = for_meter(k); print("%s: %d B, %s" % (k, len(blob), parse_desc(blob)["name"]))
