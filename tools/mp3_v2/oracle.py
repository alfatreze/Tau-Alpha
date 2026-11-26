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

# Stage build (WP-06): an instrumented COPY of minimp3.h (three hook calls inserted at anchors that must each occur exactly once); the vendored file is untouched.
STAGE_ANCHORS = [
    "        int n_long_bands = (gr_info->mixed_block_flag ? 2 : 0) << (int)(HDR_GET_MY_SAMPLE_RATE(h->header) == 2);\n",
    "        L3_change_sign(s->grbuf[ch]);\n",
    "                mp3d_synth_granule(dec->qmf_state, scratch.grbuf[0], 18, info->channels, pcm, scratch.syn[0]);\n",
]
HOOK1 = "        ORACLE_HOOK_SPECTRUM(s->grbuf[ch], gr_info->sfbtab, gr_info->n_long_sfb, gr_info->n_short_sfb, gr_info->block_type, n_long_bands, h->mdct_overlap[ch], ch);\n"
HOOK2 = "        ORACLE_HOOK_SUBBAND(s->grbuf[ch], ch);\n"
HOOK3 = "                ORACLE_HOOK_GRANULE(dec->qmf_state, info->channels);\n"

def instrument(text):
    """instrumented text: hook 1 after the n_long_bands line (spectrum before reorder), hook 2 after change_sign (subband samples), hook 3 before the synthesis call"""
    for k, a in enumerate(STAGE_ANCHORS):
        if text.count(a) != 1:
            raise SystemExit("stage anchor %d occurs %d times in minimp3.h (expected exactly once)" % (k, text.count(a)))
    text = text.replace(STAGE_ANCHORS[0], STAGE_ANCHORS[0] + HOOK1)
    text = text.replace(STAGE_ANCHORS[1], STAGE_ANCHORS[1] + HOOK2)
    text = text.replace(STAGE_ANCHORS[2], HOOK3 + STAGE_ANCHORS[2])
    return text

def build_stage(out_dir=None):
    out = Path(out_dir) if out_dir else OUT
    sdir = out / "stage"; sdir.mkdir(parents=True, exist_ok=True)
    (sdir / "minimp3.h").write_text(instrument((HDR / "minimp3.h").read_text()))
    exe = out / "oracle_stage"
    stamp = out / "oracle_stage.stamp"
    key = hashlib.sha256(SRC.read_bytes() + (HDR / "minimp3.h").read_bytes() + Path(__file__).read_bytes()).hexdigest()
    if exe.exists() and stamp.exists() and stamp.read_text() == key:
        return exe
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, *_sdk_args(), *FLAGS, "-DORACLE_STAGE_DUMP", "-I", str(sdir), "-o", str(exe), str(SRC), "-lm"], check=True)
    stamp.write_text(key)
    return exe

def decode(exe, mp3, pcm=None, f32=None, frames=None, stage=None):
    cmd = [str(exe), str(mp3)]
    if stage: cmd += ["--stage", str(stage)]
    if pcm: cmd += ["--pcm", str(pcm)]
    if f32: cmd += ["--f32", str(f32)]
    if frames is not None: cmd += ["--frames", str(frames)]
    r = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return json.loads(r.stdout.strip().splitlines()[-1])

if __name__ == "__main__":
    exe = build()
    r = subprocess.run([str(exe), *sys.argv[1:]])
    sys.exit(r.returncode)
