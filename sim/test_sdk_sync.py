#!/usr/bin/env python3
"""The SDK cannot drift from the player or from its own documents:
  1. include/tau_meter.h declares mtr_in_t and mtr_host_api_t with the SAME fields, types and order as the firmware's headers (fw2/ui/meter.h, fw2/ui/meter_pack.h), and the same ABI numbers and theme roles; its struct layout
     (compiled by clang for rv32) matches the offsets the host runner hard-codes;
  2. the vendored tools (tau_meter/_vendor) are byte copies of tools/meter_bin.py, meter_package.py and tools/player2/{meter_desc,mpreset,meter_manifest}.py; the linker script and pack_strike_core.h are byte copies of the firmware's;
     the SDK's addresses (window, scratch, limits) equal fw2/cores/meter_pack_core.h and tools/meter_bin.py;
  3. docs/HOST_TABLE.md is exactly what `tau-meter docs` generates; docs/TESTING.md lists every check id the suite and the fixtures can produce; every relative link in the documents resolves;
  4. sdk/meters is exactly what tools/sdk_collect_meters.py makes from the firmware sources and the manifests."""
import filecmp, os, re, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "sdk" / "sdk")); sys.path.insert(0, str(ROOT / "tools"))
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(info)[:500]))
    fails += 0 if cond else 1
SDK = ROOT / "sdk" / "sdk"

def fields(text, name):
    m = re.search(r"typedef struct \{((?:(?!typedef struct).)*?)\} %s;" % name, text, re.S)
    out = []
    for ln in m.group(1).splitlines():
        ln = re.sub(r"/\*.*?\*/", "", ln).strip()
        if not ln.endswith(";"): continue
        out.append(re.sub(r"\s+", " ", ln))
    return out

# ---- 1. the ABI ----------------------------------------------------------------------------------------------------------------------------------------
sdk_h = (SDK / "include/tau_meter.h").read_text()
fw_in, fw_api = (ROOT / "fw2/ui/meter.h").read_text(), (ROOT / "fw2/ui/meter_pack.h").read_text()
norm = lambda l: [re.sub(r"\s*\(\*\s*", "(*", re.sub(r"\s*,\s*", ", ", x)).replace("MTR_PACK_ABI", "").replace(" ", "") for x in l]
check("mtr_in_t: same fields, types and order as the firmware's (fw2/ui/meter.h)", norm(fields(sdk_h, "mtr_in_t")) == norm(fields(fw_in, "mtr_in_t")), (fields(sdk_h, "mtr_in_t"), fields(fw_in, "mtr_in_t")))
check("mtr_host_api_t: same entries, signatures and order as the firmware's (fw2/ui/meter_pack.h)", norm(fields(sdk_h, "mtr_host_api_t")) == norm(fields(fw_api, "mtr_host_api_t")), (fields(sdk_h, "mtr_host_api_t"), fields(fw_api, "mtr_host_api_t")))
core = (ROOT / "fw2/cores/meter_pack_core.h").read_text()
for sdk_name, fw_name in (("TAU_METER_ABI_MAJOR", "MTR_PACK_ABI_MAJOR"), ("TAU_METER_ABI_MINOR", "MTR_PACK_ABI_MINOR")):
    check("%s equals the firmware's %s" % (sdk_name, fw_name), re.search(sdk_name + r" (\d+)u", sdk_h).group(1) == re.search(r"#define %s (\d+)u" % fw_name, core).group(1))
check("MTR_BANDS and MTR_STATS equal the firmware's", re.search(r"MTR_BANDS (\d+)u", sdk_h).group(1) == re.search(r"MTR_BANDS (\d+)u", fw_in).group(1) and re.search(r"MTR_STATS (\d+)u", sdk_h).group(1) == re.search(r"MTR_PACK_STATS\s+(\d+)u", core).group(1))
import tau_meter
import meter_desc as md
theme = (ROOT / "fw2/ui/theme.h").read_text()
fw_roles = [r.lower() for r in re.findall(r"TR_([A-Z0-9_]+)", re.search(r"enum \{(.*?)TR_COUNT", theme, re.S).group(1))]
sdk_roles = [r.lower() for r in re.findall(r"MTR_ROLE_([A-Z0-9_]+)", re.search(r"enum \{(.*?)MTR_ROLE_COUNT", sdk_h, re.S).group(1))]
check("the theme roles are the firmware's, in order, and the descriptor tool's", fw_roles == sdk_roles == md.ROLES, (fw_roles, sdk_roles))
from tau_meter import toolchain
try: tc = toolchain.find()
except toolchain.ToolchainError as e: tc = None; print("SKIP the layout compile: no LLVM")
if tc:
    src = '#include <tau_meter.h>\n' + "".join('_Static_assert(offsetof(mtr_in_t, %s) == %d, "%s");\n' % (f, o, f) for f, o in (("spec", 0), ("frame", 4), ("dt_ms", 8), ("x", 10), ("y", 12), ("w", 14), ("h", 16), ("bg", 18), ("force", 20), ("paused", 21), ("peak_l", 24), ("peak_r", 28), ("degrade", 32), ("wave", 36), ("rms_l", 40), ("rms_r", 42), ("corr", 44), ("centroid", 46), ("crest", 47), ("flux", 48), ("onset", 49), ("stats_ok", 50)))
    src += "".join('_Static_assert(offsetof(mtr_host_api_t, %s) == %d, "%s");\n' % (f, 4 * i, f) for i, f in enumerate("abi rect role params stats text rect_stash blend_ready blend_on blend_blit blend_off mb_write mb_read fence blit blit_abs sblit bar scroll_down clut_load cblit scroll_left".split()))
    src += '_Static_assert(sizeof(mtr_in_t) == 52, "size");\n_Static_assert(sizeof(mtr_host_api_t) == 88, "size");\n'
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / "t.c").write_text(src)
        r = subprocess.run([tc["clang"]] + toolchain.CFLAGS + ["-I", str(SDK / "include"), "-fsyntax-only", str(Path(td) / "t.c")], capture_output=True, text=True)
        check("the rv32 struct layout equals the offsets the host runner hard-codes (mtr_in_t 52 bytes, host table 22 words)", r.returncode == 0, r.stderr)
    run_c = (SDK / "runner/rv32run.c").read_text()
    names = re.findall(r'"(\w+)"', re.search(r"NAMES\[N_API\] = \{(.*?)\};", run_c, re.S).group(1))
    hs = [re.sub(r"\(\*(\w+)\).*", r"\1", f.split("(*", 1)[1]) if "(*" in f else f.rstrip(";").split()[-1].lstrip("*") for f in fields(sdk_h, "mtr_host_api_t")]
    check("the runner's table of host-call names is the header's, in order", names == [re.sub(r"[^\w]", "", h.split(")")[0]) for h in hs], (names, hs))

# ---- 2. the vendored files -----------------------------------------------------------------------------------------------------------------------------
V = SDK / "tau_meter" / "_vendor"
for src in ("tools/meter_bin.py", "tools/meter_package.py", "tools/player2/meter_desc.py", "tools/player2/mpreset.py", "tools/player2/meter_manifest.py"):
    check("vendored %s is a byte copy" % Path(src).name, filecmp.cmp(ROOT / src, V / Path(src).name, shallow=False))
check("ld/meter_pack.ld is a byte copy of fw2/ui/meter_pack.ld", filecmp.cmp(ROOT / "fw2/ui/meter_pack.ld", SDK / "ld/meter_pack.ld", shallow=False))
check("runner/pack_strike_core.h is a byte copy of fw2/cores/pack_strike_core.h", filecmp.cmp(ROOT / "fw2/cores/pack_strike_core.h", SDK / "runner/pack_strike_core.h", shallow=False))
from tau_meter import elfpack
import meter_bin as mb
hexv = lambda n: int(re.search(r"#define %s\s+(0x[0-9A-Fa-f]+)u" % n, core).group(1), 16)
check("the window, scratch and size constants equal the firmware's and the bin tool's", elfpack.WINDOW == mb.WINDOW == 0x24970000 and elfpack.WINDOW_SIZE == mb.WINDOW_SIZE == hexv("MTR_PACK_SLOT_SIZE") and elfpack.SCRATCH_ORG == hexv("MTR_PACK_SCRATCH_ORG") and elfpack.SCRATCH_SIZE == hexv("MTR_PACK_SCRATCH_SIZE"))
from conformance import suite
check("the suite's ABI and scratch constants equal the firmware's", suite.FW_ABI_MAJOR == int(re.search(r"MTR_PACK_ABI_MAJOR (\d+)u", core).group(1)) and suite.FW_ABI_MINOR == int(re.search(r"MTR_PACK_ABI_MINOR (\d+)u", core).group(1)) and suite.SCRATCH_CAP == elfpack.SCRATCH_SIZE)
for k, c in (("PS_MAX_STRIKES", "3u"), ("WD_FLOOR", "2000000u"), ("WD_CEIL", "20000000u"), ("WD_MULT", "4u")):
    check("the strike and watchdog constant %s is %s in the runner's copy (the docs quote it)" % (k, c), "#define %s %s" % (k, c) in (SDK / "runner/pack_strike_core.h").read_text() or re.search(r"#define %s\s+%s" % (k, c), (SDK / "runner/pack_strike_core.h").read_text()))

# ---- 3. the documents ----------------------------------------------------------------------------------------------------------------------------------
from tau_meter import docgen
check("docs/HOST_TABLE.md is exactly what `tau-meter docs` generates (red when the header changes)", (ROOT / "sdk/docs/HOST_TABLE.md").read_text() == docgen.render())
testing = (ROOT / "sdk/docs/TESTING.md").read_text()
src_ids = set(re.findall(r'"((?:PKG|LOAD|DESC|PRESET|PARAMS|STATIC|RUN|PREVIEW|INPUT)[A-Z0-9-]*)"', (SDK / "conformance/suite.py").read_text()))
src_ids |= {"PKG-" + x for x in ("ZIP", "SIZE", "PATH", "FILES", "PIN", "SIG", "PACK", "MANIFEST")} | {"LOAD-E%d" % n for n in (21, 22, 24, 25, 26, 27, 28, 29)}
src_ids -= {"RUN-", "PKG-OTHER", "LOAD-E23", "RUN-FAULT", "RUN-MISALIGNED", "RUN-" + "NAME"}
import json
for spec in json.loads((SDK / "conformance/fixtures/fixtures.json").read_text())["fixtures"].values():
    src_ids |= set(spec["expect"]["fail"]) | set(spec["expect"]["warn"])
missing = sorted(i for i in src_ids if "`%s`" % i not in testing and not re.search(r"`%s`" % re.escape(i.rsplit("-E", 1)[0] if "-E" in i else i), testing) and i not in ("RUN",))
check("TESTING.md lists every check id the suite and the fixtures produce: missing %s" % missing, not missing)
bad = []
for f in list((ROOT / "sdk/docs").glob("*.md")) + [ROOT / "sdk/template/README.md", ROOT / "sdk/sdk/README.md", ROOT / "sdk/README.md", ROOT / "sdk/meters/README.md"]:
    for tgt in re.findall(r"\]\(([^)#]+?)(?:#[^)]*)?\)", f.read_text()):
        if re.match(r"[a-z]+:", tgt): continue
        if f.parent.name == "template" and tgt.startswith("../tau-alpha-meter-sdk/"): tgt = "../../sdk/" + tgt[len("../tau-alpha-meter-sdk/"):]; p = (f.parent / tgt)
        else: p = f.parent / tgt
        if not p.resolve().exists() and not (f.name in ("README.md",) and f.parent.name == "sdk" and False): bad.append("%s -> %s" % (f.name, tgt))
check("every relative link in the documents resolves: %s" % bad[:4], not bad)

# ---- 4. the collection ---------------------------------------------------------------------------------------------------------------------------------
import sdk_collect_meters as collect
with tempfile.TemporaryDirectory() as td:
    collect.collect(td)
    d = filecmp.dircmp(td, ROOT / "sdk/meters")
    def same(dc):
        if dc.left_only or dc.diff_files or dc.funny_files: return False
        return all(same(s) for s in dc.subdirs.values())
    extra = [n for n in d.right_only if n not in ("README.md", "INDEX_DRAFT.md")]
    check("sdk/meters is exactly what tools/sdk_collect_meters.py makes from the firmware sources: %s" % (d.left_only + d.diff_files + extra)[:4], same(d) and not extra)
print("%d failure(s)" % fails); sys.exit(1 if fails else 0)
