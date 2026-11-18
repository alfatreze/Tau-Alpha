"""The build driver: a meter project -> ELF -> TMPK pack -> `<id>.tmeter`, with the pinned flags (tau_meter/toolchain.py) and the window linker script (ld/meter_pack.ld).

  build(project_dir, out_dir=None) -> {"package": Path, "pack": bytes, "info": {...}, "log": [lines], "manifest": {...}}

The package is deterministic (same sources, same compiler: same bytes) and is verified with the same rules the player's installer applies (tools/meter_package.py verify) before it is returned."""
import hashlib, json, os, subprocess, sys, tempfile
from pathlib import Path
from . import SDK_ROOT, toolchain, project, elfpack
import meter_bin as mb, meter_package as mpk, meter_desc, mpreset


class BuildError(RuntimeError):
    pass


def _run(cmd, log, what):
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if out: log.extend(out.splitlines())
    if r.returncode: raise BuildError("%s failed:\n%s" % (what, out or "(no message)"))
    return out


def compile_and_link(root, d, work, tc, log):
    """-> the ELF path"""
    root, work = Path(root), Path(work); work.mkdir(parents=True, exist_ok=True)
    b = d.get("build", {})
    incs = ["-I", str(SDK_ROOT / "include"), "-I", str(root / "src")] + sum((["-I", str(root / i)] for i in b.get("include", [])), [])
    defs = ["-D" + x for x in b.get("defines", [])] + list(b.get("cflags", []))
    objs = []
    srcs = [root / s for s in project.sources(root, d)]
    if b.get("descriptor", "generate") == "generate":
        desc = work / "tau_meter_desc.c"
        desc.write_text(meter_desc.c_array(meter_desc.build_desc(d, project.thumb(root)), "mtr_pack_desc"))
        srcs.append(desc)
    srcs.append(SDK_ROOT / "runner" / "tau_meter_rt.c")
    for i, s in enumerate(srcs):
        o = work / ("%02d_%s.o" % (i, s.stem))
        _run([tc["clang"]] + toolchain.CFLAGS + incs + defs + ["-c", str(s), "-o", str(o)], log, "compiling %s" % s.name)
        objs.append(str(o))
    elf = work / "meter.elf"
    link = [tc["clang"]] + toolchain.CFLAGS + toolchain.LDFLAGS + ["--ld-path=" + tc["lld"], "-Wl,--defsym=PACK_ORG=0x%X" % elfpack.WINDOW, "-Wl,--defsym=DATA_ALIAS=0x%X" % elfpack.DATA_ALIAS,
            "-Wl,--defsym=SCRATCH_ORG=0x%X" % elfpack.SCRATCH_ORG, "-T", str(SDK_ROOT / "ld" / "meter_pack.ld")] + objs + ["-o", str(elf)]
    _run(link, log, "linking")
    return elf


def abi_minor(d):
    a = d.get("abi", "1.0")
    try: major, minor = (int(x) for x in a.split("."))
    except ValueError: raise BuildError("abi %r in meter.json is not major.minor" % a)
    if major != 1: raise BuildError("abi %r: this SDK builds ABI 1.x meters" % a)
    if minor > 2: raise BuildError("abi 1.%d: this SDK knows ABI 1.0 to 1.2" % minor)
    return minor


def make_package(root, d, blob, out_dir):
    root = Path(root)
    if not (root / "LICENSE").is_file(): raise BuildError("the project has no LICENSE file: a package must carry its licence (the installer shows it)")
    name = mb.desc_info(blob[mb.PACK_HDR:])["name"]
    ph = mb.pack_header(blob)
    readme = (root / "README.md").read_bytes() if (root / "README.md").is_file() else ("# %s\n\nA meter for Tau Alpha, version %s, by %s.\n\nA meter runs with full access to the player.\n" % (name, d["version"], d["author"])).encode()
    files = {"meter.bin": blob, "README.md": readme, "LICENSE": (root / "LICENSE").read_bytes()}
    if d["presets"]:
        for rel, data in mpreset.factory_files(d).items(): files["presets/" + rel.split("/", 1)[1]] = data
    for p in sorted(root.glob("src/**/*")):
        if p.is_file(): files["source/" + str(p.relative_to(root)).replace("\\", "/")] = p.read_bytes()
    for n in ("meter.json", "thumb.json", "presets.json"):
        if (root / n).is_file(): files["source/" + n] = (root / n).read_bytes()
    man = {"schema": 1, "id": d["id"], "name": name, "version": d["version"], "author": d["author"], "licence": d["licence"], "homepage": d.get("homepage", ""),
           "abi": "%d.%d" % (ph["abi"] >> 8, ph["abi"] & 255), "tau_min": d.get("tau_min", "0.0"), "params_rev": d["params_rev"]}
    if d.get("source"): man["source"] = d["source"]
    man["files"] = {n: hashlib.sha256(b).hexdigest() for n, b in sorted(files.items())}
    man["signature"] = None
    files["meter.json"] = (json.dumps(man, indent=2) + "\n").encode()
    out = Path(out_dir); out.mkdir(parents=True, exist_ok=True)
    p = out / (d["id"] + ".tmeter"); p.write_bytes(mpk._zip(files))
    bad = mpk.verify(p)
    if bad: raise BuildError("the package does not pass the installer's rules:\n  - " + "\n  - ".join(bad))
    return p, man


def build(project_dir, out_dir=None, tc=None, work=None):
    root = Path(project_dir).resolve()
    try: d = project.load(root)
    except project.ProjectError as e: raise BuildError(str(e))
    try: tc = tc or toolchain.find()
    except toolchain.ToolchainError as e: raise BuildError(str(e))
    log = ["toolchain: " + tc["line"] + ("" if tc["pinned"] else "  (not the pinned %s: run `tau-meter doctor --prove` to check it)" % toolchain.PINNED_LLVM)]
    minor = abi_minor(d)
    with tempfile.TemporaryDirectory() as td:
        w = Path(work) if work else Path(td)
        try:
            elf = compile_and_link(root, d, w, tc, log)
            blob, info = elfpack.make_pack(elf, minor, int(d["id"][:8], 16) & 0xFFFF)
        except (elfpack.ElfError, project.ProjectError) as e: raise BuildError(str(e))
        if info["scratch"] > elfpack.SCRATCH_SIZE: raise BuildError("the meter's static data is %d bytes but the scratch area for every meter is %d: shrink your static arrays (or move a history into TAU_PSTATE)" % (info["scratch"], elfpack.SCRATCH_SIZE))
        if info["image"] + info["pstate"] > elfpack.WINDOW_SIZE:
            raise BuildError("the meter (%d bytes) and its pstate (%d bytes) do not fit the 64 KiB window" % (info["image"], info["pstate"]))
        pkg, man = make_package(root, d, blob, out_dir or root / "dist")
        elf_copy = None
        if work: elf_copy = str(Path(work) / "meter.elf")
    return {"package": pkg, "pack": blob, "info": info, "log": log, "manifest": man, "elf": elf_copy}
