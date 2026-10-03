#!/bin/bash
# build.sh <flac_variant.c> <TAU_LPC_FW> <outname> [extra cflags for flac.c]
set -e
T="/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/tau-alpha/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
R="/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/tau-alpha-meter-builder"
GCC="$T/riscv-none-elf-gcc"
mkdir -p out
# flac.c: exactly fw/build.sh's flags (-Os, separate TU) plus -g for line attribution (does not change code)
"$GCC" -march=rv32im -mabi=ilp32 -mno-relax -Os -ffreestanding -g -DTAU_LPC_FW=$2 $4 -I "$R/fw" -c -o out/$3_flac.o "$1"
# glue + sink: player.c is -O2 (HOT_O2)
"$GCC" -march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -g -c -o out/glue.o glue.c
"$GCC" -march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -g -I "$R/tools/host" -I "$R/fw" -c -o out/harness.o harness.c
"$GCC" -march=rv32im -mabi=ilp32 -mno-relax -nostdlib -nostartfiles -Wl,--no-warn-rwx-segments -T "$R/tools/host/link.ld" \
   "$R/tools/host/start.S" out/harness.o out/glue.o out/$3_flac.o -lgcc -o out/$3.elf
