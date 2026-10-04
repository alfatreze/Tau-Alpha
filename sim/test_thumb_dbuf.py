#!/usr/bin/env python3
"""B-572: the Settings meter previews drawn through the CLUT blit, against a host model of the double-buffered draw engine.

Extracts the real preview functions from fw/player.c and fw/settingsui.inc, builds sim/thumb_dbuf_harness.c around them, and requires every
CLUT-blit preview to equal the software-drawn one with the displayed (and CPU) buffer 0 AND 1 (B-571: with buffer 1 the four CLUT-blit previews
vanished on hardware because blit-mode commands do not follow R_DBUF_CPU), and the sticky bases to be restored. Built with the CLUT model of the
current RTL (one-slot skew, firmware start index 255: B-569/B-570)."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "thumb_dbuf"


def extract(path, name):
    lines = Path(path).read_text().splitlines()
    for i, l in enumerate(lines):
        if re.match(r"^[A-Za-z_][A-Za-z_0-9 \*]*\b" + re.escape(name) + r"\(", l) and not l.rstrip().endswith(";"):
            out = []
            for j in range(i, len(lines)):
                out.append(lines[j])
                if lines[j] == "}":
                    return "\n".join(out)
    sys.exit(f"cannot find {name} in {path}")


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    src = [("fw/player.c", n) for n in ("ui_mix", "ui_bg_cur_buf", "fb_set_bases", "fb_clut_load", "fb_cblit")] + \
          [("fw/settingsui.inc", n) for n in ("set_thumb_pal", "set_draw_thumb_soft", "set_thumb_flat_build", "set_draw_thumb")]
    pre = ("static uint16_t ui_accent = 0x07E0;\nstatic uint32_t dbg_base_src, dbg_base_dst;\nstatic uint8_t thumb_flat_ready;\n"
           "static inline uint32_t thumb_slot(uint32_t v)\n{\n    return v - (v > VIZ_RETIRED_LEVELS) - (v > VIZ_RETIRED_MIRROR) - (v > VIZ_RETIRED_EYE) - (v > VIZ_RETIRED_TAPE) - (v > VIZ_CHLADNI);\n}\n")
    (BUILD / "thumb_fw.inc").write_text(pre + "\n".join(extract(ROOT / p, n) + "\n" for p, n in src))
    exe = BUILD / "harness"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-I", str(ROOT / "fw"), "-I", str(BUILD), "-o", str(exe),
                        str(ROOT / "sim/thumb_dbuf_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit("harness build failed")
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    print(r.stdout.strip())
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
