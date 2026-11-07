#!/usr/bin/env python3
"""BUG-001, step 1 (docs/issues/001-unicode-playlist-paths.md): what Player 2 hands to the card for a track path whose name is not plain ASCII, and what the host tools do to such names. HOST-ONLY characterisation: the real
fw2/stream/slot.c compiled against a model of the APF target channel (sim/p2_slot_open_harness.c). It does NOT say what the Pocket's APF does with non-ASCII bytes: that is the card matrix (step 2 and 3).

Firmware side (slot_open_path): the 0192 parameter struct is the 0190 template with the printable path replaced by ours and the length word before it patched.
  A. an ASCII path, and a UTF-8 path (NFC, NFD, CJK) in an ASCII template: the bytes reach the struct unchanged and the length word is the BYTE length (Player 2 is byte-transparent on its side);
  B. a Latin-1 byte (0xE9) the same;
  C. the mechanism of the old player's bug: a TEMPLATE that holds a non-ASCII byte (the longest printable-ASCII run is a fragment): the length word is not recognised, so it is not patched, and APF would read the
     OLD length: the path is wrong. Player 2 is immune only because its template is always the library slot's own ASCII path;
  D. the capacity: the path must fit the 256-byte struct after the template's path offset; a long path of 3-byte characters is refused with SLOT_E_TOOLONG at a CHARACTER count a third of an ASCII name's.
Host-tool side (tools/sync_media.py, tools/tau_library.py): names are folded to ASCII; the folding merges distinct names (a collision stops the sync) and gives 'track.ext' to a name with no ASCII letter.
Mutants of the harness are not needed: the assertions fail if the firmware's behaviour changes, which is the point (a fix flips them on purpose)."""
import subprocess, sys, tempfile, unicodedata
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:500]))
    fails += 0 if ok else 1
with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(exe), str(ROOT / "sim/p2_slot_open_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr[-1500:]); sys.exit(1)
    def template(path=b"/Assets/tau/common/tau-lib.tdb", off=32):
        """a 0190 response: header words, the length word at off-4, the path at off (the shape the firmware's heuristic expects). ASSUMED shape: no capture of a real APF response is in the repository; the template path is 30 bytes long so that the
        length word's low byte (0x1E) is not printable (a printable one would fuse with the path in the firmware's longest-printable-run search). The conclusions A and B hold for any shape: the path bytes are copied verbatim."""
        b = bytearray(256)
        for i in range(0, off - 4, 4): b[i:i + 4] = (0x00000010 + i).to_bytes(4, "big")
        b[off - 4:off] = len(path).to_bytes(4, "big"); b[off:off + len(path)] = path
        return bytes(b)
    def run(path, tpl=None, off=32):
        tpl = tpl or template(off=off)
        out = subprocess.run([str(exe)], input=tpl.hex() + "\n" + path.hex() + "\n", capture_output=True, text=True).stdout.split("\n")
        rc, err = int(out[0].split()[1]), int(out[0].split()[3]); buf = bytes.fromhex(out[1])
        return rc, err, buf
    def apf_view(buf, off=32):
        """what APF reads: the length word at off-4 and that many bytes at off"""
        n = int.from_bytes(buf[off - 4:off], "big"); return n, buf[off:off + n]
    # A. byte-transparent for ASCII and UTF-8
    base = "/Assets/tau/common/Music/"
    for label, name in (("ASCII control", "Cafe.mp3"), ("NFC (precomposed)", unicodedata.normalize("NFC", "Café.mp3")), ("NFD (decomposed, what macOS writes)", unicodedata.normalize("NFD", "Café.mp3")), ("CJK", "日本語.mp3")):
        p = (base + name).encode("utf-8"); rc, err, buf = run(p); n, got = apf_view(buf)
        check("A. %s: the struct carries the exact UTF-8 bytes (%d) and their byte length" % (label, len(p)), rc == 1 and n == len(p) and got == p, (rc, err, n, got))
    p = (base + "Caf\xe9.mp3").encode("latin-1"); rc, err, buf = run(p); n, got = apf_view(buf)
    check("B. a Latin-1 byte 0xE9 in the name also goes through unchanged (%d bytes)" % len(p), rc == 1 and n == len(p) and got == p, (rc, err, n, got))
    # C. the old bug's mechanism: a non-ASCII byte inside the TEMPLATE's path
    bad = template(path="/Assets/tau/common/Caf\xe9/x.mp3".encode("latin-1"))
    p = b"/Assets/tau/common/Music/A.mp3"; rc, err, buf = run(p, tpl=bad); n, got = apf_view(buf)
    check("C. a template whose path holds a non-ASCII byte: the length word is NOT patched (APF would read %d bytes of a %d-byte path): the old player's bug" % (n, len(p)), rc == 1 and n != len(p), (rc, n, got))
    nad = template(); rc, err, buf = run(p, tpl=nad); n, got = apf_view(buf)
    check("C. the same path in the ASCII library template (Player 2's own) is fine", rc == 1 and n == len(p) and got == p, (rc, n, got))
    # D. capacity
    ascii_ok = b"/Assets/tau/common/" + b"a" * 150 + b".mp3"
    rc1, _, _ = run(ascii_ok); check("D. an ASCII path of %d bytes fits" % len(ascii_ok), rc1 == 1)
    lim = next(n for n in range(10, 400) if run(b"/" + b"a" * n)[0] == 0)
    check("D. the longest path that fits is %d bytes (SLOT_E_TOOLONG above): %d characters of 3-byte UTF-8 (CJK) at the most, %d of 2-byte (accented, Cyrillic)" % (lim - 1, (lim - 2) // 3, (lim - 2) // 2), 120 < lim < 230, lim)
    cjk = "/" + "日" * ((lim - 1) // 3 + 3); rc, err, _ = run(cjk.encode()); check("D. a CJK name over the limit is refused with SLOT_E_TOOLONG (err 2), not corrupted", rc == 0 and err == 2, (rc, err))
    # host tools
    import tau_library as tl
    spec = importlib_spec = None
    import importlib.util
    s = importlib.util.spec_from_file_location("sync_media", ROOT / "tools/sync_media.py"); sm = importlib.util.module_from_spec(s); s.loader.exec_module(sm)
    f = sm.ascii_part
    check("H. the sync folds an accented name to its plain letters: Café.mp3 -> %s, NFD the same -> %s" % (f("Café.mp3"), f(unicodedata.normalize("NFD", "Café.mp3"))), f("Café.mp3") == "Cafe.mp3" == f(unicodedata.normalize("NFD", "Café.mp3")))
    check("H. a name with no ASCII letter becomes track.ext: %s, %s, %s" % (f("日本語.mp3"), f("音楽.mp3"), f("Привет.flac")), f("日本語.mp3") == "track.mp3" == f("音楽.mp3") and f("Привет.flac") == "track.flac")
    check("H. two different names that fold to one: Björk.mp3 / Bjork.mp3 -> %s / %s (the sync STOPS on a collision, tools/sync_media.py: 'name collision after ASCII conversion')" % (f("Björk.mp3"), f("Bjork.mp3")), f("Björk.mp3") == f("Bjork.mp3"))
    check("H. tau_library.ascii_name agrees with sync_media.ascii_part on accented names", all(tl.ascii_name(x) == f(x) for x in ("Café.mp3", "Björk.mp3", "Naïve.flac")))
    check("H. FINDING: for a name with no ASCII letter they differ (tau_library.ascii_name -> %r, sync_media.ascii_part -> %r): harmless while the index is built from card names the sync already converted, wrong if it is ever built from source names" % (tl.ascii_name("日本語.mp3"), f("日本語.mp3")), tl.ascii_name("日本語.mp3") == ".mp3" and f("日本語.mp3") == "track.mp3")
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
