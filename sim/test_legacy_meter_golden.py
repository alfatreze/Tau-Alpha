#!/usr/bin/env python3
"""Golden frames for the eight older meters (Bars, Spectrum, Peak Dots, Waterfall, Scroll, VU needles, Oscilloscope, Phase scope).

The Winamp pair, Layered Wave, VU Master and Chladni are compared against JS twins (test_meter_golden.py and friends). The older eight
have no twin, so this test freezes THEIR drawing instead: it cuts the real viz_*_tick() functions out of fw/player.c, runs them on the
host with stub engine calls over a fixed pseudo-random trace (80 frames per scenario, paused stretch, forced repaint, two box widths,
both Bars layouts), and hashes every draw command in order. The hashes are stored in sim/golden/legacy_meters.json.

  python3 sim/test_legacy_meter_golden.py                 compare the working tree against the stored hashes
  python3 sim/test_legacy_meter_golden.py --write REF     regenerate the hashes from the firmware as it was at git REF (e.g. HEAD)
  python3 sim/test_legacy_meter_golden.py --dump NAME     print the command stream of one scenario (debugging)

The harness feeds BOTH the old inputs (the paused / wave / wave_pk globals) and, when the source's mtr_in_t has them, the new fields
(in->paused, in->env, in->env_pk), so one harness measures the firmware before and after the contract moved. A change to how any of the
eight meters draws, caches or scales fails here, not on a card. --selftest proves the comparison can fail (it mutates the source).
"""
import json, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOLD = ROOT / "sim" / "golden" / "legacy_meters.json"
FILES = ["fw/player.c", "fw/meter.h", "fw/meter_core.h"]
FUNCS = ["viz_scroll_tick", "viz_led_tick", "viz_dots_tick", "viz_water_tick", "viz_vu_tick", "viz_wave_tick", "viz_phase_tick", "viz_bars_tick"]
DEFS = ["UI_MARGIN", "UI_WAVE_N", "UI_WAVE_Y", "UI_WAVE_H", "UI_WAVE_GAP", "SCOPE_N", "SCOPE_HIST", "VU_ATT", "VU_DEC", "VU_STEPS", "WAVE_COLS",
        "SCOPE_UNIT", "LED_ROWS", "LED_GAPV", "LED_BLKH", "SPEC_GAPX", "SPEC_OCT", "SPEC_BANDS"]


def source(ref):
    out = {}
    for f in FILES:
        if ref is None:
            out[f] = (ROOT / f).read_text()
        else:
            out[f] = subprocess.run(["git", "show", f"{ref}:{f}"], capture_output=True, text=True, cwd=ROOT, check=True).stdout
    return out


def cut(src, sig):
    i = src.index(sig)
    j = src.index("\n}\n", i) + 3
    return src[i:j]


HARNESS = r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
%(defs)s
#define UI_TRACK 0x2104u
#define UI_WHITE 0xFFFFu
#define LED_LO   0x07E0u
#define LED_MIDC 0xFFE0u
#define LED_HI   0xF800u
enum { TS_1X = 0 };
#include "meter.h"
#include "meter_core.h"
static uint16_t ui_accent;
static uint8_t paused, bars_layout, vu_face, vu_shown_l, vu_shown_r, scope_head;
static uint16_t vu_face_w;
static uint32_t vu_l, vu_r;
static uint8_t spec_lvl[16];
static unsigned char spec_drawn[SPEC_BANDS], wave[UI_WAVE_N], wave_drawn[UI_WAVE_N], wave_pk[UI_WAVE_N], wave_pk_drawn[UI_WAVE_N];
static signed char scope_x[SCOPE_HIST][SCOPE_N], scope_y[SCOPE_HIST][SCOPE_N];
static unsigned long long H; static unsigned long N; static int DUMP;
static void emit(const char *s) { N++; if (DUMP) puts(s); for (; *s; s++) { H ^= (unsigned char)*s; H *= 1099511628211ull; } H ^= 10; H *= 1099511628211ull; }
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { char b[96]; snprintf(b, sizeof b, "rect %%u %%u %%u %%u %%u", x, y, w, h, c); emit(b); }
static void fb_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t l, uint16_t u) { char b[96]; snprintf(b, sizeof b, "bar %%u %%u %%u %%u %%u %%u %%u", x, y, w, h, lit, l, u); emit(b); }
static void fb_copy(uint32_t x, uint32_t y, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h) { char b[96]; snprintf(b, sizeof b, "copy %%u %%u %%u %%u %%u %%u", x, y, sx, sy, w, h); emit(b); }
static void fb_set_color(uint16_t a, uint16_t b2) { char b[64]; snprintf(b, sizeof b, "color %%u %%u", a, b2); emit(b); }
static void fb_text_clipped(uint32_t x, uint32_t y, const char *t, int a, int b2, uint32_t m) { char b[96]; snprintf(b, sizeof b, "text %%u %%u %%s %%d %%d %%u", x, y, t, a, b2, m); emit(b); }
static void ui_bg_restore(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { char b[96]; snprintf(b, sizeof b, "bg %%u %%u %%u %%u", x, y, w, h); emit(b); }
static void blit_probe_ensure(void) {}
static uint16_t ui_grad_at(uint32_t y) { return (uint16_t)(0x1081u + ((y * 37u) & 0x7FFu)); }
static uint16_t ui_mix(uint16_t a, uint16_t b, uint32_t t, uint32_t n)
{
    uint32_t r = (((a >> 11) & 0x1Fu) * (n - t) + ((b >> 11) & 0x1Fu) * t) / n;
    uint32_t g = (((a >> 5)  & 0x3Fu) * (n - t) + ((b >> 5)  & 0x3Fu) * t) / n;
    uint32_t bl = ((a & 0x1Fu) * (n - t) + (b & 0x1Fu) * t) / n;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
%(vu)s
%(funcs)s
static uint32_t rs = 12345u;
static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : 0; DUMP = argc > 2;
    static const char *names[8] = { "scroll", "led", "dots", "water", "vu", "wave", "phase", "bars" };
    static const uint16_t bw[2] = { 246, 360 };
    for (int m = 0; m < 8; m++) for (int bx = 0; bx < 2; bx++) for (int lay = 0; lay < (m == 7 ? 2 : 1); lay++) {
        char nm[48]; snprintf(nm, sizeof nm, "%%s_w%%u_l%%d", names[m], bw[bx], lay);
        if (only && strcmp(only, nm)) continue;
        H = 1469598103934665603ull; N = 0; rs = 12345u + (uint32_t)m * 7919u + (uint32_t)bx * 104729u + (uint32_t)lay;
        ui_accent = (uint16_t)(0x3C0Fu + m * 0x0841u); bars_layout = (uint8_t)lay;
        memset(spec_drawn, 0xFF, sizeof spec_drawn); memset(wave_drawn, 0, sizeof wave_drawn); memset(wave_pk_drawn, 0, sizeof wave_pk_drawn);
        memset(wave, 0, sizeof wave); memset(wave_pk, 0, sizeof wave_pk);
        vu_face = 0; vu_face_w = 0; vu_shown_l = vu_shown_r = 0; vu_l = vu_r = 0; scope_head = 0;
        memset(scope_x, 0, sizeof scope_x); memset(scope_y, 0, sizeof scope_y);
        for (int n = 0; n < 80; n++) {
            const int force = (n == 0 || n == 40);
            paused = (n >= 20 && n < 28);
            const int silent = (n >= 50 && n < 60);
            uint32_t loud = silent ? 0u : 4000u + (rnd() %% 28000u);
            if (n %% 9 == 0 && !silent) loud = 32767u;
            uint32_t pl = silent ? 0u : (loud * (60u + rnd() %% 40u)) / 100u, pr = silent ? 0u : (loud * (60u + rnd() %% 40u)) / 100u;
            for (int i = 0; i < 16; i++) spec_lvl[i] = silent ? 0 : (uint8_t)((rnd() %% 256u) * (16u - (uint32_t)i / 2u) / 16u);
            int8_t wv[WAVE_COLS];
            for (int i = 0; i < (int)WAVE_COLS; i++) wv[i] = silent ? 0 : (int8_t)((int)(rnd() %% 201u) - 100);
            scope_head = (uint8_t)((scope_head + 1u) %% SCOPE_HIST);
            for (uint32_t k = 0; k < SCOPE_N; k++) { scope_x[scope_head][k] = (signed char)((int)(rnd() %% 201u) - 100); scope_y[scope_head][k] = (signed char)((int)(rnd() %% 201u) - 100); }
            /* the host's per-tick envelope step (ui_meter_redraw): shift in the newest amplitude, then let each peak sink one pixel */
            uint32_t amp = (loud * UI_WAVE_H) / 32768u; if (amp > UI_WAVE_H) amp = UI_WAVE_H;
            if (!paused) {
                for (uint32_t i = 0; i < UI_WAVE_N - 1u; i++) { wave[i] = wave[i + 1]; wave_pk[i] = wave_pk[i + 1]; }
                wave[UI_WAVE_N - 1u] = (unsigned char)amp; wave_pk[UI_WAVE_N - 1u] = (unsigned char)amp;
            }
            for (uint32_t i = 0; i < UI_WAVE_N; i++) if (wave_pk[i] > wave[i]) wave_pk[i]--;
            if (force) { memset(spec_drawn, 0xFF, sizeof spec_drawn); memset(wave_drawn, 0, sizeof wave_drawn); memset(wave_pk_drawn, 0, sizeof wave_pk_drawn); }
            mtr_in_t in; memset(&in, 0, sizeof in);
            in.spec = spec_lvl; in.wave = wv; in.peak = loud; in.peak_l = pl; in.peak_r = pr; in.frame = (uint32_t)n; in.dt_ms = 26u;
            in.x = UI_MARGIN; in.y = UI_WAVE_Y; in.w = bw[bx]; in.h = UI_WAVE_H; in.bg = 0x1082u; in.force = (uint8_t)force;
%(newin)s
            char fb[16]; snprintf(fb, sizeof fb, "F %%d", n); emit(fb);
            switch (m) {
            case 0: viz_scroll_tick(&in); break; case 1: viz_led_tick(&in); break; case 2: viz_dots_tick(&in); break; case 3: viz_water_tick(&in); break;
            case 4: viz_vu_tick(&in); break; case 5: viz_wave_tick(&in); break; case 6: viz_phase_tick(&in); break; default: viz_bars_tick(&in); break;
            }
        }
        if (!DUMP) printf("S %%s %%lu %%016llx\n", nm, N, H);
    }
    return 0;
}
'''


def build(src, mutate=None):
    p = src["fw/player.c"]
    if mutate:
        p = mutate(p)
    defs = []
    for name in DEFS:
        m = re.search(r"^#define\s+%s\s+(.*)$" % name, p, re.M)
        v = re.sub(r"/\*.*", "", m.group(1)).strip()
        defs.append("#define %s %s" % (name, v))
    # the two small tables end with "};" rather than a closing brace on its own line
    def table(sig):
        i = p.index(sig); return p[i:p.index("};", i) + 3]
    vu = table("static const int16_t vu_sn[17]") + "\n" + table("static const int16_t vu_cs[17]") + "\n" + cut(p, "static void vu_angle")
    funcs = "\n".join(cut(p, "static void %s(" % f) for f in FUNCS)
    newin = "#ifdef HAS_ENV\n            in.paused = paused; in.env = wave; in.env_pk = wave_pk;\n#endif" if "env_pk" in src["fw/meter.h"] else ""
    return HARNESS % {"defs": "\n".join(defs), "vu": vu, "funcs": funcs, "newin": newin.replace("#ifdef HAS_ENV\n", "").replace("\n#endif", "")}


def run(src, args=(), mutate=None):
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "meter.h").write_text(src["fw/meter.h"]); (d / "meter_core.h").write_text(src["fw/meter_core.h"])
        (d / "h.c").write_text(build(src, mutate))
        r = subprocess.run(["cc", "-O1", "-w", "-I", str(d), "-o", str(d / "h"), str(d / "h.c")], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr[:3000]); sys.exit(1)
        out = subprocess.run([str(d / "h")] + list(args), capture_output=True, text=True, check=True).stdout
    return out


def parse(out):
    res = {}
    for ln in out.splitlines():
        if ln.startswith("S "):
            _, nm, n, h = ln.split()
            res[nm] = {"commands": int(n), "hash": h}
    return res


def main():
    a = sys.argv[1:]
    if a[:1] == ["--write"]:
        res = parse(run(source(a[1])))
        GOLD.parent.mkdir(parents=True, exist_ok=True)
        GOLD.write_text(json.dumps({"from": a[1], "scenarios": res}, indent=1) + "\n")
        print("wrote %d scenarios from %s" % (len(res), a[1])); return
    if a[:1] == ["--dump"]:
        print(run(source(None), [a[1], "dump"])); return
    cur = parse(run(source(None)))
    if "--selftest" in a:
        def mutate(p):
            q = p.replace("fb_rect(cx, cy, 1, 1, UI_TRACK)", "fb_rect(cx, cy, 1, 2, UI_TRACK)").replace("if (pk > bh + 1u)", "if (pk > bh + 2u)")
            assert q != p; return q
        bad = parse(run(source(None), mutate=mutate))
        diff = [k for k in cur if bad.get(k) != cur[k]]
        if not diff: print("SELFTEST FAILED: the mutated source produced identical frames"); sys.exit(1)
        print("selftest OK: a deliberate drawing change was caught in %d scenario(s)" % len(diff)); return
    gold = json.loads(GOLD.read_text())["scenarios"]
    bad = [k for k in sorted(set(gold) | set(cur)) if gold.get(k) != cur.get(k)]
    if bad:
        print("LEGACY METER GOLDEN FRAMES CHANGED in: " + ", ".join(bad) + "\n(inspect with --dump NAME; regenerate only for an approved look change)"); sys.exit(1)
    print("legacy meter golden frames OK (%d scenarios, %d draw commands identical)" % (len(cur), sum(v["commands"] for v in cur.values())))


if __name__ == "__main__":
    main()
