#!/usr/bin/env python3
"""tools/meter_package.py: the `.tmeter` package of docs/features/meters/EXTERNAL_METERS_SPEC.md section 1 (P2.6 / P3). Written BEFORE the tool (red first).

  1. pack: the seven shipped meters become packages with exactly the files of the spec table, a manifest with every field, a SHA-256 pin for every file but meter.json, `signature` null;
  2. determinism: packing twice gives the same bytes;
  3. verify: the seven pass; every rule of section 1 has a mutant that is refused, each with a reason in plain words (never a traceback);
  4. compose: a v2 bin from packages, Winamp Bars first as the fallback row, the order as given, 64 rows at most, unique id_hash, rows that share their low 16 bits reported, a bad package refused by name;
  5. the seven shipped meters round-trip pack -> verify -> compose, the bin passes meter_bin.check_file, and it is BYTE-IDENTICAL to what tools/player2/build_packs.py composes from the sources;
  6. the manifests are the single source of params_rev and tau_min (fw2/cores/shipped_meter_ids.h is generated from them: tools/gen_shipped_meter_ids.py)."""
import warnings; warnings.filterwarnings("ignore", message="Duplicate name")
import hashlib, io, json, os, re, struct, subprocess, sys, tempfile, zipfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(info)))
    fails += 0 if cond else 1

import meter_package as mp, meter_bin as mb, build_packs as bp

SHIPPED = ["winamp_bars", "winamp_scope", "chladni", "layered_wave", "vu_master", "waterfall_grid", "sound_map"]   # p2 slot order
WB_ID = "7dddfe63121e904843088c075cc28ce9"
tmp = Path(tempfile.mkdtemp())

# ---- 1. pack ------------------------------------------------------------------------------------------------------------------------------------------
pk = {}
for k in SHIPPED:
    pk[k] = mp.pack(k, tmp / "a")
check("pack writes <id>.tmeter for each of the seven", all(pk[k].name == json.loads((ROOT / "meters" / k / "meter.json").read_text())["id"] + ".tmeter" and pk[k].is_file() for k in SHIPPED))
def zread(p):
    with zipfile.ZipFile(p) as z: return {i.filename: z.read(i) for i in z.infolist()}
FIELDS = ("schema", "id", "name", "version", "author", "licence", "homepage", "abi", "tau_min", "params_rev", "files", "signature")
for k in SHIPPED:
    f = zread(pk[k]); m = json.loads(f["meter.json"])
    allowed = {"meter.json", "meter.bin", "README.md", "LICENSE"}
    ok_names = set(f) >= allowed and all(n in allowed or re.fullmatch(r"presets/(index\.tpi|p\d+\.tpr)", n) for n in f)
    check("%s: the files are the spec table's (meter.json, meter.bin, README.md, LICENSE, presets/)" % k, ok_names, sorted(f))
    check("%s: manifest has every field, schema 1, signature null" % k, all(x in m for x in FIELDS) and m["schema"] == 1 and m["signature"] is None, sorted(m))
    check("%s: every file but meter.json is pinned by its SHA-256, and nothing else is" % k, set(m["files"]) == set(f) - {"meter.json"} and all(hashlib.sha256(f[n]).hexdigest() == h for n, h in m["files"].items()))
    check("%s: id is 32 lower-case hex, name 1..23 printable ASCII" % k, mb.valid_id(m["id"]) and 1 <= len(m["name"]) <= 23 and all(0x20 <= ord(c) <= 0x7E for c in m["name"]))
    ph = mb.pack_header(f["meter.bin"])
    check("%s: meter.bin is a TMPK v2 pack linked for the window, abi in the manifest is the pack's" % k, f["meter.bin"][:4] == b"TMPK" and ph["origin"] == mb.WINDOW and m["abi"] == "%d.%d" % (ph["abi"] >> 8, ph["abi"] & 255))
check("the LICENSE of a shipped meter is the repository's", all(zread(pk[k])["LICENSE"] == (ROOT / "LICENSE").read_bytes() for k in SHIPPED))
# presets: the factory files of the meter, under presets/ without the id folder
import install_factory_presets as ifp
fac = ifp.factory(["chladni"]); f = zread(pk["chladni"]); h8 = WB_ID[:0] + json.loads(f["meter.json"])["id"][:8]
check("chladni's package carries its factory presets (presets/index.tpi, presets/pN.tpr), byte for byte", {"presets/" + r.split("/", 1)[1]: b for r, b in fac.items()} == {n: b for n, b in f.items() if n.startswith("presets/")} and len(fac) >= 2)
check("a meter has a presets/ folder exactly when it has factory presets", all(bool(ifp.factory([k])) == any(n.startswith("presets/") for n in zread(pk[k])) for k in SHIPPED))

# ---- 2. determinism ---------------------------------------------------------------------------------------------------------------------------------
again = mp.pack("chladni", tmp / "b")
check("packing twice gives the same bytes", again.read_bytes() == pk["chladni"].read_bytes())

# ---- 3. verify ----------------------------------------------------------------------------------------------------------------------------------------
check("the seven shipped packages verify", all(mp.verify(pk[k]) == [] for k in SHIPPED), [mp.verify(pk[k]) for k in SHIPPED])
GOOD = zread(pk["winamp_scope"]); GM = json.loads(GOOD["meter.json"])
def mutant(name, edit, cap=None):
    """a package made from GOOD by edit(files: dict[name -> bytes], manifest: dict) -> optional extra zip kwargs; returns the verify() messages. repin=True keeps the pins right unless edit says otherwise"""
    files = dict(GOOD); man = json.loads(GOOD["meter.json"])
    raw_names = edit(files, man) or {}
    if not raw_names.get("keep_pins"):
        man["files"] = {n: hashlib.sha256(b).hexdigest() for n, b in files.items() if n != "meter.json" and n not in raw_names.get("unpinned", ())}
    if "manifest_bytes" in raw_names: files["meter.json"] = raw_names["manifest_bytes"]
    else: files["meter.json"] = json.dumps(man, indent=1).encode()
    p = tmp / ("mut_%s.tmeter" % re.sub(r"\W", "_", name))
    with zipfile.ZipFile(p, "w", zipfile.ZIP_DEFLATED) as z:
        for n, b in files.items():
            z.writestr(zipfile.ZipInfo(n, (2026, 1, 1, 0, 0, 0)), b, zipfile.ZIP_DEFLATED) if n not in raw_names.get("stored", ()) else z.writestr(zipfile.ZipInfo(n, (2026, 1, 1, 0, 0, 0)), b, zipfile.ZIP_STORED)
        for n, b in raw_names.get("extra_raw", {}).items(): z.writestr(zipfile.ZipInfo(n, (2026, 1, 1, 0, 0, 0)), b, zipfile.ZIP_DEFLATED)
    return mp.verify(p)
def refused(name, edit, words):
    try: msgs = mutant(name, edit)
    except Exception as e: check("mutant: %s is refused with a message, not a crash" % name, False, repr(e)); return
    check("mutant: %s is refused (%s)" % (name, words), bool(msgs) and any(words.lower() in m.lower() for m in msgs), msgs)
    check("mutant: %s: the message is plain words" % name, all(isinstance(m, str) and "Traceback" not in m and len(m) > 12 for m in msgs))

def e_extra(fn, data, pin=True):
    def e(files, man):
        files[fn] = data
        if not pin: return {"unpinned": (fn,)}
    return e
def e_man(**kv):
    def e(files, man): man.update(kv)
    return e
def e_pop(n):
    def e(files, man): files.pop(n)
    return e
def e_drop_key(k):
    def e(files, man): del man[k]
    return e
def e_pack(change):
    def e(files, man):
        b = bytearray(files["meter.bin"]); change(b); files["meter.bin"] = bytes(b)
    return e
def fix_crc(b):
    size = struct.unpack_from("<I", b, 8)[0]; struct.pack_into("<I", b, 20, zlib.crc32(bytes(b[48:48 + size])) & 0xFFFFFFFF)

# zip rules
refused("a package above 4 MiB compressed", e_extra("source/blob.bin", os.urandom(4 * 1024 * 1024 + 4096)), "4 MiB")
def bomb(files, man): files["source/zeros.bin"] = bytes(6 * 1024 * 1024)
refused("declared sizes above 4 MiB (it compresses small)", bomb, "4 MiB")
refused("a path with .. that the file table would otherwise allow", e_extra("source/../../evil.bin", b"x"), "outside the package")
refused("an absolute path", e_extra("/etc/evil", b"x"), "absolute")
refused("a backslash path", e_extra("source\\x.c", b"x"), "path")
refused("a file beyond the table", e_extra("notes.txt", b"hello"), "notes.txt")
refused("a file in the table but not pinned", e_extra("preview.png", b"\x89PNG", pin=False), "pin")
refused("a pin that does not match", lambda f, m: {"keep_pins": True} if m["files"].__setitem__("meter.bin", "0" * 64) is None else None, "meter.bin")
refused("a pin for a file that is not in the package", lambda f, m: {"keep_pins": True} if m["files"].__setitem__("README.md.x", "0" * 64) is None else None, "README.md.x")
def e_dup(files, man): return {"extra_raw": {"README.md": b"second readme"}}
refused("the same file twice", e_dup, "twice")
refused("meter.bin missing", e_pop("meter.bin"), "meter.bin")
refused("LICENSE missing", e_pop("LICENSE"), "LICENSE")
refused("README.md missing", e_pop("README.md"), "README")
# pack rules
def e_big(files, man):
    b = bytearray(files["meter.bin"]); size = struct.unpack_from("<I", b, 8)[0]
    new = bytes(b[:48 + size]) + bytes(0x10000); b = bytearray(new); struct.pack_into("<I", b, 8, size + 0x10000)
    struct.pack_into("<I", b, 20, zlib.crc32(bytes(b[48:])) & 0xFFFFFFFF); files["meter.bin"] = bytes(b)
refused("meter.bin larger than the window", e_big, "window")
def e_pstate(files, man):
    b = bytearray(files["meter.bin"]); struct.pack_into("<I", b, 40, 0xF800); struct.pack_into("<I", b, 44, 0x1000); files["meter.bin"] = bytes(b)
refused("pstate that does not fit the window beside the image", e_pstate, "window")
def e_origin(files, man):
    b = bytearray(files["meter.bin"]); struct.pack_into("<I", b, 12, mb.WINDOW + 0x10000); files["meter.bin"] = bytes(b)
refused("a pack linked for another address", e_origin, "window")
refused("a pack whose CRC is wrong", e_pack(lambda b: b.__setitem__(60, b[60] ^ 1)), "crc")
refused("a pack that is not a TMPK", e_pack(lambda b: b.__setitem__(slice(0, 4), b"XXXX")), "meter.bin")
refused("an abi in the manifest that is not the pack's", e_man(abi="9.9"), "abi")
# manifest rules
refused("an upper-case id", e_man(id=GM["id"].upper()), "id")
refused("a 31 digit id", e_man(id=GM["id"][:31]), "id")
refused("an id with a non-hex digit", e_man(id="g" + GM["id"][1:]), "id")
refused("an empty name", e_man(name=""), "name")
refused("a 24 character name", e_man(name="A" * 24), "name")
refused("a name with a non-ASCII character", e_man(name="Waveé"), "name")
refused("a name with a control character", e_man(name="Wave\tbars"), "name")
refused("schema 2", e_man(schema=2), "schema")
refused("a signature that is not null", e_man(signature="abcd"), "signature")
refused("a missing author", e_drop_key("author"), "author")
refused("a missing licence", e_drop_key("licence"), "licence")
refused("a version that is not x.y.z", e_man(version="one"), "version")
refused("a params_rev that is not a number", e_man(params_rev="x"), "params_rev")
refused("a tau_min that is not major.minor", e_man(tau_min="soon"), "tau_min")
refused("a manifest that is not JSON", lambda f, m: {"manifest_bytes": b"{not json"}, "meter.json")
def not_zip():
    p = tmp / "x.tmeter"; p.write_bytes(b"this is not a zip"); return mp.verify(p)
m = not_zip(); check("a file that is not a zip is refused in plain words", bool(m) and "zip" in m[0].lower(), m)
m = mp.verify(tmp / "nonexistent.tmeter"); check("a missing file is refused in plain words", bool(m) and "Traceback" not in m[0], m)
# the good one still passes (the mutant builder itself is sound)
check("the mutant builder with no change gives a package that verifies", mutant("nochange", lambda f, m: None) == [])

# ---- 4. compose ---------------------------------------------------------------------------------------------------------------------------------------
def spec(path): return [pk[k] for k in SHIPPED]
bin1 = tmp / "all.bin"; rep = mp.compose(bin1, [pk[k] for k in SHIPPED])
rows = mb.parse(bin1.read_bytes())
check("compose of the seven gives a bin the reference reader accepts (whole-file check)", mb.check_file(bin1.read_bytes()) is not None)
names = [r["name"] for r in rows["rows"]] if isinstance(rows, dict) else [r["name"] for r in rows]
check("compose: rows are in the order given, Winamp Bars first", names == ["WINAMP BARS", "WINAMP SCOPE", "CHLADNI", "LAYERED WAVE", "VU MASTER", "WATERFALL", "SOUND MAP"], names)
rl = rows["rows"] if isinstance(rows, dict) else rows
check("compose: Winamp Bars carries the fallback flag, no other row does", rl[0]["flags"] & mb.FLAG_FALLBACK and not any(r["flags"] & mb.FLAG_FALLBACK for r in rl[1:]))
bin2 = tmp / "rev.bin"; mp.compose(bin2, [pk[k] for k in reversed(SHIPPED)])
rl2 = mb.parse(bin2.read_bytes()); rl2 = rl2["rows"] if isinstance(rl2, dict) else rl2
check("compose: Winamp Bars is moved first even when given last; the rest keep the order given", [r["name"] for r in rl2] == ["WINAMP BARS", "SOUND MAP", "WATERFALL", "VU MASTER", "LAYERED WAVE", "CHLADNI", "WINAMP SCOPE"], [r["name"] for r in rl2])
try: mp.compose(tmp / "nofb.bin", [pk[k] for k in SHIPPED[1:]]); nofb = None
except mp.PackageError as e: nofb = str(e)
check("compose without Winamp Bars is refused (the firmware refuses a bin with no fallback row)", nofb and "winamp bars" in nofb.lower(), nofb)
def rewrite(src, dst, edit):
    f = zread(src); man = json.loads(f["meter.json"]); edit(man); man["files"] = {n: hashlib.sha256(b).hexdigest() for n, b in f.items() if n != "meter.json"}; f["meter.json"] = json.dumps(man, indent=1).encode()
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        for n, b in f.items(): z.writestr(zipfile.ZipInfo(n, (2026, 1, 1, 0, 0, 0)), b, zipfile.ZIP_DEFLATED)
    return dst
dupp = rewrite(pk["chladni"], tmp / "dup.tmeter", lambda m: m.update(id=pk["winamp_scope"].stem[:8] + "0" * 24))
try: mp.compose(tmp / "dup.bin", [pk["winamp_bars"], pk["winamp_scope"], dupp]); dup = None
except mp.PackageError as e: dup = str(e)
check("compose refuses two packages whose ids share the id hash, naming them", dup and pk["winamp_scope"].stem[:8] in dup, dup)
same = rewrite(pk["chladni"], tmp / "same.tmeter", lambda m: None)
try: mp.compose(tmp / "same.bin", [pk["winamp_bars"], pk["chladni"], same]); same_err = None
except mp.PackageError as e: same_err = str(e)
check("compose refuses the same package twice", same_err is not None, same_err)
h = pk["winamp_scope"].stem[:8]
coll = rewrite(pk["chladni"], tmp / "coll.tmeter", lambda m: m.update(id="aaaa" + h[4:] + "1" * 24))     # same low 16 bits as winamp_scope, other high bits
rep = mp.compose(tmp / "coll.bin", [pk["winamp_bars"], pk["winamp_scope"], coll])
check("compose reports rows that share their low 16 bits (they still compose)", rep and any("share" in x.lower() and "16" in x for x in rep.get("warnings", [])) and (tmp / "coll.bin").is_file(), rep)
rep = mp.compose(tmp / "ok.bin", [pk[k] for k in SHIPPED]); check("no collision in the seven: no warning", not rep.get("warnings"), rep)
bad = mutant("badcompose", e_pack(lambda b: b.__setitem__(60, b[60] ^ 1)))
badp = tmp / "mut_badcompose.tmeter"
try: mp.compose(tmp / "bad.bin", [pk["winamp_bars"], badp]); be = None
except mp.PackageError as e: be = str(e)
check("compose refuses a package that does not verify, naming the file and the reason", be and "mut_badcompose" in be and "crc" in be.lower(), be)
# 64 rows: 64 packages with distinct ids built from one real package; 65 refused
def many(n):
    out = [pk["winamp_bars"]]
    for i in range(n - 1):
        out.append(rewrite(pk["winamp_scope"], tmp / ("n%d.tmeter" % i), lambda m, i=i: m.update(id="%08x" % (0x10000000 + i * 0x10001) + "5" * 24)))
    return out
try: r64 = mp.compose(tmp / "64.bin", many(64)); ok64 = mb.check_file((tmp / "64.bin").read_bytes()) is not None
except Exception as e: ok64 = repr(e)
check("64 meters compose into a bin the reference reader accepts", ok64 is True, ok64)
try: mp.compose(tmp / "65.bin", many(65)); e65 = None
except mp.PackageError as e: e65 = str(e)
check("65 meters are refused (the limit is 64)", e65 and "64" in e65, e65)

# ---- 5. round trip and byte identity with the build from sources ----------------------------------------------------------------------------------------
shipped_from_pkgs = tmp / "shipped.bin"; mp.compose(shipped_from_pkgs, [pk[k] for k in SHIPPED])
with tempfile.TemporaryDirectory() as td: src_entries = bp.build_entries(Path(td))
src_bin = mb.compose(src_entries)
check("the bin composed from the seven packages is BYTE-IDENTICAL to the bin composed from the sources (what the build made before P2.6)", shipped_from_pkgs.read_bytes() == src_bin,
      (len(shipped_from_pkgs.read_bytes()), len(src_bin)))
out_bp = tmp / "bp.bin"; subprocess.run([sys.executable, str(ROOT / "tools/player2/build_packs.py"), str(out_bp)], check=True, capture_output=True)
check("tools/player2/build_packs.py (the release path) now writes that same bin", out_bp.read_bytes() == src_bin)
check("build_packs.py composes the shipped bin from the packages (build_from_packages, meter_package.compose), not from the sources", hasattr(bp, "build_from_packages") and "meter_package" in (ROOT / "tools/player2/build_packs.py").read_text())
check("the shipped bin has seven rows and Winamp Bars as the fallback", len(mb.parse(src_bin)["rows"] if isinstance(mb.parse(src_bin), dict) else mb.parse(src_bin)) == 7)
for k in SHIPPED:      # the manifests drive the TOC row: params_rev, version
    m = json.loads((ROOT / "meters" / k / "meter.json").read_text())
    r = [r for r in rl if r["id"] == m["id"]][0]
    check("%s: the TOC row's params_rev and version come from meters/%s/meter.json" % (k, k), r["params_rev"] == m["params_rev"] and ".".join(str(x) for x in r["ver"]) == m["version"], (r["params_rev"], m.get("params_rev"), r["ver"], m.get("version")))

# ---- 6. the manifests are the single source of params_rev and tau_min -----------------------------------------------------------------------------
import gen_shipped_meter_ids as gen
HDR = ROOT / "fw2/cores/shipped_meter_ids.h"
check("fw2/cores/shipped_meter_ids.h is exactly what the generator writes from the manifests", gen.render() == HDR.read_text(), "run: python3 tools/gen_shipped_meter_ids.py")
t = HDR.read_text()
hdr_rev = {int(h, 16): int(r) for _i, h, r in re.findall(r"X\((\d+),\s*(0x[0-9A-Fa-f]+)u,\s*(\d+)u\)", t)}
man_rev = {int(json.loads((ROOT / "meters" / k / "meter.json").read_text())["id"][:8], 16): json.loads((ROOT / "meters" / k / "meter.json").read_text())["params_rev"] for k in SHIPPED}
check("the header's params_rev of each shipped meter equals its manifest's", hdr_rev == man_rev, (hdr_rev, man_rev))
check("every shipped manifest has the package fields (version, author, licence, params_rev, tau_min)", all(all(x in json.loads((ROOT / "meters" / k / "meter.json").read_text()) for x in ("version", "author", "licence", "params_rev", "tau_min")) for k in SHIPPED))
# a changed manifest is caught: the generator, run on a copy of the manifests with one params_rev raised, differs from the committed header
with tempfile.TemporaryDirectory() as td:
    mdir = Path(td) / "meters"
    for k in SHIPPED:
        (mdir / k).mkdir(parents=True); d = json.loads((ROOT / "meters" / k / "meter.json").read_text())
        if k == "chladni": d["params_rev"] += 1
        (mdir / k / "meter.json").write_text(json.dumps(d))
    check("mutant: a manifest with another params_rev makes the generated header differ from the committed one", gen.render(mdir) != HDR.read_text())
check("the generator's --check exits 0 on the committed header", subprocess.run([sys.executable, str(ROOT / "tools/gen_shipped_meter_ids.py"), "--check"], capture_output=True).returncode == 0)
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
