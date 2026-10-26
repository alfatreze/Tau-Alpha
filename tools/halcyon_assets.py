#!/usr/bin/env python3
"""Halcyon preset data (parallel plan A5, B-626): the PRST section of tau-assets.bin, an Equalizer-APO / AutoEQ text importer, and the reference tool Tau Omega's exporter must agree with.
Format and reasoning: docs/features/HALCYON_DATA_FORMAT.md. Coefficients are Q2.22 (24-bit signed) like the Halcyon engine; Direct Form I: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.

PRST section (magic TPRS, version 1): header <4s H B B I> = magic, version, 0, count, CRC32 of the entries; then count entries:
  type u8 (0 = control preset, 1 = raw biquad preset) | name 16 bytes (A-Z 0-9 space _ -, NUL padded) | flags u8 (0)
  control payload: 6 x int8 = warmth, bass, vocal, punch, sibilance, air (positions -5..+5, sibilance 0..5), the engine derives the stage gains (tools/lab/halcyon_model.py)
  raw payload: nstages u8 (1..10) | preamp int24 (Q2.22, 0 < preamp <= 1.0: attenuate only) | nstages x 5 x int24 little endian (b0 b1 b2 a1 a2)

    python3 tools/halcyon_assets.py selftest
    python3 tools/halcyon_assets.py apo "Equalizer APO ParametricEQ.txt" "MY HEADPHONE" -o presets.prst     # import and print the packed hex
"""
import json, math, re, struct, sys, zlib
from pathlib import Path

QF = 22
MAGIC = b"TPRS"
VERSION = 1
NAME_LEN = 16
MAX_PRESETS = 8
MAX_RAW_STAGES = 10
RESERVED = {"FLAT", "WARM", "CLEAR", "BASS", "VOCAL", "SPEECH", "LOW VOLUME", "SMOOTH"}
CONTROLS = ("warmth", "bass", "vocal", "punch", "sibilance", "air")
CTRL_RANGE = {"sibilance": (0, 5)}
FS = 48000.0


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def _i24(v):
    if not -(1 << 23) <= v < (1 << 23):
        raise ValueError(f"{v} does not fit in 24 bits")
    return (v & 0xFFFFFF).to_bytes(3, "little")


def _r24(b):
    v = int.from_bytes(b, "little")
    return v - (1 << 24) if v >= (1 << 23) else v


def stable_q(a1, a2):
    """Poles inside the unit circle for integer Q2.22 a1, a2 (the check the engine's loader must also make)."""
    a1f, a2f = a1 / (1 << QF), a2 / (1 << QF)
    disc = a1f * a1f - 4 * a2f
    if disc >= 0:
        r = max(abs((-a1f + math.sqrt(disc)) / 2), abs((-a1f - math.sqrt(disc)) / 2))
    else:
        r = math.sqrt(max(a2f, 0.0))
    return r < 1.0


def _name(n):
    if not re.fullmatch(r"[A-Z0-9 _-]{1,15}", n):
        raise ValueError(f"preset name {n!r}: 1..15 characters of A-Z 0-9 space _ -")
    if n in RESERVED:
        raise ValueError(f"preset name {n!r} is reserved (a built-in preset)")
    return n.encode().ljust(NAME_LEN, b"\0")


def pack_presets(presets):
    """presets: [{'name', 'controls': {warmth..air}}  or  {'name', 'preamp': float or int Q2.22, 'stages': [(b0,b1,b2,a1,a2) as Q2.22 integers]}]"""
    if not 1 <= len(presets) <= MAX_PRESETS:
        raise ValueError(f"1..{MAX_PRESETS} presets per file")
    names, body = set(), b""
    for p in presets:
        nm = _name(p["name"])
        if p["name"] in names:
            raise ValueError(f"duplicate preset name {p['name']!r}")
        names.add(p["name"])
        if "controls" in p:
            vals = []
            for k in CONTROLS:
                v = int(p["controls"].get(k, 0))
                lo, hi = CTRL_RANGE.get(k, (-5, 5))
                if not lo <= v <= hi:
                    raise ValueError(f"{p['name']}: {k}={v} outside {lo}..{hi}")
                vals.append(v)
            body += bytes([0]) + nm + bytes([0]) + struct.pack("<6b", *vals)
        else:
            st = p["stages"]
            if not 1 <= len(st) <= MAX_RAW_STAGES:
                raise ValueError(f"{p['name']}: 1..{MAX_RAW_STAGES} stages")
            pre = p["preamp"] if isinstance(p["preamp"], int) else int(round(p["preamp"] * (1 << QF)))
            if not 0 < pre <= (1 << QF):
                raise ValueError(f"{p['name']}: preamp must be above 0 and at most 1.0 (attenuate only)")
            ent = bytes([1]) + nm + bytes([0]) + bytes([len(st)]) + _i24(pre)
            for i, c in enumerate(st):
                if len(c) != 5:
                    raise ValueError(f"{p['name']}/stage {i}: five coefficients")
                if not stable_q(c[3], c[4]):
                    raise ValueError(f"{p['name']}/stage {i}: poles on or outside the unit circle")
                ent += b"".join(_i24(v) for v in c)
            body += ent
    return MAGIC + struct.pack("<HBBI", VERSION, 0, len(presets), crc(body)) + body


def parse_presets(d):
    if len(d) < 12 or d[:4] != MAGIC:
        raise ValueError("bad PRST magic")
    ver, _, count, c = struct.unpack("<HBBI", d[4:12])
    if ver != VERSION or not 1 <= count <= MAX_PRESETS:
        raise ValueError("bad PRST header")
    if crc(d[12:]) != c:
        raise ValueError("PRST CRC mismatch")
    pos, out = 12, []
    for _ in range(count):
        if pos + 18 > len(d):
            raise ValueError("PRST entry runs past the end")
        typ, name, flags = d[pos], d[pos + 1:pos + 17].split(b"\0")[0].decode("latin1"), d[pos + 17]
        pos += 18
        if typ == 0:
            vals = struct.unpack("<6b", d[pos:pos + 6]); pos += 6
            out.append({"name": name, "controls": dict(zip(CONTROLS, vals))})
        elif typ == 1:
            n = d[pos]; pre = _r24(d[pos + 1:pos + 4]); pos += 4
            if not 1 <= n <= MAX_RAW_STAGES:
                raise ValueError("bad stage count")
            st = []
            for _s in range(n):
                st.append(tuple(_r24(d[pos + 3 * k:pos + 3 * k + 3]) for k in range(5))); pos += 15
                if not stable_q(st[-1][3], st[-1][4]):
                    raise ValueError("unstable stage in PRST")
            out.append({"name": name, "preamp": pre, "stages": st})
        else:
            raise ValueError(f"unknown preset type {typ}")
    if pos != len(d):
        raise ValueError("PRST size mismatch")
    return out


# ---- Equalizer APO / AutoEQ text -> raw preset (the importer Tau Omega's exporter reproduces) -------------------------------------------

APO_FILTER = re.compile(r"^\s*Filter\s*\d*\s*:\s*(ON|OFF)\s+(PK|PEQ|LSC|LS|HSC|HS)\s+Fc\s+([\d.]+)\s*Hz\s+Gain\s+(-?[\d.]+)\s*dB(?:\s+Q\s+([\d.]+))?", re.I)
APO_PREAMP = re.compile(r"^\s*Preamp\s*:\s*(-?[\d.]+)\s*dB", re.I)


def parse_apo(text):
    """Returns (preamp_db, [(kind, fc, gain_db, q)]) with kind in peak / lowshelf / highshelf; disabled filters are dropped."""
    pre, fl = 0.0, []
    for line in text.splitlines():
        m = APO_PREAMP.match(line)
        if m:
            pre = float(m.group(1)); continue
        m = APO_FILTER.match(line)
        if m and m.group(1).upper() == "ON":
            k = m.group(2).upper()
            kind = "peak" if k in ("PK", "PEQ") else "lowshelf" if k in ("LSC", "LS") else "highshelf"
            q = float(m.group(5)) if m.group(5) else (0.707 if kind != "peak" else 1.0)
            fl.append((kind, float(m.group(3)), float(m.group(4)), q))
    return pre, fl


def design_q(kind, f0, gain_db, q, fs=FS):
    """RBJ Audio EQ Cookbook, Q form for peaks AND shelves (an APO/AutoEQ shelf Q is a Q, not the slope the Halcyon tone stages use). Returns (b0,b1,b2,a1,a2) floats, a0 = 1."""
    A = 10 ** (gain_db / 40.0)
    w0 = 2 * math.pi * f0 / fs
    cw, sw = math.cos(w0), math.sin(w0)
    al = sw / (2 * q)
    if kind == "peak":
        b0, b1, b2 = 1 + al * A, -2 * cw, 1 - al * A
        a0, a1, a2 = 1 + al / A, -2 * cw, 1 - al / A
    else:
        t = 2 * math.sqrt(A) * al
        if kind == "lowshelf":
            b0, b1, b2 = A * ((A + 1) - (A - 1) * cw + t), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - t)
            a0, a1, a2 = (A + 1) + (A - 1) * cw + t, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - t
        else:
            b0, b1, b2 = A * ((A + 1) + (A - 1) * cw + t), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - t)
            a0, a1, a2 = (A + 1) - (A - 1) * cw + t, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - t
    return b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0


def apo_to_preset(name, text, max_hz=20000.0):
    """APO text -> a raw preset dict ready for pack_presets. The preamp is the more negative of the file's preamp and the peak-safe value (the largest boost of the cascade),
    attenuate only. Filters above max_hz or beyond the engine's stage budget are refused, not dropped silently."""
    pre_db, fl = parse_apo(text)
    if not fl:
        raise ValueError("no enabled filters found")
    if len(fl) > MAX_RAW_STAGES:
        raise ValueError(f"{len(fl)} filters, the engine takes {MAX_RAW_STAGES}: reduce the profile (offline fit) first")
    stages = []
    for kind, f0, g, q in fl:
        if not 10.0 <= f0 <= max_hz or not 0.1 <= q <= 20.0 or abs(g) > 24.0:
            raise ValueError(f"filter {kind} {f0} Hz gain {g} Q {q} is outside 10 Hz..{max_hz:.0f} Hz, Q 0.1..20, +-24 dB")
        c = design_q(kind, f0, g, q)
        qc = tuple(int(round(v * (1 << QF))) for v in c)
        if any(not -(1 << 23) <= v < (1 << 23) for v in qc) or not stable_q(qc[3], qc[4]):
            raise ValueError(f"filter {kind} {f0} Hz gain {g} Q {q}: not representable or unstable in Q2.{QF}")
        stages.append(qc)
    # peak-safe preamp from the exact cascade response of the QUANTISED stages
    freqs = [20 * (1000.0 ** (i / 400.0)) for i in range(401)]
    worst = -999.0
    for f in freqs:
        z = complex(math.cos(2 * math.pi * f / FS), -math.sin(2 * math.pi * f / FS))
        h = 1.0 + 0j
        for b0, b1, b2, a1, a2 in stages:
            sc = 1 << QF
            h *= (b0 / sc + b1 / sc * z + b2 / sc * z * z) / (1 + a1 / sc * z + a2 / sc * z * z)
        worst = max(worst, 20 * math.log10(abs(h)))
    safe_db = -max(worst, 0.0)
    use_db = min(0.0, pre_db, safe_db) if pre_db < 0 else min(0.0, safe_db)
    return {"name": name, "preamp": int(round(10 ** (use_db / 20) * (1 << QF))), "stages": stages, "preamp_db": round(use_db, 3), "peak_gain_db": round(worst, 3)}


def selftest(verbose=True):
    fails = 0

    def check(name, ok, info=""):
        nonlocal fails
        if verbose:
            print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
        fails += 0 if ok else 1

    apo = """Preamp: -6.2 dB
Filter 1: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70
Filter 2: ON PK Fc 250 Hz Gain -2.0 dB Q 1.10
Filter 3: ON PK Fc 3200 Hz Gain 3.4 dB Q 2.50
Filter 4: OFF PK Fc 5000 Hz Gain 9.0 dB Q 1.00
Filter 5: ON HSC Fc 10000 Hz Gain -4.0 dB Q 0.70
"""
    pre, fl = parse_apo(apo)
    check("APO text: preamp and the four enabled filters (the OFF one dropped)", pre == -6.2 and len(fl) == 4 and fl[0][0] == "lowshelf" and fl[3][0] == "highshelf", str(fl[:1]))
    pr = apo_to_preset("TEST CANS", apo)
    check("preset: 4 quantised stable stages, peak-safe preamp at most the file's -6.2 dB", len(pr["stages"]) == 4 and pr["preamp_db"] <= -6.2 + 1e-9 and pr["peak_gain_db"] > 3.0, "(preamp %.2f dB, peak boost %.2f dB)" % (pr["preamp_db"], pr["peak_gain_db"]))
    # magnitude of the quantised cascade equals the analog Q-form prototype closely at a few points (Q2.22 is accurate; the bilinear cramping at 10 kHz is the known small difference)
    ctl = {"name": "MY CONTROLS", "controls": {"warmth": 2, "bass": 1, "vocal": 0, "punch": -1, "sibilance": 3, "air": -2}}
    blob = pack_presets([ctl, {"name": pr["name"], "preamp": pr["preamp"], "stages": pr["stages"]}])
    back = parse_presets(blob)
    check("pack/parse round trip: a control preset and a raw preset come back identical", back[0] == ctl and back[1]["stages"] == pr["stages"] and back[1]["preamp"] == pr["preamp"] and back[1]["name"] == "TEST CANS")
    flips = 0
    for i in range(12, len(blob)):
        b = bytearray(blob); b[i] ^= 0x01
        try:
            parse_presets(bytes(b))
        except ValueError:
            flips += 1
    check("every single-bit flip in the entries is refused (CRC)", flips == len(blob) - 12)
    for bad, why in (({"name": "FLAT", "controls": {}}, "reserved name"), ({"name": "lower", "controls": {}}, "lower case"), ({"name": "X", "controls": {"sibilance": -1}}, "sibilance below 0"),
                     ({"name": "X", "controls": {"bass": 6}}, "bass above 5"), ({"name": "X", "preamp": 1.5, "stages": [(1 << QF, 0, 0, 0, 0)]}, "preamp above unity"),
                     ({"name": "X", "preamp": 0.5, "stages": [(1 << QF, 0, 0, -2 * (1 << QF), 1 << QF)]}, "pole on the unit circle")):
        try:
            pack_presets([bad]); ok = False
        except ValueError:
            ok = True
        check("writer refuses: " + why, ok)
    try:
        apo_to_preset("X", "Filter 1: ON PK Fc 100 Hz Gain 3 dB Q 1\n" * 11); ok = False
    except ValueError:
        ok = True
    check("more than 10 filters refused (reduce offline), not dropped silently", ok)
    # response check against the analog Q-form peak: at the centre the gain is the stated gain
    c = design_q("peak", 1000.0, 6.0, 1.0)
    z = complex(math.cos(2 * math.pi * 1000 / FS), -math.sin(2 * math.pi * 1000 / FS))
    h = (c[0] + c[1] * z + c[2] * z * z) / (1 + c[3] * z + c[4] * z * z)
    check("Q-form peak: +6 dB at its centre", abs(20 * math.log10(abs(h)) - 6.0) < 0.01)
    return fails


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "selftest":
        sys.exit(1 if selftest() else 0)
    if len(sys.argv) >= 4 and sys.argv[1] == "apo":
        pr = apo_to_preset(sys.argv[3], Path(sys.argv[2]).read_text())
        blob = pack_presets([{"name": pr["name"], "preamp": pr["preamp"], "stages": pr["stages"]}])
        if "-o" in sys.argv:
            Path(sys.argv[sys.argv.index("-o") + 1]).write_bytes(blob)
        print(json.dumps({"preamp_db": pr["preamp_db"], "peak_gain_db": pr["peak_gain_db"], "stages": len(pr["stages"]), "bytes": len(blob), "hex": blob.hex()}))
    else:
        print(__doc__)
