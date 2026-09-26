#!/usr/bin/env python3
"""Theme data for the firmware (docs/THEME_SPEC.md, roadmap item 0, step 0b).

themes/<name>.json  ->  fw/theme_data.h   (built-in themes, both polarities, RGB565)

    python3 tools/gen_themes.py            # regenerate fw/theme_data.h and print the report
    python3 tools/gen_themes.py --check    # exit 1 if theme_data.h is stale, a theme is incomplete, a contrast rule
                                           # fails, or theme 0 dark no longer equals the firmware's TH_DEF_* defaults

The report also scores the RTL text weight table (src/fpga/core/mp3_fb.sv, fitted by tools/gen_text_gamma.py for light
text on a dark ramp) against each theme/polarity, next to the identity table, so a polarity that the fitted table
serves worse than a plain linear blend is visible before hardware.
"""
import argparse, json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_text_gamma as gtg  # noqa: E402

# Role keys in theme files, in the C enum's names (theme.h). Accent, bg/top and accent-2 are not in the files:
# accent is the user's palette pick, bg/top is derived from it at the theme's bg_luma.
ROLES = [("bg_bottom", "TR_BG_BOTTOM"), ("surface", "TR_SURFACE"), ("surface_track", "TR_SURFACE_TRACK"),
         ("text_primary", "TR_TEXT_PRIMARY"), ("text_secondary", "TR_TEXT_SECONDARY"), ("on_accent", "TR_ON_ACCENT"),
         ("ok", "TR_OK"), ("warn", "TR_WARN"), ("danger", "TR_DANGER"), ("base", "TR_BASE"), ("chrome", "TR_CHROME"),
         ("pill", "TR_PILL"), ("error", "TR_ERROR"), ("faint", "TR_FAINT"), ("splash_bg", "TR_SPLASH_BG"),
         ("splash_bar", "TR_SPLASH_BAR"), ("fs_red", "TR_FS_RED"), ("fs_track", "TR_FS_TRACK")]
DEFAULT_KEYS = {"bg_bottom": "BG_BOTTOM", "surface": "SURFACE", "surface_track": "SURFACE_TRACK",
                "text_primary": "TEXT_PRIMARY", "text_secondary": "TEXT_SECONDARY", "on_accent": "ON_ACCENT",
                "ok": "OK", "warn": "WARN", "danger": "DANGER", "base": "BASE", "chrome": "CHROME", "pill": "PILL",
                "error": "ERROR", "faint": "FAINT", "splash_bg": "SPLASH_BG", "splash_bar": "SPLASH_BAR",
                "fs_red": "FS_RED", "fs_track": "FS_TRACK"}
LIGHT_ACC_MAX_L = 110      # Light polarity: an accent brighter than this (Rec.709 luma of the 8-bit code values) is scaled down to it
POLS = ("dark", "light")
# contrast rules (WCAG ratio): (fg role, bg roles, minimum). "ramp" = the gradient top and the mid ramp for sample accents.
RULES = [("text_primary", ["surface", "base", "ramp"], 4.5), ("text_secondary", ["surface", "base"], 3.0),
         ("text_secondary", ["ramp"], 2.6)]


def snap(v):
    if isinstance(v, str) and v.lower().startswith("0x"):
        return int(v, 16)
    s = v.lstrip("#")
    r, g, b = (int(s[i:i + 2], 16) for i in (0, 2, 4))
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def rgb8(c):
    return ((c >> 11) * 255 // 31, ((c >> 5) & 0x3F) * 255 // 63, (c & 0x1F) * 255 // 31)


def lum(c):
    r, g, b = rgb8(c)
    f = lambda x: x / 255 / 12.92 if x / 255 <= 0.04045 else (((x / 255) + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def acc_eff(a, pol):
    """Mirror of th_accent_of() in fw/player.c."""
    if pol != "light":
        return a
    r, g, b = rgb8(a)
    l = (2126 * r + 7152 * g + 722 * b) // 10000
    if l > LIGHT_ACC_MAX_L:
        r, g, b = r * LIGHT_ACC_MAX_L // l, g * LIGHT_ACC_MAX_L // l, b * LIGHT_ACC_MAX_L // l
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def grad_top(accent, luma, pol="dark"):
    """Mirror of ui_grad_set()."""
    r, g, b = rgb8(accent)
    l = (2126 * r + 7152 * g + 722 * b) // 10000 or 1
    r, g, b = ((r * luma + l // 2) // l, (g * luma + l // 2) // l, (b * luma + l // 2) // l)
    r, g, b = ((r + luma + 1) // 2, (g + luma + 1) // 2, (b + luma + 1) // 2)
    r, g, b = min(255, r), min(255, g), min(255, b)
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def ramp(top, bot, n=40):
    return [gtg.mix565(top, bot, i, n) for i in range(n + 1)]


def load():
    themes = []
    for p in sorted((ROOT / "themes").glob("*.json")):
        d = json.loads(p.read_text())
        for pol in POLS:
            if pol not in d:
                raise SystemExit(f"{p.name}: missing polarity {pol}")
            miss = [k for k, _ in ROLES if k not in d[pol]] + (["bg_luma"] if "bg_luma" not in d[pol] else [])
            if miss:
                raise SystemExit(f"{p.name} {pol}: missing {miss}")
        themes.append(d)
    themes.sort(key=lambda t: t["index"])
    if [t["index"] for t in themes] != list(range(len(themes))):
        raise SystemExit("theme indexes must be 0..n-1")
    if len(themes[0]["name"]) > 15:
        raise SystemExit("theme name too long")
    return themes


def defaults():
    txt = (ROOT / "fw" / "theme.h").read_text()
    return {k: int(re.search(r"#define TH_DEF_" + v + r"\s+(0x[0-9A-Fa-f]+)u", txt).group(1), 16)
            for k, v in DEFAULT_KEYS.items() if re.search(r"#define TH_DEF_" + v + r"\s", txt)}


def emit(themes):
    out = ["/* GENERATED by tools/gen_themes.py from themes/*.json. Do not edit; edit the json and regenerate. */",
           "#ifndef TAU_THEME_DATA_H", "#define TAU_THEME_DATA_H", "",
           "#define TH_THEME_N %du" % len(themes),
           "#define TH_LIGHT_ACC_MAX_L %du   /* Light polarity: accents brighter than this luma are scaled down (th_accent_of) */" % LIGHT_ACC_MAX_L,
           "typedef struct { const char *name; uint8_t bg_luma[2]; uint16_t role[2][TR_COUNT]; } th_theme_t;",
           "static const th_theme_t th_themes[TH_THEME_N] = {"]
    for t in themes:
        out.append('  { "%s", { %du, %du }, {' % (t["name"], t["dark"]["bg_luma"], t["light"]["bg_luma"]))
        for pol in POLS:
            row = ", ".join("[%s] = 0x%04Xu" % (c, snap(t[pol][k])) for k, c in ROLES)
            out.append("    { %s }," % row)
        out.append("  } },")
    out += ["};", "", "#endif", ""]
    return "\n".join(out)


def gamma_report(themes):
    rtl = [int(m) for m in re.findall(r"4'd\d+\s*:\s*cov_weight\s*=\s*5'd(\d+);", gtg.FB.read_text())]
    ident = list(range(15)) + [16]
    accents = [0xF79E, 0x2D40, 0xEEE0, 0xD925, 0x4F5D, 0x6B59]
    rows = []
    for t in themes:
        for pol in POLS:
            d = t[pol]
            fg = [snap(d["text_primary"]), snap(d["text_secondary"]), snap(d["faint"])] + [acc_eff(a, pol) for a in accents]
            bgs = []
            for a in accents:
                bgs += ramp(grad_top(acc_eff(a, pol), d["bg_luma"]), snap(d["bg_bottom"]))[::5]
            bgs += [snap(d["surface"]), snap(d["base"]), snap(d["chrome"])]
            score = []
            for table in (rtl, ident):
                e = n = 0
                for cov in range(1, 15):
                    a = cov / 15.0
                    for f in fg:
                        for b in bgs:
                            for (fv, fm), (bv, _), lw in zip(gtg.channels(f), gtg.channels(b), gtg.LUMA):
                                want = gtg.ideal(fv, fm, bv, fm, a)
                                got = gtg.hw(fv, bv, table[cov], fm)
                                e += lw * ((got - want) / fm) ** 2
                                n += lw
                score.append((e / n) ** 0.5)
            rows.append((t["name"], pol, score[0], score[1]))
    return rows


SAMPLE_ACCENTS = [0xF79E, 0xEEE0, 0xD925, 0x4F5D, 0x6B59, 0x2D40, 0x0843, 0xFFC0]


def check_theme(t, verbose=False):
    """Contrast rules for one theme dict (both polarities); returns a list of problems. Shared with tools/tau_assets.py."""
    bad = []
    for pol in POLS:
        d = t[pol]
        for fgk, bgks, need in RULES:
            worst = 99.0
            for bk in bgks:
                if bk == "ramp":
                    for ac in SAMPLE_ACCENTS:
                        top = grad_top(acc_eff(ac, pol), d["bg_luma"])
                        for c in (top, gtg.mix565(top, snap(d["bg_bottom"]), 20, 40)):
                            worst = min(worst, contrast(snap(d[fgk]), c))
                else:
                    worst = min(worst, contrast(snap(d[fgk]), snap(d[bk])))
            flag = "ok " if worst >= need else "LOW"
            if verbose:
                print(f"  {t['name']:6s} {pol:5s} {fgk:15s} vs {'/'.join(bgks):18s} {worst:5.2f} (need {need}) {flag}")
            if worst < need:
                bad.append(f"{t['name']} {pol}: {fgk} vs {bgks} = {worst:.2f} < {need}")
        for ac in SAMPLE_ACCENTS:      # accent as text/fill against the surface
            e = acc_eff(ac, pol)
            worst = contrast(e, snap(d["surface"]))
            if pol == "light" and worst < 3.0:
                bad.append(f"{t['name']} light: accent 0x{ac:04X} -> 0x{e:04X} vs surface = {worst:.2f} < 3.0")
    return bad


def light_fit(themes):
    """Best 16-entry weight table for the Light polarity (what an RTL polarity bit would select), fitted like gen_text_gamma.py."""
    accents = [0xF79E, 0xEEE0, 0xD925, 0x4F5D, 0x6B59, 0x2D40, 0x0843, 0xFFC0]
    fg, bgs = [], []
    for t in themes:
        d = t["light"]
        fg += [snap(d["text_primary"]), snap(d["text_secondary"]), snap(d["faint"])] + [acc_eff(a, "light") for a in accents]
        for a in accents:
            bgs += ramp(grad_top(acc_eff(a, "light"), d["bg_luma"]), snap(d["bg_bottom"]))[::5]
        bgs += [snap(d["surface"]), snap(d["base"]), snap(d["chrome"])]
    saved = (gtg.FG, gtg.backgrounds)
    gtg.FG, gtg.backgrounds = fg, (lambda: bgs)
    try:
        table, report = gtg.fit()
    finally:
        gtg.FG, gtg.backgrounds = saved
    return table, report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    themes = load()
    bad = []
    dflt = defaults()
    for k, v in dflt.items():
        if snap(themes[0]["dark"][k]) != v:
            bad.append(f"theme 0 dark {k} = 0x{snap(themes[0]['dark'][k]):04X}, firmware default 0x{v:04X}")
    print("contrast (WCAG ratio, worst over sample accents)")
    for t in themes:
        bad += check_theme(t, verbose=True)
    print("\ntext weights: RMS error of the RTL table vs a plain linear blend, per theme/polarity (lower is better)")
    for name, pol, rtl, ident in gamma_report(themes):
        note = "" if rtl <= ident else "   <-- RTL table serves this worse than linear: needs its own weights"
        print(f"  {name:6s} {pol:5s} rtl {rtl:.4f}  linear {ident:.4f}{note}")
    lt, lrep = light_fit(themes)
    print("\nLight polarity: best fitted weights (what an RTL polarity bit would select): " + ", ".join(map(str, lt)))
    print("  fitted RMS " + ", ".join("%.4f" % r[3] for r in lrep if r[0] not in (0, 15)))
    text = emit(themes)
    path = ROOT / "fw" / "theme_data.h"
    if a.check:
        if not path.exists() or path.read_text() != text:
            bad.append("fw/theme_data.h is stale (run tools/gen_themes.py)")
        if bad:
            print("\nFAIL:\n  " + "\n  ".join(bad), file=sys.stderr)
            sys.exit(1)
        print("\ntheme data OK")
    else:
        path.write_text(text)
        print(f"\nwrote {path}" + ("" if not bad else "\nWARN:\n  " + "\n  ".join(bad)))


if __name__ == "__main__":
    main()
