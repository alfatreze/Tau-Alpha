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
    {"id": "settings-home", "status": "model", "file": "settings-home.framebuffer.png",
     "note": "Settings home (Appearance/Audio/Playback) over the now-playing fixture; labels parsed from fw/settingsui.inc."},
    {"id": "settings-appearance", "status": "model", "file": "settings-appearance.framebuffer.png",
     "note": "Settings Appearance page, sample values; labels parsed from fw/settingsui.inc."},
    {"id": "settings-audio", "status": "model", "file": "settings-audio.framebuffer.png",
     "note": "Settings Audio page, sample values."},
    {"id": "settings-playback", "status": "model", "file": "settings-playback.framebuffer.png",
     "note": "Settings Playback page, last row selected, sample values."},
    {"id": "settings-colour", "status": "model", "file": "settings-colour.framebuffer.png",
     "note": "Colour choice list: a swatch circle per theme colour, ring on the active one."},
    {"id": "settings-meter", "status": "model", "file": "settings-meter.framebuffer.png",
     "note": "Meter choice list: radio circle, grey placeholder thumbnail and name per meter, scrolled."},
    {"id": "settings-eq", "status": "model", "file": "settings-eq.framebuffer.png",
     "note": "Equalizer choice list with the active preset marked."},
    {"id": "settings-repeat", "status": "model", "file": "settings-repeat.framebuffer.png",
     "note": "Repeat choice list."},
    {"id": "settings-blank", "status": "model", "file": "settings-blank.framebuffer.png",
     "note": "Screen-blank choice list."},
    {"id": "settings-diagnostics", "status": "model", "file": "settings-diagnostics.framebuffer.png",
     "note": "Diagnostics group page (developer builds), one row: Info."},
    {"id": "settings-info", "status": "model", "file": "settings-info.framebuffer.png",
     "note": "Diagnostics Info page: 11 read-only live values, sample data; labels parsed from fw/settingsui.inc."},
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
    {"id": "sdram-cpu-diagnostic-running", "status": "model",
     "file": "sdram-cpu-diagnostic-running.framebuffer.png",
     "note": "Developer-only Phase 2 uncached CPU-window test before memory traffic."},
    {"id": "sdram-cpu-diagnostic-pass", "status": "model",
     "file": "sdram-cpu-diagnostic-pass.framebuffer.png",
     "note": "Developer-only CPU load/store and byte-lane diagnostic success state."},
    {"id": "sdram-cpu-diagnostic-fail", "status": "model",
     "file": "sdram-cpu-diagnostic-fail.framebuffer.png",
     "note": "Developer-only CPU-window diagnostic example failure with byte address."},
    {"id": "sdram-cpu-diagnostic-version-mismatch", "status": "model",
     "file": "sdram-cpu-diagnostic-version-mismatch.framebuffer.png",
     "note": "Developer-only CPU-window boot interlock failure state."},
    {"id": "sdram-cpu-preflight-running", "status": "model",
     "file": "sdram-cpu-preflight-running.framebuffer.png",
     "note": "Mailbox preflight through the shared mux/CDC bridge before CPU-window access."},
    {"id": "sdram-cpu-preflight-fail", "status": "model",
     "file": "sdram-cpu-preflight-fail.framebuffer.png",
     "note": "Preflight failure that explicitly confirms no CPU-window access was attempted."},
    {"id": "sdram-cpu-preflight-readback-fail", "status": "model",
     "file": "sdram-cpu-preflight-readback-fail.framebuffer.png",
     "note": "Firmware-only A-060 discriminator: mailbox write/read passed, mapped CPU read of that same word failed."},
    {"id": "sdram-cpu-probe-bar", "status": "model",
     "file": "sdram-cpu-probe-bar.framebuffer.png",
     "note": "Phase-2-only scanout overlay: persistent first-request handoff evidence after a CPU stall."},
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
