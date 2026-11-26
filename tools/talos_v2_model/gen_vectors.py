#!/usr/bin/env python3
"""Directed vector sets (conformance plan 2.2) and the constrained-random stream generator (section 4) for Talos v2.

Every vector is one JSON line in the format of conformance plan 2.1:
  set, name, rules, model_version, seed,
  params                      (a) build parameters ("SET" names a named set / matrix entry, plus overrides)
  init                        (b) initial memory: the probe pattern k*0x9E37+0x1234 at word k, plus "regions"
                                  [[addr, [words]]] and "const_regions" [[addr, n, value]]
                              (c) "clut" [256], "rc" [16], "font_ram"/"font_rom" ({"salt": s} = the synthetic
                                  atlas of talos_v2_model.synthetic_font, or a word list), "sticky", "text_light", "dbuf_cpu"
  actions                     (d) CPU actions: push (all cmd_* fields, optional "repeat"), mmio_w/mmio_r (byte offset),
                                  sticky (field, v), clut/rc/font (idx, [v...]), text_light, dbuf_cpu, flip_req,
                                  vblank, fill (line), view (en/base/stride/dbl inputs), wait_idle, fence, step,
                                  stall (memory unavailable: nothing dispatches), reset, cpu_w (CPU mailbox write)
  expect                      (e) "regions": the final word at every address any command wrote, RLE
                                  (["c", start, n, value] | ["w", start, [words]]); "crc32": CRC-32 (zlib) of the
                                  whole 2^25-word memory, little-endian words in address order
                              (f) "reads": [[t, offset, value]], "fills": [[t, line, crc32 of the words]],
                                  "status": the counters at the end, "domain": legal | ood, "nondeterministic": [...]
Usage:  python3 model/gen_vectors.py [--sets V-RECT,V-BAR,...] [--random N] [--out vectors]
"""
import argparse
import json
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import talos_v2_model as M  # noqa: E402

FONT_RAM = {"salt": 1}
FONT_ROM = {"salt": 2}


def P(op, **kw):
    d = {"a": "push", "op": op}
    d.update(kw)
    return d


def src_fields(S):
    """3.0: S = {fg[2:0], bg[15:0]}; fg[15:3] is set to a non-zero junk value to prove it is ignored."""
    return {"fg": 0xA5A8 | ((S >> 16) & 7), "bg": S & 0xFFFF}


def frame(x, y):
    return y * 512 + x


# ---- vector construction ---------------------------------------------------------------------------------------------
_MEM = None


def make_vector(set_name, name, rules, params, init, actions, seed=0, memory=None):
    global _MEM
    if _MEM is None:
        _MEM = M.Memory()
    mem, wl, tr = M.run(params, init, actions, memory=_MEM)
    eng = M.run.last_engine
    extra = [x for x in mem.dirty]          # also expect every word the init image or the CPU wrote
    regions = M.written_regions(mem, wl, extra_addrs=extra)
    domains = sorted({c["domain"] for c in eng.commands}) or ["legal"]
    v = {
        "set": set_name, "name": name, "rules": rules, "model_version": M.MODEL_VERSION, "seed": seed,
        "params": params, "init": init, "actions": actions,
        "expect": {
            "regions": regions, "crc32": mem.crc32(),
            "reads": [[e["t"], e["off"], e["v"]] for e in tr if e["ev"] == "read"],
            "fills": [[e["t"], e["line"], e["crc"]] for e in tr if e["ev"] in ("fill", "vfill")],
            "status": eng.status(),
            "retired_ops": [e["op"] for e in tr if e["ev"] == "retire"],
            "domain": "ood" if "ood" in domains else "legal",
            "ood_commands": [[i, c["notes"]] for i, c in enumerate(eng.commands) if c["domain"] == "ood"],
            "nondeterministic": eng.nondeterministic,
        },
    }
    if any(a["a"] == "fill" for a in actions):
        v["expect"]["linebuffer"] = [int(x) for x in eng.lb]
    return v


# ---- directed sets (conformance plan 2.2) ----------------------------------------------------------------------------
def v_rect():
    out = []
    Ws = [0, 1, 2, 127, 128, 400, 511]
    Hs = [0, 1, 2, 16, 359, 360, 511]
    for cpu in (0, 1):
        for w in Ws:
            for h in Hs:
                acts = [{"a": "dbuf_cpu", "v": cpu}, P(M.OP_RECT, addr=frame(0, 0), w=w, h=h, fg=0x1234 + w + h)]
                out.append(("rect_w%d_h%d_buf%d" % (w, h, cpu), ["C-RECT-1", "C-GEN-1", "C-DBUF-1"], {"SET": "TAU"}, {}, acts))
        for name, A, w, h in [("row_start", frame(0, 10), 37, 5), ("row_end", frame(512 - 37, 11), 37, 5),
                              ("stash_rows", frame(3, 700), 100, 20), ("stash_cols", frame(400, 5), 112, 30),
                              ("straddle_359_360", frame(5, 355), 50, 10), ("near_2e19", (1 << 19) - 512 * 3 - 20, 20, 3),
                              ("wrap_row", frame(500, 20), 30, 4), ("last_row", frame(0, 1023), 511, 1),
                              ("ood_past_2e19", (1 << 19) - 100, 200, 2)]:
            acts = [{"a": "dbuf_cpu", "v": cpu}, P(M.OP_RECT, addr=A, w=w, h=h, fg=0xBEEF)]
            out.append(("addr_%s_buf%d" % (name, cpu), ["C-RECT-1", "C-MAP-1", "C-DBUF-1"], {"SET": "TAU"}, {}, acts))
        for w in (0, 1, 100, 511):
            acts = [{"a": "dbuf_cpu", "v": cpu}, P(M.OP_RUN, addr=frame(7, 30), w=w, h=77, fg=0x0F0F)]
            out.append(("run_w%d_buf%d" % (w, cpu), ["C-RUN-1", "C-GEN-1"], {"SET": "TAU"}, {}, acts))
    return out


def v_bar():
    out = []
    for h in (1, 10, 200, 511):
        for lit in sorted({0, 1, h - 1, h, h + 1, 127, 128, 511}):
            if lit < 0:
                continue
            for w in (0, 1, 33, 511):
                if w == 511 and h == 511:
                    A = frame(0, 0)
                else:
                    A = frame(3, 2)
                acts = [P(M.OP_BAR, addr=A, w=w, h=h, glyph=lit & 0x7F, glyph_hi=lit >> 7, fg=0x07E0, bg=0x8010)]
                out.append(("bar_h%d_lit%d_w%d" % (h, lit, w), ["C-BAR-1", "C-GEN-1"], {"SET": "TAU"}, {}, acts))
    acts = [P(M.OP_BAR, addr=frame(10, 100), w=4, h=10, glyph=172 & 127, glyph_hi=172 >> 7, fg=1, bg=2),
            P(M.OP_BAR, addr=frame(20, 100), w=4, h=200, glyph=130 & 127, glyph_hi=130 >> 7, fg=3, bg=4)]
    out.append(("bar_probe_examples", ["C-BAR-1"], {"SET": "TAU"}, {}, acts))
    out.append(("bar_buf1_straddle", ["C-BAR-1", "C-DBUF-1"], {"SET": "TAU"}, {},
                [{"a": "dbuf_cpu", "v": 1}, P(M.OP_BAR, addr=frame(10, 350), w=9, h=20, glyph=5, fg=0xFFFF, bg=0)]))
    return out


COLOR_PAIRS = [(0xFFFF, 0x0000), (0x0000, 0xFFFF), (0xF800, 0x0000), (0x07E0, 0x0000), (0x001F, 0x0000),
               (0xE71C, 0x18E3), (0x2124, 0xDEFB), (0x7BEF, 0x4208)]


def v_char():
    out = []
    for tl in (0, 1):
        for blk in range(16):
            acts = [{"a": "text_light", "v": tl}]
            for i in range(16):
                code = blk * 16 + i
                fg, bg = COLOR_PAIRS[(code + tl) % len(COLOR_PAIRS)]
                acts.append(P(M.OP_CHAR, addr=frame(16 * i, 20 * blk), glyph=code & 0x7F, glyph_hi=(code >> 7) | 2,
                              fg=fg, bg=bg, w=5, h=7))
            out.append(("ram_codes_%02x_light%d" % (blk * 16, tl), ["C-CHAR-1", "C-CHAR-2", "C-CHAR-3", "C-CHAR-4", "C-FONT-1"],
                        {"SET": "TAU"}, {"font_ram": FONT_RAM}, acts))
    for blk in range(8):
        acts = []
        for i in range(16):
            code = blk * 16 + i
            acts.append(P(M.OP_CHAR, addr=frame(16 * i, 20 * blk), glyph=code, glyph_hi=1, fg=0xFFFF, bg=0x0841))
        out.append(("rom_codes_%02x" % (blk * 16), ["C-CHAR-1", "C-CHAR-2", "C-CHAR-3"],
                    {"SET": "TAU", "FONT_RAM_ENABLE": 0}, {"font_rom": FONT_ROM}, acts))
    codes = [0x20, 0x41, 0x67, 0x7E, 0x7F, 0xA0, 0xC9, 0xFF, 0x00, 0x9F]
    for sx in range(4):
        for sy in range(4):
            acts = [P(M.OP_CHAR, addr=frame(50 * (i % 8), 60 * (i // 8)), glyph=c & 0x7F, glyph_hi=c >> 7,
                      sx=sx, sy=sy, fg=0xFFE0, bg=0x0010) for i, c in enumerate(codes)]
            out.append(("scale_sx%d_sy%d" % (sx, sy), ["C-CHAR-2", "C-CHAR-6", "C-CHAR-5"], {"SET": "TAU"},
                        {"font_ram": FONT_RAM}, acts))
    for tname, table in (("shipped_dark", list(M.TEXT_W_DARK_SHIPPED)), ("derived_light", list(M.TEXT_W_LIGHT_DERIVED))):
        for tl in (0, 1):
            acts = [{"a": "text_light", "v": tl}] + [P(M.OP_CHAR, addr=frame(16 * i, 0), glyph=0x41 + i, fg=fg, bg=bg)
                                                      for i, (fg, bg) in enumerate(COLOR_PAIRS)]
            key = "TEXT_W_DARK" if "dark" in tname else "TEXT_W_LIGHT"
            out.append(("table_%s_light%d" % (tname, tl), ["C-CHAR-4"], {"SET": "TAU", key: table},
                        {"font_ram": FONT_RAM}, acts))
    acts = [{"a": "dbuf_cpu", "v": 1}, P(M.OP_CHAR, addr=frame(100, 350), glyph=0x58, sx=3, sy=3, fg=0xFFFF, bg=0)]
    out.append(("char_3x_buf1_straddle", ["C-CHAR-5", "C-DBUF-1"], {"SET": "TAU"}, {"font_ram": FONT_RAM}, acts))
    # font written through the MMIO path (FONT_IDX / FONT_DATA), then drawn: C-FONT-1 layout and C-ORD-4
    words = [((i * 0x11111111) ^ 0x76543210) & 0xFFFFFFFF for i in range(32)]
    acts = [{"a": "font", "idx": 32 * (0x41 - 0x20), "v": words}, {"a": "mmio_r", "off": M.R_FONT_CTRL},
            P(M.OP_CHAR, addr=frame(0, 0), glyph=0x41, fg=0xFFFF, bg=0)]
    out.append(("font_mmio_write_A", ["C-FONT-1", "C-ORD-4"], {"SET": "TAU"}, {"font_ram": FONT_RAM}, acts))
    return out


def v_copy():
    out = []
    for cpu in (0, 1):
        for w in (1, 2, 126, 127):
            for h in (1, 20):
                for ov, (sx, sy, dx, dy) in {"none": (0, 400, 200, 100), "left": (10, 50, 7, 50), "right": (7, 50, 10, 50),
                                             "up": (20, 60, 20, 57), "down": (20, 60, 20, 63)}.items():
                    acts = [{"a": "dbuf_cpu", "v": cpu}, P(M.OP_COPY, addr=frame(dx, dy), w=w, h=h, **src_fields(frame(sx, sy)))]
                    out.append(("copy_w%d_h%d_%s_buf%d" % (w, h, ov, cpu), ["C-COPY-1", "C-ORD-1", "C-ORD-2", "C-DBUF-1"],
                                {"SET": "TAU"}, {}, acts))
        acts = [{"a": "dbuf_cpu", "v": cpu}, P(M.OP_COPY, addr=frame(0, 355), w=50, h=10, **src_fields(frame(100, 352)))]
        out.append(("copy_straddle_buf%d" % cpu, ["C-COPY-1", "C-DBUF-1"], {"SET": "TAU"}, {}, acts))
    out.append(("copy_ood_w200", ["C-GEN-2"], {"SET": "TAU"}, {},
                [P(M.OP_COPY, addr=frame(0, 500), w=200, h=3, **src_fields(frame(0, 600)))]))
    return out


def sticky_all(src_base=0, src_stride=512, dst_base=0, dst_stride=512, key_en=0, key=0, blend_en=0, mode=0, alpha=0, reindex=0):
    """The seven BLT_DATA writes after one BLT_IDX write (2.2.2)."""
    return [{"a": "mmio_w", "off": M.R_BLT_IDX, "v": 0},
            {"a": "mmio_w", "off": M.R_BLT_DATA, "v": src_base}, {"a": "mmio_w", "off": M.R_BLT_DATA, "v": src_stride},
            {"a": "mmio_w", "off": M.R_BLT_DATA, "v": dst_base}, {"a": "mmio_w", "off": M.R_BLT_DATA, "v": dst_stride},
            {"a": "mmio_w", "off": M.R_BLT_DATA, "v": (key_en << 16) | key},
            {"a": "mmio_w", "off": M.R_BLT_DATA, "v": blend_en | (mode << 1) | (alpha << 8)},
            {"a": "mmio_w", "off": M.R_BLT_DATA, "v": reindex}]


EXTREMES = [0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x8410, 0x7BEF, 0x0821, 0xF81F, 0x07FF]


def v_blit():
    out = []
    bases = [0, 1 << 24, (1 << 25) - (1 << 16)]
    for sb in bases:
        for st in (0, 1, 511, 512, 513, 1023):
            for w in (1, 64, 127):
                acts = sticky_all(src_base=sb, src_stride=st, dst_base=(sb + 3_000_000) & M.MASK25, dst_stride=(st * 7 + 5) & 1023) + \
                       [P(M.OP_BLIT, addr=1234, w=w, h=3, **src_fields(77_777))]
                out.append(("addr_b%x_s%d_w%d" % (sb, st, w), ["C-BLIT-1", "C-DBUF-1"], {"SET": "TAU"}, {}, acts))
    # key: at row start, row end, every word
    S, A = 200_000, 300_000
    for where in ("start", "end", "every"):
        if where == "every":
            init = {"const_regions": [[S, 600, 0x1357]]}
            key = 0x1357
        else:
            init = {}
            key = M.pattern_word(S + (0 if where == "start" else 99))
        acts = sticky_all(key_en=1, key=key) + [P(M.OP_BLIT, addr=A & M.MASK19, w=100, h=1, **src_fields(S))]
        out.append(("key_%s" % where, ["C-BLIT-2"], {"SET": "TAU"}, init, acts))
    # blend: channel extremes in source rows and destination rows
    srcv = [EXTREMES[i % 10] for i in range(100)]
    dstv = [EXTREMES[(i // 10) % 10] for i in range(100)]
    init = {"regions": [[10_000, srcv], [20_000, dstv]]}
    for alpha in (0, 1, 127, 128, 254, 255):
        acts = sticky_all(blend_en=1, mode=0, alpha=alpha) + [P(M.OP_BLIT, addr=20_000, w=100, h=1, **src_fields(10_000))]
        out.append(("blend_m0_a%d" % alpha, ["C-BLIT-3"], {"SET": "TAU"}, init, acts))
    for mode in range(1, 8):
        acts = sticky_all(blend_en=1, mode=mode, alpha=77) + [P(M.OP_BLIT, addr=20_000, w=100, h=1, **src_fields(10_000))]
        out.append(("blend_m%d" % mode, ["C-BLIT-4"], {"SET": "TAU"}, init, acts))
    acts = sticky_all(blend_en=1, mode=0, alpha=128) + [P(M.OP_BLIT, addr=20_000, w=100, h=1, **src_fields(10_000))]
    out.append(("blend_param_off", ["C-BLIT-3"], {"SET": "TAU", "BLIT_BLEND_ENABLE": 0}, init, acts))
    acts = sticky_all(blend_en=1, mode=2, key_en=1, key=0xFFFF) + [P(M.OP_BLIT, addr=20_000, w=100, h=1, **src_fields(10_000))]
    out.append(("key_and_blend", ["C-BLIT-5"], {"SET": "TAU"}, init, acts))
    # overlaps (C-ORD-1/2): A left in one row, B down in one command, C down in chunks (bottom-up), right, up
    base = frame(50, 50)
    out.append(("overlap_A_left", ["C-ORD-1"], {"SET": "TAU"}, {},
                [P(M.OP_BLIT, addr=base, w=100, h=30, **src_fields(base + 5))]))
    out.append(("overlap_B_down_one_cmd", ["C-ORD-2"], {"SET": "TAU"}, {},
                [P(M.OP_BLIT, addr=base + 512 * 3, w=90, h=30, **src_fields(base))]))
    chunks = [P(M.OP_BLIT, addr=base + 512 * (3 + y), w=90, h=3, **src_fields(base + 512 * y)) for y in range(27, -1, -3)]
    out.append(("overlap_C_down_chunks", ["C-ORD-1"], {"SET": "TAU"}, {}, chunks))
    out.append(("overlap_right", ["C-ORD-1"], {"SET": "TAU"}, {},
                [P(M.OP_BLIT, addr=base + 5, w=100, h=30, **src_fields(base))]))
    out.append(("overlap_up", ["C-ORD-1"], {"SET": "TAU"}, {},
                [P(M.OP_BLIT, addr=base, w=90, h=30, **src_fields(base + 512 * 3))]))
    acts = sticky_all(blend_en=1, mode=1) + [P(M.OP_BLIT, addr=base, w=100, h=10, **src_fields(base + 3))]
    out.append(("overlap_left_blended", ["C-ORD-1"], {"SET": "TAU"}, {}, acts))
    out.append(("dbuf_never_offsets_blit", ["C-DBUF-1"], {"SET": "TAU"}, {},
                [{"a": "dbuf_cpu", "v": 1}, P(M.OP_BLIT, addr=frame(0, 10), w=50, h=5, **src_fields(frame(0, 500)))]))
    out.append(("ood_w300", ["C-GEN-2", "C-BLIT-6"], {"SET": "TAU"}, {},
                [P(M.OP_BLIT, addr=frame(0, 700), w=300, h=2, **src_fields(frame(0, 800)))]))
    return out


def v_sblit():
    out = []
    for s in (1, 2, 3, 42, 43, 63, 64, 127, 128, 200):
        for sx in range(4):
            for sy in range(4):
                acts = [P(M.OP_SBLIT, addr=frame(0, 0), w=s, h=s, sx=sx, sy=sy, **src_fields(frame(0, 600)))]
                out.append(("s%d_sx%d_sy%d" % (s, sx, sy), ["C-SBLIT-1", "C-SBLIT-2", "C-SBLIT-3"], {"SET": "TAU"}, {}, acts))
    out.append(("probe_60x60_3x", ["C-SBLIT-2"], {"SET": "TAU"}, {},
                [P(M.OP_SBLIT, addr=frame(0, 0), w=60, h=60, sx=3, sy=3, **src_fields(frame(0, 600)))]))
    out.append(("probe_5x3_1p5x", ["C-SBLIT-3"], {"SET": "TAU"}, {},
                [P(M.OP_SBLIT, addr=frame(0, 0), w=5, h=3, sx=1, sy=1, **src_fields(frame(0, 600)))]))
    acts = sticky_all(key_en=1, key=M.pattern_word(frame(0, 600)), blend_en=1, mode=0, alpha=64) + \
        [P(M.OP_SBLIT, addr=frame(0, 0), w=40, h=40, sx=2, sy=2, **src_fields(frame(0, 600)))]
    out.append(("key_blend_ignored", ["C-SBLIT-4"], {"SET": "TAU"}, {}, acts))
    acts = sticky_all(src_base=5_000_000, src_stride=700, dst_base=9_000_000, dst_stride=333) + \
        [P(M.OP_SBLIT, addr=17, w=30, h=20, sx=1, sy=3, **src_fields(123))]
    out.append(("sticky_bases_strides", ["C-SBLIT-1"], {"SET": "TAU"}, {}, acts))
    return out


def v_cblit():
    out = []
    rnd = random.Random(7)
    for ri in (0, 1, 200, 255):
        pal = [rnd.randrange(65536) for _ in range(256)]
        for w, h in ((1, 1), (127, 9), (64, 40)):
            acts = sticky_all(reindex=ri, key_en=1, key=M.pattern_word(frame(0, 600)), blend_en=1, alpha=99) + \
                [P(M.OP_CBLIT, addr=frame(10, 10), w=w, h=h, **src_fields(frame(0, 600)))]
            out.append(("ri%d_w%d_h%d" % (ri, w, h), ["C-CBLIT-1", "C-CBLIT-2", "C-CBLIT-3"], {"SET": "TAU"}, {"clut": pal}, acts))
    pal = [(i * 257) & 0xFFFF for i in range(256)]
    acts = [{"a": "clut", "idx": 250, "v": [0x1111 * (i + 1) & 0xFFFF for i in range(10)]}] + \
        [P(M.OP_CBLIT, addr=frame(390, 355), w=30, h=10, **src_fields(frame(0, 600)))]
    out.append(("clut_mmio_wrap_noclip", ["C-CBLIT-1", "C-CBLIT-3", "C-ORD-4"], {"SET": "TAU"}, {"clut": pal}, acts))
    return out


TABLES = {"zero": [0] * 16, "monotone": [0] + list(range(1, 16)),
          "round": [0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11]}   # "Helios' default" is not given (GAP-18)


def v_rrect(op=M.OP_RRECT, setname="TAU", prefix=""):
    out = []
    for tname, tab in TABLES.items():
        for r in list(range(16)) + [16, 64, 127]:
            acts = [P(op, addr=frame(20, 20), w=40, h=30, glyph=r, fg=0x1111, bg=0xEEEE)]
            out.append(("%sr%d_%s" % (prefix, r, tname), ["C-RRECT-1", "C-RRECT-2", "C-CORN-1"], {"SET": setname}, {"rc": tab}, acts))
    tab = TABLES["monotone"]
    for name, w, h, r in (("r_eq_h", 40, 6, 6), ("cut_eq_w", 15, 30, 15), ("ood_r_gt_h", 40, 3, 10), ("ood_cut_gt_w", 5, 30, 12),
                          ("w0", 0, 30, 5), ("h0", 30, 0, 5)):
        acts = [P(op, addr=frame(20, 20), w=w, h=h, glyph=r, fg=0x1111, bg=0xEEEE)]
        out.append(("%sedge_%s" % (prefix, name), ["C-RRECT-1", "C-GEN-2"], {"SET": setname}, {"rc": tab}, acts))
    acts = [{"a": "dbuf_cpu", "v": 1}, P(op, addr=frame(0, 345), w=40, h=30, glyph=8, fg=0x1111, bg=0xEEEE)]
    out.append(("%sbuf1_straddle" % prefix, ["C-DBUF-1"], {"SET": setname}, {"rc": tab}, acts))
    acts = [{"a": "rc", "idx": 1, "v": [1, 2, 3, 4]}, P(op, addr=frame(60, 60), w=20, h=20, glyph=4, fg=0x2222, bg=0xDDDD)]
    out.append(("%src_mmio" % prefix, ["C-ORD-4"], {"SET": setname}, {"rc": [0] * 16}, acts))
    return out


def v_corn():
    out = []
    g = {"SET": "GALLERY"}
    out.append(("gallery_r2_10x6", ["C-CORN-1"], g, {"rc": [0, 0, 1]},
                [P(M.OP_CORNERS, addr=frame(5, 5), w=10, h=6, glyph=2, fg=0x1111, bg=0xABCD)]))
    out.append(("gallery_r3_20x9", ["C-CORN-1"], g, {"rc": [0, 0, 1, 3]},
                [P(M.OP_CORNERS, addr=frame(5, 5), w=20, h=9, glyph=3, fg=0x1111, bg=0xABCD)]))
    out.append(("gallery_r0", ["C-CORN-1"], g, {"rc": [0, 5, 5, 5]},
                [P(M.OP_CORNERS, addr=frame(5, 5), w=20, h=9, glyph=0, bg=0xABCD)]))
    out.append(("gallery_w0", ["C-CORN-1"], g, {"rc": [0, 5, 5, 5]},
                [P(M.OP_CORNERS, addr=frame(5, 5), w=0, h=9, glyph=3, bg=0xABCD)]))
    out.append(("gallery_rect_after", ["C-CORN-1", "C-Q-1"], g, {"rc": [0, 0, 1, 3]},
                [P(M.OP_CORNERS, addr=frame(5, 5), w=20, h=9, glyph=3, bg=0xABCD),
                 P(M.OP_RECT, addr=frame(5, 5), w=20, h=1, fg=0x5555)]))
    out.append(("feature_off_noop", ["C-OP-1"], {"SET": "TAU"}, {"rc": [0, 0, 1, 3]},
                [P(M.OP_CORNERS, addr=frame(5, 5), w=20, h=9, glyph=3, bg=0xABCD)]))
    out += v_rrect(M.OP_CORNERS, "GALLERY", "sweep_")
    return out


def v_dsb():
    out = []
    g = {"SET": "GALLERY"}
    cases = [("r34", 0, 0, 64, 48), ("r23", 1, 1, 60, 45), ("r12", 2, 2, 80, 40), ("r13", 3, 3, 90, 60),
             ("mix_34x12", 0, 2, 64, 40), ("mix_13x23", 3, 1, 90, 30), ("w96_23by34", 1, 0, 96, 12)]
    for st in (512, 1208):
        for name, sx, sy, w, h in cases:
            acts = sticky_all(src_base=2_000_000, src_stride=st, dst_base=3_000_000, dst_stride=512) + \
                [P(M.OP_DSBLIT, addr=0, w=w, h=h, sx=sx, sy=sy, **src_fields(0))]
            out.append(("%s_stride%d" % (name, st), ["C-DSB-1"], g, {}, acts))
    for sx in range(4):
        for s in (1, 2, 3, 4, 5, 168, 169, 170, 171, 190, 191, 254, 255, 381, 382, 511):
            acts = sticky_all(src_base=2_000_000, src_stride=600, dst_base=3_000_000, dst_stride=512) + \
                [P(M.OP_DSBLIT, addr=0, w=s, h=s, sx=sx, sy=sx, **src_fields(0))]
            out.append(("extent_sel%d_s%d" % (sx, s), ["C-DSB-1"], g, {}, acts))
    acts = sticky_all(src_base=2_000_000, src_stride=512, dst_base=3_000_000, key_en=1,
                      key=M.pattern_word(2_000_000), blend_en=1, mode=1) + \
        [P(M.OP_DSBLIT, addr=0, w=96, h=12, sx=1, sy=0, **src_fields(0))]
    out.append(("key_blend_ignored", ["C-DSB-2"], g, {}, acts))
    out.append(("feature_off_noop", ["C-OP-1"], {"SET": "TAU"}, {},
                [P(M.OP_DSBLIT, addr=0, w=96, h=12, sx=1, sy=0, **src_fields(0))]))
    return out


def v_fence():
    out = []
    t = {"SET": "TAU"}
    rd = lambda off, lab=None: {"a": "mmio_r", "off": off, "label": lab}
    out.append(("fence_alone", ["C-FENCE-1", "C-IDLE-1"], t, {},
                [P(M.OP_FENCE), rd(M.R_FB_GO, "busy after push"), rd(M.R_GFX_FENCE), {"a": "wait_idle"},
                 rd(M.R_GFX_FENCE), rd(M.R_GFX_STATUS)]))
    for op in range(16):
        acts = [P(op, addr=frame(1, 1), w=3, h=2, glyph=0x41, fg=1, bg=2), P(M.OP_FENCE), {"a": "step"},
                rd(M.R_GFX_FENCE, "fence not yet retired"), rd(M.R_GFX_STATUS), {"a": "step"}, rd(M.R_GFX_FENCE), rd(M.R_FB_GO)]
        out.append(("after_op%d" % op, ["C-FENCE-1", "C-RET-1"], {"SET": "GALLERY"}, {"font_ram": FONT_RAM, "rc": [0, 1, 2], "clut": list(range(256))}, acts))
    out.append(("fifty_in_a_row", ["C-FENCE-1"], t, {}, [P(M.OP_FENCE, repeat=50), rd(M.R_GFX_STATUS),
                                                          {"a": "wait_idle"}, rd(M.R_GFX_FENCE), rd(M.R_GFX_STATUS)]))
    acts = [{"a": "stall", "v": 1}]
    for i in range(300):
        acts.append(P(M.OP_FENCE) if i % 3 == 0 else P(M.OP_RECT, addr=frame(i % 400, 2), w=1, h=1, fg=i))
    acts += [rd(M.R_GFX_STATUS), rd(M.R_GFX_COUNTS), rd(M.R_FB_GO), {"a": "stall", "v": 0}, {"a": "wait_idle"},
             rd(M.R_GFX_FENCE), rd(M.R_GFX_STATUS)]
    out.append(("stalled_300_interleaved", ["C-FENCE-1", "C-Q-2", "C-Q-3"], t, {}, acts))
    out.append(("fence_counter_via_action", ["C-FENCE-1"], t, {}, [{"a": "fence"}, {"a": "fence"}, rd(M.R_GFX_FENCE)]))
    return out


def v_esc():
    out = []
    for setname in ("ALL_OFF", "ESC", "GALLERY", "CORNERS_DSBLIT"):
        for op in range(10, 16):
            for sub in (0, 1, 63):
                word = M.encode_fb_go({"op": op, "subop": sub, "glyph": 3})
                acts = [{"a": "mmio_w", "off": M.R_FB_ADDR, "v": frame(4, 4)}, {"a": "mmio_w", "off": M.R_FB_SIZE, "v": 8 | (8 << 9)},
                        {"a": "mmio_w", "off": M.R_FB_COLOR, "v": 0x1234 | (0x5678 << 16)},
                        {"a": "mmio_w", "off": M.R_FB_GO, "v": word}, {"a": "wait_idle"},
                        {"a": "mmio_r", "off": M.R_GFX_STATUS}]
                out.append(("%s_op%d_sub%d" % (setname, op, sub), ["C-ESC-1", "C-OP-1", "C-IF-1", "C-GEN-1"],
                            {"SET": setname}, {"rc": [0, 1, 2, 3]}, acts))
    return out


def v_dbuf():
    out = []
    t = {"SET": "TAU"}
    straddle = [P(M.OP_RUN, addr=frame(0, 359), w=30), P(M.OP_RECT, addr=frame(40, 355), w=20, h=10, fg=0x1111),
                P(M.OP_BAR, addr=frame(70, 355), w=5, h=10, glyph=4, fg=0x2222, bg=0x3333),
                P(M.OP_CHAR, addr=frame(90, 350), glyph=0x41, fg=0xFFFF, bg=0),
                P(M.OP_COPY, addr=frame(120, 355), w=20, h=10, **src_fields(frame(150, 352))),
                P(M.OP_RRECT, addr=frame(200, 352), w=30, h=12, glyph=4, fg=0x4444, bg=0x5555),
                P(M.OP_BLIT, addr=frame(250, 355), w=20, h=10, **src_fields(frame(280, 352))),
                P(M.OP_SBLIT, addr=frame(300, 355), w=10, h=5, sx=2, sy=2, **src_fields(frame(280, 352))),
                P(M.OP_CBLIT, addr=frame(330, 355), w=20, h=10, **src_fields(frame(280, 352))),
                P(M.OP_DSBLIT, addr=frame(360, 355), w=20, h=20, sx=2, sy=2, **src_fields(frame(280, 340))),
                P(M.OP_CORNERS, addr=frame(380, 352), w=30, h=12, glyph=4, bg=0x6666)]
    for cpu in (0, 1):
        for setname in ("TAU", "GALLERY"):
            acts = [{"a": "dbuf_cpu", "v": cpu}] + straddle + [{"a": "mmio_r", "off": M.R_DBUF_CPU}]
            out.append(("straddle_all_buf%d_%s" % (cpu, setname), ["C-DBUF-1"], {"SET": setname},
                        {"font_ram": FONT_RAM, "rc": TABLES["monotone"], "clut": list(range(256))}, acts))
    for nflip in (0, 1, 2, 3):
        acts = [{"a": "mmio_w", "off": M.R_DBUF_DISP, "v": 1}] * nflip + \
               [{"a": "mmio_r", "off": M.R_DBUF_DISP}, {"a": "fill", "line": 0}, {"a": "vblank"},
                {"a": "mmio_r", "off": M.R_DBUF_DISP}, {"a": "fill", "line": 1}, {"a": "fill", "line": 359}, {"a": "vblank"},
                {"a": "mmio_r", "off": M.R_DBUF_DISP}]
        out.append(("flips_%d_before_vblank" % nflip, ["C-DBUF-2", "C-FILL-1", "C-FILL-5"], t, {}, acts))
    acts = [{"a": "dbuf_cpu", "v": 1}, {"a": "mmio_w", "off": M.R_DBUF_DISP, "v": 1}, {"a": "vblank"},
            P(M.OP_RECT, addr=frame(0, 0), w=400, h=360, fg=0x0F0F), {"a": "flip_req"}, {"a": "vblank"},
            {"a": "mmio_r", "off": M.R_DBUF_DISP}, {"a": "mmio_r", "off": M.R_DBUF_CPU}]
    out.append(("dbuf_disabled_ignores", ["3.14"], {"SET": "TAU", "DBUF_ENABLE": 0}, {}, acts))
    return out


def v_fill():
    out = []
    for fw in (400, 512):
        for disp in (0, 1):
            acts = ([{"a": "flip_req"}, {"a": "vblank"}] if disp else []) + \
                [{"a": "fill", "line": n} for n in (0, 1, 358, 359)] + [{"a": "mmio_r", "off": M.R_DBUF_DISP}]
            out.append(("lines_fw%d_disp%d" % (fw, disp), ["C-FILL-1"], {"SET": "TAU", "FILL_WORDS": fw}, {}, acts))
    acts = [P(M.OP_RECT, addr=frame(0, 5), w=400, h=1, fg=0xAAAA), {"a": "fill", "line": 5}, {"a": "wait_idle"},
            {"a": "fill", "line": 5}]
    out.append(("fill_sees_retired_only", ["C-FILL-1", "C-Q-1"], {"SET": "TAU"}, {}, acts))
    return out


def v_stat():
    out = []
    t = {"SET": "TAU"}
    rd = lambda off: {"a": "mmio_r", "off": off}
    acts = [{"a": "stall", "v": 1}, rd(M.R_GFX_STATUS)]
    for i in range(300):
        acts.append(P(M.OP_RECT, addr=frame(i % 400, 3), w=1, h=1, fg=i))
        if i in (0, 1, 253, 254, 255, 299):
            acts += [rd(M.R_GFX_STATUS), rd(M.R_FB_GO), rd(M.R_GFX_COUNTS)]
    acts += [{"a": "stall", "v": 0}, {"a": "step", "n": 100}, rd(M.R_GFX_STATUS), {"a": "wait_idle"}, rd(M.R_GFX_STATUS),
             rd(M.R_GFX_COUNTS)]
    out.append(("t3_300_into_stalled", ["C-Q-2", "C-Q-3", "C-Q-4", "C-IDLE-1", "C-RET-1"], t, {}, acts))
    acts = [{"a": "stall", "v": 1}, P(M.OP_RECT, addr=0, w=1, h=1, repeat=255), P(M.OP_RECT, addr=0, w=1, h=1, repeat=66_000),
            rd(M.R_GFX_COUNTS), {"a": "stall", "v": 0}, {"a": "wait_idle"}, rd(M.R_GFX_COUNTS)]
    out.append(("ovf_saturates", ["C-Q-3"], t, {}, acts))
    acts = [rd(M.R_GFX_STATUS), P(M.OP_RECT, addr=0, w=1, h=1), rd(M.R_FB_GO), rd(M.R_GFX_STATUS), {"a": "wait_idle"},
            rd(M.R_FB_GO), rd(M.R_GFX_STATUS)]
    out.append(("push_then_busy", ["C-IDLE-1"], t, {}, acts))
    for setname in ("ALL_OFF", "CORNERS", "DSBLIT", "ESC", "VIEW", "VIEW_DBL", "GALLERY", "CORNERS_DSBLIT", "DEFAULTS"):
        acts = [rd(o) for o in (M.R_GFX_STATUS, M.R_TEXT_MODE, M.R_DBUF_CPU, M.R_DBUF_DISP, M.R_FONT_CTRL,
                                M.R_VIEW_CAPS, M.R_VIEW_STRIDE, M.R_GFX_COUNTS, M.R_GFX_FENCE)]
        out.append(("caps_%s" % setname, ["1.5", "2.2.3", "C-IF-5"], {"SET": setname}, {}, acts))
    return out


def v_rst():
    out = []
    t = {"SET": "TAU"}
    rd = lambda off: {"a": "mmio_r", "off": off}
    acts = [{"a": "stall", "v": 1}, P(M.OP_RECT, addr=frame(0, 1), w=10, h=1, fg=7, repeat=300), P(M.OP_FENCE),
            rd(M.R_GFX_STATUS), rd(M.R_GFX_COUNTS), {"a": "reset"}, rd(M.R_GFX_STATUS), rd(M.R_GFX_COUNTS), rd(M.R_GFX_FENCE),
            P(M.OP_FENCE), {"a": "wait_idle"}, rd(M.R_GFX_FENCE)]
    out.append(("reset_full_queue", ["C-RST-1"], t, {}, acts))
    for op in range(12):
        acts = sticky_all(src_base=1000, src_stride=300, reindex=9) + [
            {"a": "clut", "idx": 0, "v": [5, 6, 7]}, {"a": "rc", "idx": 1, "v": [1, 2]},
            P(M.OP_RECT, addr=frame(0, 0), w=4, h=4, fg=0x1111), {"a": "wait_idle"},
            P(op, addr=frame(10, 10), w=8, h=8, glyph=2, fg=0x2222, bg=0x3333), {"a": "reset"},
            rd(M.R_GFX_STATUS), {"a": "flip_req"}, {"a": "vblank"},
            P(M.OP_CBLIT, addr=frame(30, 30), w=3, h=1, **src_fields(0)),
            P(M.OP_RRECT, addr=frame(40, 40), w=8, h=8, glyph=2, fg=0x4444, bg=0x5555), {"a": "wait_idle"},
            rd(M.R_DBUF_DISP), rd(M.R_GFX_FENCE)]
        out.append(("reset_pending_op%d" % op, ["C-RST-1", "C-RST-3", "C-DBUF-3"], {"SET": "GALLERY"},
                    {"font_ram": FONT_RAM, "clut": [0x0101 * (i % 200) for i in range(256)]}, acts))
    return out


def v_view():
    out = []
    for dbl in (0, 1):
        acts = [{"a": "mmio_w", "off": M.R_VIEW_BASE, "v": 4096}, {"a": "mmio_w", "off": M.R_VIEW_STRIDE, "v": 1208},
                {"a": "mmio_w", "off": M.R_VIEW_CTRL, "v": dbl}, {"a": "view", "en": 1},
                {"a": "fill", "line": 0},                                   # still UI: mode latches at vblank only
                {"a": "vblank"}, {"a": "mmio_r", "off": M.R_VIEW_CAPS}]
        acts += [{"a": "fill", "line": n} for n in range(720)]
        acts += [{"a": "mmio_w", "off": M.R_VIEW_BASE, "v": 9000}, {"a": "fill", "line": 5},   # mid-frame change: no effect
                 {"a": "vblank"}, {"a": "fill", "line": 5},
                 {"a": "view", "en": 0}, {"a": "vblank"}, {"a": "fill", "line": 5}]
        out.append(("frame_dbl%d" % dbl, ["C-VIEW-1", "C-VIEW-2", "C-DBL-1"], {"SET": "GALLERY"}, {}, acts))
    acts = [{"a": "mmio_w", "off": M.R_VIEW_CTRL, "v": 1}, {"a": "view", "en": 1, "dbl": 1}, {"a": "vblank"},
            {"a": "fill", "line": 3}, {"a": "fill", "line": 4}]
    out.append(("dbl_without_param", ["C-DBL-1"], {"SET": "VIEW"}, {}, acts))
    acts = [{"a": "view", "en": 1}, {"a": "vblank"}, P(M.OP_RECT, addr=0, w=4, h=1, fg=1), {"a": "fill", "line": 1},
            {"a": "reset"}, {"a": "fill", "line": 2}]
    out.append(("reset_clears_latched_mode", ["C-VIEW-1", "C-RST-1"], {"SET": "GALLERY"}, {}, acts))
    return out


DIRECTED = {"V-RECT": v_rect, "V-BAR": v_bar, "V-CHAR": v_char, "V-COPY": v_copy, "V-BLIT": v_blit, "V-SBLIT": v_sblit,
            "V-CBLIT": v_cblit, "V-RRECT": v_rrect, "V-CORN": v_corn, "V-DSB": v_dsb, "V-FENCE": v_fence, "V-ESC": v_esc,
            "V-DBUF": v_dbuf, "V-FILL": v_fill, "V-STAT": v_stat, "V-RST": v_rst, "V-VIEW": v_view}


def directed_cases(sets=None):
    """-> [(set, name, rules, params, init, actions)] (no expectations: cheap, used by the mutation checks)."""
    out = []
    for s, fn in DIRECTED.items():
        if sets and s not in sets:
            continue
        for name, rules, params, init, acts in fn():
            out.append((s, name, rules, params, init, acts))
    return out


# ---- constrained-random streams (conformance plan section 4) ----------------------------------------------------------
REAL_MIX = [(M.OP_RECT, 45), (M.OP_CHAR, 30), (M.OP_BLIT, 10), (M.OP_BAR, 5), (M.OP_CBLIT, 4), (M.OP_SBLIT, 2), (M.OP_FENCE, 4)]
EDGES = [0, 1, 2, 126, 127, 128, 255, 256, 359, 360, 400, 511]


def gen_stream(seed, n=200, profile="real", mode="legal", params=None):
    """-> (params, init, actions). Implements: opcode mix (real/uniform), size draws (half edges, half uniform),
    address classes (visible, stash rows/cols, 359/360 straddles, x+w = 512, page edges, near 2^19, bases near 2^24
    and the top of 2^25), overlaps with p = 0.2, sticky / CLUT / corner / font / text_light / CPU-buffer changes only
    after a fence (legal) or anywhere (ood), fence storms, flips and vblanks, fills, queue back-pressure (stall while
    300 pushes arrive), reset mid-run (1 run in 20). Cycle-level hazards (p0 gaps, p0_ready delays, fill jitter) are
    not expressible at transaction level: see REPORT.md section "random generator"."""
    rnd = random.Random(seed)
    params = dict(params or {"SET": "GALLERY"})
    p = M.resolve_params(params)
    legal = mode == "legal"
    init = {"font_ram": FONT_RAM, "font_rom": FONT_ROM, "rc": [0] + sorted(rnd.randrange(0, 12) for _ in range(15)),
            "clut": [rnd.randrange(65536) for _ in range(256)]}
    acts = []
    pending_changes = []

    def size(lo, hi):
        if rnd.random() < 0.5:
            v = rnd.choice(EDGES)
            return min(max(v, 0), hi) if legal else min(v, 511)
        return rnd.randint(lo, hi)

    def faddr(w, h):
        k = rnd.random()
        if k < 0.4:
            x, y = rnd.randrange(400), rnd.randrange(360)
        elif k < 0.55:
            x, y = rnd.randrange(512), rnd.randrange(360, 1024)
        elif k < 0.65:
            x, y = rnd.randrange(400, 512), rnd.randrange(1024)
        elif k < 0.75:
            x, y = rnd.randrange(400), rnd.randrange(340, 360)
        elif k < 0.85:
            x, y = max(0, 512 - w), rnd.randrange(1000)
        elif k < 0.92:
            x, y = rnd.randrange(512), 2 * rnd.randrange(512) + 1        # page edge: 1,024-word pages
        else:
            A = (1 << 19) - 512 * max(1, h) - max(1, w)
            return max(0, A) if legal else rnd.randrange((1 << 19) - 600, 1 << 19)
        A = y * 512 + x
        if legal:
            while A + 512 * (max(h, 1) - 1) + max(w, 1) - 1 >= (1 << 19):
                A -= 512 * 64
        return A & M.MASK19

    def sticky_change():
        f = rnd.randrange(7)
        if f in (0, 2):
            v = rnd.choice([0, rnd.randrange(1 << 25), (1 << 24) - rnd.randrange(4096), (1 << 25) - rnd.randrange(1 << 16)])
        elif f in (1, 3):
            v = rnd.choice([0, 1, 511, 512, 513, 1023, rnd.randrange(1024)])
        elif f == 4:
            v = (rnd.randrange(2) << 16) | rnd.randrange(65536)
        elif f == 5:
            v = rnd.randrange(2) | (rnd.randrange(8) << 1) | (rnd.choice([0, 1, 127, 128, 254, 255, rnd.randrange(256)]) << 8)
        else:
            v = rnd.randrange(256)
        return {"a": "sticky", "field": f, "v": v}

    def other_change():
        k = rnd.randrange(6)
        if k == 0: return {"a": "clut", "idx": rnd.randrange(256), "v": [rnd.randrange(65536) for _ in range(rnd.randint(1, 8))]}
        if k == 1: return {"a": "rc", "idx": rnd.randrange(16), "v": [rnd.randrange(32) for _ in range(rnd.randint(1, 4))]}
        if k == 2: return {"a": "font", "idx": rnd.randrange(M.FONT_RAM_WORDS - 8), "v": [rnd.randrange(1 << 32) for _ in range(4)]}
        if k == 3: return {"a": "text_light", "v": rnd.randrange(2)}
        if k == 4: return {"a": "dbuf_cpu", "v": rnd.randrange(2)}
        return sticky_change()

    for i in range(n):
        r = rnd.random()
        if r < 0.05:
            ch = sticky_change() if rnd.random() < 0.6 else other_change()
            if legal:
                acts.append({"a": "fence"})                                    # C-ORD-4: change only after a fence
            acts.append(ch)
            continue
        if r < 0.07:
            acts.append(P(M.OP_FENCE, repeat=rnd.randint(1, 50)))              # fence storm
            continue
        if r < 0.09:
            acts += [{"a": "flip_req"}] * rnd.randint(1, 2) + [{"a": "vblank"}]
            continue
        if r < 0.13:
            acts.append({"a": "fill", "line": rnd.randrange(360)})
            continue
        if r < 0.135:
            acts += [{"a": "stall", "v": 1}, P(M.OP_RECT, addr=faddr(4, 4), w=4, h=4, fg=rnd.randrange(65536), repeat=300),
                     {"a": "mmio_r", "off": M.R_GFX_STATUS}, {"a": "stall", "v": 0}]
            continue
        if r < 0.16:
            acts.append({"a": "step", "n": rnd.randint(1, 20)})
            continue
        if profile == "real":
            ops, wts = zip(*REAL_MIX)
            op = rnd.choices(ops, wts)[0]
        else:
            op = rnd.randrange(16)
        w = size(1, 127 if (op in (M.OP_BLIT, M.OP_CBLIT, M.OP_COPY) and legal) else 511)
        h = size(1, 511)
        if legal and op in (M.OP_BLIT, M.OP_CBLIT, M.OP_COPY):
            w = min(w, 127)
        cmd = P(op, w=w, h=h, fg=rnd.randrange(65536), bg=rnd.randrange(65536), glyph=rnd.randrange(128),
                glyph_hi=rnd.randrange(4), sx=rnd.randrange(4), sy=rnd.randrange(4), subop=rnd.randrange(64))
        if op == M.OP_CHAR:
            cmd["addr"] = faddr(48, 48)
        elif op in (M.OP_RRECT, M.OP_CORNERS):
            cmd["addr"] = faddr(w, h)
            if legal:                                                           # C-RRECT-1 domain: r <= h, cut <= w
                cmd["glyph"] = rnd.randrange(min(h, 16)) if h else 0
                cmd["w"] = max(w, 32) if w else 0                               # rc entries are drawn below 32
        else:
            cmd["addr"] = faddr(w, h)
        if op in (M.OP_COPY, M.OP_BLIT, M.OP_CBLIT, M.OP_SBLIT, M.OP_DSBLIT):
            if rnd.random() < 0.2:                                              # overlap, four directions
                d = rnd.choice([1, -1, 512, -512, 3, -3 * 512])
                S = (cmd["addr"] + d) & M.MASK19
            else:
                S = faddr(w, h)
            cmd.update(src_fields(S))
        acts.append(cmd)
        if not legal and rnd.random() < 0.05:
            acts.append(other_change())                                        # OOD: changes at random points
    if rnd.randrange(20) == 0:
        acts.insert(rnd.randrange(len(acts)), {"a": "reset"})
    acts += [{"a": "wait_idle"}, {"a": "mmio_r", "off": M.R_GFX_STATUS}, {"a": "mmio_r", "off": M.R_GFX_FENCE},
             {"a": "mmio_r", "off": M.R_GFX_COUNTS}]
    return params, init, acts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sets", default="")
    ap.add_argument("--random", type=int, default=6, help="random streams per profile (legal real, legal uniform, ood)")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                                  "work", "talos_v2_model", "vectors"))      # repository copy: git-ignored work/
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    sets = [s for s in a.sets.split(",") if s]
    total = 0
    t0 = time.time()
    for s, fn in DIRECTED.items():
        if sets and s not in sets:
            continue
        path = os.path.join(a.out, s + ".jsonl")
        n = 0
        with open(path, "w") as f:
            for name, rules, params, init, acts in fn():
                f.write(json.dumps(make_vector(s, name, rules, params, init, acts, seed=0)) + "\n")
                n += 1
        total += n
        print("%-8s %4d vectors  -> %s" % (s, n, os.path.relpath(path)))
    if a.random and (not sets or "V-RANDOM" in sets):
        path = os.path.join(a.out, "V-RANDOM.jsonl")
        n = 0
        with open(path, "w") as f:
            for k in range(a.random):
                for prof, mode in (("real", "legal"), ("uniform", "legal"), ("uniform", "ood")):
                    seed = 0x7A105000 + 3 * k + ["real", "uniform"].index(prof) + (mode == "ood")
                    params, init, acts = gen_stream(seed, n=150, profile=prof, mode=mode,
                                                    params={"SET": ["GALLERY", "TAU", "ESC", "CORNERS_DSBLIT"][k % 4]})
                    v = make_vector("V-RANDOM", "%s_%s_seed%08x" % (prof, mode, seed), ["section 4"], params, init, acts, seed=seed)
                    v["expect"]["compare"] = "full" if mode == "legal" else "safety-only"
                    f.write(json.dumps(v) + "\n")
                    n += 1
        total += n
        print("%-8s %4d vectors  -> %s" % ("V-RANDOM", n, os.path.relpath(path)))
    print("%d vectors in %.1f s (model %s)" % (total, time.time() - t0, M.MODEL_VERSION))


if __name__ == "__main__":
    main()
