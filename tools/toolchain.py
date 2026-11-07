#!/usr/bin/env python3
"""Where the build tools are: ONE place for every script, test and build.sh (B-985).

  RISC-V (the xPack gcc that builds the firmware, ~1.6 GB, git-ignored under toolchain/): riscv_bin() looks, in this order, at
      1. RISCV_TOOLCHAIN_BIN (it must exist: a wrong value is an error, never a silent fallback),
      2. <this tree>/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin,
      3. the same folder of the MAIN checkout (a git worktree has no toolchain/ of its own: `git worktree list` names the main one),
      4. riscv-none-elf-gcc on PATH,
    and raises ToolchainError with the fix when none exists.
  macOS native compiles (every host test builds C with `cc`): the default SDK can be unusable for the linker (2026-11: MacOSX.sdk -> 27.2 fails with "unknown architecture arm64e.x1"). sdkroot() returns "" when the
      default works (or off macOS), else the newest installed SDK that links a test program; host_env() is os.environ with SDKROOT set accordingly (an SDKROOT already in the environment that works is kept).
  WebAssembly (the meter modules, tools/meters/preview/wasm/build_wasm.py): wasm_tools() returns (clang, wasm-ld) from WASM_CLANG / WASM_LD, then PATH, then the Homebrew llvm and lld prefixes (Apple Silicon or
      Intel), keeping only a clang that really builds for wasm32 (Apple's does not); None when there is none.

  python3 tools/toolchain.py bin | riscv NAME | sdkroot | export | wasm      (export prints `export SDKROOT=...` lines for a shell: eval "$(python3 tools/toolchain.py export)")"""
import os, shlex, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
XPACK = Path("toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin")
GCC = "riscv-none-elf-gcc"


class ToolchainError(RuntimeError):
    pass


def main_checkout(root=ROOT):
    """the first worktree `git worktree list` names (the main checkout), or None"""
    try:
        out = subprocess.run(["git", "-C", str(root), "worktree", "list", "--porcelain"], capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for ln in out.splitlines():
        if ln.startswith("worktree "): return Path(ln[len("worktree "):])
    return None


def riscv_bin(root=ROOT, env=None, main=None):
    env = os.environ if env is None else env
    given = env.get("RISCV_TOOLCHAIN_BIN")
    if given:
        if not (Path(given) / GCC).exists(): raise ToolchainError("RISCV_TOOLCHAIN_BIN=%s has no %s" % (given, GCC))
        return Path(given)
    here = Path(root) / XPACK
    if (here / GCC).exists(): return here
    m = main_checkout(root) if main is None else main
    if m and (Path(m) / XPACK / GCC).exists() and Path(m).resolve() != Path(root).resolve(): return Path(m) / XPACK
    p = shutil.which(GCC, path=env.get("PATH"))
    if p: return Path(p).parent
    raise ToolchainError("no RISC-V toolchain: put the xPack riscv-none-elf-gcc 15.2.0-1 in %s, or set RISCV_TOOLCHAIN_BIN, or put %s on PATH (a git worktree uses the main checkout's toolchain/ automatically)" % (here.parent.parent, GCC))


def riscv(name, **kw):
    """the path of riscv-none-elf-<name> (gcc, objcopy, nm, objdump, size, readelf ...)"""
    return str(riscv_bin(**kw) / ("riscv-none-elf-" + name))


def _links(sdk, cc="cc"):
    """a C program that needs libSystem links with this SDK (sdk None = the compiler's default)"""
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; src.write_text("#include <stdio.h>\nint main(void){puts(\"x\");return 0;}\n")
        env = {k: v for k, v in os.environ.items() if k != "SDKROOT"}
        if sdk: env["SDKROOT"] = str(sdk)
        try: return subprocess.run([cc, str(src), "-o", str(Path(d) / "t")], capture_output=True, env=env, timeout=60).returncode == 0
        except (OSError, subprocess.SubprocessError): return False


def sdk_candidates():
    """installed macOS SDKs, newest first (the symlinks MacOSX.sdk / MacOSX26.sdk are skipped: the versioned folders are the real ones)"""
    dirs = [Path("/Library/Developer/CommandLineTools/SDKs")]
    try:
        xs = subprocess.run(["xcode-select", "-p"], capture_output=True, text=True, timeout=20).stdout.strip()
        if xs: dirs.append(Path(xs) / "Platforms/MacOSX.platform/Developer/SDKs")
    except (OSError, subprocess.SubprocessError): pass
    found = {}
    for d in dirs:
        for p in d.glob("MacOSX*.sdk") if d.is_dir() else []:
            if p.is_symlink(): continue
            v = p.name[len("MacOSX"):-len(".sdk")]
            if v: found[p.resolve()] = tuple(int(x) if x.isdigit() else 0 for x in v.split("."))
    return [p for p, _ in sorted(found.items(), key=lambda kv: kv[1], reverse=True)]


def sdkroot(links=_links, candidates=sdk_candidates, platform=None, env=None):
    """"" = nothing to set; else the SDKROOT that links. Raises ToolchainError when none does."""
    env = os.environ if env is None else env
    if (platform or sys.platform) != "darwin": return ""
    cur = env.get("SDKROOT")
    if cur and links(cur): return cur
    if links(None): return ""
    for sdk in candidates():
        if links(sdk): return str(sdk)
    raise ToolchainError("no macOS SDK links a C program (the default fails and none of %s works): install the Xcode command line tools or select another Xcode (xcode-select -s)" % [str(p) for p in candidates()])


def host_env(**kw):
    env = dict(os.environ)
    s = sdkroot(**kw)
    if s: env["SDKROOT"] = s
    return env


def _wasm_ok(clang):
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; src.write_text("int f(int x){return x+1;}\n")
        try: return subprocess.run([str(clang), "--target=wasm32", "-ffreestanding", "-nostdlib", "-c", str(src), "-o", str(Path(d) / "t.o")], capture_output=True, timeout=60).returncode == 0
        except (OSError, subprocess.SubprocessError): return False


def wasm_tools(env=None, prefixes=None):
    """(clang, wasm-ld) that build WebAssembly, or None"""
    env = os.environ if env is None else env
    pre = prefixes if prefixes is not None else [Path("/opt/homebrew/opt"), Path("/usr/local/opt")]
    clangs = [env["WASM_CLANG"]] if env.get("WASM_CLANG") else ([w for w in (shutil.which("clang", path=env.get("PATH")),) if w] + [str(p / "llvm/bin/clang") for p in pre])
    lds = [env["WASM_LD"]] if env.get("WASM_LD") else ([w for w in (shutil.which("wasm-ld", path=env.get("PATH")),) if w] + [str(p / "lld/bin/wasm-ld") for p in pre] + [str(p / "llvm/bin/wasm-ld") for p in pre])
    ld = next((l for l in lds if Path(l).exists()), None)
    clang = next((c for c in clangs if Path(c).exists() and _wasm_ok(c)), None)
    return (clang, ld) if clang and ld else None


def clang_version(clang):
    try: return subprocess.run([str(clang), "--version"], capture_output=True, text=True, timeout=20).stdout.splitlines()[0].strip()
    except (OSError, subprocess.SubprocessError, IndexError): return ""


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    try:
        if cmd == "bin": print(riscv_bin())
        elif cmd == "riscv" and len(argv) > 2: print(riscv(argv[2]))
        elif cmd == "sdkroot": print(sdkroot())
        elif cmd == "export":
            s = sdkroot()
            if s: print("export SDKROOT=%s" % shlex.quote(s))
            try: print("export RISCV_TOOLCHAIN_BIN=%s" % shlex.quote(str(riscv_bin())))
            except ToolchainError: pass
        elif cmd == "wasm":
            w = wasm_tools(); print("%s %s" % w if w else "")
            return 0 if w else 1
        else: print(__doc__); return 2
    except ToolchainError as e:
        print(str(e), file=sys.stderr); return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
