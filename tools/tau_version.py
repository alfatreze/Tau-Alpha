#!/usr/bin/env python3
"""The full version a Tau ROM shows on its splash and on Info > FIRMWARE (owner request, 2026-10-08: "the version is often wrong or not
detailed enough").

The firmware reserves a 48-byte field `TAUVER:<version>` (fw/player.c `tau_ver_field`), built with the plain `APP_VER` ("0.6.0"). The
packagers stamp the exact build into the packaged ROM, after the build, so every package says what it is no matter how its ROM was built:

    <SemVer>+<git commit>[.dirty]      e.g. 0.6.0-preview.1+f2d3373, 0.6.0-dev.385+f2d3373.dirty, 0.6.0+f2d3373

The SemVer part is also written into the package's core.json `version`, so the Pocket's Select Core row, the splash and Info agree.
Safe to patch: the cold image is bound to the ROM by a layout-id word and its own body CRC, not by the ROM's other bytes.

  python3 tools/tau_version.py read  ROM
  python3 tools/tau_version.py stamp ROM VERSION        (VERSION without the +commit; the commit is added from git)
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARK = b"TAUVER:"
FIELD = 48                                    # bytes including the marker; the text is NUL-terminated inside it
MAX_TEXT = FIELD - len(MARK) - 1              # 40 characters
SEMVER = re.compile(r"\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?")


class VersionError(Exception):
    pass


def _find(rom):
    i = rom.find(MARK)
    if i < 0:
        raise VersionError("the ROM has no TAUVER field (built before 2026-10-08): it cannot carry its full version")
    if rom.find(MARK, i + 1) >= 0:
        raise VersionError("the ROM has more than one TAUVER field")
    if len(rom) < i + FIELD:
        raise VersionError("the TAUVER field runs past the end of the ROM")
    return i


def read(rom):
    """The version text in the ROM's field, or None for a ROM built before the field existed."""
    try:
        i = _find(rom)
    except VersionError:
        return None
    return rom[i + len(MARK):i + FIELD].split(b"\0", 1)[0].decode("ascii", "replace")


def git_build(root=ROOT):
    c = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=root, capture_output=True, text=True).stdout.strip()
    d = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no", "--", ".", ":(exclude)dist", ":(exclude)release"],
                       cwd=root, capture_output=True, text=True).stdout.strip()
    if not c:
        raise VersionError("cannot read the git commit")
    return c + (".dirty" if d else "")


def full_version(semver, build=None):
    if not SEMVER.fullmatch(semver):
        raise VersionError(f"not a version: {semver!r} (expected X.Y.Z or X.Y.Z-label.N)")
    text = f"{semver}+{build}" if build else semver
    if len(text) > MAX_TEXT:
        raise VersionError(f"{text!r} is {len(text)} characters; the ROM field holds {MAX_TEXT}")
    if not re.fullmatch(r"[0-9A-Za-z.+-]+", text):
        raise VersionError(f"{text!r}: only letters, digits, '.', '-' and '+' (the device font draws them)")
    return text


def stamp(rom, text):
    """The ROM bytes with `text` in the TAUVER field (rest of the field cleared)."""
    i = _find(rom)
    body = text.encode("ascii")
    if len(body) > MAX_TEXT:
        raise VersionError(f"{text!r} does not fit the {MAX_TEXT}-character field")
    out = bytearray(rom)
    out[i + len(MARK):i + FIELD] = body + b"\0" * (FIELD - len(MARK) - len(body))
    return bytes(out)


def stamp_file(path, semver, root=ROOT):
    """Stamp `semver+<commit>` into the ROM at `path`; returns the text."""
    text = full_version(semver, git_build(root))
    p = Path(path)
    p.write_bytes(stamp(p.read_bytes(), text))
    if read(p.read_bytes()) != text:
        raise VersionError(f"{p}: the stamp did not read back")
    return text


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("read"); r.add_argument("rom", type=Path)
    s = sub.add_parser("stamp"); s.add_argument("rom", type=Path); s.add_argument("version")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "read":
            v = read(a.rom.read_bytes())
            print(v if v is not None else "(no TAUVER field: built before 2026-10-08)")
        else:
            print(stamp_file(a.rom, a.version))
    except VersionError as e:
        print("FAIL:", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
