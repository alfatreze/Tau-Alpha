#!/usr/bin/env python3
"""PHASE_F_SPEC.md section 12: run the blit engine's fixed scene through
tools/host/blit_reference.py (the software reference renderer) and through
RTL simulation (sim/tb_blit_scene.v), then diff the two buffers exactly.

Also confirms the diff is a real regression net, not just a passing test: it
re-runs the RTL scene with each of tb_mp3_fb.v's existing mutation
parameters (BUG_IGNORE_BLIT_STRIDE, BUG_IGNORE_KEY, BUG_SBLIT_NO_SCALE,
BUG_BLEND_ALWAYS_SRC) and requires the diff to catch every one -- the
"injected-fault case that must be caught" section 12 asks for, reusing the
mutation hooks that already exist rather than inventing new ones.

The scene's literal addresses/values are hand-picked to avoid any unintended
overlap between commands, and MUST match sim/tb_blit_scene.v's own scene
exactly -- the two are hand-synchronised, not generated from one source,
so a change to one always needs the matching change in the other.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "host"))
from blit_reference import Renderer, load_font_words  # noqa: E402

TB = ROOT / "sim" / "tb_blit_scene.v"
MP3_FB = ROOT / "src" / "fpga" / "core" / "mp3_fb.sv"
FONT_ROM = ROOT / "src" / "fpga" / "core" / "font_rom.v"
BUILD = ROOT / "build" / "rtl"


def reference_scene() -> dict[int, int]:
    """The same 10-command scene sim/tb_blit_scene.v plays, computed by the
    independent Python model."""
    r = Renderer(load_font_words())
    # 1. RUN: addr=0, w=8, fg=0x1234
    r.run(0, 8, 0x1234)
    # 2. RECT: addr=1024, w=6, h=3, fg=0x2222
    r.rect(1024, 6, 3, 0x2222)
    # 3. Pre-paint for the keyed/blended BLITs: addr=3072, w=4, h=2, fg=0x5555
    r.rect(3072, 4, 2, 0x5555)
    # 4. COPY: dest addr=4608, w=4, h=2, source offset=(5<<16)|100
    r.copy(4608, 4, 2, (5 << 16) | 100)
    # 5. BLIT plain: dest addr=6144, w=4, h=2, source offset=200
    r.blit(6144, 200, 4, 2)
    # 6. BLIT keyed: dest addr=3072, w=4, h=1, source offset=300, key=302
    r.blit(3072, 300, 4, 1, key_en=True, key=302)
    # 7. BLIT blended: dest addr=3584, w=4, h=1, source offset=400, DSP alpha=128
    r.blit(3584, 400, 4, 1, blend_en=True, blend_mode=0, blend_alpha=128)
    # 8. BAR: addr=7680, w=5, h=4, fg=0x0F0F, bg=0x00F0, 3 lit rows
    r.bar(7680, 5, 4, 0x0F0F, 0x00F0, 3)
    # 9. SBLIT: dest addr=10240, source 2x2 at offset=500, 2x/2x -> 4x4 out
    r.sblit(10240, 500, 2, 2, 2, 2)
    # 10. BLIT custom stride: dest addr=16384, w=4, h=2, source offset=600,
    #     dst_stride=96, src_stride=64
    r.blit(16384, 600, 4, 2, dst_stride=96, src_stride=64)
    # 11. CHAR: 'A' (0x41), scale 1x1, fg=0xFFFF, bg=0x0000, addr=12800
    r.char(12800, 0x41, 0xFFFF, 0x0000, 0, 0)
    return r.mem


def run_rtl_scene(mutation: str | None = None) -> dict[int, int]:
    """Compile and run sim/tb_blit_scene.v, optionally with one mutation
    parameter forced on, and parse its dump file into an {addr: value} map."""
    BUILD.mkdir(parents=True, exist_ok=True)
    vvp = BUILD / ("tb_blit_scene_mut.vvp" if mutation else "tb_blit_scene.vvp")
    dump = BUILD / ("blit_scene_mut.txt" if mutation else "blit_scene.txt")
    cmd = ["iverilog", "-g2012"]
    if mutation:
        cmd.append(f"-Ptb_blit_scene.{mutation}=1")
    cmd += ["-o", str(vvp), str(TB), str(MP3_FB), str(FONT_ROM)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"iverilog failed: {r.stdout}\n{r.stderr}")
    r = subprocess.run(
        ["vvp", str(vvp), f"+DUMP={dump}"], capture_output=True, text=True
    )
    if "PASSED" not in r.stdout:
        raise RuntimeError(f"tb_blit_scene did not finish cleanly: {r.stdout}")
    out: dict[int, int] = {}
    for line in dump.read_text().splitlines():
        addr, val = line.split()
        out[int(addr)] = int(val)
    return out


def diff(expected: dict[int, int], actual: dict[int, int]) -> list[str]:
    problems = []
    for addr, exp in expected.items():
        got = actual.get(addr)
        if got is None:
            problems.append(f"addr {addr}: expected 0x{exp:04X}, never written")
        elif got != exp:
            problems.append(f"addr {addr}: expected 0x{exp:04X}, got 0x{got:04X}")
    extra = set(actual) - set(expected)
    for addr in sorted(extra):
        problems.append(f"addr {addr}: written 0x{actual[addr]:04X}, not in reference")
    return problems


def main() -> int:
    expected = reference_scene()
    print(f"reference: {len(expected)} words computed")

    actual = run_rtl_scene()
    problems = diff(expected, actual)
    if problems:
        print(f"FAILED: {len(problems)} mismatches on the clean scene")
        for p in problems[:20]:
            print(f"  {p}")
        return 1
    print(f"ok:   clean scene matches the reference exactly ({len(actual)} words)")

    # Injected-fault case: every existing mp3_fb.sv mutation hook must be
    # caught by this same diff, or the pixel-diff isn't a real regression net.
    mutations = [
        "BUG_IGNORE_BLIT_STRIDE",
        "BUG_IGNORE_KEY",
        "BUG_SBLIT_NO_SCALE",
        "BUG_BLEND_ALWAYS_SRC",
    ]
    for mut in mutations:
        mutated = run_rtl_scene(mutation=mut)
        mut_problems = diff(expected, mutated)
        if not mut_problems:
            print(f"FAILED: mutant not caught: {mut}")
            return 1
        print(f"ok:   mutant caught: {mut} ({len(mut_problems)} mismatches)")

    print("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
