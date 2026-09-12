#!/usr/bin/env python3
"""Generate Tau's named UI-state review set and coverage report.

The loading frame is decoded from the actual packaged asset. Existing upstream
screenshots are retained as explicitly labelled references until their states
are moved onto the production framebuffer harness. Missing states remain in
the manifest as pending, so visual coverage cannot be mistaken for complete.
"""

import html
import json
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "work/previews"

STATES = [
    {"id": "boot-loading", "status": "exact", "file": "boot-loading.framebuffer.png",
     "note": "Decoded from the shipped TAU1 RLE/RGB565 asset."},
    {"id": "empty-library", "status": "reference", "source": "docs/idle_preview.png",
     "file": "empty-library.reference.png"},
    {"id": "playlist-error", "status": "reference", "source": "docs/idle_preview_err.png",
     "file": "playlist-error.reference.png"},
    {"id": "now-playing", "status": "reference", "source": "docs/screenshot.png",
     "file": "now-playing.reference.png"},
    {"id": "playlist-browser", "status": "reference", "source": "docs/playlist_browser.png",
     "file": "playlist-browser.reference.png"},
    {"id": "paused", "status": "pending"},
    {"id": "stopped", "status": "pending"},
    {"id": "seeking", "status": "pending"},
    {"id": "now-playing-no-art", "status": "pending"},
    {"id": "metadata-long", "status": "pending"},
    {"id": "metadata-missing", "status": "pending"},
    {"id": "toast", "status": "pending"},
    {"id": "visualizer-bars", "status": "pending"},
    {"id": "visualizer-waterfall", "status": "pending"},
    {"id": "visualizer-levels", "status": "pending"},
    {"id": "visualizer-phase-scope", "status": "pending"},
    {"id": "visualizer-oscilloscope", "status": "pending"},
    {"id": "visualizer-vu", "status": "pending"},
    {"id": "visualizer-waveform", "status": "pending"},
    {"id": "visualizer-mirrored-bars", "status": "pending"},
    {"id": "visualizer-peak-dots", "status": "pending"},
    {"id": "visualizer-magic-eye", "status": "pending"},
    {"id": "visualizer-spectrum", "status": "pending"},
    {"id": "battery-full", "status": "pending"},
    {"id": "battery-medium", "status": "pending"},
    {"id": "battery-low", "status": "pending"},
    {"id": "battery-critical", "status": "pending"},
    {"id": "battery-charging", "status": "pending"},
    {"id": "battery-unavailable", "status": "pending"},
]


def generate():
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        sys.executable, str(ROOT / "tools/capture_splash_frame.py"),
        "--output", str(OUT / "boot-loading.framebuffer.png"),
    ], check=True)

    for state in STATES:
        if state["status"] != "reference":
            continue
        source = ROOT / state["source"]
        if not source.is_file():
            raise SystemExit(f"missing visual reference: {source}")
        shutil.copy2(source, OUT / state["file"])

    manifest = {
        "native_framebuffer": {"width": 400, "height": 360, "format": "RGB565"},
        "fidelity": {
            "exact": "Derived from a packaged asset or production framebuffer path.",
            "reference": "Existing visual reference; must be replaced by an exact fixture.",
            "pending": "Required state with no capture yet.",
        },
        "states": STATES,
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                       encoding="utf-8")

    cards = []
    for state in STATES:
        image = (f'<img src="{html.escape(state["file"])}" alt="">'
                 if state.get("file") else '<div class="pending">Pending</div>')
        note = html.escape(state.get("note", ""))
        cards.append(
            f'<article class="{state["status"]}">{image}'
            f'<h2>{html.escape(state["id"])}</h2>'
            f'<p>{state["status"]}{": " + note if note else ""}</p></article>')
    page = """<!doctype html><meta charset="utf-8"><title>Tau visual review</title>
<style>
body{background:#111419;color:#e9eef2;font:14px system-ui;margin:24px}
h1{font-size:24px}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:18px}
article{background:#1b2027;border:1px solid #343c47;border-radius:10px;padding:12px}
article.exact{border-color:#5ca985}article.reference{border-color:#b18b4e}article.pending{opacity:.62}
img,.pending{display:block;width:100%;aspect-ratio:10/9;object-fit:contain;background:#07090b;image-rendering:pixelated}
.pending{display:grid;place-items:center}h2{font-size:16px;margin:10px 0 4px}p{margin:0;color:#aeb8c3}
</style><h1>Tau UI state review</h1><p>Green: exact. Amber: legacy reference. Dim: pending.</p>
<div class="grid">""" + "".join(cards) + "</div>"
    (OUT / "index.html").write_text(page, encoding="utf-8")

    counts = {kind: sum(s["status"] == kind for s in STATES)
              for kind in ("exact", "reference", "pending")}
    print(f'wrote {OUT / "index.html"}')
    print("visual states: " + ", ".join(f"{k}={v}" for k, v in counts.items()))


if __name__ == "__main__":
    generate()
