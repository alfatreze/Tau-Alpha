"""tau-meter: one entry point for the meter SDK.

  tau-meter doctor [--prove]            is the toolchain found and the right one; --prove builds and checks the template
  tau-meter id new                      a new meter id (128 random bits, 32 hex digits)
  tau-meter build [DIR] [--out DIR]     the project in DIR (default .) -> dist/<id>.tmeter
  tau-meter verify FILE...              the package rules the installer applies (exit 1 and the reasons)
  tau-meter test FILE.tmeter            the conformance suite (every rule the player enforces; exit 0 pass, 1 fail, 2 unusable input)
  tau-meter preview FILE.tmeter OUT.png a few rendered frames of the meter
  tau-meter thumb DIR                   write DIR/thumb.json from a simulated frame
  tau-meter compose OUT.bin PKG...      a user bin (tau-meters.bin) from packages
  tau-meter docs                        regenerate docs/HOST_TABLE.md from include/tau_meter.h
  tau-meter selftest                    check the conformance suite itself against its broken-meter fixtures
"""
import argparse, sys
from pathlib import Path
from . import __version__, SDK_ROOT


def main(argv=None):
    ap = argparse.ArgumentParser(prog="tau-meter", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", action="version", version="tau-meter " + __version__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("doctor"); p.add_argument("--prove", action="store_true")
    p = sub.add_parser("id"); p.add_argument("what", choices=["new"])
    p = sub.add_parser("build"); p.add_argument("dir", nargs="?", default="."); p.add_argument("--out", type=Path)
    p = sub.add_parser("verify"); p.add_argument("files", nargs="+", type=Path)
    p = sub.add_parser("test"); p.add_argument("file", type=Path); p.add_argument("--profile", choices=["author", "shipped"], default="author"); p.add_argument("--preview", type=Path, help="also write the preview PNG here"); p.add_argument("--quick", action="store_true", help="fewer scenarios"); p.add_argument("--json", action="store_true")
    p = sub.add_parser("preview"); p.add_argument("file", type=Path); p.add_argument("out", type=Path); p.add_argument("--profile", choices=["author", "shipped"], default="author")
    p = sub.add_parser("thumb"); p.add_argument("dir", nargs="?", default=".")
    p = sub.add_parser("compose"); p.add_argument("out", type=Path); p.add_argument("packages", nargs="+", type=Path)
    sub.add_parser("docs"); sub.add_parser("selftest")
    a = ap.parse_args(argv)
    if a.cmd == "id":
        from . import project; print(project.new_id()); return 0
    if a.cmd == "build": return _build(a)
    if a.cmd == "verify":
        import meter_package as mpk
        return mpk.main(["verify"] + [str(f) for f in a.files])
    if a.cmd == "compose": return _compose(a)
    if a.cmd == "doctor": return _doctor(a)
    from . import commands
    if a.cmd == "selftest": return commands.selftest()
    return commands.run(a)


def _build(a):
    from . import build
    try: r = build.build(a.dir, a.out)
    except build.BuildError as e:
        print("build failed: %s" % e); return 1
    for l in r["log"]: print(l)
    i = r["info"]
    print("%s: pack %d bytes, scratch %d bytes (data %d + bss %d), pstate %d bytes" % (r["package"], i["image"], i["scratch"], i["data"], i["bss"], i["pstate"]))
    return 0


def _compose(a):
    import meter_package as mpk, shutil
    pk = list(a.packages)
    from .commands import shipped_winamp_bars
    wb = shipped_winamp_bars()
    import json, zipfile
    ids = []
    for p in pk:
        try: ids.append(json.loads(zipfile.ZipFile(p).read("meter.json"))["id"])
        except Exception: ids.append(None)
    if mpk.WINAMP_BARS_ID not in ids and wb: pk.insert(0, wb); print("adding the shipped Winamp Bars (the player needs it as the fallback meter): %s" % wb.name)
    return mpk.main(["compose", str(a.out)] + [str(x) for x in pk])


def _doctor(a):
    from . import toolchain
    try: tc = toolchain.find()
    except toolchain.ToolchainError as e:
        print("not ready: %s" % e); return 1
    print("clang: %s\nld.lld: %s\n%s" % (tc["clang"], tc["lld"], tc["line"]))
    print("pinned version %s: %s" % (toolchain.PINNED_LLVM, "yes" if tc["pinned"] else "NO (another version: use --prove to check it)"))
    if a.prove:
        from . import commands
        return commands.prove(tc)
    return 0


if __name__ == "__main__":
    sys.exit(main())
