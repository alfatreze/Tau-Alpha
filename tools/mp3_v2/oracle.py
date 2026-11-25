#!/usr/bin/env python3
"""Build and run the host float oracle (tools/mp3_v2/oracle.c around the PRISTINE third_party/minimp3/minimp3.h).
Library use:  import oracle; exe = oracle.build(); info = oracle.decode(exe, "x.mp3", pcm="x.s16", f32="x.f32")
CLI:          oracle.py <in.mp3> [--pcm out] [--f32 out] [--frames N]    (prints the JSON summary line)
The build goes to build/mp3_v2/ (git-ignored) and is rebuilt only when oracle.c or minimp3.h changed. Host C needs the older SDK on this Mac
(the default SDK's libSystem.tbd is broken): SDKROOT is taken from the environment, else the known-good MacOSX26.5 SDK when present."""
import hashlib, json, os, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
HDR = ROOT / "third_party/minimp3"
SRC = Path(__file__).with_name("oracle.c")
OUT = ROOT / "build/mp3_v2"
FLAGS = ["-O2", "-ffp-contract=off", "-DMINIMP3_NO_SIMD", "-DMINIMP3_ONLY_MP3", "-DMINIMP3_FLOAT_OUTPUT"]
KNOWN_SDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk"

def _sdk_args():
    sdk = os.environ.get("SDKROOT") or (KNOWN_SDK if Path(KNOWN_SDK).is_dir() else "")
    return ["-isysroot", sdk] if sdk and sys.platform == "darwin" else []

def build(out_dir=None, extra=(), name="oracle"):
    out = Path(out_dir) if out_dir else OUT
    out.mkdir(parents=True, exist_ok=True)
    exe = out / name
    stamp = out / (name + ".stamp")
    key = hashlib.sha256(SRC.read_bytes() + (HDR / "minimp3.h").read_bytes() + " ".join(FLAGS + list(extra)).encode()).hexdigest()
    if exe.exists() and stamp.exists() and stamp.read_text() == key:
        return exe
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, *_sdk_args(), *FLAGS, *extra, "-I", str(HDR), "-o", str(exe), str(SRC), "-lm"], check=True)
    stamp.write_text(key)
    return exe

def decode(exe, mp3, pcm=None, f32=None, frames=None):
    cmd = [str(exe), str(mp3)]
    if pcm: cmd += ["--pcm", str(pcm)]
    if f32: cmd += ["--f32", str(f32)]
    if frames is not None: cmd += ["--frames", str(frames)]
    r = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return json.loads(r.stdout.strip().splitlines()[-1])

if __name__ == "__main__":
    exe = build()
    r = subprocess.run([str(exe), *sys.argv[1:]])
    sys.exit(r.returncode)
