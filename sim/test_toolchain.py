#!/usr/bin/env python3
"""tools/toolchain.py (B-985): the one place that says where the build tools are.
  1. riscv_bin: RISCV_TOOLCHAIN_BIN wins and must exist (a wrong one is an error, never a silent fallback); else this tree's toolchain/; else the MAIN checkout's (a git worktree has none of its own); else PATH; else an error that
     says how to fix it;
  2. sdkroot: the default SDK when it links (""), the SDKROOT already set when it links, else the newest installed SDK that links, an error when none does, "" off macOS; host_env puts the result in SDKROOT;
  3. wasm_tools: a WASM_CLANG that does not exist gives None (no silent fallback), the real tools give a pair when they are installed;
  4. every committed meter module records the clang that built it (build_wasm.py compares bytes only for the same compiler);
  5. no script, test or makefile recipe hard-codes the xPack path any more (fw/build.sh keeps its documented vendored lookup);
  6. main_checkout() names an existing folder with .git; check_env.py runs and reports the tools."""
import json, os, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import toolchain as tc
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:400]))
    fails += 0 if ok else 1
def raises(fn, needle=""):
    try: fn()
    except tc.ToolchainError as e: return needle in str(e)
    return False
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    def fake(base):
        b = base / tc.XPACK; b.mkdir(parents=True); (b / tc.GCC).write_text("#!/bin/sh\n"); return b
    tree, main, envdir = td / "tree", td / "main", td / "envbin"
    envdir.mkdir(); (envdir / tc.GCC).write_text("x")
    tb, mb = fake(tree), fake(main)
    nopath = {"PATH": str(td / "empty")}
    check("RISCV_TOOLCHAIN_BIN wins", tc.riscv_bin(root=tree, env={"RISCV_TOOLCHAIN_BIN": str(envdir)}, main=main) == envdir)
    check("a RISCV_TOOLCHAIN_BIN without the compiler is an error, not a fallback", raises(lambda: tc.riscv_bin(root=tree, env={"RISCV_TOOLCHAIN_BIN": str(td / "nowhere")}, main=main), "RISCV_TOOLCHAIN_BIN"))
    check("this tree's toolchain/ comes next", tc.riscv_bin(root=tree, env=nopath, main=main) == tb)
    bare = td / "bare"; bare.mkdir()
    check("a tree without one (a git worktree) uses the main checkout's", tc.riscv_bin(root=bare, env=nopath, main=main) == mb)
    check("riscv(name) names the tool", tc.riscv("nm", root=tree, env=nopath, main=main) == str(tb / "riscv-none-elf-nm"))
    pdir = td / "pathbin"; pdir.mkdir(); g = pdir / tc.GCC; g.write_text("#!/bin/sh\n"); g.chmod(0o755)
    check("PATH is the last resort", tc.riscv_bin(root=bare, env={"PATH": str(pdir)}, main=td / "none") == pdir)
    check("nothing anywhere: an error that says how to fix it", raises(lambda: tc.riscv_bin(root=bare, env=nopath, main=td / "none"), "RISCV_TOOLCHAIN_BIN"))
    ok = lambda good: (lambda sdk, cc="cc": (sdk is not None and str(sdk) in good) or (sdk is None and "default" in good))
    cands = lambda: [Path("/sdk/27.2"), Path("/sdk/26.5"), Path("/sdk/26")]
    check("the default SDK links: nothing to set", tc.sdkroot(links=ok({"default"}), candidates=cands, platform="darwin", env={}) == "")
    check("the default fails: the newest SDK that links, skipping the one that fails", tc.sdkroot(links=ok({"/sdk/26.5", "/sdk/26"}), candidates=cands, platform="darwin", env={}) == "/sdk/26.5")
    check("an SDKROOT in the environment that links is kept", tc.sdkroot(links=ok({"/mine", "default"}), candidates=cands, platform="darwin", env={"SDKROOT": "/mine"}) == "/mine")
    check("an SDKROOT in the environment that does not link is replaced", tc.sdkroot(links=ok({"/sdk/26"}), candidates=cands, platform="darwin", env={"SDKROOT": "/bad"}) == "/sdk/26")
    check("no SDK links: an error that names the fix", raises(lambda: tc.sdkroot(links=ok(set()), candidates=cands, platform="darwin", env={}), "xcode-select"))
    check("off macOS there is nothing to set", tc.sdkroot(links=ok(set()), candidates=cands, platform="linux", env={}) == "")
    he = tc.host_env(links=ok({"/sdk/26.5"}), candidates=cands, platform="darwin", env={})
    check("host_env puts it in SDKROOT", he.get("SDKROOT") == "/sdk/26.5" and "PATH" in he)
check("a WASM_CLANG that does not exist gives None", tc.wasm_tools(env={"WASM_CLANG": "/nowhere/clang", "WASM_LD": "/nowhere/wasm-ld", "PATH": ""}, prefixes=[]) is None)
w = tc.wasm_tools()
check("the real WebAssembly tools (when installed) are a pair that builds wasm32", w is None or (Path(w[0]).exists() and Path(w[1]).exists() and tc._wasm_ok(w[0])))
mods = sorted((ROOT / "tools/meters/preview/meters").glob("*_wasm.js"))
miss = []
for m in mods:
    t = m.read_text(); meta = json.loads(t[t.index("/*META") + 6:t.index("META*/")])
    if not meta.get("clang"): miss.append(m.name)
check("every committed meter module records the clang that built it (%d modules)" % len(mods), mods and not miss, miss)
import re
bad = []
for p in list((ROOT / "tools").rglob("*.py")) + list((ROOT / "sim").glob("*.py")) + [ROOT / "Makefile", ROOT / "fw2/build.sh", ROOT / "package.py"]:
    if p.name == "toolchain.py" or p == Path(__file__).resolve() or not p.is_file(): continue
    if "xpack-riscv-none-elf-gcc-15.2.0-1" in p.read_text(errors="ignore"): bad.append(str(p.relative_to(ROOT)))
check("no script, test or recipe hard-codes the xPack path (they ask tools/toolchain.py)", not bad, bad)
m = tc.main_checkout()
check("main_checkout() names the main checkout", m is not None and (Path(m) / ".git").exists(), m)
r = subprocess.run([sys.executable, str(ROOT / "tools/check_env.py"), "--strict"], capture_output=True, text=True)
check("check_env.py runs and finds the required tools here", r.returncode == 0 and "RISC-V gcc" in r.stdout, r.stdout[-400:] + r.stderr[-200:])
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
