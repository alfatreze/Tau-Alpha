"""Loads a meter manifest (meters/<key>/meter.json) with its factory presets (meters/<key>/presets.json, next to it) merged in as manifest["presets"], the way every tool and test reads a meter (B-969: the
presets are a file of their own, edited without touching the parameters; a meter that still has them inside meter.json, such as the retired Player 1 meters, loads as it always did)."""
import json
from pathlib import Path

def load(path):
    path = Path(path)
    d = json.loads(path.read_text())
    pj = path.with_name("presets.json")
    if pj.exists():
        d["presets"] = json.loads(pj.read_text())["presets"]
    return d
