#!/bin/bash
# b2.sh <name> <opt> [extra -D ...]   build the flac2 core harness
set -e
REPO="${REPO:-$(cd "$(dirname "$0")/../../.." && pwd)}"
G="$REPO/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc"; mkdir -p out
"$G" -march=rv32im -mabi=ilp32 -mno-relax $2 "${@:3}" -ffreestanding -g -I "$REPO/tools/host" -I "$REPO/fw2/cores" -c -o out/$1.o f2harness.c
"$G" -march=rv32im -mabi=ilp32 -mno-relax -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments -T "$REPO/tools/host/link.ld" "$REPO/tools/host/start.S" out/$1.o -lgcc -o out/$1.elf
