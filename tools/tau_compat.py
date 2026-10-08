#!/usr/bin/env python3
"""tau-compat.json: the compatibility manifest Tau Omega reads for each GitHub release (schema 1).

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
"""
import argparse
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from check_fw_bitstream_pair import rom_accepts, rom_needs  # noqa: E402  (the pairing gate's own marker readers)

SCHEMA = 1
REV = bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))


class CompatError(Exception):
    pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------- bitstream CORE_VERSION

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


def bitstream_core_version(rbf=None, override=None):
    if override:
        if not re.fullmatch(r"[0-9A-Fa-f]{8}", override):
            raise CompatError(f"--bitstream-version must be 8 hex digits: {override!r}")
        return override.upper()
    if rbf is None:
        raise CompatError("need --rbf (with its fit manifest <rbf>.json) or --bitstream-version")
    man = Path(str(rbf) + ".json")
    if not man.is_file():
        raise CompatError(f"{rbf} has no fit manifest {man.name} (fits collected before B-653): pass --bitstream-version explicitly")
    j = json.loads(man.read_text())
    if j.get("rbf_sha256") != sha(Path(rbf).read_bytes()):
        raise CompatError(f"{man.name} describes a different RBF (rbf_sha256 mismatch)")
    return core_version_for_macros(j["macros"])


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
    acc, need = rom_accepts(rom), rom_needs(rom)
    if acc is None:
        raise CompatError(f"{path.name}: tau.rom has no TAUFWPAIR marker (built before B-582): cannot vouch for it")
    return {"zip": path.name, "zip_sha256": sha(raw), "core_id": folder, "version": meta["version"],
            "date_release": meta["date_release"], "bitstream": bit, "rom_sha256": sha(rom), "cold_sha256": sha(cold),
            "rom_accepts": acc, "rom_needs": need or [], "interact": inter}


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
    cv = bitstream_core_version(rbf, bitstream_version)
    if rbf is not None and Path(rbf).read_bytes().translate(REV) != pkgs[0]["bitstream"]:
        raise CompatError(f"{Path(rbf).name} is not the bitstream in the zips")
    for p in pkgs:
        if cv not in p["rom_accepts"]:
            raise CompatError(f"{p['zip']}: tau.rom accepts {p['rom_accepts']} but the bitstream is {cv} (black screen on the Pocket)")
    req = dict(tree_facts(root))
    req["persist_ids_changed"] = persist_ids_changed(pkgs, previous) if previous is not None else []
    req["min_omega"] = json.loads(Path(omega_cfg).read_text())["min_omega"]
    return {
        "schema": SCHEMA,
        "release": release,
        "date_release": pkgs[0]["date_release"],
        "prerelease": m.group(2) is not None,
        "packages": [{"zip": p["zip"], "zip_sha256": p["zip_sha256"], "core_id": p["core_id"],
                      "bitstream_sha256": sha(p["bitstream"]), "bitstream_core_version": cv,
                      "rom_sha256": p["rom_sha256"], "cold_sha256": p["cold_sha256"],
                      "rom_accepts": p["rom_accepts"], "rom_needs": p["rom_needs"]} for p in pkgs],
        "requires_omega": {k: req[k] for k in ("library_index_version", "assets_sections", "assets_read_limit_bytes",
                                               "report_tags_max", "persist_ids_changed", "min_omega")},
        "notes": "\n".join(notes),
    }


def dumps(doc):
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
        elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b) and a and isinstance(a[0], dict):
            for i, (x, y) in enumerate(zip(a, b)):
                walk(x, y, f"{at}[{i}]")
        elif a != b:
            errs.append(f"{at}: file has {a!r}, recomputed {b!r}")
    walk(have, want, "tau-compat")
    if list(have) != list(want):
        errs.append(f"key order {list(have)} != {list(want)}")
    return errs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
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
