#!/bin/bash
# Build core firmware -> dist/Assets/tau/common/tau.rom
#
#   ./build.sh            # Stage 3 player (Helix decode + playback)  [default]
#   ./build.sh player-stress # Developer-only SDRAM contention player
#   ./build.sh bringup    # Stage 1/2 bring-up (tone + 0180 test, no decoder)
#   ./build.sh player-sdram-pl # A-103 player with playlist buffers in SDRAM (needs window RBF)
#   ./build.sh sdram-diag # Phase 1 Pocket SDRAM mailbox diagnostic
#   ./build.sh sdram-cpu-diag # Phase 2 uncached CPU-window SDRAM diagnostic
#   ./build.sh sdram-cpu-readback # A-060 mailbox-to-CPU readback discriminator
#   ./build.sh sdram-cpu-log-probe # A-081 target-write/flush result discriminator
#   ./build.sh sdram-cpu-log-readback # A-083 target slot readback discriminator
#   ./build.sh psram-diag # B-004 PSRAM mailbox diagnostic (needs a TAU_PSRAM_PROBE RBF)
#   ./build.sh player-profile # Phase F step 1: per-stage MP3/FLAC decode cost, on the shipped RBF
#   ./build.sh player-library-diagnostic-profile # B-088/B-089: the same, reported via the Check QR record
#
# The .rom is loaded from SD into BRAM by data_loader at boot, exactly like an
# arcade core's ROM -- which is the point: firmware changes cost seconds here
# instead of a full Quartus compile.
set -e

TARGET="${1:-player}"
STRESS_CFLAGS=""
HEAP_MIN=0
FLAC_O_CFLAGS=""    # reaches the SEPARATE flac.o compile line below; player-profile sets it

# Resolve the checkout instead of assuming the original author's Windows path.
# Override RISCV_TOOLCHAIN_BIN and/or RISCV_PREFIX when the tools are not on
# PATH.  Examples:
#   RISCV_TOOLCHAIN_BIN=/opt/xpack-riscv/bin bash fw/build.sh
#   RISCV_PREFIX=riscv64-unknown-elf- bash fw/build.sh
FW="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$FW/.." && pwd)"
TOOL_BIN="${RISCV_TOOLCHAIN_BIN:-}"
TOOL_PREFIX="${RISCV_PREFIX:-riscv-none-elf-}"
VENDORED_TOOL_BIN="$ROOT/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
if [[ -z "$TOOL_BIN" && -x "$VENDORED_TOOL_BIN/riscv-none-elf-gcc" ]]; then
    TOOL_BIN="$VENDORED_TOOL_BIN"
fi
GCC="${CC_RISCV:-${TOOL_BIN:+$TOOL_BIN/}${TOOL_PREFIX}gcc}"
OBJCOPY="${OBJCOPY_RISCV:-${TOOL_BIN:+$TOOL_BIN/}${TOOL_PREFIX}objcopy}"
SIZE="${SIZE_RISCV:-${TOOL_BIN:+$TOOL_BIN/}${TOOL_PREFIX}size}"
PYTHON="${PYTHON:-python3}"
HELIX="$ROOT/third_party/libhelix-mp3"
OUT="$ROOT/dist/Assets/tau/common"

for tool in "$GCC" "$OBJCOPY" "$SIZE" "$PYTHON"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "missing build tool: $tool" >&2
        echo "set RISCV_TOOLCHAIN_BIN or RISCV_PREFIX, then retry" >&2
        exit 127
    fi
done

mkdir -p "$OUT"

# -mno-relax: no real __global_pointer$ in this bare-metal build, so gp-relative
# relaxation would emit stores through an uninitialised gp.
# EXTRA_CFLAGS lets a build turn on things that are off by default without
# editing source, e.g.:  EXTRA_CFLAGS=-DDEBUG_DIAG=1 bash fw/build.sh
CFLAGS="-march=rv32im -mabi=ilp32 -mno-relax -O2 -ffreestanding -nostartfiles -ffunction-sections -fdata-sections -Wl,--gc-sections ${EXTRA_CFLAGS:-}"
ROM="tau.rom"

case "$TARGET" in
bringup)
    SRCS=("$FW/start.S" "$FW/main.c")
    INC=()
    ;;
player)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    ;;
player-profile)
    # Phase F step 1 (docs/PHASE_F_SPEC.md section 14): per-stage decode cost.
    # Same sources as `player`; only the profiling macros differ. MP3_PROFILE
    # goes through the normal CFLAGS (mp3dec.c is compiled with the rest of
    # SRCS below); FLAC_PROFILE also has to reach the SEPARATE flac.o compile
    # line near the end of this script, which is why FLAC_O_CFLAGS exists.
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/decoder-profile"
    STRESS_CFLAGS="-DMP3_PROFILE=1 -DFLAC_PROFILE=1 -DUI_SHOW_DECODE_PROFILE=1"
    FLAC_O_CFLAGS="-DFLAC_PROFILE=1"
    ;;
player-blit-probe)
    # PHASE_F_SPEC.md section 12: the BLIT_READY() fail-safe probe (fw/blit_probe.inc).
    # Same sources as `player`; only TAU_BLIT_PROBE differs. Nothing calls BLIT_READY()
    # operationally yet (no feature uses the blit engine), so this exists purely to prove
    # the probe itself works before any real caller depends on it.
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/blit-probe"
    STRESS_CFLAGS="-DTAU_BLIT_PROBE=1"
    ;;
player-stress)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/sdram-stress"
    STRESS_CFLAGS="-DTAU_SDRAM_STRESS=1 -DTAU_STRESS_HUD=1 -Wl,--defsym=_min_heap=128"
    ;;
player-stress-window)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/sdram-stress-window"
    STRESS_CFLAGS="-DTAU_SDRAM_STRESS=1 -DTAU_STRESS_HUD=1 -DTAU_SDRAM_STRESS_WINDOW=1 -Wl,--defsym=_min_heap=128"
    ;;
player-sdram-pl)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/playlist-sdram"
    STRESS_CFLAGS="-DTAU_PL_SDRAM=1"
    ;;
player-settings)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/settings-ui"
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_METER_THUMBS=1"
    HEAP_MIN=6144        # with the previews (about 6 KiB) the floor is 6 KiB; the hard link minimum is 1 KiB        # release-style build: keep at least 8 KiB of heap gap
    ;;
player-library)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/library"
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_METER_THUMBS=1 -DTAU_LIBRARY=1 -DTAU_COLD=1"
    COLD_PACK=1
    HEAP_MIN=6144        # with the previews (about 6 KiB) the floor is 6 KiB; the hard link minimum is 1 KiB        # release-style build: keep at least 8 KiB of heap gap
    ;;
player-library-check)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/library-check"
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_METER_THUMBS=1 -DTAU_LIBRARY=1 -DTAU_COLD=1 -DTAU_COLD_CODE=1 -DTAU_CHECK=1 -DTAU_G4=${G4:-3}"
    COLD_PACK=1
    HEAP_MIN=6144        # with the previews (about 6 KiB) the floor is 6 KiB; the hard link minimum is 1 KiB        # release-style build: keep at least 8 KiB of heap gap
    ;;
release)
    # The shipped product: settings menu, Info page, the playlist in SDRAM, the media library and Phase G (cold
    # code in PSRAM), built into dist/. v0.4.0 (B-078): TAU_LIBRARY/TAU_COLD/TAU_COLD_CODE/TAU_G4 added -- the
    # Check (TAU_CHECK) stays Diagnostic-Build-only (B-073 decision), and TAU_DIAG_TESTS/TAU_SDRAM_STRESS stay off.
    # Needs the G3 window+ifetch RBF in the same package (the seed-1 build, B-050) and its cold image (COLD_PACK=1).
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_METER_THUMBS=1 -DTAU_LIBRARY=1 -DTAU_COLD=1 -DTAU_COLD_CODE=1 -DTAU_G4=${G4:-3}"
    COLD_PACK=1
    HEAP_MIN=6144        # with the previews (about 6 KiB) the floor is 6 KiB; the hard link minimum is 1 KiB
    ;;
player-diagnostic)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/diagnostic-build"
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_DIAG_TESTS=1 -DTAU_SDRAM_STRESS=1 -DTAU_SDRAM_STRESS_WINDOW=1 -DTAU_STRESS_HUD=1"
    HEAP_MIN=4096        # developer build: the tests may use the space, never below 4 KiB
    ;;
player-cold-diagnostic)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/cold-diagnostic"
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_DIAG_TESTS=1 -DTAU_SDRAM_STRESS=1 -DTAU_SDRAM_STRESS_WINDOW=1 -DTAU_STRESS_HUD=1 -DTAU_COLD=1 -DTAU_COLD_CODE=1"
    COLD_PACK=1
    HEAP_MIN=4096        # developer build: the tests may use the space, never below 4 KiB
    ;;
player-library-diagnostic)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/library-diagnostic"
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_DIAG_TESTS=1 -DTAU_SDRAM_STRESS=1 -DTAU_SDRAM_STRESS_WINDOW=1 -DTAU_STRESS_HUD=1 -DTAU_COLD=1 -DTAU_COLD_CODE=1 -DTAU_LIBRARY=1 -DTAU_METER_THUMBS=1 -DTAU_CHECK=1 -DTAU_G4=${G4:-3}"
    COLD_PACK=1
    HEAP_MIN=4096        # developer build: the tests may use the space, never below 4 KiB
    ;;
player-library-diagnostic-profile)
    # B-088/B-089 (docs/TEST_SUITE_SPEC.md section 11): the Diagnostic Build
    # above, plus MP3_PROFILE/FLAC_PROFILE so Check's CT_AUD window (the
    # existing 15 s playback-counter test) also reports decode-stage cost in
    # the QR record (SR_T_DECPROF) -- "boot, run Check, read the QR" instead
    # of screenshotting the bench-only screen row (B-086/B-087). MP3_PROFILE/
    # FLAC_PROFILE stay off everywhere else; this is the one opt-in variant.
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/library-diagnostic-profile"
    # SDRAM_BUSY=1 (default 0): pair with a bitstream built with TAU_SDRAM_BUSY=1 (Phase F B7).
    # Kept opt-in, not a default, because R_SDR_BUSY reads a hardwired 0 on any OTHER bitstream
    # (mp3_soc.v gates it on SDRAM_BUSY_ENABLE) -- turning this on against the wrong bitstream
    # would report a real-looking but false "0% busy" instead of CT_BLT's own N/A sentinel.
    STRESS_CFLAGS="-DTAU_SETTINGS_UI=1 -DTAU_PL_SDRAM=1 -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAG_INFO=1 -DTAU_DIAG_TESTS=1 -DTAU_SDRAM_STRESS=1 -DTAU_SDRAM_STRESS_WINDOW=1 -DTAU_STRESS_HUD=1 -DTAU_COLD=1 -DTAU_COLD_CODE=1 -DTAU_LIBRARY=1 -DTAU_METER_THUMBS=1 -DTAU_CHECK=1 -DTAU_G4=${G4:-3} -DMP3_PROFILE=1 -DFLAC_PROFILE=1 -DTAU_SDRAM_BUSY=${SDRAM_BUSY:-0}"
    FLAC_O_CFLAGS="-DFLAC_PROFILE=1"
    COLD_PACK=1
    HEAP_MIN=4096
    ;;
player-sdram-pl-fault)
    SRCS=(
      "$HELIX/mp3dec.c" "$HELIX/mp3tabs.c"
      "$HELIX/real/bitstream.c" "$HELIX/real/buffers.c" "$HELIX/real/dct32.c"
      "$HELIX/real/dequant.c" "$HELIX/real/dqchan.c" "$HELIX/real/huffman.c"
      "$HELIX/real/hufftabs.c" "$HELIX/real/imdct.c" "$HELIX/real/polyphase.c"
      "$HELIX/real/scalfact.c" "$HELIX/real/stproc.c" "$HELIX/real/subband.c"
      "$HELIX/real/trigtabs.c"
      "$FW/start.S" "$FW/player.c" "$FW/sysio.c" "$FW/alloc.c"
      "$FW/picojpeg.o" "$FW/flac.o"
    )
    INC=(-I "$HELIX/pub" -I "$HELIX/real" -I "$ROOT/third_party/picojpeg")
    OUT="$ROOT/work/diagnostics/playlist-sdram-fault"
    STRESS_CFLAGS="-DTAU_PL_SDRAM=1 -DTAU_PL_SDRAM_FAULT=1"
    ;;
sdram-diag)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram"
    ;;
sdram-cpu-diag)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1"
    ;;
sdram-cpu-readback)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-readback"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1"
    ;;
sdram-cpu-log-probe)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-probe"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1"
    ;;
sdram-cpu-log-readback)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-readback"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1 -DTAU_LOG_READBACK_PROBE=1"
    ;;
sdram-cpu-log-open)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-open"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1 -DTAU_LOG_READBACK_PROBE=1"
    ;;
sdram-cpu-log-settle)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-settle"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1 -DTAU_LOG_READBACK_PROBE=1"
    ;;
sdram-cpu-log-write-read)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-write-read"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1 -DTAU_LOG_READBACK_PROBE=1"
    ;;
sdram-cpu-log-source)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-source"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1 -DTAU_LOG_READBACK_PROBE=1 -DTAU_LOG_SOURCE_PROBE=1"
    ;;
sdram-cpu-log-bridge)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-bridge"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1 -DTAU_LOG_READBACK_PROBE=1 -DTAU_LOG_SOURCE_PROBE=1"
    ;;
sdram-cpu-log-table)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-table"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_COMMAND_PROBE=1 -DTAU_LOG_READBACK_PROBE=1 -DTAU_LOG_TABLE_PROBE=1"
    ;;
sdram-cpu-log-interact)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-log-interact"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_INTERACT_PROBE=1"
    ;;
sdram-cpu-disc)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-disc"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_INTERACT_PROBE=1 -DTAU_DISCRIMINATOR_PROBE=1"
    ;;
sdram-cpu-latency)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-latency"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_INTERACT_PROBE=1 -DTAU_LATENCY_PROBE=1"
    ;;
sdram-cpu-soak)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-soak"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_INTERACT_PROBE=1 -DTAU_SOAK_PROBE=1"
    ;;
sdram-cpu-full)
    SRCS=("$FW/start.S" "$FW/sdram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/sdram-cpu-full"
    STRESS_CFLAGS="-DTAU_CPU_WINDOW_DIAG=1 -DTAU_CPU_WINDOW_READBACK_DIAG=1 -DTAU_LOG_INTERACT_PROBE=1 -DTAU_FULL_PROBE=1"
    ;;
psram-diag-sim)
    SRCS=("$FW/start.S" "$FW/psram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/psram-diag-sim"
    STRESS_CFLAGS="-DPSRAM_FILL_LOG2=6 -DPSRAM_WARMUP=200 -DPSRAM_PUBLISH_WAIT=200"
    ;;
psram-diag)
    SRCS=("$FW/start.S" "$FW/psram_diag.c")
    INC=(-I "$FW")
    OUT="$ROOT/work/diagnostics/psram-diag"
    ;;
*)
    echo "usage: $0 {player|player-profile|player-library-diagnostic-profile|player-blit-probe|player-stress|player-stress-window|bringup|sdram-diag|sdram-cpu-diag|sdram-cpu-readback|sdram-cpu-log-probe|sdram-cpu-log-readback|sdram-cpu-log-open|sdram-cpu-log-settle|sdram-cpu-log-write-read|sdram-cpu-log-source|sdram-cpu-log-bridge|sdram-cpu-log-table|sdram-cpu-log-interact|sdram-cpu-disc|sdram-cpu-latency|sdram-cpu-soak|sdram-cpu-full|psram-diag|psram-diag-sim}"; exit 1 ;;
esac

# Build flags are selected by target rather than remembered in a shell history.
# This makes the installed stress ROM reproducible and avoids accidentally
# placing developer contention behavior in the normal TAU artifact.
CFLAGS="$CFLAGS $STRESS_CFLAGS"

# RAM_192K=1 (default 0, every target): links against 192 KB instead of 256 KB (fw/link.ld's
# _ram_limit) -- the RAM-shrink RTL's own real benefit, timing-closed B-235, not yet card-tested.
# Applied here globally rather than per-target-case, so it always reaches the link step
# regardless of which target is chosen (an earlier version added this only to
# player-library-diagnostic-profile's own STRESS_CFLAGS and it silently did nothing for
# `release` -- caught only by comparing `nm` symbols after the build, not by any error).
# Always safe to combine with any target: a 192 KB-linked image still runs fine on the current
# 256 KB bitstream (see fw/link.ld's own comment on _ram_limit), so this is purely opt-in
# testing, not a bitstream-mismatch hazard.
if [[ "${RAM_192K:-0}" == "1" ]]; then
    CFLAGS="$CFLAGS -Wl,--defsym=RAM_192K=1"
fi

# A specialised target may redirect OUT away from the release Assets folder.
# Create it after target selection so objcopy never fails on a missing staging
# directory (the first sdram-diag build exposed the old ordering).
mkdir -p "$OUT"

# Compile status is checked EXPLICITLY. This used to be
#   "$GCC" ... 2>&1 | grep -v "LOAD segment with RWX" || true
# which reports the pipeline's status (grep's), and `|| true` then swallowed
# even that -- so a compile error printed, `set -e` did not fire, and objcopy
# happily re-packaged the PREVIOUS fw.elf. The script said "built" and shipped
# a stale .rom. That is a hardware-test cycle wasted on code that was never
# compiled, which is the exact opposite of what this script exists for.
# Deleting the elf first means a failure can never fall back to an old one.
rm -f "$FW/fw.elf"
# The FLAC decoder is compiled -Os and the rest -O2 on purpose. Its text is
# 10602 bytes at -O2 against 6182 at -Os, and those 4420 bytes are the
# difference between the image fitting below the DMA buffers and not. The hot
# MP3 path keeps -O2: it needs 45.7 MHz of 60 and cannot afford the loss.
# If FLAC turns out CPU-bound, this is the first thing to revisit.
rm -f "$FW/flac.o"
if ! "$GCC" -march=rv32im -mabi=ilp32 -mno-relax -Os -ffreestanding $FLAC_O_CFLAGS -c         -o "$FW/flac.o" "$FW/flac.c" > "$FW/build.log" 2>&1; then
    cat "$FW/build.log" >&2
    echo "*** flac.c FAILED TO COMPILE ***" >&2
    exit 1
fi

# picojpeg gets -Os for the same reason, and with less to lose than flac.c: it
# decodes album art ONCE per track load, inside the silent gap where the FIFO
# has already been flushed. Nothing it does is on the audio path, so trading
# its speed for size costs a few ms of a load that is already hundreds.
rm -f "$FW/picojpeg.o"
if ! "$GCC" -march=rv32im -mabi=ilp32 -mno-relax -Os -ffreestanding         -I "$ROOT/third_party/picojpeg" -c         -o "$FW/picojpeg.o" "$ROOT/third_party/picojpeg/picojpeg.c"         > "$FW/build.log" 2>&1; then
    cat "$FW/build.log" >&2
    echo "*** picojpeg.c FAILED TO COMPILE ***" >&2
    exit 1
fi

# B-203: moved to cold code (PSRAM) when opted in, same TAU_G4>=2 tier art_decode() (fw/art.inc)
# itself already uses -- it is picojpeg's ONLY caller, already COLD_READY()-gated at every one of
# art_decode()'s own call sites in player.c, so picojpeg needs no gate of its own: it can only ever
# be reached through that already-gated path. Lower risk than the meters (B-199..B-202): this
# comment's own reasoning above already establishes picojpeg is never on the audio path, only a
# one-time per-track-load decode with seconds of slack, not a ~26 ms per-frame budget. Vendored
# source (third_party/picojpeg/picojpeg.c) is left untouched -- objcopy renames the whole object's
# .text/.rodata to .cold_text/.cold_data after an ordinary compile, which fw/link.ld already
# collects (the same output sections every other cold function/data already uses). ~11 KB of .text
# moves (measured, not the ~8 KB estimate PHASE_F_SPEC.md cited before this was built).
#
# PICOJPEG_COLD defaults to 1 (on): promoted 2026-09-25 (B-213) after TAU_DEV_47's ENDURANCE soak
# hardware-confirmed it alongside G4=3 (0 failures across all 10 checks, including a 30s Blit storm
# and Cold frame run together). Set PICOJPEG_COLD=0 to build the pre-promotion (BRAM) variant.
if [[ "${PICOJPEG_COLD:-1}" == "1" ]] && [[ "$STRESS_CFLAGS" == *"TAU_COLD_CODE=1"* ]]; then
    if ! "$OBJCOPY" --rename-section .text=.cold_text --rename-section .rodata=.cold_data \
            "$FW/picojpeg.o" > "$FW/build.log" 2>&1; then
        cat "$FW/build.log" >&2
        echo "*** picojpeg.o objcopy (cold-code section rename) FAILED ***" >&2
        exit 1
    fi
fi

if ! "$GCC" $CFLAGS "${INC[@]}" -T "$FW/link.ld" -o "$FW/fw.elf" "${SRCS[@]}" -lm \
        > "$FW/build.log" 2>&1; then
    grep -v "LOAD segment with RWX" "$FW/build.log" >&2 || true
    echo "*** COMPILE FAILED -- no .rom written ***" >&2
    exit 1
fi
grep -v "LOAD segment with RWX" "$FW/build.log" >&2 || true

"$SIZE" "$FW/fw.elf"
"$OBJCOPY" -O binary -R .cold_data -R .cold_text "$FW/fw.elf" "$OUT/$ROM"      # cold data is not part of the ROM image
# GNU objcopy inherits the ELF executable bit on Unix. A ROM is data, and the
# shipped artifact is tracked as 0644, so normalize it for reproducible status.
chmod 0644 "$OUT/$ROM"
# Phase G1: split the cold data into tau-cold.bin and bind it to this ROM (the ROM carries the layout id).
if [ "${COLD_PACK:-0}" = 1 ]; then "$PYTHON" "$ROOT/tools/pack_cold.py" pack "$FW/fw.elf" "$OUT" --rom "$OUT/$ROM"; fi

"$PYTHON" -c "
import os
n = os.path.getsize(r'$OUT/$ROM')
lim = 192*1024 - 16*1024        # RAM minus stack reserve
print('$ROM: %d bytes (%.1f%% of usable RAM)' % (n, 100.0*n/lim))
assert n < lim, 'firmware image exceeds usable RAM'
"
# The splash version and the version the Pocket shows in its core list live in
# two different files, and they drifted: v1.3.0 was built, tested and pushed to
# a card for days still announcing 1.2.0 on both. Neither is checked by anything
# else, and neither is visible from the other, so this compares them on every
# build and FAILS rather than shipping a lie.
#
# date_release is not checked -- only a human knows the release date -- but it
# is printed here so it cannot be forgotten silently.
# cut on the quotes rather than a sed backreference: escaping is what broke the
# first version of this check, and one that silently compares two empty strings
# is worse than no check at all.
CORE_JSON="$ROOT/dist/Cores/alfatreze.TAU/core.json"
APP_VER=$(grep -m1 '#define APP_VER'  "$FW/player.c" | cut -d'"' -f2)
JSON_VER=$(grep -m1 '"version"'       "$CORE_JSON"   | cut -d'"' -f4)
JSON_DATE=$(grep -m1 '"date_release"' "$CORE_JSON"   | cut -d'"' -f4)
if [ -z "$APP_VER" ] || [ -z "$JSON_VER" ]; then
  echo "*** VERSION CHECK BROKE: could not read a version from either file ***" >&2
  exit 1
fi
if [ "$APP_VER" != "$JSON_VER" ]; then
  echo "*** VERSION MISMATCH: player.c says $APP_VER, core.json says $JSON_VER ***" >&2
  echo "    both must match before release; core.json also carries date_release" >&2
  exit 1
fi
# The README states it too, in its opening paragraph, so a visitor learns the
# version without clicking through. That makes THREE copies, and an unchecked
# third copy is just a slower way to be wrong -- the README is the first thing
# read, so a stale line there is the most visible of the three. Checked here
# with the other two rather than trusted to a checklist.
README="$ROOT/README.md"
DOC_VER=$(grep -m1 -o 'Current version \*\*v[0-9.]*\*\*' "$README" | grep -o '[0-9][0-9.]*')

if [ -z "$DOC_VER" ]; then
  echo "*** VERSION CHECK BROKE: no 'Current version **vX.Y.Z**' in README.md ***" >&2
  exit 1
fi
if [ "$APP_VER" != "$DOC_VER" ]; then
  echo "*** VERSION MISMATCH: player.c says $APP_VER, README says $DOC_VER ***" >&2
  exit 1
fi

echo "version $APP_VER (core.json date_release $JSON_DATE)"

if [ "$HEAP_MIN" -gt 0 ]; then
    GAP=$("$TOOL_BIN/${TOOL_PREFIX}nm" "$FW/fw.elf" | "$PYTHON" -c "
import sys
s = {l.split()[2]: int(l.split()[0], 16) for l in sys.stdin if len(l.split()) == 3}
print(s['_heap_end'] - s['_heap_start'])")
    echo "heap gap: $GAP B (minimum for $TARGET: $HEAP_MIN B)"
    if [ "$GAP" -lt "$HEAP_MIN" ]; then echo "*** heap gap below the $TARGET minimum ***" >&2; exit 1; fi
fi
echo "built [$TARGET] -> $OUT/$ROM"
