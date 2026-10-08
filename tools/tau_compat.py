#!/usr/bin/env python3
"""tau-compat.json: the compatibility manifest Tau Omega reads for each GitHub release (schema 2).

Schema 2 = schema 1 plus packages[].layout (RELEASE_SYSTEM_SPEC section 11): every file the release expects on a card and
what an installer may do with it (owned / shared / generated / user / obsolete). The JSON Schema is
docs/schemas/tau-compat.schema.json; `check-card` checks a card (or an unpacked install) against the manifest.

Spec: docs/features/RELEASE_SYSTEM_SPEC.md section 10 (Tau Omega's request). One extra release asset next to the zips and
SHA256SUMS.txt; the zips themselves are unchanged. Every field is derived, never typed:

  release, prerelease       --release, refused unless CHANGELOG.md has that heading and core.json carries its X.Y.Z
  date_release, packages[]  the zips: core.json, bitstream.rbf_r, Assets/*/common/tau.rom and tau-cold.bin, ROM markers
  bitstream_core_version    the RBF's fit manifest (<rbf>.json, B-653) evaluated through the CORE_VERSION ifdefs in
                            src/fpga/core/mp3_soc.v (not the first literal), or --bitstream-version for an RBF without one;
                            the RBF must be the one in the zips and every ROM must accept the value
  library_index_version     fw/library_core.h (the newest index version the firmware reads), cross-checked with tools/tau_library.py
  assets_sections           every as_find(..., "XXXX") the firmware does
  assets_read_limit_bytes   AS_MAX_FILE in fw/assets_core.h
  report_tags_max           the last SR_T_* tag in fw/suite_core.h
  persist_ids_changed       persisted interact.json ids whose name/type/default/range changed or that were removed, against
                            the previous release's zip of the same core (--previous)
  min_omega                 tools/omega_compat.json (a reviewed judgement, not typed at release time)
  notes                     lines "- Omega: ..." in the release's CHANGELOG section

The tree-derived fields (library, assets, tags) describe the firmware built from this tree, so build and verify must run on
the tree the release was built from (make_release.py does both in one run).

  python3 tools/tau_compat.py build  --release v0.6.0-alpha.5 --zip A.zip --zip B.zip --previous OLD_A.zip --previous OLD_B.zip --rbf RAW.rbf -o tau-compat.json
  python3 tools/tau_compat.py verify tau-compat.json --zip A.zip --zip B.zip --previous ... --rbf RAW.rbf
  python3 tools/tau_compat.py check-card tau-compat.json CARD_DIR [--core alfatreze.TAU]
"""
import argparse
import hashlib
import subprocess
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from check_fw_bitstream_pair import rom_accepts, rom_needs  # noqa: E402  (the pairing gate's own marker readers)
import fit_manifest  # noqa: E402  (B-653: FEATURES, macros_of)

SCHEMA = 2
SCHEMA_FILE = ROOT / "docs/schemas/tau-compat.schema.json"
ROLES = ("owned", "shared", "generated", "user", "obsolete")
REV = bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))


class CompatError(Exception):
    pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------- bitstream CORE_VERSION

def selecting_macros(soc_text=None):
    """The macros whose `ifdef/`elsif chains enclose a CORE_VERSION localparam (today TAU_CLK66, TAU_RAM_192K)."""
    text = soc_text if soc_text is not None else (ROOT / "src/fpga/core/mp3_soc.v").read_text()
    stack, names = [], set()
    for line in text.splitlines():
        m = re.match(r"\s*`(ifdef|ifndef|elsif|else|endif)\b\s*(\w*)", line)
        if m:
            kw, name = m.groups()
            if kw in ("ifdef", "ifndef"):
                stack.append([name])
            elif kw == "elsif":
                stack[-1].append(name)
            elif kw == "endif":
                stack.pop()
            continue
        if re.search(r"localparam\s*\[31:0\]\s*CORE_VERSION\b", line):
            names |= {n for chain in stack for n in chain}
    return sorted(names)


def core_version_for_macros(macros, soc_text=None):
    """Evaluate the `ifdef/`elsif/`else/`endif around `localparam ... CORE_VERSION` for this set of defined macros."""
    text = soc_text if soc_text is not None else (ROOT / "src/fpga/core/mp3_soc.v").read_text()
    defined = set(macros)
    stack, found = [], []          # stack of [parent_active, branch_taken, active]
    active = True
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r"`(ifdef|ifndef|elsif|else|endif)\b\s*(\w*)", s)
        if m:
            kw, name = m.groups()
            if kw in ("ifdef", "ifndef"):
                cond = (name in defined) == (kw == "ifdef")
                stack.append([active, cond, active and cond])
            elif kw == "elsif":
                top = stack[-1]
                cond = (not top[1]) and name in defined
                top[1] |= cond
                top[2] = top[0] and cond
            elif kw == "else":
                top = stack[-1]
                top[2] = top[0] and not top[1]
                top[1] = True
            else:
                stack.pop()
            active = stack[-1][2] if stack else True
            continue
        v = re.search(r"localparam\s*\[31:0\]\s*CORE_VERSION\s*=\s*32'h([0-9A-Fa-f]{8})", s)
        if v and active:
            found.append(v.group(1).upper())
    if len(found) != 1:
        raise CompatError(f"CORE_VERSION resolves to {found or 'nothing'} for macros {sorted(defined)}")
    return found[0]


def git_show(commit, path, root=ROOT):
    r = subprocess.run(["git", "show", f"{commit}:{path}"], cwd=root, capture_output=True, text=True)
    if r.returncode != 0:
        raise CompatError(f"cannot read {path} at commit {commit} (git show failed: {r.stderr.strip()[:120]})")
    return r.stdout


def fit_manifest_of(rbf):
    man = Path(str(rbf) + ".json")
    if not man.is_file():
        return None
    j = json.loads(man.read_text())
    if j.get("rbf_sha256") != sha(Path(rbf).read_bytes()):
        raise CompatError(f"{man.name} describes a different RBF (rbf_sha256 mismatch)")
    return j


def zero_defined(bundle_text, soc_text):
    """CORE_VERSION-selecting macros a fit bundle writes as NAME=0."""
    return [n for n in selecting_macros(soc_text) if re.search(rf'VERILOG_MACRO\s+"?{n}\s*=\s*0\b', bundle_text)]


def bitstream_core_version(rbf=None, override=None, root=ROOT):
    """CORE_VERSION of the bitstream: from its fit manifest (the macros it was built with), evaluated against mp3_soc.v AT THE
    COMMIT THE FIT WAS BUILT FROM (review H3), never today's tree. Refuses a fit built from a dirty RTL tree (its mp3_soc.v is
    unknown) and a fit bundle that writes a CORE_VERSION-selecting macro as NAME=0 (Verilog `ifdef sees it as defined, our
    tooling as off). --bitstream-version overrides all of this for an RBF without a usable manifest."""
    if override:
        if not re.fullmatch(r"[0-9A-Fa-f]{8}", override):
            raise CompatError(f"--bitstream-version must be 8 hex digits: {override!r}")
        return override.upper()
    if rbf is None:
        raise CompatError("need --rbf (with its fit manifest <rbf>.json) or --bitstream-version")
    j = fit_manifest_of(rbf)
    if j is None:
        raise CompatError(f"{rbf} has no fit manifest {Path(str(rbf) + '.json').name} (fits collected before B-653): pass --bitstream-version explicitly")
    if j.get("rtl_dirty"):
        raise CompatError(f"the fit of {Path(rbf).name} was built from a dirty RTL tree (rtl_dirty): its mp3_soc.v is unknown; pass --bitstream-version explicitly")
    commit = j.get("commit")
    if not commit:
        raise CompatError(f"the fit manifest of {Path(rbf).name} has no commit: pass --bitstream-version explicitly")
    soc = git_show(commit, "src/fpga/core/mp3_soc.v", root)
    if j.get("append"):
        bundle = git_show(commit, j["append"], root)
        zero = zero_defined(bundle, soc)
        if zero:
            raise CompatError(f"{j['append']} defines {zero} as =0: Verilog `ifdef treats that as defined, the fit manifest as off; remove the line")
    return core_version_for_macros(j["macros"], soc)


def bitstream_features(rbf=None):
    """The firmware-visible features (fit_manifest.FEATURES names) the bitstream was built with, or None without a manifest."""
    j = fit_manifest_of(rbf) if rbf is not None else None
    if j is None:
        return None
    return sorted(f for f, macro in fit_manifest.FEATURES.items() if macro in set(j["macros"]))


def source_state(root=ROOT):
    """The commit the tree-derived fields come from, and whether the tree differs from it (dist/ and release/ excluded: a release
    rebuilds dist/ on purpose)."""
    c = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=root, capture_output=True, text=True)
    d = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no", "--", ".", ":(exclude)dist", ":(exclude)release"],
                       cwd=root, capture_output=True, text=True)
    if c.returncode != 0 or d.returncode != 0:
        raise CompatError("cannot read the git state of the tree")
    return {"commit": c.stdout.strip(), "dirty": bool(d.stdout.strip())}


# ---------------------------------------------------------------- facts from the firmware tree

def tree_facts(root=ROOT):
    fw = root / "fw"
    lib = re.search(r"lib_ld16\(win \+ 6\) > (\d+)u", (fw / "library_core.h").read_text())
    tl = re.search(r"^VERSION = (\d+)", (root / "tools/tau_library.py").read_text(), re.M)
    if not lib or not tl:
        raise CompatError("cannot find the library index version in fw/library_core.h or tools/tau_library.py")
    if int(lib.group(1)) != int(tl.group(1)):
        raise CompatError(f"firmware reads index version <= {lib.group(1)} but tools/tau_library.py writes {tl.group(1)}")
    sections = set()
    for p in sorted(list(fw.glob("*.inc")) + list(fw.glob("*.h")) + list(fw.glob("*.c"))):
        sections |= set(re.findall(r'as_find\([^;]*?"([A-Z0-9]{4})"', p.read_text(errors="replace")))
    mx = re.search(r"#define\s+AS_MAX_FILE\s+(0x[0-9A-Fa-f]+|\d+)u?", (fw / "assets_core.h").read_text())
    enum = re.search(r"enum\s*\{\s*(SR_T_BUILD\s*=\s*1\s*,[^}]*)\}", (fw / "suite_core.h").read_text())
    if not sections or not mx or not enum:
        raise CompatError("cannot read assets sections, AS_MAX_FILE or the SR_T_* enum from fw/")
    tag, last = 0, 0
    for item in [x.strip() for x in enum.group(1).split(",") if x.strip()]:
        n, _, val = item.partition("=")
        tag = int(val.strip(), 0) if val.strip() else tag + 1
        last = max(last, tag)
    return {"library_index_version": int(lib.group(1)), "assets_sections": sorted(sections),
            "assets_read_limit_bytes": int(mx.group(1), 0), "report_tags_max": last}


# ---------------------------------------------------------------- facts from the zips

def read_zip(path):
    path = Path(path)
    raw = path.read_bytes()
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        cores = [n for n in names if re.fullmatch(r"Cores/[^/]+/core\.json", n)]
        if len(cores) != 1:
            raise CompatError(f"{path.name}: expected one Cores/*/core.json, found {len(cores)}")
        folder = cores[0].split("/")[1]
        meta = json.loads(z.read(cores[0]))["core"]["metadata"]
        if f"{meta['author']}.{meta['shortname']}" != folder:
            raise CompatError(f"{path.name}: core folder {folder} does not match author.shortname")

        def one(pattern, what):
            hits = [n for n in names if re.fullmatch(pattern, n)]
            if len(hits) != 1:
                raise CompatError(f"{path.name}: expected one {what}, found {len(hits)}")
            return z.read(hits[0])
        bit = one(rf"Cores/{re.escape(folder)}/bitstream\.rbf_r", "bitstream.rbf_r")
        rom = one(r"Assets/[^/]+/common/tau\.rom", "tau.rom")
        cold = one(r"Assets/[^/]+/common/tau-cold\.bin", "tau-cold.bin")
        inter = json.loads(z.read(f"Cores/{folder}/interact.json"))
        data = json.loads(z.read(f"Cores/{folder}/data.json"))
        files = {n: sha(z.read(n)) for n in names if not n.endswith("/")}
    acc, need = rom_accepts(rom), rom_needs(rom)
    if acc is None:
        raise CompatError(f"{path.name}: tau.rom has no TAUFWPAIR marker (built before B-582): cannot vouch for it")
    return {"zip": path.name, "zip_sha256": sha(raw), "core_id": folder, "version": meta["version"],
            "date_release": meta["date_release"], "bitstream": bit, "rom_sha256": sha(rom), "cold_sha256": sha(cold),
            "rom_accepts": acc, "rom_needs": need or [], "interact": inter,
            "data": data, "files": files, "platform": (meta.get("platform_ids") or [""])[0]}


def persisted(interact):
    out = {}
    for v in interact["interact"].get("variables", []):
        if not v.get("persist"):
            continue
        g = v.get("graphical", {})
        out[int(str(v["id"]), 0)] = (v.get("name"), v.get("type"), v.get("defaultval"), g.get("min"), g.get("max"))
    return out


def persist_ids_changed(pkgs, previous):
    prev = {}
    for p in previous:
        r = read_zip(p)
        prev[r["core_id"]] = persisted(r["interact"])
    changed = set()
    for p in pkgs:
        if p["core_id"] not in prev:
            raise CompatError(f"no --previous zip for {p['core_id']} (pass the last release's zip, or --no-previous for the first compat file)")
        old, new = prev[p["core_id"]], persisted(p["interact"])
        changed |= {i for i in old if new.get(i) != old[i]}
    return sorted(changed)


# ---------------------------------------------------------------- schema 2: the card layout of each package

def firmware_formats(root=ROOT):
    """Format names and the versions this firmware reads, from the firmware's own checks."""
    fw = root / "fw"
    taua = re.search(r"lib_ld16\(b \+ 4\) != (\d+)u\) return AS_E_VERSION", (fw / "assets_core.h").read_text())
    tail = re.search(r'static const char tail\[\] = "([^"]+)"', (fw / "timg.inc").read_text())
    if not taua or not tail:
        raise CompatError("cannot read the TAUA version check (fw/assets_core.h) or the cover file name (fw/timg.inc)")
    t = tree_facts(root)
    return {"tau-library.tdb": {"name": "tau-library", "version": t["library_index_version"]},
            "tau-assets.bin": {"name": "TAUA", "version": int(taua.group(1)), "sections": t["assets_sections"],
                               "max_bytes": t["assets_read_limit_bytes"], "preserve_unknown_sections": True},
            "cover": {"name": "TIM1", "version": 1, "tail": tail.group(1)}}


# Files a data slot names that are NOT shipped in the zip, and what they are. A slot filename missing from both the zip and
# this table stops the release: someone added a slot and must say who makes the file.
NOT_SHIPPED = {"tau-library.tdb": "generated", "tau-assets.bin": "user"}
# Slots opened by name at run time (no filename), by extension set.
BY_EXTENSION = {("flac", "mp3"): "media", ("timg",): "cover"}


def layout(pkg, root=ROOT, obsolete=()):
    core, plat, fmts = pkg["core_id"], pkg["platform"], firmware_formats(root)
    slots = pkg["data"]["data"]["data_slots"]
    by_name = {sl["filename"]: sl for sl in slots if sl.get("filename")}
    common = f"Assets/{plat}/common/"
    out = []
    for path, h in sorted(pkg["files"].items()):
        if path.startswith((f"Cores/{core}/", f"Assets/{plat}/{core}/")):
            role, slot = "owned", None
        elif path.startswith(common) and path[len(common):] in NOT_SHIPPED:
            raise CompatError(f"{pkg['zip']}: ships {path}, which is {NOT_SHIPPED[path[len(common):]]} data: installing it would overwrite the user's copy")
        elif path.startswith(common) and path[len(common):] in by_name:
            role, slot = "owned", by_name[path[len(common):]]
        elif path.startswith("Platforms/"):
            role, slot = "shared", None
        else:
            raise CompatError(f"{pkg['zip']}: {path} has no layout role (not the core's folders, not a data-slot file, not Platforms/)")
        e = {"path": path, "role": role, "sha256": h, "slot": slot["id"] if slot else None,
             "required": bool(slot and slot.get("required"))}
        out.append(e)
    shipped = {e["path"] for e in out}
    for sl in slots:
        fn = sl.get("filename")
        if fn:
            if common + fn in shipped:
                continue
            if fn not in NOT_SHIPPED:
                raise CompatError(f"{pkg['zip']}: data slot {sl['id']} names {fn}, which is neither shipped nor in tau_compat.NOT_SHIPPED")
            fmt = dict(fmts[fn])
            if fmt["name"] == "tau-library":
                fmt["root"] = "/" + common                       # review M8: the absolute root the index must embed (B-136)
            out.append({"path": common + fn, "role": NOT_SHIPPED[fn], "slot": sl["id"], "required": bool(sl.get("required")),
                        "format": fmt})
        else:
            kind = BY_EXTENSION.get(tuple(sorted(sl.get("extensions", []))))
            if kind == "media":
                out.append({"path": common + "**/*.{" + ",".join(sorted(sl["extensions"])) + "}", "pattern": True, "role": "user",
                            "slot": sl["id"], "required": False})
            elif kind == "cover":
                f = dict(fmts["cover"]); tail = f.pop("tail")
                out.append({"path": common + "**/" + tail, "pattern": True, "role": "generated", "slot": sl["id"],
                            "required": False, "format": f})
            else:
                raise CompatError(f"{pkg['zip']}: data slot {sl['id']} has no filename and extensions {sl.get('extensions')} that tau_compat.BY_EXTENSION does not know")
    out.append({"path": f"Settings/{core}/Interact/interact_persist.json", "role": "user", "slot": None, "required": False,
                "format": {"name": "interact_persist", "version": 1}})
    for ob in obsolete:
        if ob["core_id"] != core:
            continue
        path = ob["path"]
        if any(e["path"] == path for e in out):
            raise CompatError(f"{path} is listed as obsolete but this release still ships or uses it")
        out.append({"path": path, "role": "obsolete", "slot": None, "required": False})
    return sorted(out, key=lambda e: (e["path"], e["role"]))


# ---------------------------------------------------------------- the published JSON Schema (a minimal checker, no dependency)

def schema_errors(doc, schema=None, at="$"):
    """Checks the subset of JSON Schema the published file uses: type, const, enum, pattern, minimum, required, properties,
    additionalProperties (bool), items, oneOf-free. Unknown keys in an object are allowed unless additionalProperties is false."""
    schema = schema if schema is not None else json.loads(SCHEMA_FILE.read_text())
    errs = []
    types = {"object": dict, "array": list, "string": str, "boolean": bool, "null": type(None)}
    t = schema.get("type")
    if t:
        ok = any((isinstance(doc, int) and not isinstance(doc, bool)) if x == "integer" else isinstance(doc, types[x])
                 for x in (t if isinstance(t, list) else [t]))
        if not ok:
            return [f"{at}: expected {t}, got {type(doc).__name__}"]
    if "const" in schema and doc != schema["const"]:
        errs.append(f"{at}: must be {schema['const']!r}")
    if "enum" in schema and doc not in schema["enum"]:
        errs.append(f"{at}: {doc!r} not in {schema['enum']}")
    if "pattern" in schema and isinstance(doc, str) and not re.search(schema["pattern"], doc):
        errs.append(f"{at}: {doc!r} does not match {schema['pattern']}")
    if "minimum" in schema and isinstance(doc, int) and doc < schema["minimum"]:
        errs.append(f"{at}: below {schema['minimum']}")
    if isinstance(doc, dict):
        for k in schema.get("required", []):
            if k not in doc:
                errs.append(f"{at}: missing {k}")
        props = schema.get("properties", {})
        for k, v in doc.items():
            if k in props:
                errs += schema_errors(v, props[k], f"{at}.{k}")
            elif schema.get("additionalProperties") is False:
                errs.append(f"{at}: unexpected key {k}")
    if isinstance(doc, list) and "items" in schema:
        for i, v in enumerate(doc):
            errs += schema_errors(v, schema["items"], f"{at}[{i}]")
    return errs


# ---------------------------------------------------------------- check a card (or an unpacked install) against the manifest

def _format_ok(path, fmt):
    b = Path(path).read_bytes()
    n = fmt["name"]
    if n == "tau-library":
        if len(b) < 8 or b[:4] != (0x42494C54).to_bytes(4, "little"):
            return "not a Tau library index (magic)"
        if int.from_bytes(b[6:8], "little") > fmt["version"]:
            return f"index needs reader version {int.from_bytes(b[6:8], 'little')}, this release reads <= {fmt['version']}"
        if "root" in fmt:
            import tau_library
            try:
                root = tau_library.parse(b, sample_walk=False).root
            except Exception as e:                      # tau_library.LibError and any damage the parser trips on
                return f"index does not parse ({e})"
            if root != fmt["root"]:
                return f"index root is {root}, this core's media is under {fmt['root']} (B-136: tracks would not open)"
    elif n == "TAUA":
        if len(b) < 12 or b[:4] != b"TAUA":
            return "not a TAUA container (magic)"
        if int.from_bytes(b[4:6], "little") != fmt["version"]:
            return f"TAUA version {int.from_bytes(b[4:6], 'little')}, this release reads {fmt['version']}"
        if len(b) > fmt["max_bytes"]:
            return f"{len(b)} bytes, this release reads at most {fmt['max_bytes']}"
    elif n == "TIM1":
        if b[:4] != b"TIM1":
            return "not a TIM1 image (magic)"
    elif n == "interact_persist":
        try:
            json.loads(b)
        except ValueError:
            return "not valid JSON"
    return None


def _glob(card, pattern):
    m = re.fullmatch(r"(.*?)\*\*/(?:\*\.\{([^}]*)\}|(.+))", pattern)
    base = Path(card) / m.group(1)
    if not base.is_dir():
        return []
    if m.group(2):
        exts = {"." + x for x in m.group(2).split(",")}
        return [p for p in base.rglob("*") if p.is_file() and p.suffix.lower() in exts and not p.name.startswith("._")]
    return [p for p in base.rglob(m.group(3).split("/")[-1]) if p.is_file() and str(p).endswith(m.group(3))]


def check_card(doc, card, core=None, max_pattern_files=2000):
    """Returns [(level, message)], level 'error' or 'warn'. Checks one installed package (or every package whose core folder
    exists on the card): owned files present with the right hash, shared files present, generated/user files in a format this
    release reads, obsolete files gone, no stray files in the core's own folders."""
    card = Path(card)
    out = []
    pkgs = [p for p in doc["packages"] if (core is None and (card / "Cores" / p["core_id"]).is_dir()) or p["core_id"] == core]
    if not pkgs:
        return [("error", f"no package of {doc['release']} is installed on this card" + (f" ({core})" if core else ""))]
    for p in pkgs:
        listed = set()
        for e in p["layout"]:
            f = card / e["path"]
            listed.add(e["path"])
            if e["role"] in ("owned", "shared"):
                if not f.is_file():
                    out.append(("error", f"{p['core_id']}: {e['path']} is missing"))
                elif sha(f.read_bytes()) != e["sha256"]:
                    out.append(("error" if e["role"] == "owned" else "warn", f"{p['core_id']}: {e['path']} differs from {doc['release']}"))
            elif e["role"] == "obsolete":
                if f.exists():
                    out.append(("warn", f"{p['core_id']}: {e['path']} is obsolete and should be removed"))
            elif e.get("pattern"):
                if "format" in e:
                    for g in _glob(card, e["path"])[:max_pattern_files]:
                        why = _format_ok(g, e["format"])
                        if why:
                            out.append(("error", f"{p['core_id']}: {g.relative_to(card)}: {why}"))
            elif f.is_file():
                if "format" in e:
                    why = _format_ok(f, e["format"])
                    if why:
                        out.append(("error", f"{p['core_id']}: {e['path']}: {why}"))
            elif e["required"]:
                out.append(("error", f"{p['core_id']}: required {e['path']} is missing"))
        plat = next((e["path"].split("/")[1] for e in p["layout"] if e["path"].startswith("Assets/")), None)
        for d in [card / "Cores" / p["core_id"]] + ([card / "Assets" / plat / p["core_id"]] if plat else []):
            if d.is_dir():
                for g in d.rglob("*"):
                    rel = str(g.relative_to(card))
                    if g.is_file() and not g.name.startswith("._") and g.name != ".DS_Store" and rel not in listed:
                        out.append(("warn", f"{p['core_id']}: {rel} is not part of {doc['release']} (stale file)"))
    return out


def package_match(doc, pkg_dir, core_id):
    """The manifest's entry for core_id if it describes exactly the package directory about to be installed (every owned/shared
    file present with its hash, and no unlisted file in the core's own folders), else (None, reasons)."""
    pkg_dir = Path(pkg_dir)
    entry = next((p for p in doc.get("packages", []) if p["core_id"] == core_id), None)
    if entry is None:
        return None, [f"{doc.get('release')} has no package for {core_id}"]
    errs, listed = [], set()
    for e in entry["layout"]:
        if e["role"] not in ("owned", "shared"):
            continue
        listed.add(e["path"])
        f = pkg_dir / e["path"]
        if not f.is_file():
            errs.append(f"{e['path']} is in the manifest but not in the package")
        elif sha(f.read_bytes()) != e["sha256"]:
            errs.append(f"{e['path']} differs from the manifest")
    owned_dirs = {"/".join(e["path"].split("/")[:3]) for e in entry["layout"] if e["role"] == "owned" and e["path"].startswith("Assets/")
                  and len(e["path"].split("/")) > 3 and e["path"].split("/")[2] == core_id} | {f"Cores/{core_id}"}
    for d in owned_dirs:
        for g in (pkg_dir / d).rglob("*") if (pkg_dir / d).is_dir() else []:
            rel = str(g.relative_to(pkg_dir))
            if g.is_file() and not g.name.startswith("._") and g.name != ".DS_Store" and rel not in listed:
                errs.append(f"{rel} is in the package but not in the manifest")
    return (entry if not errs else None), errs


# ---------------------------------------------------------------- release-level facts

def changelog_section(release, changelog):
    lines = Path(changelog).read_text().splitlines()
    start = next((i for i, l in enumerate(lines) if re.match(rf"^## {re.escape(release)}(\s|$)", l)), None)
    if start is None:
        raise CompatError(f"{Path(changelog).name} has no '## {release}' heading: update the changelog first")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## ")), len(lines))
    return lines[start + 1:end]


def build(release, zips, previous, rbf=None, bitstream_version=None, changelog=ROOT / "CHANGELOG.md",
          omega_cfg=ROOT / "tools/omega_compat.json", root=ROOT):
    m = re.fullmatch(r"v(\d+\.\d+\.\d+)(-[0-9A-Za-z.]+)?", release)
    if not m:
        raise CompatError(f"--release must look like v0.6.0 or v0.6.0-alpha.5: {release!r}")
    notes = [re.sub(r"^\s*-\s*Omega:\s*", "", l).strip() for l in changelog_section(release, changelog)
             if re.match(r"^\s*-\s*Omega:", l)]
    pkgs = [read_zip(z) for z in zips]
    if not pkgs:
        raise CompatError("no --zip given")
    if len({p["core_id"] for p in pkgs}) != len(pkgs):
        raise CompatError("two zips carry the same core")
    for p in pkgs:
        if p["version"] != m.group(1):
            raise CompatError(f"{p['zip']}: core.json version {p['version']} is not {m.group(1)} ({release})")
    if len({p["date_release"] for p in pkgs}) != 1:
        raise CompatError("the zips disagree on date_release")
    if len({sha(p["bitstream"]) for p in pkgs}) != 1:
        raise CompatError("the zips carry different bitstreams")
    cv = bitstream_core_version(rbf, bitstream_version, root)
    feats = bitstream_features(rbf) if not bitstream_version else None
    if rbf is not None and Path(rbf).read_bytes().translate(REV) != pkgs[0]["bitstream"]:
        raise CompatError(f"{Path(rbf).name} is not the bitstream in the zips")
    for p in pkgs:
        if cv not in p["rom_accepts"]:
            raise CompatError(f"{p['zip']}: tau.rom accepts {p['rom_accepts']} but the bitstream is {cv} (black screen on the Pocket)")
        missing = sorted(set(p["rom_needs"]) - set(feats)) if feats is not None else []
        if missing:
            raise CompatError(f"{p['zip']}: tau.rom needs {missing} but the bitstream was built without them (the feature reads NO UNIT, B-653)")
    req = dict(tree_facts(root))
    req["persist_ids_changed"] = persist_ids_changed(pkgs, previous) if previous is not None else []
    cfg = json.loads(Path(omega_cfg).read_text())
    req["min_omega"] = cfg["min_omega"]
    return {
        "schema": SCHEMA,
        "release": release,
        "date_release": pkgs[0]["date_release"],
        "prerelease": m.group(2) is not None,
        "packages": [{"zip": p["zip"], "zip_sha256": p["zip_sha256"], "core_id": p["core_id"],
                      "bitstream_sha256": sha(p["bitstream"]), "bitstream_core_version": cv, "bitstream_features": feats,
                      "rom_sha256": p["rom_sha256"], "cold_sha256": p["cold_sha256"],
                      "rom_accepts": p["rom_accepts"], "rom_needs": p["rom_needs"],
                      "layout": layout(p, root, cfg.get("obsolete", []))} for p in pkgs],
        "requires_omega": {k: req[k] for k in ("library_index_version", "assets_sections", "assets_read_limit_bytes",
                                               "report_tags_max", "persist_ids_changed", "min_omega")},
        "notes": "\n".join(notes),
        "source": source_state(root),
    }


def dumps(doc):
    errs = schema_errors(doc)
    if errs:
        raise CompatError("does not match docs/schemas/tau-compat.schema.json: " + "; ".join(errs[:5]))
    return json.dumps(doc, indent=2) + "\n"


def verify(path, **kw):
    """Recompute from the zips and the tree; return a list of differences (empty = ok)."""
    have = json.loads(Path(path).read_text())
    want = build(**kw)
    errs = []

    def walk(a, b, at):
        if isinstance(a, dict) and isinstance(b, dict):
            for k in sorted(set(a) | set(b)):
                walk(a.get(k, "<missing>"), b.get(k, "<missing>"), f"{at}.{k}")
        elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b) and a and isinstance(a[0], dict) and isinstance(b[0], dict):
            for i, (x, y) in enumerate(zip(a, b)):
                walk(x, y, f"{at}[{i}]")
        elif a != b:
            errs.append(f"{at}: file has {a!r}, recomputed {b!r}")
    errs += [f"schema: {e}" for e in schema_errors(have)]
    walk(have, want, "tau-compat")
    if list(have) != list(want):
        errs.append(f"key order {list(have)} != {list(want)}")
    return errs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    cc = sub.add_parser("check-card")
    cc.add_argument("file", type=Path)
    cc.add_argument("card", type=Path)
    cc.add_argument("--core")
    for name in ("build", "verify"):
        p = sub.add_parser(name)
        if name == "verify":
            p.add_argument("file", type=Path)
        p.add_argument("--release", required=True)
        p.add_argument("--zip", action="append", type=Path, required=True)
        g = p.add_mutually_exclusive_group(required=True)
        g.add_argument("--previous", action="append", type=Path, help="the previous release's zip of each core")
        g.add_argument("--no-previous", action="store_true", help="first compat file: persist_ids_changed = []")
        p.add_argument("--rbf", type=Path, help="raw RBF of the release (its fit manifest gives CORE_VERSION)")
        p.add_argument("--bitstream-version", help="CORE_VERSION when the RBF has no fit manifest")
        p.add_argument("--changelog", type=Path, default=ROOT / "CHANGELOG.md")
        if name == "build":
            p.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args(argv)
    if a.cmd == "check-card":
        doc = json.loads(a.file.read_text())
        errs = schema_errors(doc)
        res = [("error", f"schema: {e}") for e in errs] or check_card(doc, a.card, a.core)
        for lvl, msg in res:
            print(f"{lvl.upper():5} {msg}")
        bad = sum(1 for lvl, _ in res if lvl == "error")
        print(("FAIL" if bad else "ok  ") + f" card checked against {doc.get('release')}: {bad} error(s), {len(res) - bad} warning(s)")
        return 1 if bad else 0
    kw = dict(release=a.release, zips=a.zip, previous=None if a.no_previous else a.previous, rbf=a.rbf,
              bitstream_version=a.bitstream_version, changelog=a.changelog)
    try:
        if a.cmd == "build":
            a.out.write_text(dumps(build(**kw)))
            print(f"wrote {a.out}")
        else:
            errs = verify(a.file, **kw)
            for e in errs:
                print("FAIL:", e)
            if errs:
                return 1
            print(f"ok   {a.file.name} matches the zips and the tree")
    except CompatError as e:
        print("FAIL:", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
