"""Where the compiler is, and what it must be: ONE place (docs/TOOLCHAIN.md).

The SDK builds meters with LLVM: `clang` (compiler driver) and `ld.lld` (linker) for the RISC-V target, nothing else. The pinned and tested version is PINNED_LLVM; the same clang builds the
host runner of `tau-meter test`. Lookup order for each tool: the environment variable (TAU_METER_CLANG, TAU_METER_LLD; it must exist: a wrong value is an error, never a silent fallback), then PATH,
then the usual install folders (Homebrew on macOS, /usr/lib/llvm-*/bin on Debian and Ubuntu, C:\\Program Files\\LLVM\\bin on Windows)."""
import os, re, shutil, subprocess, sys, tempfile
from pathlib import Path

PINNED_LLVM = "23.1.5"             # the version the seven shipped meters were proven with (docs/TOOLCHAIN.md); other versions work if the proof (`tau-meter doctor --prove`) passes
MIN_LLVM = 18                      # older than this has no usable rv32 + lld combination for the linker script: refused
TARGET = "riscv32-unknown-elf"
CFLAGS = ["--target=" + TARGET, "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-mcmodel=medany", "-Os", "-std=gnu11", "-ffreestanding", "-fno-pic", "-fno-builtin-printf",
          "-fno-stack-protector", "-fno-jump-tables", "-fno-exceptions", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-ffunction-sections", "-fdata-sections",
          "-Wall", "-Wextra", "-Wno-unused-parameter"]
LDFLAGS = ["-nostdlib", "-nostartfiles", "-static", "-Wl,--gc-sections", "-Wl,--build-id=none", "-Wl,-z,norelro", "-Wl,--no-eh-frame-hdr"]


class ToolchainError(RuntimeError):
    pass


def _candidates(name):
    out = []
    for d in ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin", "/opt/homebrew/opt/lld/bin", "/usr/local/opt/lld/bin", r"C:\Program Files\LLVM\bin", "/usr/lib/llvm/bin"):
        out.append(Path(d) / name)
    for p in sorted(Path("/usr/lib").glob("llvm-*/bin"), reverse=True): out.append(p / name)
    return out


def _has_rv32(clang):
    try: return "riscv32" in subprocess.run([clang, "--print-targets"], capture_output=True, text=True).stdout
    except OSError: return False


def _find(name, envvar):
    """the environment variable, else the first match on PATH or in the usual folders; for clang, the first one that has the riscv32 target (Apple's /usr/bin/clang does not)"""
    given = os.environ.get(envvar)
    if given:
        if not Path(given).exists(): raise ToolchainError("%s=%s does not exist" % (envvar, given))
        return str(given)
    exe = name + (".exe" if os.name == "nt" else "")
    found = []
    w = shutil.which(exe)
    if w: found.append(w)
    found += [str(c) for c in _candidates(exe) if c.exists()]
    for f in found:
        if name != "clang" or _has_rv32(f): return f
    return found[0] if found else None


def version_of(clang):
    out = subprocess.run([clang, "--version"], capture_output=True, text=True).stdout
    m = re.search(r"clang version (\d+)\.(\d+)\.(\d+)", out)
    return tuple(int(x) for x in m.groups()) if m else None


def find():
    """-> {"clang", "lld", "version": (a, b, c), "line": the clang --version line}; raises ToolchainError with the fix in plain words"""
    clang = _find("clang", "TAU_METER_CLANG"); lld = _find("ld.lld", "TAU_METER_LLD")
    if not clang: raise ToolchainError("clang was not found. Install LLVM (docs/TOOLCHAIN.md): macOS `brew install llvm lld`, Debian/Ubuntu `sudo apt install clang lld`, Windows: the LLVM installer from github.com/llvm/llvm-project/releases. Or set TAU_METER_CLANG.")
    if not lld: raise ToolchainError("ld.lld was not found. Install lld (docs/TOOLCHAIN.md): macOS `brew install lld`, Debian/Ubuntu `sudo apt install lld`, Windows: it comes with the LLVM installer. Or set TAU_METER_LLD.")
    v = version_of(clang)
    if not v: raise ToolchainError("%s did not say its version; is it a clang?" % clang)
    if v[0] < MIN_LLVM: raise ToolchainError("clang %d.%d.%d is older than the supported minimum, LLVM %d (pinned and tested: %s)" % (v + (MIN_LLVM, PINNED_LLVM)))
    tg = subprocess.run([clang, "--print-targets"], capture_output=True, text=True).stdout
    if "riscv32" not in tg: raise ToolchainError("this clang has no riscv32 target (Apple's own clang does not): install LLVM from Homebrew or llvm.org (docs/TOOLCHAIN.md)")
    line = subprocess.run([clang, "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    return {"clang": clang, "lld": lld, "version": v, "line": line, "pinned": ".".join(map(str, v)) == PINNED_LLVM}


def host_env():
    """os.environ for building the host runner: on macOS the default SDK is sometimes unusable for the linker; an SDKROOT that works is kept, else the newest installed SDK that links is chosen"""
    env = dict(os.environ)
    if sys.platform != "darwin": return env
    return env


def host_cc_flags(tc):
    """flags for `clang` building a native program: on macOS point at the SDK when the compiler cannot find it"""
    if sys.platform != "darwin": return []
    cands = []
    if os.environ.get("SDKROOT"): cands.append(os.environ["SDKROOT"])
    try:
        p = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
        if p: cands.append(p)
    except OSError: pass
    cands += [str(p) for p in sorted(Path("/Library/Developer/CommandLineTools/SDKs").glob("MacOSX*.sdk"), reverse=True)]
    for sdk in cands + [None]:
        flags = ["-isysroot", sdk] if sdk else []
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "t.c"; src.write_text("#include <stdio.h>\nint main(void){puts(\"x\");return 0;}\n")
            r = subprocess.run([tc["clang"]] + flags + [str(src), "-o", str(Path(d) / "t")], capture_output=True, text=True)
            if r.returncode == 0: return flags
    raise ToolchainError("clang cannot link a native program on this Mac (the Command Line Tools SDK is missing or broken): run `xcode-select --install`")
