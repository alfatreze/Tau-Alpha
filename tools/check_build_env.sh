#!/usr/bin/env bash
set -u

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL_BIN="${RISCV_TOOLCHAIN_BIN:-}"
TOOL_PREFIX="${RISCV_PREFIX:-riscv-none-elf-}"
VENDORED_TOOL_BIN="$ROOT/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
if [[ -z "$TOOL_BIN" && -x "$VENDORED_TOOL_BIN/riscv-none-elf-gcc" ]]; then
    TOOL_BIN="$VENDORED_TOOL_BIN"
fi
SCOPE="${1:-all}"

case "$SCOPE" in
    all|firmware|fpga) ;;
    *) echo "usage: $0 {all|firmware|fpga}" >&2; exit 2 ;;
esac

resolve_riscv_tool() {
    local label="$1"
    local override="$2"
    local fallback="$3"
    local tool="${override:-${TOOL_BIN:+$TOOL_BIN/}${TOOL_PREFIX}$fallback}"

    if command -v "$tool" >/dev/null 2>&1; then
        printf 'ok       %-18s %s\n' "$label" "$(command -v "$tool")"
        return 0
    fi

    printf 'missing  %-18s %s\n' "$label" "$tool"
    return 1
}

resolve_plain_tool() {
    local label="$1"
    local override="$2"
    local fallback="$3"
    local tool="${override:-$fallback}"

    if command -v "$tool" >/dev/null 2>&1; then
        printf 'ok       %-18s %s\n' "$label" "$(command -v "$tool")"
        return 0
    fi

    printf 'missing  %-18s %s\n' "$label" "$tool"
    return 1
}

missing=0
if [[ "$SCOPE" == all || "$SCOPE" == firmware ]]; then
    resolve_riscv_tool "RISC-V GCC" "${CC_RISCV:-}" gcc || missing=1
    resolve_riscv_tool "RISC-V objcopy" "${OBJCOPY_RISCV:-}" objcopy || missing=1
    resolve_riscv_tool "RISC-V size" "${SIZE_RISCV:-}" size || missing=1
fi
if [[ "$SCOPE" == all || "$SCOPE" == fpga ]]; then
    resolve_plain_tool "Quartus" "${QUARTUS_SH:-}" quartus_sh || missing=1
fi
resolve_plain_tool "Python 3" "${PYTHON:-}" python3 || missing=1

if command -v iverilog >/dev/null 2>&1; then
    printf 'ok       %-18s %s\n' "Icarus Verilog" "$(command -v iverilog)"
else
    printf 'optional %-18s %s\n' "Icarus Verilog" "not installed"
fi

printf '\ncheckout: %s\n' "$ROOT"
if (( missing )); then
    printf 'status: required %s build tools are missing\n' "$SCOPE"
    exit 1
fi
printf 'status: ready for %s build\n' "$SCOPE"
