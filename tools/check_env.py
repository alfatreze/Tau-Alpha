#!/usr/bin/env python3
"""One look at the build tools a session needs (B-985): what is there, which version, where it was found, and the exact fix for what is missing.

  python3 tools/check_env.py            print the table (exit 0)
  python3 tools/check_env.py --strict   exit 1 when a REQUIRED tool is missing (python, a C compiler that links, the RISC-V compiler)
  make check-env

REQUIRED: python3, make, git, `cc` that links a program (macOS: with a working SDK, see tools/toolchain.py), the RISC-V xPack gcc. NEEDED FOR PARTS: node (the meter lab and the WebAssembly tests), Homebrew llvm + lld (building the meter
modules), iverilog / vvp (the RTL simulations), verilator (lint). Nothing here writes anything."""
import os, shutil, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import toolchain
ROOT = toolchain.ROOT


def first_line(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        return (r.stdout.strip() or r.stderr.strip()).splitlines()[0]
    except (OSError, subprocess.SubprocessError, IndexError): return ""


def rows():
    out = []
    def add(name, ok, detail, fix="", required=False): out.append((name, ok, detail, fix, required))
    add("python3", True, "%s (%s)" % (sys.version.split()[0], sys.executable), required=True)
    for t in ("make", "git"):
        p = shutil.which(t); add(t, bool(p), p or "", "install it (Xcode command line tools: xcode-select --install)", True)
    # the C compiler and the macOS SDK
    cc = shutil.which("cc")
    try:
        s = toolchain.sdkroot()
        add("cc + SDK", True, "%s; %s" % (first_line(["cc", "--version"]), ("SDKROOT=" + s + " (the default SDK cannot link: tools/run_host_tests.py and the Makefile set it; for a script run by hand: eval \"$(python3 tools/toolchain.py export)\")") if s else "the default SDK links"), required=True)
    except toolchain.ToolchainError as e:
        add("cc + SDK", False, "", str(e), True)
    # the RISC-V compiler
    try:
        b = toolchain.riscv_bin(); env = os.environ.get("RISCV_TOOLCHAIN_BIN")
        where = "RISCV_TOOLCHAIN_BIN" if env and Path(env) == b else "this tree" if (ROOT / toolchain.XPACK) == b else "the main checkout (%s)" % b.parent.parent.parent if str(b).endswith(str(toolchain.XPACK)) else "PATH"
        add("RISC-V gcc", True, "%s -- %s" % (first_line([str(b / toolchain.GCC), "--version"]), where), required=True)
    except toolchain.ToolchainError as e:
        add("RISC-V gcc", False, "", str(e), True)
    w = toolchain.wasm_tools()
    add("llvm + lld (WebAssembly)", bool(w), ("%s, %s" % (first_line([w[0], "--version"]), w[1])) if w else "", "brew install llvm lld (or set WASM_CLANG and WASM_LD); only needed to build the meter modules (tools/meters/preview/wasm/build_wasm.py)")
    for t, fix, why in (("node", "install node (the meter lab and the WebAssembly tests skip without it)", ""), ("iverilog", "brew install icarus-verilog (the RTL simulations)", ""), ("vvp", "comes with iverilog", ""), ("verilator", "brew install verilator (lint)", "")):
        p = shutil.which(t); add(t, bool(p), ("%s (%s)" % (first_line([t, "-V" if t == "iverilog" else "--version"]) if t != "vvp" else "", p)).strip() if p else "", fix)
    return out


def main(argv):
    r = rows(); w = max(len(x[0]) for x in r)
    for name, ok, detail, fix, req in r:
        print("%-*s  %-7s %s" % (w, name, "ok" if ok else ("MISSING" if req else "absent"), detail if ok else fix))
    bad = [x[0] for x in r if x[4] and not x[1]]
    print("\n" + ("all required tools are there" if not bad else "REQUIRED and missing: " + ", ".join(bad)))
    return 1 if (bad and "--strict" in argv) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
