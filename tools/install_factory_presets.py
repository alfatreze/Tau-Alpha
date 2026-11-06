#!/usr/bin/env python3
"""Writes the factory meter presets (tools/player2/mpreset.py: one index and one file per preset for every Player 2 meter) onto a card, where the firmware looks for them: <media root>/tau-presets/<meter id>/ (B-967).
The media root is the one the card's library was built for (the `root` of Assets/<platform>/common/tau-library.tdb, normally /Assets/tau/common/), so the same files serve the release and every developer core.

  python3 tools/install_factory_presets.py --card /Volumes/Pock              dry run: what would be written
  python3 tools/install_factory_presets.py --card /Volumes/Pock --yes        write, then verify every file's SHA-256
  options: --force   overwrite a file that is already there and differs (default: refuse: Omega's sync may have written a newer set)
           --remove  delete the tau-presets folder (the firmware then uses the presets inside the packs again)
The card is not ejected: eject it yourself (or use tools/install_dev_core.py, which does)."""
import argparse, hashlib, json, shutil, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import tau_library as tl
import mpreset as mp

def media_root(card):
    """(the root string of the card's library, the folder it names on the card)"""
    for plat in ("tau", "tau_dev"):                                    # the platforms of the Tau cores (the Pocket holds other cores' libraries too: Assets/mp3player/common was found first by a plain search)
        f = Path(card, "Assets", plat, "common", "tau-library.tdb")
        if not f.is_file(): continue
        root = tl.parse(f.read_bytes()).root
        d = Path(card) / root.strip("/")
        if d.is_dir(): return root, d
    return None, None

def factory(keys=()):
    out = {}
    for mf in sorted((ROOT / "meters").glob("*/meter.json")):
        d = json.loads(mf.read_text())
        if "p2" not in d or (keys and d["key"] not in keys): continue
        out.update(mp.factory_files(d))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--card", type=Path, required=True); ap.add_argument("--yes", action="store_true"); ap.add_argument("--force", action="store_true"); ap.add_argument("--remove", action="store_true")
    ap.add_argument("keys", nargs="*", help="meter keys (default: every Player 2 meter)")
    a = ap.parse_args(argv)
    root, common = media_root(a.card)
    if not common: print("no library (Assets/*/common/tau-library.tdb) on the card: the firmware needs it to know the media root"); return 1
    dest = common / "tau-presets"
    print("media root %s -> %s" % (root, dest) + ("" if a.yes else "   (dry run: nothing will be written; --yes writes)"))
    if a.remove:
        print("remove %s" % dest)
        if a.yes and dest.exists(): shutil.rmtree(dest)
        return 0
    files = factory(a.keys); clash = []
    for rel, data in files.items():
        f = dest / rel
        if f.exists() and f.read_bytes() != data: clash.append(rel)
    new = sum(1 for rel in files if not (dest / rel).exists()); same = sum(1 for rel, data in files.items() if (dest / rel).exists() and (dest / rel).read_bytes() == data)
    print("%d files (%d bytes): %d new, %d already there and identical, %d there and different%s" % (len(files), sum(len(v) for v in files.values()), new, same, len(clash), (": " + ", ".join(clash[:6])) if clash else ""))
    if clash and not a.force: print("refusing to overwrite (a newer set from Omega?): use --force"); return 1
    if not a.yes: return 0
    for rel, data in files.items():
        f = dest / rel; f.parent.mkdir(parents=True, exist_ok=True); f.write_bytes(data)
    bad = [rel for rel, data in files.items() if hashlib.sha256((dest / rel).read_bytes()).digest() != hashlib.sha256(data).digest()]
    if bad: print("VERIFY FAILED:", bad[:5]); return 1
    print("written and verified (%d files)" % len(files)); return 0

if __name__ == "__main__":
    sys.exit(main())
