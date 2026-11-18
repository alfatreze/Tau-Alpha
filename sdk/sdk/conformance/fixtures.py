"""The negative fixture set: broken meters, each built from a small C file (fixtures/src/) or by tampering with a good package, each with the exact failure the suite must report (fixtures/fixtures.json).
`build_all(dir)` writes every fixture as a .tmeter under dir and returns {name: path}. The positive fixtures are the seven shipped meters (the collection) and the template."""
import hashlib, io, json, shutil, tempfile, zipfile
from pathlib import Path
from tau_meter import build, elfpack, project, toolchain
import meter_package as mpk, meter_bin as mb

HERE = Path(__file__).resolve().parent
SPEC = json.loads((HERE / "fixtures" / "fixtures.json").read_text())


def fixture_id(name): return hashlib.sha256(("tau-meter-fixture:" + name).encode()).hexdigest()[:32]


def _project(name, spec, td):
    root = Path(td) / name
    shutil.copytree(HERE / "fixtures" / "base", root)
    (root / "src").mkdir()
    shutil.copyfile(HERE / "fixtures" / "src" / (spec["src"] + ".c"), root / "src" / "meter.c")
    m = json.loads((root / "meter.json").read_text()); m["id"] = fixture_id(name); m["name"] = ("FX " + name.upper().replace("_", " "))[:23]
    m.update(spec.get("meter", {})); (root / "meter.json").write_text(json.dumps(m, indent=2))
    return root


def build_unchecked(root, out, tc):
    """the driver without its size refusals: the way to make a package the driver would not (an oversized pack)"""
    d = project.load(root); log = []
    with tempfile.TemporaryDirectory() as td:
        elf = build.compile_and_link(root, d, Path(td), tc, log)
        blob, info = elfpack.make_pack(elf, build.abi_minor(d), int(d["id"][:8], 16) & 0xFFFF)
    import zlib
    pkg, man = build.make_package.__wrapped__(root, d, blob, out) if hasattr(build.make_package, "__wrapped__") else _make_unverified(root, d, blob, out)
    return pkg


def _make_unverified(root, d, blob, out_dir):
    """build.make_package without its final verify (the package is meant to break rules)"""
    orig = mpk.verify
    mpk.verify = lambda p: []
    try: return build.make_package(root, d, blob, out_dir)
    finally: mpk.verify = orig


def _rezip(path, mutate, repin=False, name=None):
    z = zipfile.ZipFile(path); files = {i.filename: z.read(i) for i in z.infolist()}; z.close()
    r = mutate(files); files = r if isinstance(r, dict) else files
    if repin and "meter.json" in files:
        man = json.loads(files["meter.json"]); man["files"] = {n: hashlib.sha256(b).hexdigest() for n, b in sorted(files.items()) if n != "meter.json"}
        files["meter.json"] = (json.dumps(man, indent=2) + "\n").encode()
    Path(path).write_bytes(mpk._zip(files))


def _patch_pack(files, **kw):
    import struct
    b = bytearray(files["meter.bin"])
    if "abi" in kw: struct.pack_into("<H", b, 4, kw["abi"])
    if "origin" in kw: struct.pack_into("<I", b, 12, kw["origin"])
    if "flip" in kw: b[48 + kw["flip"]] ^= 0x55
    files["meter.bin"] = bytes(b); return files


def _abi(f, header_abi, text):
    _patch_pack(f, abi=header_abi); m = json.loads(f["meter.json"]); m["abi"] = text; f["meter.json"] = (json.dumps(m, indent=2) + "\n").encode(); return f


def tamper(good, kind, dest):
    """copy the good package to dest and break it in the named way"""
    shutil.copyfile(good, dest)
    if kind == "not_a_zip": Path(dest).write_bytes(b"this is not a zip file\n" * 20)
    elif kind == "bad_hash": _rezip(dest, lambda f: _patch_pack(f, flip=16))
    elif kind == "tampered_file": _rezip(dest, lambda f: f.update({"README.md": f["README.md"] + b"\nbuy my other meters\n"}))
    elif kind == "path_escape": _rezip(dest, lambda f: f.update({"../evil.txt": b"x"}))
    elif kind == "extra_file": _rezip(dest, lambda f: f.update({"notes.txt": b"x"}), repin=True)
    elif kind == "oversize":
        with zipfile.ZipFile(dest, "a", zipfile.ZIP_STORED) as z: z.writestr("source/padding.bin", bytes(5 * 1024 * 1024))
    elif kind == "bad_id": _rezip(dest, lambda f: f.update({"meter.json": f["meter.json"].replace(json.loads(f["meter.json"])["id"].encode(), json.loads(f["meter.json"])["id"].upper().encode())}))
    elif kind == "bad_abi_major": _rezip(dest, lambda f: _abi(f, 0x0200, "2.0"), repin=True)
    elif kind == "bad_abi_minor": _rezip(dest, lambda f: _abi(f, 0x0103, "1.3"), repin=True)
    elif kind == "bad_origin": _rezip(dest, lambda f: _patch_pack(f, origin=0x24900000), repin=True)
    elif kind == "bad_crc_repinned": _rezip(dest, lambda f: _patch_pack(f, flip=16), repin=True)
    else: raise ValueError(kind)
    return dest


def build_all(dest, only=None, tc=None):
    dest = Path(dest); dest.mkdir(parents=True, exist_ok=True); tc = tc or toolchain.find(); out = {}
    with tempfile.TemporaryDirectory() as td:
        good = None
        for name, spec in SPEC["fixtures"].items():
            if only and name not in only: continue
            if "tamper" in spec:
                if good is None: good = build.build(_project("good_base", SPEC["fixtures"]["good"], td), Path(td) / "gb", tc=tc)["package"]
                out[name] = tamper(good, spec["tamper"], dest / (name + ".tmeter")); continue
            root = _project(name, spec, td)
            pkg = build_unchecked(root, Path(td) / ("o_" + name), tc) if spec.get("unchecked") else build.build(root, Path(td) / ("o_" + name), tc=tc)["package"]
            shutil.copyfile(pkg, dest / (name + ".tmeter")); out[name] = dest / (name + ".tmeter")
    return out
