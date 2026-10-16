#!/usr/bin/env bash
# Player 2 firmware build (stage S2). Output: work/diagnostics/player2/tau.rom (+ a filler tau-cold.bin: Player 2 has no cold image yet, but the packager declares the slot).
# Usage: bash fw2/build.sh        (no flags: Player 2 is a single configuration, the 192 KB / CLK66 link)
set -euo pipefail
FW2="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"; ROOT="$(cd -- "$FW2/.." && pwd)"
BIN="$ROOT/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
GCC="${CC_RISCV:-$BIN/riscv-none-elf-gcc}"; OBJCOPY="${OBJCOPY_RISCV:-$BIN/riscv-none-elf-objcopy}"; SIZE="${SIZE_RISCV:-$BIN/riscv-none-elf-size}"
OUT="${TAU_BUILD_OUT:-$ROOT/work/diagnostics}/player2"; mkdir -p "$OUT"
rm -f "$OUT/fw2.elf" "$OUT/tau.rom"
SRCS=("$FW2/platform/start.S" "$FW2/platform/platform.c" "$FW2/gfx/gfx.c" "$FW2/aout/aout.c" "$FW2/transport/tone.c" "$FW2/diagnostics/info_counters.c" "$FW2/diagnostics/info_page.c" "$FW2/diagnostics/report_record.c" "$FW2/diagnostics/report_page.c" "$FW2/app/main.c")
DEFS=()
if [ "${S4:-0}" = "1" ]; then DEFS=(-DP2_LISTPLAY); SRCS+=("$FW2/stream/slot.c" "$FW2/stream/library.c" "$FW2/transport/listplay.c"); fi
"$GCC" ${DEFS[@]+"${DEFS[@]}"} -march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -nostartfiles -nostdlib -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter \
    -Wl,--gc-sections -T "$FW2/platform/link.ld" -o "$OUT/fw2.elf" "${SRCS[@]}" -lgcc 2> "$OUT/build.log" || { grep -v "LOAD segment with RWX" "$OUT/build.log" >&2; echo "*** Player 2 COMPILE FAILED ***" >&2; exit 1; }
grep -v "LOAD segment with RWX" "$OUT/build.log" >&2 || true
"$SIZE" "$OUT/fw2.elf"
"$OBJCOPY" -O binary "$OUT/fw2.elf" "$OUT/tau.rom"; chmod 0644 "$OUT/tau.rom"
cp "$ROOT/dist/Assets/tau/alfatreze.TAU/tau-cold.bin" "$OUT/tau-cold.bin" 2>/dev/null || cp "$ROOT/dist/Assets/tau/common/tau-cold.bin" "$OUT/tau-cold.bin" 2>/dev/null || : > "$OUT/tau-cold.bin"
python3 - <<PY
import os
n = os.path.getsize("$OUT/tau.rom"); print("tau.rom: %d bytes" % n)
PY
echo "built [player2] -> $OUT/tau.rom"
