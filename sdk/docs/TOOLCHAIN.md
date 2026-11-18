# The toolchain: LLVM `clang` + `ld.lld`, pinned

**One compiler family builds everything the SDK needs:** LLVM's `clang` (compiler driver) and `ld.lld` (linker) for the RISC-V 32-bit target, and the same `clang` builds the small host program (`rv32run`) that runs meters in `tau-meter test`. Nothing else is required: no GCC, no binutils, no `objcopy` (the SDK reads the ELF itself), no libc, no libgcc (the SDK ships the few helpers a meter may need).

**Pinned: LLVM 23.1.5** (Homebrew `llvm` 23.1.5 and `lld` 23.1.5 on macOS arm64). The seven shipped meters were built with it and proven (see below). Another version works if `tau-meter doctor --prove` passes; the driver prints a note when the compiler is not the pinned one. LLVM older than 18 is refused.

**Why LLVM:** a developer can install it in minutes on every platform; clang is a cross compiler by default (one install gives the RISC-V target, where GCC needs a separate cross build); lld is a single binary that understands the linker script; and the project already uses the same clang for its WebAssembly meter twins. The project's own firmware build uses an xPack GCC 15.2 (1.6 GB with its tools): that is not shippable and is not needed for a meter.

## Install

| Platform | Install | Status |
|---|---|---|
| **macOS** | `brew install llvm lld` then `python3 tau-meter doctor`. Apple's own `/usr/bin/clang` has no RISC-V target: the SDK finds Homebrew's (`/opt/homebrew/opt/llvm/bin`). The host runner needs the Command Line Tools SDK (`xcode-select --install`). | **tested** (macOS, Apple Silicon, LLVM 23.1.5) |
| **Linux** (Debian, Ubuntu) | `sudo apt install clang lld python3`. The distribution's LLVM must be 18 or newer; otherwise take a release from github.com/llvm/llvm-project/releases or apt.llvm.org. Set `TAU_METER_CLANG` / `TAU_METER_LLD` if they are not on PATH. | **untested** here: expected to work, run `tau-meter doctor --prove` |
| **Windows, WSL** | Install Ubuntu in WSL and follow the Linux line. Copy the SD card files out with `explorer.exe`. | untested, expected to work |
| **Windows, native** | Install Python 3 and the LLVM installer (`LLVM-23.1.5-win64.exe` from the llvm-project releases: it contains clang and lld). Run `python tau-meter doctor` (or `tau-meter.cmd`). | **untested**: the host runner is built with the same clang; report problems |

## How a meter is built (the pinned flags)
Compile: `--target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 -mno-relax -mcmodel=medany -Os -std=gnu11 -ffreestanding -fno-pic -fno-stack-protector -fno-jump-tables -fno-exceptions -fno-unwind-tables -ffunction-sections -fdata-sections -Wall -Wextra`. (`rv32im`: no compressed instructions, no floating point: the Pocket's CPU is a VexRiscv RV32IM. `-fno-jump-tables` keeps every branch visible to the static check.)
Link: `-nostdlib -nostartfiles -static -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,norelro --ld-path=ld.lld` with the SDK's `ld/meter_pack.ld`, which places code at the 64 KiB window (`0x24970000`), constants and the descriptor at its data alias, and `.data` / `.bss` in the 4 KiB scratch area.
The driver is `tau_meter/toolchain.py` (flags) and `tau_meter/build.py`; `tau-meter build` prints the compiler it used.

## Proof: the seven shipped meters
`sim/test_sdk_toolchain.py` of the Tau Alpha repository builds each of the seven shipped meters twice (the project's GCC for the release, and this recipe), verifies both packages, runs both on the SDK's host runner over every scenario of the suite, every factory preset and each parameter at its minimum and maximum, and requires **identical host-table call traces and identical final frames, pixel for pixel** (the descriptors are byte-identical; the code is not, so the images differ by a few percent in size, always far inside the window). Layered Wave uses 3.0 KiB of stack built with GCC and 3.3 KiB with clang: both under the suite's 3.5 KiB policy limit, the clang one with a warning.

## Not supported
Apple's clang alone; GCC or any other compiler for meters (it may work; nothing here proves it); the compressed (C) or floating-point extensions; C++ (a meter is C: no exceptions, no runtime); 32-bit hosts; building a meter that uses a libc function (the link refuses an undefined name, in plain words).
