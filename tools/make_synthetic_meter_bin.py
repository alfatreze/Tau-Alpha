#!/usr/bin/env python3
"""Builds the STRESS bins for the external-meters registry (plan P2.4, docs/features/meters/EXTERNAL_METERS_P2_4_TEST.md): user bins `tau-meters.bin` with 64 rows (the maximum), made of the seven real meters and then
synthetic rows that REUSE the real packs under fresh, unique ids and names ("TEST 08" ... "TEST 64"), so the Settings list, X over many rows, the TOC read time at boot and the commit rule can be tried on a Pocket without
writing 57 meters.

  python3 tools/make_synthetic_meter_bin.py OUT_DIR [--rows N] [--hang-row ROW] [--collide] [--unavailable ROW] [--hidden ROW]

ROW numbers are 1-based and equal the number in the name ("TEST 20" is row 20). --unavailable ROW gives that synthetic row a `tau_min` no firmware reaches (listed greyed, cannot be picked); --hidden ROW clears its visible flag (not listed, X skips it).

writes  OUT_DIR/tau-meters.bin        N rows (default 64): rows 1..7 the shipped meters in their p2 order (Winamp Bars, the built-in fallback, first), rows 8..N synthetic
        OUT_DIR/tau-meters-hang.bin   the same, but row 8 is the HANG TEST pack (fw2/ui/meter_pack_winamp_bars_hang.c: it hangs after 200 pictures, the hardware watchdog cuts it, three strikes put it out): the safe-boot test
and  OUT_DIR/README.txt             the copy step.

A synthetic row is a copy of one of the six real packs (Scope, Chladni, Layered Wave, VU Master, Waterfall Grid, Sound Map, round robin) with a new 128-bit id and a new name; the pack header's `meter id` (the low 16 bits of the id
hash, informational) is patched to match the new row, and the pack image, its CRC and its descriptor are untouched, so each runs exactly like its original. Ids are derived from a fixed seed (SHA-256 of "tau-synthetic-meter-N"):
the same inputs always give the same bin, and no id collides with a real meter's, with another row's, or (checked) in its id_hash or its low 16 bits (so no row loses its tuning to a key collision: use --collide to make
rows 9 and 10 share their low 16 bits on purpose, for the tuning-collision test).

THE USER BIN IS NEVER IN A PACKAGE ZIP. The owner copies it to the card by hand: Assets/tau/common/tau-meters.bin (data slot 10, `deferload`, optional; the firmware prefers it to the core's own tau-packs.bin when it is sound).
Remove it again to go back to the seven shipped meters."""
import argparse, hashlib, struct, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb, build_packs as bp, pack_meter as pm

MAX_ROWS = mb.MAX_METERS
HANG_ROW = 8                                    # 1-based position of the hang pack in the hang variant


def synth_id(n, collide=None):
    """the 128-bit id (32 lower-case hex digits) of synthetic row n (1-based); `collide` = (n, other): row n takes the low 16 bits of row `other`'s id hash"""
    h = hashlib.sha256(("tau-synthetic-meter-%d" % n).encode()).hexdigest()[:32]
    if collide and n == collide[0]:
        o = hashlib.sha256(("tau-synthetic-meter-%d" % collide[1]).encode()).hexdigest()[:32]
        h = h[:4] + o[4:8] + h[8:]              # the first 8 hex digits = id_hash: keep the high half of its own, take the other's low 16 bits
    return h


def patched(pack, id_hex):
    """a copy of a pack with the header's `meter id` (u16 at 6) = the low 16 bits of the new id_hash (the loader checks it against the TOC row)"""
    b = bytearray(pack); struct.pack_into("<H", b, 6, int(id_hex[:8], 16) & 0xFFFF); return bytes(b)


UNAVAILABLE_TAU_MIN = 0x7F00                    # a TAU version (major 127) that no firmware has: the row is listed but this firmware cannot run it


def build(rows=MAX_ROWS, hang=False, collide=False, workdir=None, real=None, hang_row=HANG_ROW, unavailable=None, hidden=None):
    """[Entry] x rows: the shipped meters, then synthetic rows (row `hang_row`, 8 by default, is the hang pack when hang); `real` = the shipped entries when the caller has built them already (tests)"""
    if not 8 <= rows <= MAX_ROWS: raise ValueError("rows must be 8..%d (the seven shipped meters and at least one synthetic row)" % MAX_ROWS)
    for what, v in (("unavailable", unavailable), ("hidden", hidden)):
        if v is not None and not 8 <= v <= rows: raise ValueError("the %s row must be 8..%d (1-based, a synthetic row)" % (what, rows))
    if unavailable is not None and unavailable == hidden: raise ValueError("a row cannot be both unavailable and hidden")
    if hang and hang_row in (unavailable, hidden): raise ValueError("the hang row cannot be the unavailable or the hidden row")
    with tempfile.TemporaryDirectory() as td:
        wd = Path(workdir) if workdir else Path(td)
        real = list(real) if real else bp.build_entries(wd)
        src = [e for e in real if not (e.flags & mb.FLAG_FALLBACK)]            # the six real packs to reuse (the fallback's pack is never run)
        out = list(real)
        used = {e.id for e in real}
        for n in range(len(real) + 1, rows + 1):
            col = (10, 9) if collide else None
            ident = synth_id(n, col)
            if ident in used or any(ident[:8] == u[:8] for u in used) or (not collide and any(ident[4:8] == u[4:8] for u in used)): raise ValueError("synthetic id %d collides: change the seed" % n)
            used.add(ident)
            if hang and n == hang_row:
                blob, _ = pm.build("winamp_bars", mb.WINDOW, wd / "hang.elf", ["-DHANG_MODE=0"], source="winamp_bars_hang", header_id=int(ident[:8], 16) & 0xFFFF)
                out.append(mb.Entry(id=ident, name="HANG TEST", version=(0, 0, 1), pack=blob, flags=mb.FLAG_VISIBLE, params_rev=1))
            else:
                base = src[(n - len(real) - 1) % len(src)]
                out.append(mb.Entry(id=ident, name="TEST %02d" % n, version=(0, 0, 1), pack=patched(base.pack, ident), flags=0 if n == hidden else mb.FLAG_VISIBLE, params_rev=base.params_rev,
                                    tau_min=UNAVAILABLE_TAU_MIN if n == unavailable else 0))
        return out


README = """Synthetic meter bins for the external-meters stress test (TAU DEV 222, docs/features/meters/EXTERNAL_METERS_P2_4_TEST.md).

  tau-meters.bin        %d rows: the seven shipped meters, then TEST 08 ... TEST %02d (each runs like one of the real meters)
  tau-meters-hang.bin   the same, but row %d is HANG TEST (a pack that hangs after 200 pictures): the safe-boot test

COPY (by hand; the user bin is never in a package zip; nothing here writes the card):
  1. Eject nothing yet. Copy ONE of the two files to the card as   Assets/tau/common/tau-meters.bin   (the name must be exactly that).
  2. The firmware prefers it to the core's own tau-packs.bin when it is sound. Boot TAU DEV 222.
  3. To go back: delete Assets/tau/common/tau-meters.bin (the core's own bin of seven meters is used again).
Do not leave tau-meters-hang.bin on the card for daily use.
"""


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", type=Path); ap.add_argument("--rows", type=int, default=MAX_ROWS); ap.add_argument("--collide", action="store_true", help="rows 9 and 10 share their low 16 id bits (the tuning-collision case)"); ap.add_argument("--unavailable", type=int, default=None, help="the 1-based synthetic row (8..rows) that needs a newer TAU than this firmware (listed greyed, cannot be picked)"); ap.add_argument("--hidden", type=int, default=None, help="the 1-based synthetic row (8..rows) that is not flagged visible (not listed, X skips it)"); ap.add_argument("--hang-row", type=int, default=HANG_ROW, help="the 1-based row of HANG TEST in the hang variant (8..rows; 64 puts it last, so a walk over the other rows never meets it)")
    a = ap.parse_args(argv)
    if not 8 <= a.hang_row <= a.rows: ap.error("--hang-row must be 8..%d" % a.rows)
    for nm in ("unavailable", "hidden"):
        v = getattr(a, nm)
        if v is not None and not 8 <= v <= a.rows: ap.error("--%s must be 8..%d" % (nm, a.rows))
    if a.unavailable is not None and a.unavailable == a.hidden: ap.error("--unavailable and --hidden must be different rows")
    if a.hang_row in (a.unavailable, a.hidden): ap.error("--hang-row cannot be the unavailable or hidden row")
    a.out.mkdir(parents=True, exist_ok=True)
    for name, hang in (("tau-meters.bin", False), ("tau-meters-hang.bin", True)):
        blob = mb.compose(build(a.rows, hang, a.collide, hang_row=a.hang_row, unavailable=a.unavailable, hidden=a.hidden))
        (a.out / name).write_bytes(blob)
        info = mb.open_toc(blob)
        print("%s: %d B, %d rows (%s ... %s)" % (a.out / name, len(blob), len(info["rows"]), info["rows"][0]["name"], info["rows"][-1]["name"]))
    (a.out / "README.txt").write_text(README % (a.rows, a.rows, a.hang_row))
    return 0


if __name__ == "__main__":
    sys.exit(main())
