"""The CLI commands that need the suite or the runner: test, preview, thumb, docs, doctor --prove."""
import json, sys, tempfile
from pathlib import Path
from . import SDK_ROOT, toolchain


def shipped_winamp_bars():
    """the packaged Winamp Bars the player needs as its fallback row: share/ in an exported SDK, else None"""
    for d in (SDK_ROOT / "share",):
        p = d / "7dddfe63121e904843088c075cc28ce9.tmeter"
        if p.is_file(): return p
    return None


def run(a):
    if a.cmd == "test":
        from conformance import suite
        rep = suite.run(a.file, profile=a.profile, quick=a.quick, preview=a.preview)
        print(rep.json() if a.json else rep.text())
        return rep.exit_code()
    if a.cmd == "preview":
        from conformance import suite
        rep = suite.run(a.file, profile=a.profile, quick=True, preview=a.out)
        for r in rep.results:
            if r.id.startswith("PREVIEW") or r.status == "fail": print(r.line())
        return 0 if rep.preview else 1
    if a.cmd == "thumb": return thumb(a.dir)
    if a.cmd == "docs":
        from . import docgen
        return docgen.main()
    return 2


def thumb(project_dir):
    """render the meter in the simulator (loud music, the 'main' box) and write thumb.json from the picture"""
    from . import build, thumb as th
    from conformance import suite, canvas
    import zipfile
    with tempfile.TemporaryDirectory() as td:
        r = build.build(project_dir, td)
        z = zipfile.ZipFile(r["package"]); binf = Path(td) / "m.bin"; binf.write_bytes(z.read("meter.bin"))
        from . import project
        d = project.load(project_dir)
        from conformance import suite as s
        dd = s.descriptor(binf.read_bytes()); params = [p.get("def", p["min"]) for p in dd["params"]]
        res = s.run_scenario(str(binf), None, s.scenario("music")[:1] + s.scenario("music")[1:13], params, dd["max_cycles"], 0 if d.get("abi", "1.0") == "1.0" else int(d["abi"].split(".")[1]), "author", want_frames={12})
        box, rgb = res["frames"][12]
    t = th.from_rgb(rgb, box[2], box[3])
    (Path(project_dir) / "thumb.json").write_text(json.dumps(t, indent=1) + "\n")
    print("wrote %s/thumb.json (%d bytes of stream)" % (project_dir, len(t["rle"]) // 2)); return 0


def prove(tc):
    """build the template with this toolchain and run the suite on the result"""
    import shutil
    from . import build
    from conformance import suite
    src = SDK_ROOT.parent / "template"
    if not src.is_dir():
        print("the template folder is not next to the SDK (clone tau-alpha-meter-template beside it); skipping the proof"); return 0
    with tempfile.TemporaryDirectory() as td:
        dst = Path(td) / "t"; shutil.copytree(src, dst, ignore=shutil.ignore_patterns("dist", "build", ".git"))
        m = json.loads((dst / "meter.json").read_text()); m["id"] = "0123456789abcdef0123456789abcdef"; (dst / "meter.json").write_text(json.dumps(m))
        try: r = build.build(dst, dst / "dist", tc=tc)
        except build.BuildError as e: print("PROOF FAILED: the template did not build: %s" % e); return 1
        rep = suite.run(r["package"], quick=True, tc=tc)
        print(rep.text()); return rep.exit_code()


def selftest():
    """build every negative fixture and check the suite reports exactly what fixtures.json says (the core of the Tau Alpha repository's sim/test_sdk_conformance.py, without the mutants)"""
    import tempfile
    from conformance import fixtures, suite
    tc = toolchain.find(); bad = 0
    with tempfile.TemporaryDirectory() as td:
        for name, path in fixtures.build_all(td, tc=tc).items():
            exp = fixtures.SPEC["fixtures"][name]["expect"]; rep = suite.run(path, quick=True, tc=tc)
            ok = sorted(rep.ids("fail")) == sorted(exp["fail"]) and sorted(rep.ids("warn")) == sorted(exp["warn"])
            print("%s %-18s fails %s" % ("ok  " if ok else "FAIL", name, rep.ids("fail"))); bad += 0 if ok else 1
    print("%d fixture(s) differ from the expectation" % bad); return 1 if bad else 0
