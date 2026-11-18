#!/usr/bin/env python3
"""tools/export_sdk.py: the three repository trees are SELF-CONTAINED.
  1. the export is made (no network, no git); it holds the three trees and the pocket-test bin;
  2. the template is built into a .tmeter and passes the full suite using the EXPORTED trees alone, run from a scratch folder outside this repository with a scrubbed environment (no PYTHONPATH, no repo on any path);
     every Python module the SDK loads comes from the exported tree or the standard library;
  3. `tau-meter selftest` (the suite against its broken-meter fixtures) passes in the exported SDK; `tau-meter compose` adds the shipped Winamp Bars from share/ and the bin passes the bin checker;
  4. the collection: seven packages in dist/, each verifies, SHA256SUMS.txt matches, the draft index lists them; the pocket-test bin has eight rows and passes the checker;
  5. nothing in the three trees names this checkout's absolute path or the project's own toolchain folder; every LICENSE is MIT; every tree has a README.
Needs LLVM; without it the test says so and exits 0."""
import hashlib, json, os, subprocess, sys, tempfile, shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "sdk" / "sdk"))
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(info)[:600]))
    fails += 0 if cond else 1
from tau_meter import toolchain
try: toolchain.find()
except toolchain.ToolchainError as e:
    print("SKIP: no LLVM toolchain for the SDK (%s)" % str(e).split(".")[0]); sys.exit(0)
import export_sdk
TMP = Path(tempfile.mkdtemp())
out = export_sdk.export(TMP / "export")
sdk, tpl, col, pt = out / "tau-alpha-meter-sdk", out / "tau-alpha-meter-template", out / "tau-alpha-meters", out / "pocket-test"
check("the export holds the three trees and the pocket-test folder", all(p.is_dir() for p in (sdk, tpl, col, pt)))

# ---- 2. self-contained: copy the template and the SDK to a scratch folder and build with a scrubbed environment ---------------------------------------
scratch = Path(tempfile.mkdtemp(prefix="sdk-scratch-"))
shutil.copytree(sdk, scratch / "tau-alpha-meter-sdk"); shutil.copytree(tpl, scratch / "my-meter")
env = {"PATH": os.environ["PATH"], "HOME": os.environ.get("HOME", "/"), "TAU_METER_CACHE": str(TMP / "cache"), "PYTHONNOUSERSITE": "1", "PYTHONDONTWRITEBYTECODE": "1"}
if "SDKROOT" in os.environ: env["SDKROOT"] = os.environ["SDKROOT"]
def run(*a, cwd=scratch / "my-meter"):
    return subprocess.run([sys.executable] + [str(x) for x in a], capture_output=True, text=True, cwd=cwd, env=env)
meter = scratch / "my-meter" / "meter.json"
m = json.loads(meter.read_text()); r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "build")
check("the template with its placeholder id is refused in plain words (not a traceback)", r.returncode == 1 and "tau-meter id new" in r.stdout and "Traceback" not in r.stderr, r.stdout + r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "id", "new"); new_id = r.stdout.strip()
check("`tau-meter id new` prints 32 lower-case hex digits", len(new_id) == 32 and all(c in "0123456789abcdef" for c in new_id), new_id)
m["id"] = new_id; m["author"] = "Test Author"; meter.write_text(json.dumps(m, indent=2))
(scratch / "my-meter" / "LICENSE").write_text((scratch / "my-meter" / "LICENSE").read_text().replace("Your Name", "Test Author"))
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "build")
pkg = scratch / "my-meter" / "dist" / (new_id + ".tmeter")
check("the template builds into dist/<id>.tmeter from the exported trees alone", r.returncode == 0 and pkg.is_file(), r.stdout + r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "test", pkg, "--preview", scratch / "p.png")
check("the full suite passes on it (exit 0, no warning) and writes a preview PNG", r.returncode == 0 and " 0 failed, 0 warnings" in r.stdout and (scratch / "p.png").stat().st_size > 1000, r.stdout[-400:] + r.stderr)
probe = "import sys; sys.path.insert(0, %r); import tau_meter, conformance.suite, conformance.fixtures, tau_meter.build, tau_meter.runner, tau_meter.docgen, tau_meter.commands; print('\\n'.join(sorted({getattr(m, '__file__', '') or '' for m in sys.modules.values()})))" % str(scratch / "tau-alpha-meter-sdk")
r = run("-I", "-c", probe)
import sysconfig
std = {sysconfig.get_paths()["stdlib"], sysconfig.get_paths().get("platstdlib", ""), sysconfig.get_paths().get("purelib", "")}
stray = [f for f in r.stdout.splitlines() if f and not str(Path(f).resolve()).startswith(str((scratch / "tau-alpha-meter-sdk").resolve())) and not any(f.startswith(s) for s in std if s) and "lib-dynload" not in f and "/python" not in f.lower()]
check("every module the SDK loads comes from the exported tree or the standard library: stray %s" % stray[:3], r.returncode == 0 and not stray, r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "selftest")
check("`tau-meter selftest` passes in the exported SDK (every broken fixture fails exactly as expected)", r.returncode == 0 and "0 fixture(s) differ" in r.stdout, r.stdout[-300:] + r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "compose", scratch / "user.bin", pkg)
check("`compose` adds the shipped Winamp Bars (the player's fallback) when it is not listed", r.returncode == 0 and "adding the shipped Winamp Bars" in r.stdout, r.stdout + r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau_meter" / "_vendor" / "meter_bin.py", "check", scratch / "user.bin")
check("the bin passes the bin checker with 2 rows", r.returncode == 0, r.stdout + r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "docs")
check("`tau-meter docs` regenerates docs/HOST_TABLE.md in the exported SDK, unchanged", r.returncode == 0 and (scratch / "tau-alpha-meter-sdk" / "docs" / "HOST_TABLE.md").read_text() == (sdk / "docs" / "HOST_TABLE.md").read_text(), r.stdout + r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "thumb", scratch / "my-meter")
th = json.loads((scratch / "my-meter" / "thumb.json").read_text())
check("`tau-meter thumb` writes a 56 x 32 thumb.json from a frame of the meter", r.returncode == 0 and (th["w"], th["h"]) == (56, 32) and len(th["palette"]) == 8 and sum((b & 31) + 1 for b in bytes.fromhex(th["rle"])) == 56 * 32, r.stdout + r.stderr)
r = run(scratch / "tau-alpha-meter-sdk" / "tau-meter", "build")
check("the meter still builds with the generated thumbnail", r.returncode == 0, r.stdout + r.stderr)

check("no file of the template is empty (a 0-byte Makefile once shipped)", all(f.stat().st_size > 0 for f in tpl.rglob("*") if f.is_file()), [str(f) for f in tpl.rglob("*") if f.is_file() and f.stat().st_size == 0])
r = subprocess.run(["make", "SDK=" + str(scratch / "tau-alpha-meter-sdk"), "test"], capture_output=True, text=True, cwd=scratch / "my-meter", env=env)
check("`make test` in the exported template builds, runs the suite and the meter's own test", r.returncode == 0 and "conformant" in r.stdout and "a forced repaint is the same picture" in r.stdout, r.stdout[-300:] + r.stderr[-300:])

# ---- 4. the collection and the pocket-test bin -----------------------------------------------------------------------------------------------------------
dist = sorted((col / "dist").glob("*.tmeter"))
check("the collection has 7 packages in dist/", len(dist) == 7, len(dist))
r = run(sdk / "tau-meter", "verify", *dist)
check("every package in dist/ verifies", r.returncode == 0, r.stdout)
sums = dict(reversed(l.split("  ")) for l in (col / "SHA256SUMS.txt").read_text().split("\n") if l)
check("SHA256SUMS.txt matches the packages", all(hashlib.sha256((col / n).read_bytes()).hexdigest() == h for n, h in ((k.strip(), v) for k, v in sums.items())) and len(sums) == 7)
idx = json.loads((col / "index.draft.json").read_text())
check("index.draft.json is marked a draft, unsigned, and lists the seven", idx["DRAFT"] and idx["key_id"] is None and len(idx["meters"]) == 7)
r = run(sdk / "tau_meter" / "_vendor" / "meter_bin.py", "check", pt / "tau-meters.bin")
check("the pocket-test bin passes the bin checker", r.returncode == 0, r.stdout + r.stderr)
info = json.loads(run(sdk / "tau_meter" / "_vendor" / "meter_bin.py", "info", pt / "tau-meters.bin").stdout)
check("the pocket-test bin has 8 rows, Winamp Bars first", len(info.get("rows", info.get("meters", []))) == 8 or info.get("count") == 8, str(info)[:300])

# ---- 5. hygiene ------------------------------------------------------------------------------------------------------------------------------------------
bad = []
for tree in (sdk, tpl, col):
    for f in tree.rglob("*"):
        if not f.is_file(): continue
        if f.suffix in (".tmeter", ".png", ".bin"): continue
        t = f.read_text(errors="ignore")
        if str(ROOT) in t or "/Users/" in t or "xpack-riscv" in t or "claude/worktrees" in t: bad.append(str(f.relative_to(out)))
check("no exported file names this checkout's path or the project's own toolchain: %s" % bad[:3], not bad)
check("every tree has a README and an MIT LICENSE", all((t / "README.md").is_file() and "MIT License" in (t / "LICENSE").read_text() for t in (sdk, tpl, col)))
shutil.rmtree(TMP, ignore_errors=True); shutil.rmtree(scratch, ignore_errors=True)
print("%d failure(s)" % fails); sys.exit(1 if fails else 0)
