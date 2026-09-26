#!/usr/bin/env python3
"""Host test for tau-assets.bin (theme step 0d): fw/assets_core.h (the real C, compiled here) against tools/tau_assets.py.
Valid files parse identically; EVERY single-byte corruption and every truncation of a valid file is refused; short role
lists keep the pre-filled defaults; more than four themes keep four; luma is clamped."""
import json, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_assets as ta  # noqa: E402

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include "assets_core.h"
#define TR 21
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); static uint8_t b[4096]; uint32_t n = (uint32_t)fread(b, 1, sizeof b, f); fclose(f);
    if (argc > 2) n = (uint32_t)atoi(argv[2]);
    uint32_t total = as_total_size(b, n);
    uint32_t off = 0, len = 0, cnt = 0;
    int e = as_find(b, n, "THEM", &off, &len);
    printf("total %u find %d\n", total, e);
    if (e) return 0;
    as_theme_t meta[4]; static uint16_t roles[4 * 2 * TR];
    for (int i = 0; i < 4 * 2 * TR; i++) roles[i] = 0xABCD;
    e = as_themes(b + off, len, TR, 4, meta, roles, TR, &cnt);
    printf("themes %d count %u\n", e, cnt);
    if (e) return 0;
    for (uint32_t i = 0; i < cnt; i++) {
        printf("T %s %u %u", meta[i].name, meta[i].luma[0], meta[i].luma[1]);
        for (int p = 0; p < 2; p++) for (int r = 0; r < TR; r++) printf(" %u", roles[(i * 2 + p) * TR + r]);
        printf("\n");
    }
    return 0;
}
'''


def build(d):
    c, exe = Path(d) / "h.c", Path(d) / "h"
    c.write_text(HARNESS)
    r = subprocess.run(["cc", "-O1", "-Wall", "-Wno-unused-function", "-Werror", "-I", str(ROOT / "fw"), "-o", str(exe), str(c)], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        sys.exit(1)
    return exe


def run(exe, path, n=None):
    args = [str(exe), str(path)] + ([str(n)] if n is not None else [])
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout.splitlines()


def ok(lines):
    return len(lines) >= 2 and lines[0].endswith("find 0") and lines[1].startswith("themes 0")


def main():
    fails = 0
    sunset = json.loads((ROOT / "themes" / "user_examples" / "sunset.json").read_text())
    blob = ta.pack_container([(b"THEM", ta.pack_themes([sunset]))])
    with tempfile.TemporaryDirectory() as d:
        exe = build(d)
        f = Path(d) / "a.bin"
        f.write_bytes(blob)
        lines = run(exe, f)
        py = ta.parse(blob)["themes"][0]
        if not ok(lines):
            print("valid file refused:", lines); fails += 1
        else:
            t = lines[2].split()
            got = [int(x) for x in t[4:]]
            want = py["dark"] + py["light"]
            if t[1] != "SUNSET" or int(t[2]) != 42 or int(t[3]) != 215 or got != want:
                print("C and python readers disagree"); fails += 1
        for i in range(len(blob)):                     # every single-byte flip is refused
            bad = bytearray(blob); bad[i] ^= 0x5A
            f.write_bytes(bytes(bad))
            if ok(run(exe, f)):
                print(f"byte {i} flipped but accepted"); fails += 1
        f.write_bytes(blob)
        for n in range(len(blob)):                     # every truncation is refused
            if ok(run(exe, f, n)):
                print(f"truncated to {n} but accepted"); fails += 1
        # 12-role file: roles 0..11 from the file, the rest keep the caller's pre-filled defaults (0xABCD here)
        body = b"SHORT".ljust(16, b"\0") + bytes([50, 210, 0, 0]) + struct.pack("<12H", *range(100, 112)) + struct.pack("<12H", *range(200, 212))
        them = b"TTHM" + struct.pack("<HBB", 1, 12, 1) + struct.pack("<I", ta.crc(body)) + body
        f.write_bytes(ta.pack_container([(b"THEM", them)]))
        lines = run(exe, f)
        t = lines[2].split() if ok(lines) else []
        if not t or [int(x) for x in t[4:4 + 21]] != list(range(100, 112)) + [0xABCD] * 9:
            print("short role list did not keep the defaults:", lines[:3]); fails += 1
        # six themes: four kept; luma 5 and 250 clamp to 20 and 235
        ents = b""
        for k in range(6):
            ents += (b"T%d" % k).ljust(16, b"\0") + bytes([5, 250, 0, 0]) + struct.pack("<21H", *([k] * 21)) + struct.pack("<21H", *([k] * 21))
        them = b"TTHM" + struct.pack("<HBB", 1, 21, 6) + struct.pack("<I", ta.crc(ents)) + ents
        f.write_bytes(ta.pack_container([(b"THEM", them)]))
        lines = run(exe, f)
        if not ok(lines) or lines[1] != "themes 0 count 4" or lines[2].split()[2:4] != ["20", "235"]:
            print("more-than-four / clamp case failed:", lines[:3]); fails += 1
        f.write_bytes(ta.pack_container([(b"METR", b"\x00\x00")]))     # a container without THEM
        if run(exe, f)[0].split()[-1] != "35":
            print("container without THEM should report AS_E_NOTHEME"); fails += 1
        bad = bytearray(blob); bad[4] = 2                               # wrong version
        f.write_bytes(bytes(bad))
        if run(exe, f)[0].split()[-1] != "32":
            print("wrong version should report AS_E_VERSION"); fails += 1
    for label, mut in [("name", lambda t: t.update(name="lower case")), ("luma", lambda t: t["dark"].update(bg_luma=5)),
                       ("role", lambda t: t["light"].pop("surface")),
                       ("contrast", lambda t: t["dark"].update(text_primary="#202020"))]:
        t = json.loads(json.dumps(sunset)); mut(t)
        try:
            ta.pack_themes([t]); print(f"packer accepted a bad {label}"); fails += 1
        except (ValueError, KeyError):
            pass
    print("tau-assets OK" if not fails else f"{fails} FAILURES")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
