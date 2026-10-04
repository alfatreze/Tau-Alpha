#!/usr/bin/env python3
"""B-574: the boot-splash asset formats (TAU1: 16-colour palette, TAU2: 256-colour palette) against the REAL firmware reader.

Extracts ui_splash_asset() and its two helpers from fw/player.c, builds sim/splash_harness.c around them and requires:
  - the shipped dist/Assets/tau/common/tau-loading.bin to draw exactly the framebuffer tools/capture_splash_frame.py decodes;
  - a TAU1 file (the old format, made here from the authored source with 16 colours) to still draw correctly;
  - damaged files (bad magic, wrong size, zero-length run, run past the end, truncated, palette index out of range) to be refused
    (return 0), never to crash."""
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
BUILD = ROOT / "build" / "splash"
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += not cond


def extract(name):
    lines = (ROOT / "fw/player.c").read_text().splitlines()
    for i, l in enumerate(lines):
        if re.match(r"^[A-Za-z_][A-Za-z_0-9 \*]*\b" + name + r"\(", l) and not l.rstrip().endswith(";"):
            out = []
            for j in range(i, len(lines)):
                out.append(lines[j])
                if lines[j] == "}":
                    return "\n".join(out)
    sys.exit(f"cannot find {name} in fw/player.c")


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    body = "\n".join(extract(n) + "\n" for n in ("tau_u16", "tau_u32", "ui_splash_asset"))
    raw = "(uint8_t *)(uintptr_t)(0xC0000000u + dst)"
    assert raw in body, "ui_splash_asset no longer reaches the tag buffer through the expression this test rewrites: update the test"
    body = body.replace(raw, "tagmem")
    (BUILD / "splash_fw.inc").write_text(body)
    defs = [l for l in (ROOT / "fw/player.c").read_text().splitlines()
            if re.match(r"#define\s+TAU_SPLASH_(SLOT_ID|W|H|HEADER2?|CHUNK)\b", l)]
    assert len(defs) == 6, f"expected 6 TAU_SPLASH_* constants in fw/player.c, found {len(defs)}"
    (BUILD / "splash_defs.h").write_text("\n".join(defs) + "\n")
    exe = BUILD / "harness"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-I", str(BUILD), "-o", str(exe), str(ROOT / "sim/splash_harness.c")],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit("harness build failed")
    return exe


def run(exe, data):
    f = BUILD / "case.bin"
    f.write_bytes(data)
    out = BUILD / "fb.rgb565"
    r = subprocess.run([str(exe), str(f), str(out)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr, r.returncode)
        sys.exit("harness crashed")
    ok = int(r.stdout.split("ok=")[1].split()[0])
    return ok, out.read_bytes()


def reference(data):
    import capture_splash_frame as c
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as t:
        t.write(data)
    px, _, _ = c.decode_asset(Path(t.name))
    return b"".join(struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in px)


def tau1_from_source():
    from PIL import Image
    src = Image.open(ROOT / "assets/ui/tau-loading-source.jpg").convert("RGB")
    q = src.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    idx = list(q.tobytes())
    pal = q.getpalette()[:48]
    pal565 = [((pal[i] & 0xF8) << 8) | ((pal[i + 1] & 0xFC) << 3) | (pal[i + 2] >> 3) for i in range(0, 48, 3)]
    enc, v, n = [], idx[0], 0
    for it in idx:
        if it == v and n < 255:
            n += 1
        else:
            enc += [n, v]; v, n = it, 1
    enc += [n, v]
    return struct.pack("<4sHHI", b"TAU1", 400, 360, len(enc)) + struct.pack("<16H", *pal565) + bytes(enc)


def main():
    exe = build()
    shipped = (ROOT / "dist/Assets/tau/common/tau-loading.bin").read_bytes()
    ok, fb = run(exe, shipped)
    check(ok == 1 and fb == reference(shipped), f"shipped asset ({shipped[:4].decode()}, {len(shipped)} bytes) draws exactly the reference framebuffer")
    t1 = tau1_from_source()
    ok, fb = run(exe, t1)
    check(ok == 1 and fb == reference(t1), "a TAU1 (16-colour) file still draws exactly the reference framebuffer")

    def bad(name, data):
        ok, _ = run(exe, data)
        check(ok == 0, f"damaged asset refused: {name}")

    bad("bad magic", b"XXXX" + shipped[4:])
    bad("wrong width", shipped[:4] + struct.pack("<H", 399) + shipped[6:])
    bad("wrong height", shipped[:6] + struct.pack("<H", 361) + shipped[8:])
    bad("truncated", shipped[:len(shipped) // 2])
    bad("header only", shipped[:12])
    bad("empty", b"")
    hdr = 524 if shipped[3:4] == b"2" else 44
    body = bytearray(shipped)
    body[hdr] = 0
    bad("zero-length run", bytes(body))
    body = bytearray(shipped)
    body[hdr] = 255
    for k in range(hdr, hdr + 2 * 1000, 2):
        body[k] = 255
    bad("runs past the end of the image", bytes(body))
    print("PASSED" if not fails else f"{fails} FAILED")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
