#!/bin/bash
# Build core firmware -> dist/Assets/tau/common/tau.rom
#
#   ./build.sh            # the full product build (same as `release`)  [default]
#   ./build.sh psram-diag # B-004 PSRAM mailbox diagnostic (needs a TAU_PSRAM_PROBE RBF)
#   ./build.sh player-library-diagnostic-profile # B-088/B-089: the same, reported via the Check QR record
#
# The .rom is loaded from SD into BRAM by data_loader at boot, exactly like an
# arcade core's ROM -- which is the point: firmware changes cost seconds here
# instead of a full Quartus compile.
set -e

TARGET="${1:-release}"
STRESS_CFLAGS=""
HEAP_MIN=0
FLAC_O_CFLAGS=""    # reaches the SEPARATE flac.o compile line below; the profile builds set it

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
NM="${NM_RISCV:-${TOOL_BIN:+$TOOL_BIN/}${TOOL_PREFIX}nm}"
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
    STRESS_CFLAGS="-DTAU_ART_TIMG=${ART_TIMG:-1} -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_G4=${G4:-3}"
    COLD_PACK=1
    HEAP_MIN=6144        # policy margin (B-565: the gap is unused RAM, malloc is the static arena); the hard link minimum is 1 KiB
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
    STRESS_CFLAGS="-DTAU_ART_TIMG=${ART_TIMG:-1} -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAGNOSTIC=1 -DTAU_G4=${G4:-3}"
    COLD_PACK=1
    HEAP_MIN=2048        # B-565: the gap is unused RAM (malloc is the static arena in alloc.c, nothing calls _sbrk), so this is a policy margin, not a need; 2 KiB, twice the link minimum
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
    STRESS_CFLAGS="-DTAU_ART_TIMG=${ART_TIMG:-1} -DTAU_ART_PSRAM=${ART_PSRAM:-1} -DTAU_DIAGNOSTIC=1 -DTAU_G4=${G4:-3} -DMP3_PROFILE=1 -DFLAC_PROFILE=1 -DTAU_SDRAM_BUSY=${SDRAM_BUSY:-0}"
    FLAC_O_CFLAGS="-DFLAC_PROFILE=1"
    COLD_PACK=1
    HEAP_MIN=2048        # B-565, as player-library-diagnostic
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
    echo "usage: $0 {release|player-library-diagnostic|player-library-diagnostic-profile|psram-diag-sim|psram-diag}"; exit 1 ;;
esac

# Build flags are selected by target rather than remembered in a shell history.
# This makes the installed stress ROM reproducible and avoids accidentally
# placing developer contention behavior in the normal TAU artifact.
CFLAGS="$CFLAGS $STRESS_CFLAGS"
# B-307: POLY_FW=1 redirects the MP3 stereo window to the hardware unit (needs a TAU_POLY bitstream; falls back to software per slot). Default 0 = byte-identical.
# It must reach EVERY translation unit (third_party Helix reads it too), hence a global flag rather than a player.c #define.
# Default 1 for the diagnostic builds (hardware-confirmed alpha.30: 404,712 slots, 0 BAD, 0 TMO, clean at 1.75x); the release target is 1 from v0.5.0 (the bitstream ships in release). On a bitstream without TAU_POLY the boot probe fails and every slot falls back to software.
case "$STRESS_CFLAGS" in *-DTAU_DIAGNOSTIC=1*) POLY_FW="${POLY_FW:-1}" ;; esac
[ "$TARGET" = "release" ] && POLY_FW="${POLY_FW:-1}"      # v0.5.0: the release ships the poly bitstream, so the release firmware uses the unit too
CFLAGS="$CFLAGS -DTAU_POLY_FW=${POLY_FW:-0}"
# B-558: TEMPO=1 builds in the Cymo C7 tempo funnel (fw/tempo_core.h; MP3 only; the Settings > Playback > TEMPO row). Default 0 = byte-identical to a build without it.
case "$STRESS_CFLAGS" in *-DTAU_DIAGNOSTIC=1*) TEMPO="${TEMPO:-1}" ;; esac   # alpha.4: the tempo row is in the Diagnostic Build (the release default stays 0)
CFLAGS="$CFLAGS -DTAU_TEMPO=${TEMPO:-0}"
if [ "${POLY_FW:-0}" = "1" ]; then INC+=(-I "$FW"); fi   # subband.c includes fw/mp3_poly_hw.h (only then, so default builds see no new include path)

# B-368/B-369/B-370: LPC_FW=1 redirects FLAC LPC reconstruction (fw/flac.c) to the hardware unit (needs
# a TAU_LPC bitstream; permanently falls back to software for the rest of the session on any real
# hardware anomaly, same per-unit-failure convention as POLY_FW). B-386: hardware-confirmed on real
# tracks (worst-case call 11ms software vs 5-6ms hardware, no correctness failures across B-355/B-360/
# B-363's repeated real-hardware runs) -- default 1 for the diagnostic builds and for the release
# target from v0.6.0-alpha.1 (the bitstream ships TAU_LPC), same POLY_FW precedent from v0.5.0/B-309.
# On a bitstream without TAU_LPC the boot probe fails and every call falls back to software. Must reach
# flac.c's own SEPARATE compile line too (FLAC_O_CFLAGS below), since flac.c is compiled outside
# $CFLAGS/$SRCS -- flac.c and player.c both already find fw/flac_lpc_hw.h/.inc via their own directory
# (both live in $FW), so no INC change is needed the way POLY_FW's subband.c one is.
case "$STRESS_CFLAGS" in *-DTAU_DIAGNOSTIC=1*) LPC_FW="${LPC_FW:-1}" ;; esac
[ "$TARGET" = "release" ] && LPC_FW="${LPC_FW:-1}"
CFLAGS="$CFLAGS -DTAU_LPC_FW=${LPC_FW:-0} -DTAU_TPG=${TPG:-1} -DTAU_INFO_EXPORT=${INFO_EXPORT:-1} -DTAU_HALCYON_FW=${HALCYON_FW:-1}"   # the Halcyon EQ is the only EQ (every build; NO UNIT on a bitstream without the engine)   # INFO_EXPORT: the Info page report-code export (A), in the normal core too since alpha.4;   # pixel grid report codes (docs/features/BARCODE_STUDY.md), Diagnostic Build only, ON by default since 2026-10-06 (D-R04 reversed); TPG=0 gives the old QR-only pages
FLAC_O_CFLAGS="$FLAC_O_CFLAGS -DTAU_LPC_FW=${LPC_FW:-0} -DFLAC_RICE_FAST=${FLAC_RICE_FAST:-1}"

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
    CFLAGS="$CFLAGS -Wl,--defsym=RAM_192K=1 -DTAU_RAM_192K_FW=1"   # -D: the boot interlock also accepts the 192 KB bitstream's CORE_VERSION (B-333)
fi

# CLK66=1 (default 0, every target): clk_sys 60 -> 66.667 MHz (B-338, docs/HARPMUDD_UPSTREAM_1.5_REVIEW.md section 1). Every deadline
# already scales off CLK_HZ; this flag just changes it. B-347: now combines with RAM_192K=1 -- both -D flags are set together and the
# RTL/boot interlock both accept the combined bitstream's own CORE_VERSION rev 26 (mp3_soc.v, fw/player.c). The two `if` blocks below
# are independent and purely additive: setting both env vars just adds both -D flags to CFLAGS.
if [[ "${CLK66:-0}" == "1" ]]; then
    CFLAGS="$CFLAGS -DTAU_CLK66_FW=1"
fi

# B-333: a 192 KB (RAM_192K=1) build never writes over the shipped 256 KB release artefacts in dist/: it goes to work/ram192k/<target>/.
if [[ "${RAM_192K:-0}" == "1" && "$OUT" == "$ROOT/dist/Assets/tau/common" ]]; then OUT="$ROOT/work/ram192k/$TARGET"; fi

# B-585: TAU_BUILD_OUT=<dir> sends the ROM and cold image of ANY target to <dir>/<target>/ instead of dist/ or work/diagnostics/. For measurement-only
# builds (tools/check_heap_gap.py): its unflagged rebuilds used to overwrite dist/ and the flagged work/diagnostics/ ROMs (the B-448 clobber hazard).
if [[ -n "${TAU_BUILD_OUT:-}" ]]; then OUT="$TAU_BUILD_OUT/$TARGET"; fi

# A specialised target may redirect OUT away from the release Assets folder.
# Create it after target selection so objcopy never fails on a missing staging
# directory (the first sdram-diag build exposed the old ordering).
# HEAP_MIN_OVERRIDE=<bytes> replaces the per-target policy floor above (B-601): the gap is unused RAM (B-565), so an experimental build that is a little under the policy margin
# (a Diagnostic Build with the tempo stretcher, 1.9 KB against the 2 KB floor) can be packaged on purpose. The hard link minimum still applies. Never used by release builds.
# alpha.4: the Diagnostic Build carries the tempo stretcher by default; its gap is about 1.2 KB, under the 2 KB policy margin (the same situation DEV 94/95 ran on a Pocket with).
case "$STRESS_CFLAGS" in *-DTAU_DIAGNOSTIC=1*) if [ "${TEMPO:-0}" = "1" ]; then HEAP_MIN_OVERRIDE="${HEAP_MIN_OVERRIDE:-1024}"; fi ;; esac
HEAP_MIN="${HEAP_MIN_OVERRIDE:-$HEAP_MIN}"

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
# B-567: -fdata-sections gives each static its own .bss.<name> input section, so fw/link.ld can place picojpeg's low-traffic
# work buffers (input, Huffman, quant tables) in PSRAM without editing the vendored source. Only when the PSRAM art path is on
# (it proves the window before any decode); otherwise they stay in hot .bss.
PJ_DS=""; [[ "${ART_PSRAM:-1}" == "1" ]] && PJ_DS="-fdata-sections"
if ! "$GCC" -march=rv32im -mabi=ilp32 -mno-relax -Os -ffreestanding $PJ_DS         -I "$ROOT/third_party/picojpeg" -c         -o "$FW/picojpeg.o" "$ROOT/third_party/picojpeg/picojpeg.c"         > "$FW/build.log" 2>&1; then
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
if [[ "${PICOJPEG_COLD:-1}" == "1" ]] && [[ "${COLD_PACK:-0}" == "1" ]]; then
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
    if [[ "${RAM_192K:-0}" == "1" ]] && grep -qE "collides with reserved DMA|no room left for even a token heap" "$FW/build.log"; then
        # B-333: say by how much the 192 KB link misses, instead of just refusing. Relink for 256 KB (same flags otherwise) and compute
        # the margin against the 192 KB layout: tag_start moves down 64 KB and the stack shrinks 8 KB.
        if "$GCC" ${CFLAGS/-Wl,--defsym=RAM_192K=1/} "${INC[@]}" -T "$FW/link.ld" -o "$FW/fw_probe.elf" "${SRCS[@]}" -lm > /dev/null 2>&1; then
            hs=$("$NM" "$FW/fw_probe.elf" 2>/dev/null | awk '$3=="_heap_start"{print "0x"$1}')
            ts=$("$NM" "$FW/fw_probe.elf" 2>/dev/null | awk '$3=="_tag_start"{print "0x"$1}')
            "$PYTHON" -c "
hs, ts, hm = int('$hs',16), int('$ts',16), ${HEAP_MIN:-1024}
ts192 = ts - 65536 + 8192
print('*** 192 KB link: image ends at %d, DMA buffers start at %d, so the image is %+d B over; %d B short of the %d B heap floor ***' % (hs, ts192, hs - ts192, hs + hm - ts192, hm))
" >&2
        fi
        rm -f "$FW/fw_probe.elf"
    fi
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
