# Host build tools: where they are and how to check (B-985)

`python3 tools/check_env.py` (or `make check-env`) prints what is installed, which version, where it was found, and the exact fix for what is missing (`--strict` exits 1 when a REQUIRED tool is missing). `tools/toolchain.py` is the one place every script, test and `build.sh` asks.

| Tool | Needed for | How it is found | If it is missing |
|---|---|---|---|
| RISC-V xPack gcc 15.2.0-1 (~1.6 GB, git-ignored `toolchain/`) | every firmware build, pack tests, cold-image tools | `RISCV_TOOLCHAIN_BIN` (must exist), else `toolchain/` in this tree, else **the main checkout's `toolchain/` when this is a git worktree**, else `riscv-none-elf-gcc` on PATH | download the xPack release into `toolchain/` of the main checkout; worktrees need nothing |
| `cc` with a working macOS SDK | every host test that compiles C natively | the default SDK if it links; else an `SDKROOT` already set that links; else the newest installed SDK that links (`tools/toolchain.py sdkroot`) | install or select an Xcode whose SDK links (`xcode-select -s`) |
| Homebrew llvm + lld | building the meter WebAssembly modules (`tools/meters/preview/wasm/build_wasm.py`, `make new-meter`) | `WASM_CLANG` / `WASM_LD`, else PATH, else the Homebrew prefixes (Apple Silicon `/opt/homebrew`, Intel `/usr/local`); only a clang that really builds wasm32 counts | `brew install llvm lld` |
| node | the meter lab tests and the WebAssembly golden tests | PATH | install node (the tests skip without it) |
| iverilog / vvp, verilator | the RTL simulations, lint | PATH | `brew install icarus-verilog verilator` |

**macOS SDK (2026-11-07).** On this machine the default `MacOSX.sdk` points to 27.2, which the linker cannot use ("unknown architecture arm64e.x1"), so every native compile failed until `SDKROOT` named the 26.5 SDK. `make` and `tools/run_host_tests.py` now set it by themselves. A test run by hand (`python3 sim/test_x.py`) inherits only your shell, so run `eval "$(python3 tools/toolchain.py export)"` once (it prints `export SDKROOT=...` and `export RISCV_TOOLCHAIN_BIN=...`).

**Git worktrees.** A worktree has no `toolchain/` (git-ignored, 1.6 GB) and used to fail ~17 host tests until it was symlinked. `tools/toolchain.py` finds the main checkout's through `git worktree list`, so `fw2/build.sh`, `fw/build.sh`, the Makefile and the Python tools work in a fresh worktree. A wrong `RISCV_TOOLCHAIN_BIN` is an error, never a silent fallback.

**Meter modules and the compiler version.** Each committed `meters/<key>_wasm.js` records its source hashes and the clang that built it. `build_wasm.py --check` always checks the source hashes, and compares a fresh build byte for byte only with the same clang; with another one it prints a note and does not fail (a Homebrew upgrade no longer breaks the tests).

Tests: `sim/test_toolchain.py` (the resolver rules with fake directories, the SDK probe, no hard-coded xPack path anywhere).
