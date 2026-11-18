#!/usr/bin/env python3
"""The meter SDK's conformance suite (sdk/sdk/conformance, `tau-meter test`), tested the way the project tests everything: red first, and every check has a mutant.

  1. the negative fixtures: every broken meter (an out-of-bounds write, a forbidden call, a hang, an overrun, an oversized pstate, a bad ABI, a bad hash, a tampered file, ...) is reported with EXACTLY the failures
     listed in sdk/sdk/conformance/fixtures/fixtures.json (the set of failing check ids, the warning ids, a plain-words fragment of each), and the exit code is 1;
  2. the positive fixtures: the template and the seven shipped meters (built by the SDK's clang recipe from sdk/meters) pass with no failure (the shipped ones under the first-party profile);
  3. exit codes and plain words: a missing file is exit 2, a failing package exit 1, a passing one 0, and no run prints a traceback;
  4. the suite is itself tested with mutants: each mutation of a check (a bound relaxed, a limit made unreachable, the hang verdict removed, the runner's write protection removed, the vendored pin check inverted, ...)
     must make at least one of the fixtures above report something different from its expectation.
Needs LLVM (clang + lld with the riscv32 target, docs/TOOLCHAIN.md); without it the test says so and exits 0."""
import json, os, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
SDK = ROOT / "sdk" / "sdk"
sys.path.insert(0, str(SDK))
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(info)))
    fails += 0 if cond else 1

TMP = Path(tempfile.mkdtemp())
os.environ["TAU_METER_CACHE"] = str(TMP / "cache")
from tau_meter import toolchain
try: TC = toolchain.find()
except toolchain.ToolchainError as e:
    print("SKIP: no LLVM toolchain for the SDK (%s)" % str(e).split(".")[0]); sys.exit(0)
from conformance import fixtures, suite
from tau_meter import build

SPEC = fixtures.SPEC["fixtures"]
fx = fixtures.build_all(TMP / "fx", tc=TC)

def outcome(rep): return {"fail": sorted(rep.ids("fail")), "warn": sorted(rep.ids("warn"))}

# ---- 1. the negative fixtures --------------------------------------------------------------------------------------------------------------------------
reports = {}
for name, path in fx.items():
    rep = suite.run(path, quick=True, tc=TC); reports[name] = rep
    exp = SPEC[name]["expect"]
    check("fixture %-18s fails exactly %s" % (name, exp["fail"] or "nothing"), sorted(exp["fail"]) == outcome(rep)["fail"], "got %s" % outcome(rep)["fail"])
    check("fixture %-18s warns exactly %s" % (name, exp["warn"] or "nothing"), sorted(exp["warn"]) == outcome(rep)["warn"], "got %s" % outcome(rep)["warn"])
    for cid, frag in exp.get("msg", {}).items():
        txt = " ".join(r.title + " " + r.detail for r in rep.results if r.id == cid)
        check("fixture %-18s %s says %r in plain words" % (name, cid, frag), frag in txt, txt[:200])
    check("fixture %-18s exit code %d" % (name, 1 if exp["fail"] else 0), rep.exit_code() == (1 if exp["fail"] else 0), rep.exit_code())
check("the control fixture 'good' passes with no warning", outcome(reports["good"]) == {"fail": [], "warn": []})
check("at least 25 negative fixtures", len([n for n in SPEC if SPEC[n]["expect"]["fail"] or SPEC[n]["expect"]["warn"]]) >= 25, len(SPEC))

# ---- 2. the positive fixtures ---------------------------------------------------------------------------------------------------------------------------
tmpl = ROOT / "sdk" / "template"
with tempfile.TemporaryDirectory() as td:
    p = Path(td) / "t"; shutil.copytree(tmpl, p, ignore=shutil.ignore_patterns("dist", "build"))
    m = json.loads((p / "meter.json").read_text()); m["id"] = "9f3c1e7a52b84d06a1c9e34b7d20f518"; (p / "meter.json").write_text(json.dumps(m))
    r0 = build.build(p, Path(td) / "o0", tc=TC); rep0 = suite.run(r0["package"], quick=True, tc=TC)
    check("the template with its placeholder text gets exactly the PKG-PLACEHOLDER warning and no failure", rep0.ids("warn") == ["PKG-PLACEHOLDER"] and not rep0.ids("fail"), rep0.text())
    m["author"] = "Test Author"; (p / "meter.json").write_text(json.dumps(m)); (p / "LICENSE").write_text((p / "LICENSE").read_text().replace("Your Name", "Test Author"))
    r = build.build(p, Path(td) / "o", tc=TC)
    rep = suite.run(r["package"], tc=TC)
    check("the template builds into a .tmeter and passes the full suite (%d checks)" % len(rep.results), rep.exit_code() == 0 and not rep.ids("warn"), rep.text())
KEYS = ["winamp_bars", "winamp_scope", "vu_master", "chladni", "layered_wave", "waterfall_grid", "sound_map"]
def shipped(k):
    r = build.build(ROOT / "sdk" / "meters" / k, TMP / "col", tc=TC)
    rep = suite.run(r["package"], profile="shipped", quick=True, tc=TC)
    return k, rep
with ThreadPoolExecutor(4) as ex:
    for k, rep in ex.map(shipped, KEYS):
        check("shipped meter %-15s built by the SDK recipe passes the suite (first-party profile): fails %s" % (k, rep.ids("fail")), rep.exit_code() == 0, rep.text())

# ---- 3. exit codes and plain words ---------------------------------------------------------------------------------------------------------------------
cli = [sys.executable, str(SDK / "tau-meter")]
def run_cli(*a):
    return subprocess.run(cli + list(a), capture_output=True, text=True, env=dict(os.environ))
r = run_cli("test", str(TMP / "nothing.tmeter"))
check("a missing file is exit code 2 with a plain sentence", r.returncode == 2 and "does not exist" in r.stdout and "Traceback" not in r.stdout + r.stderr, r.stdout + r.stderr)
r = run_cli("test", str(fx["box_left"]), "--quick")
check("a failing package: exit code 1, the failure named, no traceback", r.returncode == 1 and "RUN-BOX" in r.stdout and "NOT CONFORMANT" in r.stdout and "Traceback" not in r.stderr, r.stdout[-300:] + r.stderr[-300:])
r = run_cli("test", str(fx["good"]), "--quick", "--json")
check("--json: machine-readable results with exit 0 for a passing package", r.returncode == 0 and json.loads(r.stdout)["exit"] == 0, r.stdout[-200:] + r.stderr[-200:])
r = run_cli("verify", str(fx["bad_hash"]))
check("`tau-meter verify` names the broken pin in plain words", r.returncode == 1 and "SHA-256 pin" in r.stdout, r.stdout)

# ---- 4. mutants of the suite ---------------------------------------------------------------------------------------------------------------------------
MUTANTS = [   # (file under sdk/sdk, old text, new text, a fixture that must then differ)
    ("conformance/canvas.py", "x >= bx and y >= by and x + w <= bx + bw", "x >= bx - 64 and y >= by and x + w <= bx + bw", "box_left"),
    ("conformance/canvas.py", "y >= by and x + w <= bx + bw and y + h <= by + bh)", "y >= by and x + w <= bx + bw and y + h <= by + bh + 64)", "box_bottom"),
    ("conformance/suite.py", 'if limit and t.cycles > limit and "cyc" not in over_done', 'if limit and t.cycles > limit * 1000 and "cyc" not in over_done', "overrun"),
    ("conformance/suite.py", 'if ceiling and t.cmds > ceiling and "cmd" not in over_done', 'if ceiling and t.cmds > ceiling * 1000 and "cmd" not in over_done', "too_many_cmds"),
    ("conformance/suite.py", 'if t.status == "cut":\n                    if "RUN-HANG"', 'if t.status == "never":\n                    if "RUN-HANG"', "hang"),
    ("conformance/suite.py", 'same = again["hash"] == results["music"]["hash"]', 'same = True', "nondeterministic"),
    ("conformance/suite.py", "or (abi & 255) > FW_ABI_MINOR:", "or (abi & 255) > 99:", "bad_abi_minor"),
    ("conformance/suite.py", "if (abi >> 8) != FW_ABI_MAJOR or (abi & 255)", "if False or (abi & 255)", "bad_abi_major"),
    ("conformance/suite.py", "if max_stack > STACK_FAIL:", "if max_stack > STACK_FAIL * 100:", "stack_hog"),
    ("conformance/suite.py", 'if t.pstate_subword and "RUN-PSTATE-WORD"', 'if False and "RUN-PSTATE-WORD"', "pstate_byte"),
    ("conformance/suite.py", "if t.in_fields_new and", "if False and", "abi_fields"),
    ("conformance/rv.py", 'if k == "system":', 'if k == "systemx":', "forbidden_call"),
    ("conformance/rv.py", "if scratch_lo <= addr < scratch_hi: return True", "return True", "wild_write"),
    ("tau_meter/_vendor/meter_package.py", "elif hashlib.sha256(files[n]).hexdigest() != pins[n]:", "elif hashlib.sha256(files[n]).hexdigest() == pins[n]:", "tampered_file"),
    ("runner/rv32run.c", '    fault("writes an address that is not the meter\'s memory", a);', "    (void)a;", "wild_write"),
    ("runner/rv32run.c", "if (!profile_shipped || !mmio_known(off)) { fault(\"reads a hardware register", "if (0) { fault(\"reads a hardware register", "hw_register"),
    ("runner/rv32run.c", '{ fault("writes into the inputs the player passed (they are read-only)", a); return; }', "{ return; }", "write_input"),
    ("runner/rv32run.c", "if (in_reads_new && abi_minor < 1)", "if (0 && abi_minor < 1)", "abi_fields"),
    ("runner/pack_strike_core.h", "return p->strikes >= PS_MAX_STRIKES;", "return 0;", "overrun"),
]
PROBE = r'''
import json, os, sys
sys.path.insert(0, sys.argv[1])
from conformance import suite
out = {}
for name, path in json.loads(sys.argv[2]).items():
    rep = suite.run(path, quick=True)
    out[name] = {"fail": sorted(rep.ids("fail")), "warn": sorted(rep.ids("warn"))}
print(json.dumps(out))
'''
(TMP / "probe.py").write_text(PROBE)
def mutant(i):
    f, old, new, fixture = MUTANTS[i]
    d = TMP / ("sdk_m%d" % i)
    shutil.copytree(SDK, d, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    src = (d / f).read_text()
    if src.count(old) != 1: return i, None, "the mutation text occurs %d times in %s" % (src.count(old), f)
    (d / f).write_text(src.replace(old, new))
    want = {fixture: str(fx[fixture]), "good": str(fx["good"])}
    env = dict(os.environ); env["TAU_METER_CACHE"] = str(TMP / ("cache_m%d" % i)); env["PYTHONDONTWRITEBYTECODE"] = "1"
    r = subprocess.run([sys.executable, str(TMP / "probe.py"), str(d), json.dumps(want)], capture_output=True, text=True, env=env, timeout=600)
    if r.returncode: return i, True, "the mutant broke the suite (%s)" % r.stderr.strip().splitlines()[-1][:80]
    got = json.loads(r.stdout)
    exp = {n: {"fail": sorted(SPEC[n]["expect"]["fail"]), "warn": sorted(SPEC[n]["expect"]["warn"])} for n in want}
    return i, got != exp, "survived: the fixtures still report the expected failures" if got == exp else ""
with ThreadPoolExecutor(4) as ex:
    for i, killed, why in ex.map(mutant, range(len(MUTANTS))):
        f, old, new, fixture = MUTANTS[i]
        check("mutant %2d (%s: %s) is caught by fixture '%s'" % (i, f.split("/")[-1], old.strip()[:50], fixture), bool(killed), why)

shutil.rmtree(TMP, ignore_errors=True)
print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
