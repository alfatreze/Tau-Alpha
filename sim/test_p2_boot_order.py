#!/usr/bin/env python3
"""Boot order of fw2/app/main_player.c (a source check: main() cannot run on the host). The saved settings (theme, mode, colour, font, meter presets, the last played track) are applied by
appearance_set(), and theme_init() resets theme, mode and colour to the defaults: if theme_init() runs AFTER the restore, the saved colour is thrown away at every boot and the first settings service publishes the
default over the saved value (found on TAU DEV 177: the colour changed in Core Settings showed 7 there and did not come back). Rules: theme_init() before the first appearance_set(); meters_init() before it (the
presets); assets_load()/hand_out_assets() (file themes, presets) before settings_init(); settings_init() before appearance_set() before mp3play_last_restore(). Mutant: theme_init() moved after the restore."""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
src = (ROOT / "fw2/app/main_player.c").read_text()
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def order(text):
    a = text.index("    meters_init();"); boot = text[a:text.index("for (;;)", a)]
    pos = {k: boot.find(k) for k in ("meters_init();", "theme_init();", "assets_load();", "hand_out_assets();", "settings_init(sv);", "appearance_set(sv);", "mp3play_last_restore(")}
    return pos
def ok_order(text):
    p = order(text)
    return all(v >= 0 for v in p.values()) and p["meters_init();"] < p["theme_init();"] < p["appearance_set(sv);"] and p["assets_load();"] < p["hand_out_assets();"] < p["settings_init(sv);"] < p["appearance_set(sv);"] < p["mp3play_last_restore("]
check("theme_init() runs before the saved settings are applied (and meters_init, the assets, settings_init and the restore are in order)", ok_order(src))
check("theme_init() is called once at boot", len(re.findall(r"\btheme_init\(\);", src)) == 1)
mut = re.sub(r"    theme_init\(\);[^\n]*\n", "", src, count=1).replace("    views_set_diag(&diag_pages);", "    theme_init();\n    views_set_diag(&diag_pages);", 1)
check("mutant (theme_init after the restore) is caught", not ok_order(mut))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
