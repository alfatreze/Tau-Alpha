#!/usr/bin/env python3
"""Build the Tau meter lab (docs/METER_MODULE_SPEC.md section 7): ONE self-contained html file with the registry, the themes, the real
accent palette and every meter module inlined, so it can be opened from disk, published as an artifact or embedded by Tau Omega as a
versioned vendor drop (record its SHA-256).

    python3 tools/meters/preview/build.py --out work/meters/meter_lab.html
    python3 tools/meters/preview/build.py --write-fixtures     # refresh fixtures/theme_vectors.json from the Python mirrors
    python3 tools/meters/preview/build.py --check              # fixtures fresh, page builds, no unresolved placeholders
"""
import argparse, hashlib, json, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_themes as gt  # noqa: E402

SCRIPTS = ["tau_core.js", "tau_fb.js", "tau_theme.js", "tau_audio.js", "tau_live.js", "tau_ballistics.js", "tau_cost.js", "tau_run.js", "tau_controls.js"]


def data():
    themes = [{"name": t["name"], "dark": t["dark"], "light": t["light"]} for t in gt.load()]
    pal = [{"name": n, "c565": c} for n, c in gt.palette()]
    schema = json.loads((ROOT / "tools" / "meters_schema.json").read_text())
    return {"themes": themes, "palette": pal, "lightAccMaxL": gt.LIGHT_ACC_MAX_L, "schema": schema}


def html():
    d = data()
    js = "\n".join((HERE / s).read_text() for s in SCRIPTS)
    for p in sorted((HERE / "meters").glob("*.js")):
        js += "\n" + p.read_text()
    page = (HERE / "index.html").read_text()
    if "/*DATA*/" not in page or "/*SCRIPTS*/" not in page:
        raise SystemExit("index.html placeholders missing")
    return page.replace("/*DATA*/", json.dumps(d, separators=(",", ":")).replace("</", "<\\/")).replace("/*SCRIPTS*/", js.replace("</script", "<\\/script"))


def theme_vectors():
    d = data()
    out = {"lightAccMaxL": gt.LIGHT_ACC_MAX_L, "accent": [], "grad": []}
    for i, (n, c) in enumerate(gt.palette()):
        out["accent"].append({"idx": i, "c565": c, "dark": gt.acc_eff(c, "dark"), "light": gt.acc_eff(c, "light")})
    for ti, t in enumerate(gt.load()):
        for pol in gt.POLS:
            dd = t[pol]
            for ai in (0, 1, 5, 8, 12, 17):
                acc = gt.acc_eff(gt.palette()[ai][1], pol)
                top = gt.grad_top(acc, dd["bg_luma"])
                bottom = gt.snap(dd["bg_bottom"])
                out["grad"].append({"theme": ti, "light": pol == "light", "accent": ai, "top": top, "bottom": bottom,
                                    "rows": {str(y): ramp_at(top, bottom, y) for y in (0, 1, 2, 3, 90, 180, 270, 358, 359)}})
    return out


def ramp_at(top, bot, y, fb_h=360):
    """Independent Python statement of ui_grad_at() (same maths as sim/test_theme.py checks against the C)."""
    thr, den = (1, 5, 3, 7)[y & 3], fb_h - 1
    rem = den - y
    tl, bl = [(top >> 11) & 31, (top >> 5) & 63, top & 31], [(bot >> 11) & 31, (bot >> 5) & 63, bot & 31]
    out = []
    for a, b in zip(tl, bl):
        if a >= b:
            num = (a - b) * rem
            base = num // den + int((num % den) * 8 > thr * den)
            out.append(b + base)
        else:
            num = (b - a) * y
            base = num // den + int((num % den) * 8 > thr * den)
            out.append(a + base)
    return out[0] << 11 | out[1] << 5 | out[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--write-fixtures", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    fx = HERE / "fixtures" / "theme_vectors.json"
    text = json.dumps(theme_vectors(), separators=(",", ":")) + "\n"
    if a.write_fixtures:
        fx.parent.mkdir(exist_ok=True)
        fx.write_text(text)
        print("wrote", fx)
    if a.check:
        if not fx.exists() or fx.read_text() != text:
            sys.exit("fixtures/theme_vectors.json is stale; run build.py --write-fixtures")
        page = html()
        print("meter lab builds (%d bytes), fixtures fresh" % len(page))
    if a.out:
        out = Path(a.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        page = html()
        out.write_text(page)
        print("wrote %s (%d bytes, sha256 %s)" % (out, len(page), hashlib.sha256(page.encode()).hexdigest()[:16]))


if __name__ == "__main__":
    main()
