#!/usr/bin/env python3
"""Self-checks of the Talos v2 reference model (G1).  Run: python3 tools/talos_v2_model/test_model.py [--fast | --quick] [--perf N]
(Imported into the repository on 2026-11-26, audit B-1181: paths point at the repository copies of the public
artefacts; --fast is the trimmed make test-host variant: no vector replay, no speed run, the quick safety and mutant sweeps.)

1. every worked example of the contract, reproduced;
2. the PINNED rules the public artefacts can check: public/canvas.py (SDK model), public/tau_fb.js (lab twin, through
   node when available), public/HOST_TABLE.md (documentary pins); the contract's own stated divergences (SBLIT
   127-row clamp, BAR lit clamp, CBLIT clipping) are confirmed to exist, any OTHER divergence is reported as a finding;
3. data checks: the weight tables against text_weights/, and the derivation tool's engine arithmetic against C-CHAR-3;
4. a write-rectangle safety monitor (C-GEN-2) over directed and out-of-domain random streams;
5. replay of the generated vectors in vectors/ (determinism);
6. mutation checks: every mutant of the model (talos_v2_model.MUTANTS) must be noticed by the directed vectors;
7. speed: a long constrained-random stream.
"""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
import random

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # the repository root (tools/talos_v2_model/..)
WORK = os.path.join(ROOT, "work", "talos_v2_model")      # git-ignored: generated vectors and scratch files
VECTORS = os.environ.get("TALOS_V2_VECTORS", os.path.join(WORK, "vectors"))
sys.path.insert(0, HERE)
import talos_v2_model as M  # noqa: E402
import gen_vectors as G  # noqa: E402

RESULTS = []          # (section, name, ok, detail)
FINDINGS = []         # divergences / facts worth reporting


def check(section, name, cond, detail=""):
    RESULTS.append((section, name, bool(cond), detail))
    if not cond:
        print("  FAIL %s: %s %s" % (section, name, detail))
    return cond


def E(params, init, acts, **kw):
    M.run(params, init, acts, **kw)
    return M.run.last_engine


def words(eng, start, n):
    return [int(x) for x in eng.mem.a[(start + np.arange(n)) & M.MASK25]]


def written_set(eng, ci=None):
    s = set()
    for c, op, a, v in eng.write_log:
        if ci is None or c == ci:
            s.update(int(x) for x in a)
    return s


def frame(x, y):
    return 512 * y + x


P = G.P
SF = G.src_fields


# =====================================================================================================================
# 1. worked examples of the contract
# =====================================================================================================================
def test_worked_examples():
    S = "worked"
    pat = M.pattern_word
    # C-BAR-1 probe: L = 172 with h = 10 lights all 10 rows and nothing above; L = 130 of h = 200: rows 0..69 bg, 70..199 fg
    e = E({"SET": "TAU"}, {}, [P(M.OP_BAR, addr=frame(10, 100), w=4, h=10, glyph=172 & 127, glyph_hi=172 >> 7, fg=1, bg=2)])
    check(S, "C-BAR-1 L=172 h=10 all lit", all(words(e, frame(10, 100 + y), 4) == [1] * 4 for y in range(10)))
    check(S, "C-BAR-1 L=172 h=10 nothing above", words(e, frame(10, 99), 4) == [pat(frame(10 + i, 99)) for i in range(4)])
    e = E({"SET": "TAU"}, {}, [P(M.OP_BAR, addr=frame(20, 100), w=3, h=200, glyph=130 & 127, glyph_hi=130 >> 7, fg=3, bg=4)])
    check(S, "C-BAR-1 L=130 h=200", all(words(e, frame(20, 100 + y), 3) == ([4] * 3 if y < 70 else [3] * 3) for y in range(200)))
    # C-CORN-1 the two geometries and the ordering
    e = E({"SET": "GALLERY"}, {"rc": [0, 0, 1]}, [P(M.OP_CORNERS, addr=frame(5, 5), w=10, h=6, glyph=2, bg=0xABCD)])
    exp = {frame(5, 5), frame(14, 5), frame(5, 10), frame(14, 10)}
    check(S, "C-CORN-1 r=2 10x6 writes exactly words 0 and 9 of rows 0 and 5", written_set(e) == exp)
    e = E({"SET": "GALLERY"}, {"rc": [0, 0, 1, 3]}, [P(M.OP_CORNERS, addr=frame(5, 5), w=20, h=9, glyph=3, bg=0xABCD)])
    exp = set()
    for row, cut in ((0, 3), (8, 3), (1, 1), (7, 1)):
        exp |= {frame(5 + x, 5 + row) for x in list(range(cut)) + list(range(20 - cut, 20))}
    check(S, "C-CORN-1 r=3 20x9 geometry", written_set(e) == exp)
    e = E({"SET": "GALLERY"}, {"rc": [0, 0, 1, 3]}, [P(M.OP_CORNERS, addr=frame(5, 5), w=20, h=9, glyph=3, bg=0xABCD),
                                                     P(M.OP_RECT, addr=frame(5, 5), w=20, h=1, fg=0x5555)])
    check(S, "C-CORN-1 a command queued after CORNERS executes after all its writes", words(e, frame(5, 5), 20) == [0x5555] * 20)
    e = E({"SET": "GALLERY"}, {"rc": [0, 0, 1, 3]}, [P(M.OP_CORNERS, addr=frame(5, 5), w=20, h=40, glyph=3 + 64, bg=1)])
    check(S, "3.11 finding: CORNERS clamps the 7-bit radius field (67 -> 15)", len(written_set(e)) > 0)
    # C-SBLIT-2 probe: 60 x 60 at 3x writes 127 rows of 127
    e = E({"SET": "TAU"}, {}, [P(M.OP_SBLIT, addr=0, w=60, h=60, sx=3, sy=3, **SF(frame(0, 600)))])
    ws = written_set(e)
    check(S, "C-SBLIT-2 60x60 at 3x -> 127 x 127", ws == {frame(x, y) for x in range(127) for y in range(127)})
    # C-SBLIT-3 probe: 5 x 3 at 1.5x gives 7 x 4
    e = E({"SET": "TAU"}, {}, [P(M.OP_SBLIT, addr=0, w=5, h=3, sx=1, sy=1, **SF(frame(0, 600)))])
    check(S, "C-SBLIT-3 5x3 at 1.5x -> 7x4", written_set(e) == {frame(x, y) for x in range(7) for y in range(4)})
    check(S, "C-SBLIT-3 column map floor(2i/3)", words(e, 0, 7) == [pat(frame(x * 2 // 3, 600)) for x in range(7)])
    # SBLIT extents for 1x/2x/3x as listed: s, floor(3s/2), 2s, 3s (clamped)
    for s in (1, 5, 42, 43, 64, 200):
        for sel, f in ((0, lambda s: s), (1, lambda s: 3 * s // 2), (2, lambda s: 2 * s), (3, lambda s: 3 * s)):
            e = E({"SET": "TAU"}, {}, [P(M.OP_SBLIT, addr=0, w=s, h=1, sx=sel, sy=0, **SF(frame(0, 600)))])
            check(S, "3.7 extent sel %d s %d" % (sel, s), len(written_set(e)) == min(127, f(s)))
    # CHAR: 16, 24, 32, 48 rows for selectors 0..3, w and h ignored
    for sel, ext in ((0, 16), (1, 24), (2, 32), (3, 48)):
        e = E({"SET": "TAU"}, {"font_ram": G.FONT_RAM}, [P(M.OP_CHAR, addr=0, w=3, h=500, glyph=0x41, sx=sel, sy=sel)])
        check(S, "C-CHAR-2/-6 %d rows for selector %d" % (ext, sel), written_set(e) == {frame(x, y) for x in range(ext) for y in range(ext)})
    # C-BLIT-3 exact for a = 0, 1, 255 (formula)
    for a in (0, 1, 255):
        e = E({"SET": "TAU"}, {"regions": [[1000, [0xFFFF, 0x0000, 0xF81F]], [2000, [0x0000, 0xFFFF, 0x07E0]]]},
              G.sticky_all(blend_en=1, mode=0, alpha=a) + [P(M.OP_BLIT, addr=2000, w=3, h=1, **SF(1000))])
        exp = []
        for f, b in ((0xFFFF, 0), (0, 0xFFFF), (0xF81F, 0x07E0)):
            ch = [((f >> s & m) * a + (b >> s & m) * (256 - a)) // 256 for s, m in ((11, 31), (5, 63), (0, 31))]
            exp.append(ch[0] << 11 | ch[1] << 5 | ch[2])
        check(S, "C-BLIT-3 mode 0 a=%d" % a, words(e, 2000, 3) == exp)
    # C-CBLIT-2 REINDEX wraps modulo 256
    clut = [(i * 77 + 5) & 0xFFFF for i in range(256)]
    e = E({"SET": "TAU"}, {"clut": clut, "regions": [[5000, [0x12FE, 0x00FF, 0xFF01]]]},
          G.sticky_all(reindex=3) + [P(M.OP_CBLIT, addr=6000, w=3, h=1, **SF(5000))])
    check(S, "C-CBLIT-2 wrap", words(e, 6000, 3) == [clut[1], clut[2], clut[4]])
    # C-Q-3 / C-Q-2: 300 pushes into a stalled queue: 255 kept, 45 counted
    e = E({"SET": "TAU"}, {}, [{"a": "stall", "v": 1}, P(M.OP_RECT, w=1, h=1, repeat=300), {"a": "snapshot"},
                               {"a": "stall", "v": 0}, {"a": "wait_idle"}])
    snap = [t for t in e.trace if t["ev"] == "snapshot"][0]
    check(S, "C-Q-3 300 pushes: 255 kept, 45 counted", snap["accepted"] == 255 and snap["ovf"] == 45 and snap["full"] == 1
          and snap["free"] == 0 and e.retired == 255, str(snap))
    # C-OP-1 / C-GEN-1: reserved and zero-size commands make no memory access, retire, count for idle
    acts = [P(op, addr=frame(1, 1), w=5, h=3, fg=7, glyph=0x41) for op in (10, 11, 12, 13, 14, 15)]
    e = E({"SET": "TAU"}, {"font_ram": G.FONT_RAM}, acts)
    check(S, "C-OP-1 opcodes 10..15 off: no access, retire", not e.write_log and e.retired == 6 and e.idle())
    acts = [P(op, addr=frame(1, 1), w=w, h=h, fg=7) for op in (0, 1, 3, 4, 5, 6, 7, 8) for w, h in ((0, 5), (5, 0))
            if not (op == 0 and h == 0)]
    e = E({"SET": "GALLERY"}, {"clut": [0] * 256}, acts)
    check(S, "C-GEN-1 zero-size commands: no access", not e.write_log and e.retired == len(acts))
    # C-IF-1: opcode values of R_FB_GO
    for word, op in ((0x4001, 9), (0x4002, 10), (0x4003 | 2 << 10 | 1 << 12, 11), (0x4007, 15)):
        d = M.decode_fb_go(word, 0, 0, 0, 1)
        check(S, "C-IF-1 0x%x -> op %d" % (word, op), d["op"] == op)
    d = M.decode_fb_go(0x4003 | 2 << 10 | 1 << 12, 0, 0, 0, 1)
    check(S, "C-IF-1 DSBLIT rx<<10, ry<<12", d["sx"] == 2 and d["sy"] == 1)
    d = M.decode_fb_go((0x55 << 3) | (1 << 15) | (2 << 17), 7, 5 | 9 << 9, 0x22221111, 1)
    check(S, "C-IF-1 field packing", d["glyph"] == 0x55 and d["glyph_hi"] == 1 and d["subop"] == 2 and d["w"] == 5
          and d["h"] == 9 and d["fg"] == 0x1111 and d["bg"] == 0x2222 and d["addr"] == 7)
    check(S, "C-IF-1 subop ignored without FEAT_ESC", M.decode_fb_go(2 << 17 | 0x4007, 0, 0, 0, 0)["subop"] == 0)
    # C-CHAR-1 mapping
    eng = M.Talos({"SET": "TAU"})
    mp = {0x20: 0, 0x7E: 94, 0x7F: 0, 0x9F: 0, 0xA0: 95, 0xFF: 190, 0x00: 0, 0x41: 33}
    check(S, "C-CHAR-1 Latin-1 mapping", all(eng.glyph_index({"glyph": c & 127, "glyph_hi": c >> 7}) == g for c, g in mp.items()))
    eng = M.Talos({"SET": "TAU", "FONT_RAM_ENABLE": 0})
    check(S, "C-CHAR-1 ROM mapping ignores glyph_hi", eng.glyph_index({"glyph": 0x41, "glyph_hi": 1}) == 33
          and eng.glyph_index({"glyph": 0x10, "glyph_hi": 1}) == 0)
    # C-DBUF-1: a rectangle that straddles row 360 splits
    e = E({"SET": "TAU"}, {}, [{"a": "dbuf_cpu", "v": 1}, P(M.OP_RECT, addr=frame(0, 358), w=2, h=4, fg=9)])
    ws = written_set(e)
    check(S, "C-DBUF-1 straddle splits per row", ws == {frame(x, y) + M.DBUF_OFFSET for x in range(2) for y in (358, 359)}
          | {frame(x, y) for x in range(2) for y in (360, 361)})
    # C-DBUF-2: several requests before one edge give one flip; the fill reads the displayed buffer
    e = E({"SET": "TAU"}, {}, [{"a": "flip_req"}, {"a": "flip_req"}, {"a": "flip_req"}, {"a": "snapshot"}, {"a": "vblank"},
                               {"a": "snapshot"}, {"a": "fill", "line": 3}])
    sn = [t for t in e.trace if t["ev"] == "snapshot"]
    check(S, "C-DBUF-2 one flip at the edge", sn[0]["disp"] == 0 and sn[0]["pending"] == 1 and sn[1]["disp"] == 1 and sn[1]["pending"] == 0)
    check(S, "C-FILL-1 address with buffer 1 and lb half", [int(x) for x in e.lb[512:512 + 400]] ==
          [pat(frame(i, 3) + M.DBUF_OFFSET) for i in range(400)])
    # C-FENCE-1
    e = E({"SET": "TAU"}, {}, [P(M.OP_RECT, w=3, h=3), P(M.OP_FENCE), P(M.OP_FENCE), {"a": "wait_idle"}])
    check(S, "C-FENCE-1 count", e.fence == 2)
    # C-IDLE-1: a push followed by a read reads busy
    e = E({"SET": "TAU"}, {}, [P(M.OP_RECT, w=1, h=1), {"a": "mmio_r", "off": M.R_FB_GO}])
    check(S, "C-IDLE-1 busy after push", [t for t in e.trace if t["ev"] == "read"][0]["v"] == 0)
    # DSBLIT 96 x 12 at 2:3 by 3:4 -> 64 x 9, key/blend ignored (C-DSB-2)
    acts = lambda k: G.sticky_all(src_base=2_000_000, dst_base=3_000_000, key_en=k, key=M.pattern_word(2_000_000),
                                  blend_en=k, mode=1) + [P(M.OP_DSBLIT, addr=0, w=96, h=12, sx=1, sy=0, **SF(0))]
    e1 = E({"SET": "GALLERY"}, {}, acts(0)); w1 = words(e1, 3_000_000, 512 * 9)
    e2 = E({"SET": "GALLERY"}, {}, acts(1)); w2 = words(e2, 3_000_000, 512 * 9)
    check(S, "C-DSB-1 96x12 at 2:3 by 3:4 -> 64 x 9", written_set(e1) == {3_000_000 + 512 * y + x for x in range(64) for y in range(9)})
    check(S, "C-DSB-2 key and blend do not apply", w1 == w2)
    # DSBLIT rounding: floor((p0+p1+p2+p3+2)/4)
    e = E({"SET": "GALLERY"}, {"regions": [[2_000_000, [0x0841, 0x0000, 0, 0]], [2_000_512, [0x0841, 0x0841, 0, 0]]]},
          G.sticky_all(src_base=2_000_000, dst_base=3_000_000) + [P(M.OP_DSBLIT, addr=0, w=2, h=2, sx=2, sy=2, **SF(0))])
    check(S, "C-DSB-1 rounding (3 of 4 ones -> 1)", words(e, 3_000_000, 1) == [0x0841])
    # C-RRECT-1: radius = the 7-bit field clamped (16 -> 15, not 0)
    e = E({"SET": "TAU"}, {"rc": [0] + [1] * 15}, [P(M.OP_RRECT, addr=0, w=40, h=40, glyph=16, fg=1, bg=2)])
    check(S, "C-RRECT-1 field 16 clamps to 15", words(e, frame(0, 14), 1) == [2] and words(e, frame(0, 15), 1) == [1])
    # C-RST-1 / C-RST-3
    e = E({"SET": "TAU"}, {}, G.sticky_all(src_base=77) + [{"a": "stall", "v": 1}, P(M.OP_RECT, w=4, h=4, repeat=10),
                                                          P(M.OP_FENCE), {"a": "reset"}, {"a": "snapshot"}])
    sn = [t for t in e.trace if t["ev"] == "snapshot"][0]
    check(S, "C-RST-1 counts cleared, queue emptied, nothing retired", sn["accepted"] == 0 and sn["free"] == 255
          and sn["fence"] == 0 and not e.write_log)
    check(S, "C-RST-3 sticky survives", e.src_base == 77)
    # 1.5 capability bits
    for s, caps in (("TAU", 0), ("V", 0), ("GALLERY", 0b11011), ("ESC", 0b00100)):
        eng = M.Talos({"SET": s})
        check(S, "1.5 caps %s" % s, (eng.mmio_read(M.R_GFX_STATUS) >> 17) & 0x3FFF == caps)
    # 1.3 elaboration errors
    for bad in ({"VIEW_DBL_EN": 1}, {"VIEW_ENABLE": 1, "SOC_VID40": 0}, {"VIEW_ENABLE": 1, "VIEW_LINE_WORDS": 1000},
                {"FILL_WORDS": 300}):
        try:
            M.resolve_params(bad); ok = False
        except M.ElaborationError:
            ok = True
        check(S, "1.3 illegal combination refused %s" % bad, ok)
    # 2.2 aliasing: a 9-bit decode maps 0x280 onto 0x080 (not the viewer block)
    eng = M.Talos({"SET": "TAU"})
    check(S, "2.2 9-bit decode: 0x3BC aliases onto GFX_STATUS 0x1BC", eng.mmio_read(0x3BC) == eng.mmio_read(0x1BC))
    # C-VIEW-2 / C-DBL-1
    acts = [{"a": "mmio_w", "off": M.R_VIEW_BASE, "v": 4096}, {"a": "mmio_w", "off": M.R_VIEW_STRIDE, "v": 1208},
            {"a": "mmio_w", "off": M.R_VIEW_CTRL, "v": 1}, {"a": "view", "en": 1}, {"a": "vblank"}, {"a": "fill", "line": 7}]
    e = E({"SET": "GALLERY"}, {}, acts)
    check(S, "C-VIEW-2/C-DBL-1 line 7 doubled reads plane line 3 into vlb half 1",
          [int(x) for x in e.vlb[2048:2048 + 1200]] == [pat(4096 + 3 * 1208 + i) for i in range(1200)])


# =====================================================================================================================
# 2. public artefacts
# =====================================================================================================================
def load_canvas():
    spec = importlib.util.spec_from_file_location("pub_canvas", os.path.join(ROOT, "sdk", "sdk", "conformance", "canvas.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def canvas_words(cv):
    out = np.zeros(512 * 1024, dtype=np.uint16)
    out[:] = np.array(cv.st, dtype=np.uint16)
    px = np.array(cv.px, dtype=np.uint16).reshape(360, 400)
    o = out.reshape(1024, 512)
    o[:360, :400] = px
    return out


def fresh_canvas(mod):
    cv = mod.Canvas()
    pat = M.Memory.base()[:512 * 1024]
    cv.st = __import__("array").array("H", pat.tobytes())
    cv.px = __import__("array").array("H", pat.reshape(1024, 512)[:360, :400].copy().tobytes())
    return cv


SCALE_SEL = {1: 0, 2: 2, 3: 3}


def host_to_model(call, blend_state):
    """host-table call -> model actions, as fw2/gfx.c would issue them (assumption: bases 0, strides 512)."""
    n, a = call[0], [int(x) for x in call[1:]]
    if n in ("rect", "rect_stash"):
        x, y, w, h, c = a
        return [P(M.OP_RECT, addr=frame(x, y), w=w, h=h, fg=c)]
    if n == "bar":
        x, y, w, h, lit, fg, bg = a
        return [P(M.OP_BAR, addr=frame(x, y), w=w, h=h, glyph=lit & 127, glyph_hi=lit >> 7, fg=fg, bg=bg)]
    if n in ("blit", "blit_abs", "cblit", "blend_blit"):
        sx, sy, dx, dy, w, h = a
        op = M.OP_CBLIT if n == "cblit" else M.OP_BLIT
        return [P(op, addr=frame(dx, dy), w=w, h=h, **SF(frame(sx, sy)))]
    if n == "sblit":
        sx, sy, dx, dy, w, h, sc = a
        return [P(M.OP_SBLIT, addr=frame(dx, dy), w=w, h=h, sx=SCALE_SEL[sc], sy=SCALE_SEL[sc], **SF(frame(sx, sy)))]
    if n == "blend_on":
        return [{"a": "sticky", "field": 5, "v": 1 | (a[0] << 8)}]
    if n == "blend_off":
        return [{"a": "sticky", "field": 5, "v": 0}]
    raise ValueError(n)


def model_words(calls, clut=None, mutant=None):
    acts = []
    for c in calls:
        acts += host_to_model(c, None)
    init = {"clut": clut} if clut else {}
    e = E(dict({"SET": "TAU"}, **({"MUTANT": mutant} if mutant else {})), init, acts)
    return e.mem.a[:512 * 1024].copy()


def rand_case(rnd, kind, variant="legal", vis=False):
    """-> list of host calls of one kind (and a clut when needed)."""
    c = rnd.randrange(65536)
    if kind == "rect":
        w, h = rnd.randint(1, 120), rnd.randint(1, 100)
        return [("rect", rnd.randrange(400 - w), rnd.randrange(360 - h), w, h, c)]
    if kind == "rect_stash":
        w, h = rnd.randint(1, 111), rnd.randint(1, 100)
        return [("rect_stash", rnd.randrange(400, 512 - w + 1), rnd.randrange(1024 - h), w, h, c)] if rnd.random() < .5 else \
               [("rect_stash", rnd.randrange(512 - w), rnd.randrange(360, 1024 - h), w, h, c)]
    if kind == "bar":
        w = rnd.randint(1, 30)
        if variant == "legal":
            h = rnd.randint(1, 200); lit = rnd.randint(0, h); y = rnd.randrange(360 - h + 1)
        else:                                            # lit > h, the canvas' over-paint kept on screen
            h = rnd.randint(1, 150); lit = rnd.randint(h + 1, min(359, h + 150)); y = rnd.randint(lit - h, 360 - h)
        return [("bar", rnd.randrange(400 - w), y, w, h, lit, c, c ^ 0xFFFF)]
    if kind in ("blit", "blit_abs"):
        W, H = (400, 360) if vis else (512, 1024)        # tau_fb.js blit works inside the visible frame only
        w, h = rnd.randint(1, 127), rnd.randint(2, 40)
        sx, sy = rnd.randrange(W - w), rnd.randrange(H - h - 41)
        if variant == "overlap_down":
            return [(kind, sx, sy, sx + rnd.randint(-3, 3) if sx >= 3 and sx + w + 3 < W else sx, sy + rnd.randint(1, h - 1), w, h)]
        dx, dy = rnd.randrange(W - w), rnd.randrange(H - h)
        if not (sx + w <= dx or dx + w <= sx or sy + h <= dy or dy + h <= sy) and dy > sy:
            dy = max(0, sy - rnd.randint(0, h))          # no downward overlap inside one command (C-ORD-2)
        return [(kind, sx, sy, dx, dy, w, h)]
    if kind == "sblit":
        sc = rnd.choice([1, 2, 3])
        w = rnd.randint(1, 60)
        h = rnd.randint(1, 127 // sc) if variant == "legal" else rnd.randint(127 // sc + 1, 300 // sc)
        ow, oh = min(127, w * sc), h * sc
        dx, dy = rnd.randrange(400 - ow), rnd.randrange(360 - oh + 1)
        return [("sblit", rnd.randrange(512 - w), rnd.randrange(360, 1024 - h), dx, dy, w, h, sc)]
    if kind == "cblit":
        w, h = rnd.randint(1, 127), rnd.randint(1, 50)
        if variant == "legal":
            dx, dy = rnd.randrange(400 - w), rnd.randrange(360 - h)
        else:
            dx, dy = rnd.randrange(400 - w + 1, 512 - w), rnd.randrange(360 - h)
        return [("cblit", rnd.randrange(512 - w), rnd.randrange(360, 1024 - h), dx, dy, w, h)]
    if kind == "blend_blit":
        w, h = rnd.randint(1, 127), rnd.randint(1, 40)
        a = rnd.choice([0, 1, 127, 128, 254, 255, rnd.randrange(256)])
        dx, dy = rnd.randrange(400 - w), rnd.randrange(360 - h)
        calls = [("blend_on", a)] if variant == "legal" else []
        return calls + [("blend_blit", rnd.randrange(512 - w), rnd.randrange(360, 1024 - h), dx, dy, w, h)]
    raise ValueError(kind)


def test_canvas(n=25):
    S = "canvas.py"
    try:
        mod = load_canvas()
    except Exception as ex:   # pragma: no cover
        check(S, "load", False, repr(ex)); return
    rnd = random.Random(1234)
    pal = [rnd.randrange(65536) for _ in range(256)]
    for kind in ("rect", "rect_stash", "bar", "blit", "blit_abs", "sblit", "cblit", "blend_blit"):
        bad = 0
        for i in range(n):
            calls = rand_case(rnd, kind)
            cv = fresh_canvas(mod); cv.clut = list(pal)
            for c in calls:
                cv.apply(c)
            mw = model_words(calls, pal if kind == "cblit" else None)
            if not np.array_equal(canvas_words(cv), mw):
                bad += 1
        check(S, "%s agrees with the model (%d random legal cases)" % (kind, n), bad == 0, "%d differ" % bad)
    # the contract's stated divergences: confirm they exist (and that they are the only difference in those cases)
    for kind, variant, rule in (("bar", "lit_gt_h", "C-BAR-1 (lit > h, models do not clamp)"),
                                ("sblit", "rows_gt_127", "C-SBLIT-2 (rows > 127, models do not clamp)"),
                                ("cblit", "offscreen", "C-CBLIT-3 (models clip to the visible frame)")):
        diff = other = 0
        mut = {"bar": "M2_BAR_NOCLAMP", "sblit": "M8_SBLIT_NO_ROWCLAMP"}.get(kind)
        for i in range(n):
            calls = rand_case(rnd, kind, variant)
            cv = fresh_canvas(mod); cv.clut = list(pal)
            for c in calls:
                cv.apply(c)
            cw = canvas_words(cv)
            if not np.array_equal(cw, model_words(calls, pal if kind == "cblit" else None)):
                diff += 1
            if mut and not np.array_equal(cw, model_words(calls, None, mut)):
                other += 1                    # canvas differs from the model even with the clamp removed
            if kind == "cblit":               # outside the visible frame the canvas leaves the pattern; inside it agrees
                mw = model_words(calls, pal).reshape(1024, 512); cw2 = cw.reshape(1024, 512)
                other += not np.array_equal(mw[:360, :400], cw2[:360, :400])
        check(S, "stated divergence present: %s" % rule, diff > 0, "%d of %d differ" % (diff, n))
        check(S, "and it is the ONLY difference (model with the clamp removed / visible part equal): %s" % rule, other == 0,
              "%d cases differ otherwise" % other)
    # findings: divergences the contract does not state
    diff = 0
    for i in range(n):
        calls = rand_case(rnd, "blit", "overlap_down")
        cv = fresh_canvas(mod)
        for c in calls:
            cv.apply(c)
        diff += not np.array_equal(canvas_words(cv), model_words(calls))
    FINDINGS.append("canvas.py blit/blit_abs buffer the WHOLE rectangle before writing (memmove), the contract's C-ORD-2 "
                    "says one engine command is not a memmove for a downward overlap: %d of %d downward-overlap cases differ" % (diff, n))
    check(S, "unstated divergence measured: blit downward overlap (finding, not a failure)", True)
    diff = 0
    for i in range(n):
        calls = rand_case(rnd, "blend_blit", "no_blend_on")
        cv = fresh_canvas(mod)
        for c in calls:
            cv.apply(c)
        diff += not np.array_equal(canvas_words(cv), model_words(calls))
    FINDINGS.append("canvas.py blend_blit without a preceding blend_on blends at a = 255 (f*255+b)>>8; the engine with the "
                    "BLEND enable 0 copies: %d of %d cases differ (host-table semantics, not an engine rule)" % (diff, n))


def test_tau_fb(n=20):
    S = "tau_fb.js"
    node = shutil.which("node")
    if not node:
        check(S, "node available", True, "skipped: node not found"); return
    rnd = random.Random(99)
    pal = [rnd.randrange(65536) for _ in range(256)]
    to_js = {"rect": "rect", "rect_stash": "rectStash", "bar": "bar", "blit": "blit", "blit_abs": "blitAbs", "sblit": "sblit",
             "cblit": "cblit", "blend_blit": "blendBlit", "blend_on": "blendOn"}
    cases, meta = [], []

    def add(kind, variant, calls):
        ys = []
        for c in calls:
            a = [int(x) for x in c[1:]]
            if c[0] in ("rect", "rect_stash", "bar"):
                ys += [a[1], a[1] + a[3]]
            elif c[0] in ("blit", "blit_abs", "cblit", "blend_blit", "sblit"):
                ys += [a[3], a[3] + a[5] * (a[6] if c[0] == "sblit" else 1) + 1]
        y0, y1 = max(0, min(ys) - 2), min(1024, max(ys) + 2)
        if any(c[0] == "bar" for c in calls):
            y0 = 0
        ops = ([["clutLoad", 0, pal]] if kind == "cblit" else []) + [[to_js[c[0]]] + [int(x) for x in c[1:]] for c in calls]
        cases.append({"ops": ops, "rows": [y0, y1]})
        meta.append((kind, variant, calls, y0, y1))

    for kind in ("rect", "rect_stash", "bar", "blit", "blit_abs", "sblit", "cblit", "blend_blit"):
        for i in range(n):
            add(kind, "legal", rand_case(rnd, kind, vis=(kind == "blit")))
    for kind, variant in (("bar", "lit_gt_h"), ("sblit", "rows_gt_127"), ("cblit", "offscreen"), ("blit", "overlap_down")):
        for i in range(n):
            add(kind, variant, rand_case(rnd, kind, variant, vis=(kind == "blit")))
    # lab-only Fb.blend modes 0..4 (C-BLIT-4): hardware = OP_BLIT with blend from a one-row strip, src_stride 0
    for i in range(3 * n):
        w, h = rnd.randint(1, 127), rnd.randint(1, 30)
        x, y = rnd.randrange(400 - w), rnd.randrange(360 - h)
        col, mode, al = rnd.randrange(65536), rnd.randrange(5), rnd.randrange(256)
        cases.append({"ops": [["blend", x, y, w, h, col, mode, al]], "rows": [y, y + h]})
        meta.append(("blend", mode, (x, y, w, h, col, mode, al), y, y + h))
    os.makedirs(WORK, exist_ok=True)
    fn = os.path.join(WORK, ".tau_fb_cases.json")
    with open(fn, "w") as f:
        json.dump(cases, f)
    try:
        fo = os.path.join(WORK, ".tau_fb_out.bin")
        r = subprocess.run([node, os.path.join(HERE, "tau_fb_driver.js"), fn, fo], capture_output=True, text=True, cwd=ROOT)
        if r.returncode:
            check(S, "node driver ran", False, r.stderr[:600]); return
        raw = np.fromfile(fo, dtype="<u2"); os.unlink(fo)
        out, pos = [], 0
        for c in cases:
            n = (c["rows"][1] - c["rows"][0]) * 512
            out.append(raw[pos:pos + n]); pos += n
    finally:
        os.unlink(fn)
    tally = {}
    for (kind, variant, calls, y0, y1), js in zip(meta, out):
        if kind == "blend":
            x, y, w, h, col, mode, al = calls
            strip = 600 * 512
            acts = [{"a": "cpu_w", "addr": strip, "v": [col] * w}] + G.sticky_all(src_stride=0, blend_en=1, mode=mode, alpha=al) + \
                   [P(M.OP_BLIT, addr=frame(x, y), w=w, h=h, **SF(strip))]
            mw = E({"SET": "TAU"}, {}, acts).mem.a[:512 * 1024].copy()
            key = "blend mode %d" % mode
        else:
            mw = model_words(calls, pal if kind == "cblit" else None)
            key = "%s/%s" % (kind, variant)
        same = np.array_equal(np.array(js, dtype=np.uint16), mw[y0 * 512:y1 * 512])
        t = tally.setdefault(key, [0, 0]); t[0] += 1; t[1] += (not same)
    for key, (tot, bad) in sorted(tally.items()):
        if key.endswith("/legal") or key.startswith("blend mode"):
            check(S, "%s agrees with the model (%d cases)" % (key, tot), bad == 0, "%d differ" % bad)
        elif key in ("bar/lit_gt_h", "sblit/rows_gt_127", "cblit/offscreen"):
            check(S, "stated divergence present: %s" % key, bad > 0, "%d of %d differ" % (bad, tot))
        else:
            FINDINGS.append("tau_fb.js %s: %d of %d cases differ from the contract (whole-rectangle buffering, memmove)" % (key, bad, tot))


def test_host_table():
    S = "HOST_TABLE.md"
    t = open(os.path.join(ROOT, "sdk", "docs", "HOST_TABLE.md")).read()
    check(S, "blend_on documents a/256 (C-BLIT-3)", "source weight a/256" in t)
    check(S, "cblit documents w <= 127 (C-BLIT-6)", "w <= 127" in t)
    check(S, "bar documents bottom lit rows in fg (C-BAR-1)", "the bottom `lit` rows in fg" in t)
    check(S, "fence documents wait-for-all (C-FENCE-1)", "every queued draw command has executed" in t)


# =====================================================================================================================
# 3. data checks
# =====================================================================================================================
def parse_row(line):
    cells = [c.strip().strip("*") for c in line.strip().strip("|").split("|")]
    return [int(c.strip("*")) for c in cells[1:] if c.strip("*").isdigit()]


def test_weights():
    S = "text weights"
    doc = open(os.path.join(ROOT, "docs", "features", "TEXT_WEIGHTS_REDO_2026-11-25.md")).read().splitlines()
    con = open(os.path.join(ROOT, "docs", "features", "TALOS_ENGINE_V2_CONTRACT.md")).read().splitlines()
    row = lambda lines, prefix: next(parse_row(l) for l in lines if l.startswith(prefix) and len(parse_row(l)) == 16)
    check(S, "contract dark = doc 'new (derived)'", tuple(row(con, "| `TEXT_W_DARK`")) == tuple(row(doc, "| **new (derived)**"))
          == M.TEXT_W_DARK_DERIVED)
    check(S, "doc 'current (inherited)' = model shipped dark", tuple(row(doc, "| current (inherited)")) == M.TEXT_W_DARK_SHIPPED)
    lights = [parse_row(l) for l in con if l.startswith("| `TEXT_W_LIGHT`") and len(parse_row(l)) == 16]
    check(S, "contract light tables = model", tuple(lights[0]) == M.TEXT_W_LIGHT_IN_USE and tuple(lights[1]) == M.TEXT_W_LIGHT_DERIVED)
    check(S, "C-CHAR-4: dark differs from shipped at c = 5, 9, 12, 14 by one step",
          [c for c in range(16) if M.TEXT_W_DARK_DERIVED[c] != M.TEXT_W_DARK_SHIPPED[c]] == [5, 9, 12, 14]
          and all(M.TEXT_W_DARK_SHIPPED[c] - M.TEXT_W_DARK_DERIVED[c] == 1 for c in (5, 9, 12, 14)))
    check(S, "Q2: derived light differs at c = 8..13", [c for c in range(16) if M.TEXT_W_LIGHT_IN_USE[c] != M.TEXT_W_LIGHT_DERIVED[c]]
          == list(range(8, 14)))
    for t in (M.TEXT_W_DARK_DERIVED, M.TEXT_W_LIGHT_IN_USE, M.TEXT_W_LIGHT_DERIVED):
        check(S, "ends fixed and non-decreasing %s" % (t,), t[0] == 0 and t[15] == 16 and all(t[i] <= t[i + 1] for i in range(15)))
    # the derivation tool's model of the engine vs C-CHAR-3 (finding if they differ)
    spec = importlib.util.spec_from_file_location("dtw", os.path.join(ROOT, "tools", "derive_text_weights.py"))
    d = importlib.util.module_from_spec(spec); spec.loader.exec_module(d)
    diff = tot = 0
    for wt in range(17):
        for f in range(64):
            for b in range(64):
                for shift, mx in ((11, 31), (5, 63), (0, 31)):
                    if f > mx or b > mx:
                        continue
                    fg8, bg8 = d.unpack565(f << shift), d.unpack565(b << shift)
                    tool = d.pack565(d.blend565(bg8, fg8, wt)) >> shift & mx
                    eng = (f * wt + b * (16 - wt)) // 16
                    tot += 1; diff += tool != eng
    FINDINGS.append("derive_text_weights.py models the engine as an 8-bit mix of bit-replicated channels truncated to "
                    "RGB565; C-CHAR-3 mixes the 5/6-bit fields directly: %d of %d (channel value, weight) cases differ "
                    "(%.1f %%), so the table was fitted against an arithmetic that is not the contract's" % (diff, tot, 100.0 * diff / tot))
    check(S, "tool arithmetic compared with C-CHAR-3 (finding recorded)", True)


def test_dsb_taps():
    S = "DSBLIT"
    bad = []
    for sel, (num, den) in M.DSBLIT_RATIO.items():
        for s in range(1, 512):
            E_ = min(127, s * num // den)
            for i in range(E_):
                X = i * den // num
                if X + 1 > s - 1:
                    bad.append((sel, s, i))
    check(S, "3.12 claim: the taps never leave the source rectangle (all s 1..511)", not bad, str(bad[:5]))


# =====================================================================================================================
# 4. safety monitor (C-GEN-2) and 5. vector replay
# =====================================================================================================================
class Monitored(M.Talos):
    def execute(self, ci, c):
        n0 = len(self.write_log)
        st = (self.dst_base, self.dst_stride, self.dbuf_cpu)
        super().execute(ci, c)
        allowed = self.allowed(c, st)
        for _, _, a, _ in self.write_log[n0:]:
            if allowed is None or not set(int(x) for x in a) <= allowed:
                self.violations.append((ci, c["op"]))
                break

    def allowed(self, c, st):
        op, A, w, h = c["op"], c["addr"], c["w"], c["h"]
        dst_base, dst_stride, cpu = st
        if op == M.OP_CHAR:
            w, h = M.CHAR_SCALE[c["sx"]][2], M.CHAR_SCALE[c["sy"]][2]
        if op == M.OP_RUN:
            h = 1
        if op in (M.OP_RUN, M.OP_RECT, M.OP_BAR, M.OP_CHAR, M.OP_COPY, M.OP_RRECT, M.OP_CORNERS):
            s = set()
            for y in range(h):
                row = (A + 512 * y) & M.MASK19
                off = M.DBUF_OFFSET if (self.p["DBUF_ENABLE"] and cpu and row < M.DBUF_LIMIT) else 0
                s.update(((A + 512 * y + x) & M.MASK19) + off for x in range(w))
            return s
        if op == M.OP_SBLIT:
            num, den = M.SBLIT_RATIO[c["sx"]]; w = min(127, w * den // num)
            num, den = M.SBLIT_RATIO[c["sy"]]; h = min(127, h * den // num)
        if op == M.OP_DSBLIT:
            num, den = M.DSBLIT_RATIO[c["sx"]]; w = min(127, w * num // den)
            num, den = M.DSBLIT_RATIO[c["sy"]]; h = min(127, h * num // den)
        if op in (M.OP_BLIT, M.OP_CBLIT, M.OP_SBLIT, M.OP_DSBLIT):
            return {(dst_base + A + y * dst_stride + x) & M.MASK25 for y in range(h) for x in range(w)}
        return None


def test_safety(quick):
    S = "safety"
    viol = 0; cmds = 0
    seeds = range(4 if quick else 12)
    for k in seeds:
        params, init, acts = G.gen_stream(0x5AFE0000 + k, n=150, profile="uniform", mode="ood",
                                          params={"SET": ["GALLERY", "TAU"][k % 2]})
        eng = Monitored(params, init); eng.violations = []
        for i, a in enumerate(acts):
            eng.t = i; eng.act(a)
        eng.drain()
        viol += len(eng.violations); cmds += len(eng.commands)
    check(S, "C-GEN-2 write-rectangle monitor on OOD random streams (%d commands)" % cmds, viol == 0, "%d violations" % viol)
    for s, name, rules, params, init, acts in G.directed_cases():
        if "ood" not in name and s not in ("V-RRECT", "V-CORN"):
            continue
        eng = Monitored(params, init); eng.violations = []
        for i, a in enumerate(acts):
            eng.t = i; eng.act(a)
        eng.drain()
        check(S, "C-GEN-2 monitor %s/%s" % (s, name), not eng.violations, str(eng.violations[:3]))


def test_replay(quick):
    S = "replay"
    vd = VECTORS
    if not os.path.isdir(vd) or not os.listdir(vd):
        check(S, "vectors present", False, "run tools/talos_v2_model/gen_vectors.py first"); return
    mem = M.Memory()
    n = bad = 0
    for fn in sorted(os.listdir(vd)):
        if not fn.endswith(".jsonl"):
            continue
        for i, line in enumerate(open(os.path.join(vd, fn))):
            if quick and i % 7:
                continue
            v = json.loads(line)
            M.run(v["params"], v["init"], v["actions"], memory=mem)
            e = M.run.last_engine
            ok = M.check_regions(mem, v["expect"]["regions"]) == 0 and mem.crc32() == v["expect"]["crc32"] and \
                [[t["t"], t["off"], t["v"]] for t in e.trace if t["ev"] == "read"] == v["expect"]["reads"]
            n += 1; bad += not ok
            if not ok:
                print("   replay mismatch", fn, v["name"])
    check(S, "%d vectors replay identically" % n, bad == 0, "%d mismatch" % bad)


# =====================================================================================================================
# 6. mutation checks
# =====================================================================================================================
PREFERRED = {"M1": ["V-RECT"], "M2": ["V-BAR"], "M3": ["V-CHAR"], "M4": ["V-CHAR"], "M5": ["V-CHAR"], "M6": ["V-BLIT"],
             "M7": ["V-BLIT"], "M7b": ["V-CBLIT", "V-SBLIT", "V-DSB"], "M8": ["V-SBLIT"], "M9": ["V-CBLIT"],
             "M10": ["V-RRECT", "V-CORN"], "M11": ["V-DSB"], "M12": ["V-DBUF"], "M13": ["V-DBUF"], "M14": ["V-FENCE"],
             "M15": ["V-STAT"], "M16": ["V-STAT"], "M17": ["V-FILL"], "M19": ["V-BLIT"], "M20": ["V-VIEW"], "M21": ["V-ESC"],
             "M22": ["V-CHAR"]}


def signature(mem, eng):
    return ([(t["ev"], t.get("off"), t.get("v"), t.get("line"), t.get("crc")) for t in eng.trace
             if t["ev"] in ("read", "fill", "vfill")], eng.status())


def test_mutants(quick):
    S = "mutation"
    cases = G.directed_cases()
    memA, memB = M.Memory(), M.Memory()
    killed = 0
    for mut in M.MUTANTS:
        pref = PREFERRED.get(mut.split("_")[0], [])
        order = [c for c in cases if c[0] in pref] + [c for c in cases if c[0] not in pref]
        by = None
        for s, name, rules, params, init, acts in order:
            M.run(params, init, acts, memory=memA, log_values=False); ea = M.run.last_engine
            try:
                M.run(dict(params, MUTANT=mut), init, acts, memory=memB, log_values=False); eb = M.run.last_engine
            except M.ModelHang:
                by = "%s/%s (hang)" % (s, name); break
            addrs = np.unique(np.concatenate([np.asarray(d, dtype=np.int64) for d in memA.dirty + memB.dirty])) \
                if (memA.dirty or memB.dirty) else np.zeros(0, dtype=np.int64)
            if not np.array_equal(memA.a[addrs], memB.a[addrs]) or signature(memA, ea) != signature(memB, eb) \
                    or not np.array_equal(ea.lb, eb.lb) or not np.array_equal(ea.vlb, eb.vlb):
                by = "%s/%s" % (s, name); break
        killed += by is not None
        check(S, "%s (%s) killed" % (mut, M.MUTANTS[mut]), by is not None, "SURVIVED")
        if by:
            print("   %-22s killed by %s" % (mut, by))
    check(S, "%d of %d mutants killed" % (killed, len(M.MUTANTS)), killed == len(M.MUTANTS))


# =====================================================================================================================
# 7. speed
# =====================================================================================================================
def test_perf(n):
    S = "speed"
    params, init, acts = G.gen_stream(0xFA57, n=n, profile="real", mode="legal", params={"SET": "TAU"})
    ncmd = sum(a.get("repeat", 1) for a in acts if a["a"] == "push") + sum(1 for a in acts if a["a"] == "fence")
    t0 = time.time()
    M.run(params, init, acts, log_values=False)
    dt = time.time() - t0
    FINDINGS.append("speed: %d pushes (real profile, %d stream actions) in %.1f s (%.0f commands/s)" % (ncmd, len(acts), dt, ncmd / dt))
    check(S, "%d-action stream runs (%.1f s)" % (n, dt), True)


def main():
    fast = "--fast" in sys.argv                                   # make test-host: no replay, no speed run
    quick = "--quick" in sys.argv or fast
    perf = 100_000
    if "--perf" in sys.argv:
        perf = int(sys.argv[sys.argv.index("--perf") + 1])
    t0 = time.time()
    for name, fn in (("worked examples", test_worked_examples), ("canvas.py", test_canvas), ("tau_fb.js", test_tau_fb),
                     ("HOST_TABLE", test_host_table), ("weights", test_weights), ("DSBLIT taps", test_dsb_taps),
                     ("safety", lambda: test_safety(quick)), ("replay", lambda: test_replay(quick)),
                     ("mutants", lambda: test_mutants(quick)), ("speed", lambda: test_perf(10_000 if quick else perf))):
        if fast and name in ("replay", "speed"):
            continue
        t = time.time()
        fn()
        print("[%s] done in %.1f s" % (name, time.time() - t))
    ok = sum(r[2] for r in RESULTS)
    print("\n%d checks, %d passed, %d failed (%.0f s)" % (len(RESULTS), ok, len(RESULTS) - ok, time.time() - t0))
    for sec in sorted({r[0] for r in RESULTS}):
        rs = [r for r in RESULTS if r[0] == sec]
        print("  %-14s %3d / %3d" % (sec, sum(r[2] for r in rs), len(rs)))
    print("\nFindings:")
    for f in FINDINGS:
        print("  - " + f)
    sys.exit(0 if ok == len(RESULTS) else 1)


if __name__ == "__main__":
    main()
