#!/usr/bin/env python3
"""Host test for the theme step 0b maths in fw/player.c: the real ui_grad_at() and th_accent_of() are cut out of the source and
compiled on the host.
  1. Dark theme (black bottom): ui_grad_at() equals the ORIGINAL ramp algorithm at every row, for many tops (no visible change).
  2. Light theme (bottom brighter than top): the ramp is monotonic, hits both ends, and stays within one level of the ideal ramp.
  3. th_accent_of() equals tools/gen_themes.py's mirror for every palette colour and both polarities; Light accents meet the luma cap.
"""
import re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_themes as gt  # noqa: E402

SRC = (ROOT / "fw" / "player.c").read_text()


def cut(name, src=None):
    src = SRC if src is None else src
    i = src.index(name)
    j = src.index("\n}\n", i) + 3
    return src[i:j]


HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#define FB_H 360u
#define TR_BG_BOTTOM 0
#define TH_LIGHT_ACC_MAX_L %d
static uint16_t th_role[1];
static uint8_t th_pol;
static uint16_t ui_grad_top_c;
static uint16_t ui_palette[] = { %s };
%s
%s
int main(void) {
    for (int mode = 0; mode < 2; mode++) {
        th_pol = (uint8_t)mode;
        for (unsigned i = 0; i < sizeof(ui_palette) / sizeof(ui_palette[0]); i++) printf("A %%d %%u %%u\n", mode, i, th_accent_of(i));
    }
    unsigned tops[] = { %s };
    unsigned bots[] = { %s };
    for (unsigned k = 0; k < sizeof(tops) / sizeof(tops[0]); k++) {
        ui_grad_top_c = (uint16_t)tops[k]; th_role[0] = (uint16_t)bots[k];
        for (uint32_t y = 0; y < FB_H; y++) printf("G %%u %%u %%u %%u\n", tops[k], bots[k], y, ui_grad_at(y));
    }
    return 0;
}
'''


THUMB_HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define COLD_FN
#define TR_BASE 0
#define TR_TEXT_PRIMARY 1
static uint16_t th_role[2];
static uint16_t ui_accent;
%s
%s
int main(int argc, char **argv) {
    th_role[0] = (uint16_t)strtoul(argv[1], 0, 0); th_role[1] = (uint16_t)strtoul(argv[2], 0, 0); ui_accent = (uint16_t)strtoul(argv[3], 0, 0);
    for (uint32_t v = 0; v < 15; v++) { uint16_t o[8]; set_thumb_pal(v, o); for (int i = 0; i < 8; i++) printf("%%u %%d %%u\n", v, i, o[i]); }
    return 0;
}
'''


def check_thumbs():
    """Step 0c: set_thumb_pal() from the real source. Dark keeps the stored greys close; Light inverts; chroma entries become the accent."""
    settings = (ROOT / "fw" / "settingsui.inc").read_text()
    pal_src = (ROOT / "fw" / "meter_thumbs.h").read_text()
    pal_txt = pal_src[pal_src.index("meter_thumb_pal[15][8]"):]
    pal_txt = pal_txt[:pal_txt.index("};") + 2]
    pal = [[int(x, 16) for x in row.split(",") if x.strip()] for row in
           [r.replace("u", "").replace("{", "").replace("}", "").strip() for r in pal_txt.splitlines() if r.strip().startswith("{")]]
    assert len(pal) == 15 and all(len(r) == 8 for r in pal)
    code = THUMB_HARNESS % ("static const uint16_t meter_thumb_pal[15][8] = { " + ",".join("{" + ",".join(map(str, r)) + "}" for r in pal) + " };",
                            cut("static uint16_t ui_mix", SRC) + "\n" + cut("COLD_FN static void set_thumb_pal", settings))
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        c, exe = Path(d) / "t.c", Path(d) / "t"
        c.write_text(code)
        r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr)
            return 1
        for t in gt.load():
            for pol in gt.POLS:
                base, prim, acc = gt.snap(t[pol]["base"]), gt.snap(t[pol]["text_primary"]), 0x5D5C
                out = subprocess.run([str(exe), str(base), str(prim), str(acc)], capture_output=True, text=True, check=True).stdout.split("\n")
                got = {}
                for line in out:
                    if line:
                        v, i, c_ = (int(x) for x in line.split())
                        got[(v, i)] = c_
                for v in range(15):
                    for i in range(8):
                        r8, g8, b8 = gt.rgb8(pal[v][i])
                        chroma = max(r8, g8, b8) - min(r8, g8, b8) > 40
                        if chroma:
                            if got[(v, i)] != acc:
                                fails += 1; print(f"{t['name']} {pol}: chroma entry {v}/{i} not the accent")
                            continue
                        stored = (2126 * r8 + 7152 * g8 + 722 * b8) // 10000
                        lg = gt.lum(got[(v, i)]); lb, lp = gt.lum(base), gt.lum(prim)
                        lo, hi = min(lb, lp), max(lb, lp)
                        if not (lo - 0.002 <= lg <= hi + 0.002):
                            fails += 1; print(f"{t['name']} {pol}: entry {v}/{i} outside base..primary")
                        if pol == "dark" and t["name"] == "TAU" and abs(gt.lum(got[(v, i)]) - gt.lum(pal[v][i])) > 0.06:
                            fails += 1; print(f"TAU dark: entry {v}/{i} 0x{got[(v, i)]:04X} strays from stored 0x{pal[v][i]:04X}")
                # ordering: a lighter stored grey never maps to a colour closer to the plate than a darker one
                for v in range(15):
                    grey = [(gt.lum(pal[v][i]), gt.lum(got[(v, i)])) for i in range(8) if max(gt.rgb8(pal[v][i])) - min(gt.rgb8(pal[v][i])) <= 40]
                    grey.sort()
                    dist = [abs(g - gt.lum(base)) for _, g in grey]
                    if any(dist[k + 1] + 1e-6 < dist[k] for k in range(len(dist) - 1)):
                        fails += 1; print(f"{t['name']} {pol}: preview {v} palette order not monotonic")
    return fails


def orig_ramp(top, y):
    """The pre-theme ui_grad_at() (black bottom), from git history, as Python."""
    thr = (1, 5, 3, 7)
    den = 359
    rem = den - y
    t = thr[y & 3]
    lv = [(top >> 11) & 31, (top >> 5) & 63, top & 31]
    out = []
    for v in lv:
        num = v * rem
        base = num // den
        if (num - base * den) * 8 > t * den:
            base += 1
        out.append(base)
    return out[0] << 11 | out[1] << 5 | out[2]


def main():
    pal = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})u,\s+/\*\s+[A-Z_]+ ", SRC[SRC.index("static const uint16_t ui_palette[]"):SRC.index("#define UI_PALETTE_N")])]
    assert len(pal) == 19, len(pal)
    dark_tops = [0x2124, 0x0000, 0x2945, 0x1082, 0x3186, 0x4208, 0x0821, 0x39C7]
    light_tops = [0xC618, 0xCE79, 0xB596, 0xD69A]
    light_bot = 0xF79E
    tops = dark_tops + light_tops
    bots = [0] * len(dark_tops) + [light_bot] * len(light_tops)
    code = HARNESS % (gt.LIGHT_ACC_MAX_L, ", ".join("0x%04X" % p for p in pal), cut("static uint16_t th_accent_of"),
                      cut("static uint16_t ui_grad_at"), ", ".join(str(t) for t in tops), ", ".join(str(b) for b in bots))
    with tempfile.TemporaryDirectory() as d:
        c, exe = Path(d) / "t.c", Path(d) / "t"
        c.write_text(code)
        r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-o", str(exe), str(c)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr)
            sys.exit(1)
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
    fails = 0
    ramps = {}
    for line in out:
        f = line.split()
        if f[0] == "A":
            mode, i, got = int(f[1]), int(f[2]), int(f[3])
            want = gt.acc_eff(pal[i], "light" if mode else "dark")
            if got != want:
                fails += 1
                print(f"accent mismatch mode {mode} idx {i}: C 0x{got:04X} python 0x{want:04X}")
            if mode:
                r8, g8, b8 = gt.rgb8(got)
                l = (2126 * r8 + 7152 * g8 + 722 * b8) // 10000
                if l > gt.LIGHT_ACC_MAX_L + 4:
                    fails += 1
                    print(f"light accent {i} luma {l} above the cap")
        else:
            top, bot, y, got = (int(x) for x in f[1:])
            ramps.setdefault((top, bot), {})[y] = got
    for (top, bot), rows in ramps.items():
        if bot == 0:
            for y, got in rows.items():
                if got != orig_ramp(top, y):
                    fails += 1
                    print(f"dark ramp top 0x{top:04X} y {y}: 0x{got:04X} != original 0x{orig_ramp(top, y):04X}")
                    break
        else:
            ch = lambda c: [(c >> 11) & 31, (c >> 5) & 63, c & 31]
            first, last = ch(rows[0]), ch(rows[359])
            if first != ch(top) or last != ch(bot):
                fails += 1
                print(f"light ramp 0x{top:04X}->0x{bot:04X}: ends {first} {last}")
            for k in range(3):
                seq = [ch(rows[y])[k] for y in range(360)]
                smooth = [sum(seq[max(0, i - 3):i + 4]) / len(seq[max(0, i - 3):i + 4]) for i in range(360)]
                if any(smooth[i + 1] < smooth[i] - 0.6 for i in range(359)):
                    fails += 1
                    print(f"light ramp 0x{top:04X} channel {k} not monotonic")
                for y in range(0, 360, 9):
                    ideal = ch(top)[k] + (ch(bot)[k] - ch(top)[k]) * y / 359
                    if abs(seq[y] - ideal) > 1.01:
                        fails += 1
                        print(f"light ramp 0x{top:04X} ch {k} y {y}: {seq[y]} vs ideal {ideal:.2f}")
                        break
    fails += check_thumbs()
    print("theme maths OK" if not fails else f"{fails} FAILURES")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
