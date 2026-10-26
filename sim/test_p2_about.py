#!/usr/bin/env python3
"""The About page rows (fw2/cores/about_core.h) against independent statements of what a person should read in each state; mutants must be caught."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (("  " + extra) if (extra and not ok) else ""))
    if not ok: fails += 1
def run(scen, bug=0):
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "h"
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-o", str(exe), str(ROOT / "sim/p2_about_harness.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        out = subprocess.run([str(exe), scen], capture_output=True, text=True).stdout.strip().split("\n")
    rows = {}; heads = []
    for l in out[1:]:
        k, lab, val = l.split("|", 2)
        if k == "0": heads.append(lab)
        elif k == "1": rows[lab] = val
        else: rows.setdefault("_text", []).append(val)
    return int(out[0].split()[1]), heads, rows
n, heads, a = run("a")
check("stable release: version, build RELEASE, channel STABLE, hardware as the bitstream's id", a["VERSION"] == "0.7.0" and a["BUILD"] == "RELEASE" and a["CHANNEL"] == "STABLE" and a["HARDWARE"] == "4D50331B")
check("library in words with its counts", a["LIBRARY"] == "143 TRACKS" and a["ALBUMS"] == "12" and a["ARTISTS"] == "9")
check("playing now: the format, ReplayGain, EQ, normal speed with no remark, volume", a["FORMAT"] == "MP3 128K 44.1K STEREO" and a["REPLAYGAIN"] == "TRACK" and a["EQUALISER"] == "WARM" and a["SPEED"] == "1.00X" and a["VOLUME"] == "73%")
check("content and health read as words (nothing wrong: no dropouts, no errors)", a["THEMES"] == "2 CUSTOM" and a["EQ PRESETS"] == "8 BUILT IN, 3 YOURS" and a["METERS"] == "7 OF 7 LOADED" and a["SETTINGS"] == "OK" and a["AUDIO"] == "NO DROPOUTS" and a["CARD READS"] == "NO ERRORS" and a["RUNNING FOR"] == "30 SEC")
check("look and feel", a["THEME"] == "TAU" and a["MODE"] == "DARK" and a["COLOUR"] == "ORANGE" and a["FONT"] == "GROTESK BOLD" and a["METER"] == "WINAMP BARS" and a["ALBUM ART"] == "ON")
check("the sections in order", heads == ["TAU", "PLAYING NOW", "LIBRARY", "LOOK AND FEEL", "ON THE CARD", "HEALTH", "CREDITS AND LICENCES", "HELP"], str(heads))
long_ = [(sc, x) for sc in "abcd" for x in run(sc)[2].get("_text", []) if len(x) > 40]
check("no line of text is longer than 40 characters (DEV 192: 43 and 44-character lines were cut at the screen edge)", not long_, str(long_))
widest = max(len(k) + len(v) for sc in "abcd" for k, v in run(sc)[2].items() if k != "_text")
check(f"no label and value together longer than 44 characters (widest {widest})", widest <= 44)
check("credits and help lines present", len(a.get("_text", [])) == 7 and any("HELIX" in t for t in a["_text"]) and any("SELECT+START" in t for t in a["_text"]))
nb, hb, b = run("b")
check("dev build: version without the hash, the build hash, channel DEV", b["VERSION"] == "0.6.1-dev.191" and b["BUILD"] == "f2c7350c" and b["CHANNEL"] == "DEV")
check("no library: says how to get one; no albums/artists rows; no PLAYING NOW section", b["LIBRARY"] == "NOT LOADED: SYNC WITH TAU OMEGA" and "ALBUMS" not in b and "ARTISTS" not in b and "PLAYING NOW" not in hb)
check("problems in words: carried over settings, 3 dropouts, 1 error, 1 h 2 min, light mode, 5 of 7 meters", b["SETTINGS"] == "CARRIED OVER FROM AN OLDER VERSION" and b["AUDIO"] == "3 DROPOUTS SINCE START" and b["CARD READS"] == "1 ERROR (RETRIED)" and b["RUNNING FOR"] == "1 H 2 MIN" and b["MODE"] == "LIGHT" and b["METERS"] == "5 OF 7 LOADED: SOME FAILED")
nc, hc, c = run("c")
check("preview channel; failed library with its code; stretch 1.25x keeps pitch; album gain; 1 dropout (singular); 12 min", c["CHANNEL"] == "PREVIEW" and c["LIBRARY"] == "CANNOT BE READ (CODE 5)" and c["SPEED"] == "1.25X PITCH KEPT" and c["REPLAYGAIN"] == "ALBUM" and c["AUDIO"] == "1 DROPOUT SINCE START" and c["RUNNING FOR"] == "12 MIN")
nd, hd, d = run("d")
check("one track is singular; 2.00x pitch-changing speed; ReplayGain off; no EQ name reads OFF; 59 s", d["LIBRARY"] == "1 TRACK" and d["SPEED"] == "2.00X PITCH CHANGES" and d["REPLAYGAIN"] == "OFF" and d["EQUALISER"] == "OFF" and d["RUNNING FOR"] == "59 SEC")
nm, hm, m = run("b", 1)
check("mutant: the dropout count is never shown (caught)", m["AUDIO"] == "NO DROPOUTS" and b["AUDIO"] != m["AUDIO"])
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
