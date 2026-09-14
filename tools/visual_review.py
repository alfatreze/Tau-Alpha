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
    {"id": "empty-library", "status": "model", "file": "empty-library.framebuffer.png",
     "note": "Production UI-command model using the FPGA font ROM and RGB565 rules."},
    {"id": "playlist-error", "status": "model", "file": "playlist-error.framebuffer.png",
     "note": "Production UI-command model using the FPGA font ROM and RGB565 rules."},
    {"id": "now-playing", "status": "model", "file": "now-playing.framebuffer.png",
     "note": "No-art production UI model at a deterministic playback instant."},
    {"id": "playlist-browser", "status": "model", "file": "playlist-browser.framebuffer.png",
     "note": "Playlist overlay model over the deterministic now-playing fixture."},
    {"id": "paused", "status": "model", "file": "paused.framebuffer.png",
     "note": "Frozen pause-breath instant over the no-art playback fixture."},
    {"id": "stopped", "status": "model", "file": "stopped.framebuffer.png",
     "note": "Stopped transport and dimmed meter over the no-art playback fixture."},
    {"id": "seeking", "status": "model", "file": "seeking.framebuffer.png",
     "note": "Frozen seek feedback toast and progress position over playback."},
    {"id": "now-playing-no-art", "status": "pending"},
    {"id": "metadata-long", "status": "model", "file": "metadata-long.framebuffer.png",
     "note": "Initial frame of title/artist marquee input at production clipping bounds."},
    {"id": "metadata-missing", "status": "model", "file": "metadata-missing.framebuffer.png",
     "note": "No-tag filename fallback with absent optional metadata."},
    {"id": "toast", "status": "model", "file": "toast.framebuffer.png",
     "note": "Fresh generic toast at its full-brightness frame."},
    {"id": "sdram-diagnostic-running", "status": "model",
     "file": "sdram-diagnostic-running.framebuffer.png",
     "note": "Developer-only Phase 1 mailbox diagnostic while tests are active."},
    {"id": "sdram-diagnostic-pass", "status": "model",
     "file": "sdram-diagnostic-pass.framebuffer.png",
     "note": "Developer-only Phase 1 mailbox diagnostic success state."},
    {"id": "sdram-diagnostic-fail", "status": "model",
     "file": "sdram-diagnostic-fail.framebuffer.png",
     "note": "Developer-only Phase 1 mailbox diagnostic example failure state."},
    {"id": "sdram-diagnostic-version-mismatch", "status": "model",
     "file": "sdram-diagnostic-version-mismatch.framebuffer.png",
     "note": "Developer-only boot interlock failure; shows actual and expected RTL revisions."},
    *[{"id": f"visualizer-{name}", "status": "model",
       "file": f"visualizer-{name}.framebuffer.png",
       "note": "Frozen deterministic instance of the production visualizer family."}
      for name in ("bars", "waterfall", "levels", "phase-scope", "oscilloscope",
                   "waveform", "mirrored-bars", "peak-dots", "magic-eye", "spectrum", "vu")],
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
        if state["status"] == "model":
            subprocess.run([
                sys.executable, str(ROOT / "tools/ui_snapshot_renderer.py"), state["id"],
                "--output", str(OUT / state["file"]),
            ], check=True)
            continue
        if state["status"] != "reference":
            continue
        source = ROOT / state["source"]
        if not source.is_file():
            raise SystemExit(f"missing visual reference: {source}")
        shutil.copy2(source, OUT / state["file"])

    manifest = {
        "native_framebuffer": {"width": 400, "height": 360, "format": "RGB565"},
        "fidelity": {
            "exact": "Decoded from a packaged asset or production framebuffer path.",
            "model": "Reproduces production framebuffer commands from source; not a device capture.",
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
              for kind in ("exact", "model", "reference", "pending")}
    print(f'wrote {OUT / "index.html"}')
    print("visual states: " + ", ".join(f"{k}={v}" for k, v in counts.items()))


if __name__ == "__main__":
    generate()
