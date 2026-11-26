#!/usr/bin/env python3
"""ORACLE-SIDE experiments that settle open contract items on the OLD Talos engine (audit B-1181).
NEVER hand this file or its output to the clean-room RTL author. Each experiment is a short action list (the G1 vector
action format plus "delay") run through sim/tb_talos_v2_diff.v; the facts are printed and written to
work/talos_v2_diff/experiments.json.
    python3 tools/talos_v2_model/oracle_experiments.py [names...]
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import talos_v2_model as M  # noqa: E402
import run_diff as R  # noqa: E402

P = lambda op, **kw: dict({"a": "push", "op": op}, **kw)        # noqa: E731
RD = lambda off: {"a": "mmio_r", "off": off}                      # noqa: E731
FENCE_ADDR = 0


def run(actions, params=None, init=None, eng=None):
    p = M.resolve_params(dict(params or {"SET": "TAU"}))
    e, key = R.oracle_of(p)
    if eng == "gal":
        key = ("gal",) + key[1:5] + (p["VIEW_ENABLE"],)
    vvp = R.build(key, os.path.join(R.WORK, "build"))
    lines, soc, _ = R.stimulus(p, init or {}, actions)
    res = R.run_bench(vvp, lines, "exp_%d" % os.getpid())
    rmem = {}
    for ci, a, d in res["writes"]:
        rmem[a] = d
    res["mem"] = rmem
    res["soc"] = soc
    return res


def word(res, a):
    return res["mem"].get(a, int(M.pattern_word(a)))


FACTS = {}


def fact(name, **kw):
    FACTS[name] = kw
    print("%-26s %s" % (name, json.dumps(kw)))


def e_q7():
    """C-DBUF-3 / Q7: the displayed buffer and flip pending across the engine reset."""
    r = run([{"a": "flip_req"}, {"a": "vblank"}, RD(M.R_DBUF_DISP), {"a": "reset"}, RD(M.R_DBUF_DISP),
             {"a": "flip_req"}, RD(M.R_DBUF_DISP), {"a": "reset"}, RD(M.R_DBUF_DISP), {"a": "vblank"}, RD(M.R_DBUF_DISP)])
    q = [r["q"][t] for t in sorted(r["q"])]
    fact("Q7_dbuf_across_reset", after_flip=q[0][5:7], after_reset=q[1][5:7], flip_requested=q[2][5:7],
         after_second_reset=q[3][5:7], after_vblank=q[4][5:7], note="(disp, pending)")


def e_gap07():
    """C-Q-2/-3 GAP-07: 300 pushes into a stalled memory, stall before any dispatch vs after a dispatch."""
    a = [{"a": "stall", "v": 1}] + [P(M.OP_RECT, addr=3 * 512 + i % 400, w=1, h=1, fg=i) for i in range(300)] + \
        [RD(M.R_GFX_STATUS), RD(M.R_GFX_COUNTS), {"a": "stall", "v": 0}]
    r = run(a)
    acc = sum(x for _, x in r["acc"])
    q = [r["q"][t] for t in sorted(r["q"])]
    fact("GAP07_stall_before_dispatch", accepted=acc, dropped=300 - acc, free=q[0][2], ovf=q[1][4])
    a = [P(M.OP_RECT, addr=0, w=400, h=360, fg=1), {"a": "delay", "n": 60}, {"a": "stall", "v": 1}] + \
        [P(M.OP_RECT, addr=3 * 512 + i % 400, w=1, h=1, fg=i) for i in range(300)] + \
        [RD(M.R_GFX_STATUS), RD(M.R_GFX_COUNTS), {"a": "stall", "v": 0}]
    r = run(a)
    acc = sum(x for _, x in r["acc"][1:])
    q = [r["q"][t] for t in sorted(r["q"])]
    fact("GAP07_stall_after_dispatch", accepted_of_300=acc, dropped=300 - acc, free=q[0][2], ovf=q[1][4],
         note="the first RECT was popped and is waiting for p0_available: it no longer occupies a slot")


def e_gap09():
    """GAP-09: fill_seen and fill_done_line across the engine reset."""
    r = run([{"a": "fill", "line": 7}, {"a": "reset"}])
    z = r["z"]
    fact("GAP09_fill_state_after_reset", fill_done_line=z[7], fill_seen=z[8])


def e_gap01():
    """GAP-01: Gallery's viewer with FILL_WORDS 512 (and 400): words written per viewer line."""
    for fw in (400, 512):
        r = run([{"a": "view", "en": 1, "base": 4096, "stride": 2000}, {"a": "vblank"}, {"a": "fill", "line": 2}],
                params={"SET": "GALLERY", "FILL_WORDS": fw, "VIEW_LINE_WORDS": 3 * fw}, eng="gal")
        f = r["fills"][0][3] if r["fills"] else []
        exp = [M.pattern_word(4096 + 2 * 2000 + i) for i in range(2048)]
        n = 0
        while n < 2048 and f[n] == exp[n]:
            n += 1
        fact("GAP01_view_line_fw%d" % fw, words_fetched=n, note="Gallery fetches 3 x FILL_WORDS per viewer line")
    r = run([{"a": "fill", "line": 3}], params={"SET": "TAU", "FILL_WORDS": 512})
    fact("GAP01_main_fw512_no_view", elaborates=True, fill_words=sum(1 for x in r["fills"][0][3][:512]))


def e_gap25():
    """GAP-25: a UI fill of a line >= 360 with the displayed buffer 1."""
    # the probe pattern repeats every 65,536 words and 1,048,576 is a multiple of it: mark the two buffers first
    mark = [{"a": "cpu_w", "addr": b + 512 * n, "v": [0xA000 + n % 4096 + (0x1000 if b else 0)] * 4} for b in (0, 1048576) for n in (359, 600)]
    r = run(mark + [{"a": "flip_req"}, {"a": "vblank"}, {"a": "fill", "line": 600}, {"a": "fill", "line": 359}])
    f600, f359 = r["fills"][0][3], r["fills"][1][3]
    fact("GAP25_fill_line600_disp1", reads_buffer0=f600[0] == 0xA000 + 600, reads_buffer1=f600[0] == 0xB000 + 600,
         line359_reads_buffer1=f359[0] == 0xB000 + 359)
    k = np.arange(1 << 20, dtype=np.int64)
    fact("PATTERN_period", period_words=65536, same_word_in_both_buffers=bool(np.all(((k * 0x9E37 + 0x1234) & 0xFFFF) == (((k + 1048576) * 0x9E37 + 0x1234) & 0xFFFF))))


def e_fill3():
    """C-FILL-3: two fill requests close together; which gaps lose one."""
    out = {}
    for gap in (0, 20, 100, 200, 380, 400, 405, 410, 420, 440, 500, 800):
        a = [{"a": "fillreq", "line": 10}, {"a": "delay", "n": gap}, {"a": "fillreq", "line": 11}, {"a": "delay", "n": 3000}, {"a": "ystat"}]
        r = run(a)
        out[gap] = r["y"][-1][1:] if r.get("y") else None
    fact("CFILL3_two_requests", fills_done_by_gap_cycles={k: v for k, v in out.items()})
    # a request arriving while a long command runs (the engine is busy, not filling)
    r = run([P(M.OP_RECT, addr=0, w=511, h=300, fg=5), {"a": "fillreq", "line": 20}, {"a": "delay", "n": 5}, {"a": "fillreq", "line": 21},
             {"a": "delay", "n": 6000}, {"a": "ystat"}])
    fact("CFILL3_two_requests_while_busy", y=r.get("y"))


def e_gap05():
    """GAP-05: a row that crosses word 184,320 inside itself (row 359, x 500, w 30) with the CPU buffer 1."""
    A = 359 * 512 + 500
    r = run([{"a": "dbuf_cpu", "v": 1}, P(M.OP_RECT, addr=A, w=30, h=1, fg=0x1357)])
    written = sorted(a for a, d in r["mem"].items() if d == 0x1357)
    fact("GAP05_row_crossing_184320", first=written[0], last=written[-1], count=len(written),
         all_in_buffer1=all(a >= 1048576 for a in written), note="decided on the row's first word: the whole row moves")


def e_wide():
    """D-T24 item 6 baseline: blit-class and COPY widths >= 128 (OOD today)."""
    for op in (M.OP_COPY, M.OP_BLIT, M.OP_CBLIT):
        for w in (127, 128, 129, 200, 255, 256, 384):
            S = 100 * 512
            r = run([P(op, addr=300 * 512, w=w, h=2, fg=(S >> 16) & 7, bg=S & 0xFFFF)], init={"clut": list(range(256))})
            ret = r["ret"][0] if r["ret"] else None
            nw = len([a for a in r["mem"] if 300 * 512 <= a < 302 * 512 + 512])
            fact("WIDE_%s_w%d" % (M.OP_NAMES[op], w), words_written=nw, write_reqs=ret[5] if ret else None, read_words=ret[4] if ret else None,
                 stub_cycles=ret[2] if ret else None, protocol=r["prot"])


def e_overlap():
    """D-T24 item 6 baseline: one-command overlapping COPY/BLIT in the four directions vs a memmove."""
    for op in (M.OP_COPY, M.OP_BLIT):
        for name, d in (("left", -1), ("right", 1), ("up", -512), ("down", 512), ("down3", 3 * 512), ("right5", 5)):
            S = 50 * 512 + 100
            A = S + d
            w, h = 64, 12
            r = run([P(op, addr=A, w=w, h=h, fg=(S >> 16) & 7, bg=S & 0xFFFF)])
            src = {(x, y): M.pattern_word(S + 512 * y + x) for x in range(w) for y in range(h)}
            bad_memmove = sum(1 for (x, y), v in src.items() if word(r, A + 512 * y + x) != v)
            mm = M.run({"SET": "TAU"}, {}, [P(op, addr=A, w=w, h=h, fg=(S >> 16) & 7, bg=S & 0xFFFF)])[0]
            bad_model = sum(1 for (x, y) in src if word(r, A + 512 * y + x) != int(mm.a[A + 512 * y + x]))
            fact("OVERLAP_%s_%s" % (M.OP_NAMES[op], name), words_differing_from_memmove=bad_memmove, words_differing_from_model=bad_model, of=w * h)


def e_sblit_cost():
    """D-T24 item 4 baseline: SBLIT cost per output pixel on the functional stub."""
    for sx, w, h in ((0, 40, 40), (2, 40, 40), (3, 40, 40), (0, 127, 16), (2, 63, 8), (1, 80, 10)):
        r = run([P(M.OP_SBLIT, addr=0, w=w, h=h, sx=sx, sy=sx, fg=1, bg=0)])
        ret = r["ret"][0]
        n, d = M.SBLIT_RATIO[sx]
        ow, oh = min(127, w * d // n), min(127, h * d // n)
        fact("SBLIT_COST_s%d_%dx%d" % (sx, w, h), out_px=ow * oh, stub_cycles=ret[2], cycles_per_px=round(ret[2] / (ow * oh), 2),
             read_reqs=ret[3], write_reqs=ret[5])
    r = run([P(M.OP_BLIT, addr=0, w=127, h=16, fg=1, bg=0)])
    fact("BLIT_COST_127x16", stub_cycles=r["ret"][0][2], cycles_per_px=round(r["ret"][0][2] / (127 * 16), 2))


def e_gap13():
    """GAP-13: write traffic of a keyed BLIT (every source word keyed)."""
    S = 80 * 512
    init = {"const_regions": [[S, 16, 0x5555], [S + 512, 16, 0x5555]]}
    a = [{"a": "sticky", "field": 4, "v": (1 << 16) | 0x5555}, P(M.OP_BLIT, addr=200 * 512, w=16, h=2, fg=0, bg=S & 0xFFFF)]
    r = run(a, init=init)
    ret = r["ret"][0]
    fact("GAP13_keyed_blit_writes", write_words=ret[6], of=32, destination_unchanged=all(word(r, 200 * 512 + 512 * y + x) == M.pattern_word(200 * 512 + 512 * y + x) for x in range(16) for y in range(2)))


def e_gap15():
    """GAP-15: SBLIT 2x whose destination is the source one row down (overlap), against the model."""
    S = 40 * 512
    a = [P(M.OP_SBLIT, addr=S + 512, w=20, h=6, sx=2, sy=2, fg=0, bg=S & 0xFFFF)]
    r = run(a)
    mm = M.run({"SET": "TAU"}, {}, a)[0]
    bad = sum(1 for y in range(13) for x in range(40) if word(r, S + 512 * (y + 1) + x) != int(mm.a[S + 512 * (y + 1) + x]))
    fact("GAP15_sblit_overlap_down", words_differing_from_model=bad, of=12 * 40)


def e_gap16():
    """GAP-16: CHAR 1x whose rows cross 2^19 (row start near the top of the 19-bit space)."""
    A = (1 << 19) - 8
    r = run([P(M.OP_CHAR, addr=A, glyph=0x41, fg=0xFFFF, bg=0)], init={"font_ram": {"salt": 1}})
    ws = sorted(r["mem"])
    fact("GAP16_char_across_2e19", lowest=ws[0], highest=ws[-1], above_2e19=sum(1 for a in ws if a >= (1 << 19)),
         note="the row burst continues linearly past 2^19; the next row start wraps (19-bit row address)")


def e_gap29():
    """GAP-29: the CPU buffer changed while a multi-row RECT is executing."""
    r = run([P(M.OP_RECT, addr=0, w=400, h=300, fg=0x2222), {"a": "delay", "n": 200}, {"a": "dbuf_cpu", "v": 1}])
    rows0 = sum(1 for y in range(300) if r["mem"].get(512 * y) == 0x2222)
    rows1 = sum(1 for y in range(300) if r["mem"].get(1048576 + 512 * y) == 0x2222)
    fact("GAP29_cpu_buffer_change_mid_rect", rows_in_buffer0=rows0, rows_in_buffer1=rows1,
         note="dbuf_cpu_buf is read per row request: the rest of the command follows the new buffer")


EXPS = {"q7": e_q7, "gap07": e_gap07, "gap09": e_gap09, "gap01": e_gap01, "gap25": e_gap25, "fill3": e_fill3,
        "gap05": e_gap05, "wide": e_wide, "overlap": e_overlap, "sblit_cost": e_sblit_cost, "gap13": e_gap13,
        "gap15": e_gap15, "gap16": e_gap16, "gap29": e_gap29}


def main():
    names = sys.argv[1:] or list(EXPS)
    os.makedirs(R.WORK, exist_ok=True)
    path = os.path.join(R.WORK, "experiments.json")
    for n in names:
        EXPS[n]()
        old = json.load(open(path)) if os.path.exists(path) else {}
        old.update(FACTS)
        json.dump(old, open(path, "w"), indent=1, default=str)
    print("facts:", os.path.relpath(path, R.ROOT))


if __name__ == "__main__":
    main()
