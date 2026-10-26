#!/bin/bash
# bdr.sh <name> <optflag> [extra -D]   (DRFLAC_DIR = folder holding dr_flac.h, fetched from github.com/mackron/dr_libs; never committed)
REPO="${REPO:-$(cd "$(dirname "$0")/../../.." && pwd)}"
P="$REPO"; G="$P/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc"
"$G" -march=rv32im -mabi=ilp32 -mno-relax $2 $3 -ffreestanding -ffunction-sections -fdata-sections -g -I "${DRFLAC_DIR:?set DRFLAC_DIR}" -I "$P/tools/host" -c -o out/$1.o drharness.c 2>&1 | grep -E "error" | head -5
"$G" -march=rv32im -mabi=ilp32 -mno-relax -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments -Wl,--gc-sections -T "$P/tools/host/link.ld" "$P/tools/host/start.S" out/$1.o -lgcc -o out/$1.elf 2>&1 | head -5
