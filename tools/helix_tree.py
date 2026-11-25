#!/usr/bin/env python3
"""Build the tree every Helix consumer compiles: pristine upstream + Tau's patch + Tau's own files.

third_party/libhelix-mp3/ is byte-for-byte upstream (see UPSTREAM.md). Tau's changes are third_party/libhelix-mp3/tau.patch (a unified diff,
-p1) and the one new file, third_party/libhelix-mp3-tau/pub/mp3_profile.h. The compiled source is the product of those three, assembled here
into <repo>/build/helix-tau/ (git-ignored), with the same layout as the vendored tree (mp3dec.c, real/, pub/).

    python3 tools/helix_tree.py            # prints the path of the up-to-date tree (builds it if needed)
    from helix_tree import helix_tree      # -> pathlib.Path

Rebuilt only when the inputs change (stamp = SHA-256 of the pristine .c/.h files, the patch and the Tau files). Safe to call from parallel
tests: the tree is assembled in a private directory and moved into place with one rename.
"""
import hashlib, os, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRISTINE = ROOT / "third_party/libhelix-mp3"
TAU_FILES = ROOT / "third_party/libhelix-mp3-tau"
PATCH = PRISTINE / "tau.patch"
OUT = ROOT / "build/helix-tau"
STAMP = ".helix-tree-stamp"
SKIP = {"tau.patch", "UPSTREAM.md", "UPSTREAM.sha256"}          # Tau's additions to the vendored directory; they are not part of the compiled tree


def _inputs():
    for base in (PRISTINE, TAU_FILES):
        for p in sorted(base.rglob("*")):
            if p.is_file() and p.name not in SKIP and "projects" not in p.relative_to(base).parts and "testwrap" not in p.relative_to(base).parts:
                yield base, p
    yield PRISTINE, PATCH


def _digest():
    h = hashlib.sha256()
    for base, p in _inputs():
        h.update(str(p.relative_to(base)).encode() + b"\0" + hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def helix_tree():
    want = _digest()
    try:
        if (OUT / STAMP).read_text().strip() == want:
            return OUT
    except OSError:
        pass
    OUT.parent.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="helix-tau.", dir=OUT.parent))
    try:
        for base, p in _inputs():
            if p == PATCH: continue
            d = tmp / p.relative_to(base); d.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(p, d)
        r = subprocess.run(["patch", "-p1", "-s", "--no-backup-if-mismatch", "-d", str(tmp), "-i", str(PATCH)], capture_output=True, text=True)
        if r.returncode:
            sys.exit("tools/helix_tree.py: tau.patch does not apply to the pristine tree:\n" + r.stdout + r.stderr)
        (tmp / STAMP).write_text(want + "\n")
        old = OUT.with_name(OUT.name + ".old.%d" % os.getpid())
        if OUT.exists(): os.rename(OUT, old)
        try:
            os.rename(tmp, OUT)
        except OSError:                      # a parallel caller won the race: its tree is the same, use it
            shutil.rmtree(tmp, ignore_errors=True)
        shutil.rmtree(old, ignore_errors=True)
    finally:
        if tmp.exists(): shutil.rmtree(tmp, ignore_errors=True)
    return OUT


if __name__ == "__main__":
    print(helix_tree())
