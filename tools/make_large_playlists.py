#!/usr/bin/env python3
"""Generate the A-105/A-106 large-playlist test lists (file-only, no firmware change).

Limits under test (fw/player.c): PL_MAX = 256 tracks, PL_TEXT_MAX = 12288 bytes of .m3u text.
Writes three lists plus README.txt to work/test-music/tau_plsdram_large/ and prints the
expected counts, computed with a model of pl_read_raw()/pl_parse().
"""
from pathlib import Path

PL_MAX, PL_TEXT_MAX = 256, 12288
out = Path(__file__).resolve().parent.parent / "work/test-music/tau_plsdram_large"
REAL = ["TAU A-102 stress/01 320CBR 44k1 stereo.mp3", "TAU A-102 stress/02 320CBR 48k stereo.mp3",
        "Controls/VBR 07 Contact with the Ohmu.mp3", "Controls/CBR128 44k1 merry-farm.mp3",
        "Controls/VBR 02 Stampede of the Ohmu.mp3"]

def model(text):
    """Return (tracks, truncated) the way the firmware parses text."""
    b = text.encode()[:PL_TEXT_MAX]
    full = len(text.encode()) > PL_TEXT_MAX
    n, i, trunc = 0, 0, False
    lines = b.replace(b"\r", b"\n").split(b"\n")
    for ln in lines:
        ln = ln.rstrip(b" \t")
        if not ln or ln.startswith(b"#"): continue
        if n == PL_MAX: trunc = True; break
        n += 1
    return n, trunc or full

def write(name, lines, header):
    text = "\n".join(["# " + header] + lines) + "\n"
    (out / name).write_bytes(text.encode())
    n, t = model(text)
    print(f"{name:22} {len(text.encode()):6d} B  lines={len(lines):3d}  expected tracks={n}  clipped={t}")
    return n, t

def main():
    out.mkdir(parents=True, exist_ok=True)
    # 1. 240 tracks, about 10.3 KB: fits both limits. Real files cycle so it plays; a unique
    #    MISSING marker every 50th entry proves text/offset integrity across the whole buffer.
    l = []
    for k in range(1, 241):
        l.append(f"zz marker {k:03d} MISSING.mp3" if k in (50, 100, 150, 200) else REAL[(k - 1) % 5])
    a = write("large240.m3u", l, "A-106 large list: 240 entries, fits (markers at 50,100,150,200)")
    # 2. 300 short entries: hits PL_MAX (256) before the text buffer.
    l = REAL[:]
    l += [f"m{k:03d}.mp3" for k in range(6, 301)]
    b = write("overflow_count.m3u", l, "A-106 overflow by count: 300 entries")
    # 3. 300 long entries: fills the 12 KiB text buffer first (about 170 entries).
    l = REAL[:]
    l += [f"long {k:03d} " + "x" * 52 + ".mp3" for k in range(6, 301)]
    c = write("overflow_text.m3u", l, "A-106 overflow by text size: 300 long entries")
    (out / "README.txt").write_text(f"""A-106 large playlist tests. Copy the three .m3u files to Assets/tau_plsdram/common/
next to playlist.m3u, then pick each through the playlist file menu of the core.
Every list starts with real tracks (large240 cycles the 5 real files); MISSING/mNNN/long NNN
entries do not exist and are skipped during playback, so only scroll the overlay, do not
play through them.

large240.m3u      expect: PLAYLIST 240 TRACKS toast (no clipping), overlay entry 50, 100,
                  150 and 200 read 'zz marker NNN MISSING', entry 240 is the real file
                  number ((240-1) mod 5 + 1 = 5th real file, VBR 02 Stampede).
overflow_count.m3u expect: CLIPPED AT {b[0]} TRACKS (PL_MAX).
overflow_text.m3u  expect: CLIPPED AT about {c[0]} TRACKS (text buffer full; the last line may be
                  cut mid-name). Last visible entry is 'long NNN xxx...', its number = count.
Pass for all: no garbled name anywhere, scrolling to the last row works, shuffle keeps the
count, first (real) track still plays, no audio issue, and the ordinary 5-track playlist
still loads afterwards.
""")
    print("wrote", out)
main()
