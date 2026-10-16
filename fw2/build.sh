#!/usr/bin/env bash
# Player 2 firmware build: the player. Output: work/diagnostics/player2/tau.rom + tau-cold.bin (the cold image, bound to the ROM by tools/pack_cold.py).
# Usage: bash fw2/build.sh        (no flags: Player 2 is a single configuration, the 192 KB / CLK66 link)
set -euo pipefail
FW2="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"; ROOT="$(cd -- "$FW2/.." && pwd)"
BIN="$ROOT/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
GCC="${CC_RISCV:-$BIN/riscv-none-elf-gcc}"; OBJCOPY="${OBJCOPY_RISCV:-$BIN/riscv-none-elf-objcopy}"; SIZE="${SIZE_RISCV:-$BIN/riscv-none-elf-size}"
OUT="${TAU_BUILD_OUT:-$ROOT/work/diagnostics}/player2"; mkdir -p "$OUT"
rm -f "$OUT/fw2.elf" "$OUT/tau.rom"
# One configuration: the player. (The S2-S4 bring-up firmwares -- tone generator, card list walker -- are archived in archive/player2_bringup/; fw2/transport/tone.c stays only as the aout test
# generator of sim/test_p2_aout.py.) Options: FLAC2=0 builds the inherited FLAC decoder (until S9 retires it), LPC=0 leaves the FLAC LPC unit off, POLY=0 builds the software MP3 window (A/B only).
SRCS=("$FW2/platform/start.S" "$FW2/platform/platform.c" "$FW2/platform/cold.c" "$FW2/platform/libc_min.c" "$FW2/gfx/gfx.c" "$FW2/aout/aout.c"
      "$FW2/diagnostics/info_counters.c" "$FW2/diagnostics/info_page.c" "$FW2/diagnostics/report_record.c" "$FW2/diagnostics/report_page.c"
      "$FW2/ui/nowplaying.c" "$FW2/ui/browser_view.c" "$FW2/ui/meter.c" "$FW2/ui/settings_view.c" "$FW2/ui/views.c" "$FW2/ui/helios.c" "$FW2/art/art.c" "$FW2/transport/browser.c"
      "$FW2/aout/halcyon.c" "$FW2/ui/halcyon_view.c" "$FW2/stream/assets.c" "$FW2/stream/slot.c" "$FW2/stream/library.c" "$FW2/stream/skiplist.c" "$FW2/settings/settings.c" "$FW2/transport/mp3play.c" "$FW2/app/main_player.c")
MAINC=""
DEFS=(-DP2_COLD=1 -DP2_MP3PLAY -DP2_LISTPLAY)
INCS=()
H="$ROOT/third_party/libhelix-mp3"
SRCS+=("$H/mp3dec.c" "$H/mp3tabs.c" "$H"/real/{bitstream,buffers,dct32,dequant,dqchan,huffman,hufftabs,imdct,polyphase,scalfact,stproc,subband,trigtabs}.c)
INCS+=(-I "$H/pub" -I "$H/real" -I "$FW2/cores")
if [ "${POLY:-1}" = "1" ]; then DEFS+=(-DTAU_POLY_FW=1 -DTAU_POLY_REQUIRED=1); INCS+=(-I "$ROOT/fw"); SRCS+=("$FW2/decode/poly_hw.c"); fi
if [ "${FLAC2:-1}" = "1" ]; then
    DEFS+=(-DP2_FLAC=1 -DP2_FLAC2=1); SRCS+=("$FW2/decode/flac2_unit.c")
elif [ "${FLAC:-1}" = "1" ]; then
    DEFS+=(-DP2_FLAC=1); SRCS+=("$FW2/decode/flac_unit.c")
fi
if [ "${FLAC2:-1}" = "1" ] || [ "${FLAC:-1}" = "1" ]; then
    if [ "${LPC:-1}" = "1" ]; then DEFS+=(-DTAU_LPC_FW=1); SRCS+=("$FW2/decode/lpc_hw.c"); INCS+=(-I "$ROOT/fw"); fi
fi
# Every source is compiled to its own object (OUT/obj/<name>.o): fw2/platform/link.ld puts the cold objects (UI, diagnostics, art, library, settings) in the PSRAM cold image BY FILE NAME.
OBJ="$OUT/obj"; rm -rf "$OBJ"; mkdir -p "$OBJ"; : > "$OUT/build.log"; OBJS=()
CFLAGS=(-march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -nostartfiles -nostdlib -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter)
for src in "${SRCS[@]}"; do
    o="$OBJ/$(basename "${src%.*}").o"
    [ ! -e "$o" ] || { echo "*** two sources with the same file name: $src ***" >&2; exit 1; }
    "$GCC" ${DEFS[@]+"${DEFS[@]}"} ${INCS[@]+"${INCS[@]}"} "${CFLAGS[@]}" -c "$src" -o "$o" 2>> "$OUT/build.log" || { grep -v "LOAD segment with RWX" "$OUT/build.log" >&2; echo "*** Player 2 COMPILE FAILED ***" >&2; exit 1; }
    OBJS+=("$o")
done
"$GCC" "${CFLAGS[@]}" -Wl,--gc-sections -T "$FW2/platform/link.ld" -o "$OUT/fw2.elf" "${OBJS[@]}" -lgcc 2>> "$OUT/build.log" || { grep -v "LOAD segment with RWX" "$OUT/build.log" >&2; echo "*** Player 2 COMPILE FAILED ***" >&2; exit 1; }
grep -v "LOAD segment with RWX" "$OUT/build.log" >&2 || true
"$SIZE" "$OUT/fw2.elf"
# the entry point and the hot loop must be in hot RAM: a link.ld pattern that is too greedy (it once took start.o) would otherwise only show as a black screen
NM="${BIN}/riscv-none-elf-nm"
for sym in _start main; do
    ad="$("$NM" "$OUT/fw2.elf" | awk -v s="$sym" '$3 == s { print $1 }')"
    case "$ad" in 000*) ;; *) echo "*** $sym is at 0x$ad, not in hot RAM: check the cold object patterns in fw2/platform/link.ld ***" >&2; exit 1 ;; esac
done
[ "$("$NM" "$OUT/fw2.elf" | awk '$3 == "_start" { print $1 }')" = "00000000" ] || { echo "*** _start is not at address 0 ***" >&2; exit 1; }
"$OBJCOPY" -O binary -R .cold_data -R .cold_text "$OUT/fw2.elf" "$OUT/tau.rom"; chmod 0644 "$OUT/tau.rom"      # the cold sections are not part of the ROM image
if true; then python3 "$ROOT/tools/pack_cold.py" pack "$OUT/fw2.elf" "$OUT" --rom "$OUT/tau.rom"; else cp "$ROOT/dist/Assets/tau/alfatreze.TAU/tau-cold.bin" "$OUT/tau-cold.bin" 2>/dev/null || cp "$ROOT/dist/Assets/tau/common/tau-cold.bin" "$OUT/tau-cold.bin" 2>/dev/null || : > "$OUT/tau-cold.bin"; fi
python3 - <<PY
import os
n = os.path.getsize("$OUT/tau.rom"); print("tau.rom: %d bytes" % n)
PY
echo "built [player2] -> $OUT/tau.rom"
