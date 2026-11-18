#!/usr/bin/env python3
"""The meter registry of fw2/ui/meters.c after the P2.4 rewrite (docs/features/meters/EXTERNAL_METERS_P2_PLAN.md P2.4): rows are the TOC's, in TOC order, up to 64; hidden rows and rows this firmware cannot run (ABI, tau_min) are
skipped by X and marked; names and previews come from the TOC; a tuning key shared by two rows disables tuning for both; and the COMMIT RULE (safe boot): the saved selection follows a newly chosen meter only after MTR_COMMIT_SECS (5 s)
of clean running, a hang, a strike or a refusal inside that window leaves the previous meter in the saved word, a newly installed meter is never selected by itself.
The real fw2/ui/meters.c and fw2/ui/appearance.c run in sim/p2_meter_window_harness.c against a model of the Pocket (PSRAM copy of the TOC, the window, fence.i, a music stream with a read in flight, a free-running cycle counter that
the test advances); the bins are composed by tools/meter_bin.py from the real packs (tools/player2/build_packs.py) and by tools/make_synthetic_meter_bin.py.

WHAT THIS PROVES AND WHAT IT DOES NOT: it proves the rule on the host model of the register-file word (`save` runs appearance_get + the real settings pack, `restart` runs the boot restore): a meter cut by the watchdog / struck out
/ refused / not yet clean never reaches the saved word, so the next boot starts on the last clean meter. It does NOT prove what a Pocket does when the core RESETS or hangs for good (no register file is saved then: the Pocket
saves the persist values on Quit only, so a hang plus a power cycle never persists a selection at all) nor the wall-clock behaviour of the real 66.667 MHz counter (here the test moves the counter).
Mutants: a selection commits at once; a strike leaves the pending selection alive (it would commit Winamp Bars); a refusal commits; hidden rows are selectable; unavailable rows are selectable; the collision rule is ignored."""
import re, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb, build_packs as bp, make_synthetic_meter_bin as sy, tune_capacity as tc, meter_manifest as mm
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("  " + str(info)[:500] if info and not ok else ""))
    fails += 0 if ok else 1
FB = 0x7DDDFE63
def build(d, mutant=None):
    src = (ROOT / "fw2/ui/meters.c").read_text(); flags = []
    def sub(a, b):
        nonlocal src
        assert a in src, a[:60]; src = src.replace(a, b, 1)
    if mutant == "commit_at_once": sub("    else pend_start();\n}", "    else commit_now();\n}")
    if mutant == "nodrop": sub("    pend_drop();                                                         /* a selection still waiting for its clean run never reaches the saved word */\n", "")
    if mutant == "refusal_commits": sub("if (refused) { msel_aborts++; return; }", "if (refused) { msel_aborts++; commit_now(); return; }")
    if mutant == "hidden_ok": sub("if (fb || (av && vis)) toc_sel", "if (fb || av) toc_sel")
    if mutant == "unavail_ok": sub("if (fb || (av && vis)) toc_sel", "if (fb || vis) toc_sel")
    if mutant == "collide_ignored": flags = ["-DBUG=9"]
    if mutant == "hidden_listed": sub("if (fb || vis) toc_lst", "if (fb || av) toc_lst")
    if mutant == "step_all": sub("if (r != row && meters_present(r)) return r;", "if (r != row) return r;")
    if mutant == "cfg_commits": sub("    select_row(row, 1u);\n    cfv_on = 1u;", "    select_row(row, 0u);\n    cfv_on = 1u;")
    if mutant == "cfg_noend": sub("    cfv_on = 0u;\n    select_row(cfv_set, 1u);", "    cfv_on = 0u;")
    if mutant == "cfg_nopend": sub("if (cfv_pend) { if (cur == cfv_set)", "if (0) { if (cur == cfv_set)")
    if mutant == "cfg_nopreset": sub("if (cur == cfv_set && !act.custom) meters_preset_restore(cur, cfv_id, cfv_pre);", "")
    ms = (ROOT / "fw2/ui/meters_settings.c").read_text()
    def ssub(a, b):
        nonlocal ms
        assert a in ms, a[:60]; ms = ms.replace(a, b, 1)
    if mutant == "set_rev_ignored": ms = ms.replace("tune_key(id_hash, params_rev)", "tune_key(id_hash, 0)")
    if mutant == "set_nomax": ssub(" && s->val[k] <= max[s->idx[k]]", "")
    vsrc = (ROOT / "fw2/ui/meterlist_view.c").read_text() if (ROOT / "fw2/ui/meterlist_view.c").exists() else ""
    def vsub(a, b):
        nonlocal vsrc
        assert a in vsrc, a[:60]; vsrc = vsrc.replace(a, b, 1)
    if mutant == "nogap": vsub("gfx_rect(0, ye, GFX_W, GFX_H - ye, BG);", "(void)ye;")
    if mutant == "unavail_pickable": vsub("if (!(meters_row_flags(row) & MROW_SELECTABLE)) { ml_msg = what == MLG_X ? 2u : 1u; return MLG_REFUSED; }", "")
    if mutant == "x_nopreview": vsub("if (meters_cfg_begin(row)) { ml_msg = 4u; return MLG_REFUSED; }", "")
    if mutant == "gear_all": vsub("if (meters_row_configurable(row)) gear_draw", "if (1) gear_draw")
    if mutant == "x_noparams": vsub("if (!meters_row_configurable(row)) { ml_msg = 3u; return MLG_REFUSED; }", "")
    if mutant == "nofit": vsub("if (n < 2u || gfx_text_width(s) <= maxw) return;", "return;")
    if mutant == "noindicator": vsub("gfx_text(GFX_W - 20 - gfx_text_width(pos), 6, pos, DIM, BG);", "(void)pos;")
    ui = d / "fw2/ui"; ui.mkdir(parents=True)
    for sub_ in ("gfx", "platform", "cores", "generated", "diagnostics"): (d / "fw2" / sub_).mkdir()
    (ui / "meters.c").write_text(src); (ui / "meterlist_view.c").write_text(vsrc); (ui / "meters_settings.c").write_text(ms)
    for f in ("ui/meterlist_thumb.c", "ui/meterlist_thumb.h", "ui/meterlist_view.h", "ui/meters_settings.c", "ui/meters_settings.h", "generated/meter_thumbs.h", "ui/meter.h", "ui/meters_toc.h", "ui/theme.h", "ui/meter_pack.h", "ui/appearance.h", "ui/appearance.c", "gfx/gfx.h", "gfx/font.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/pack_strike_core.h",
              "cores/helios_core.h", "cores/shipped_meter_ids.h", "cores/meter_desc_core.h", "cores/mpreset_core.h", "cores/mpreset_host.h", "cores/meter_pack_core.h", "cores/meterbin_core.h", "cores/library_core.h", "cores/audio_stats_core.h",
              "cores/settings_core.h", "generated/meter_presets.h", "generated/meter_registry.h", "cores/gfx_touch_core.h", "cores/ramp_core.h"):
        pth = ROOT / "fw2" / f
        if not pth.exists(): continue
        if (d / "fw2" / f).exists(): continue
        (d / "fw2" / f).parent.mkdir(parents=True, exist_ok=True); (d / "fw2" / f).write_text(pth.read_text())
    (d / "sim").mkdir(); (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_window_harness.c").read_text()); (d / "sim/p2_host_regs.h").write_text((ROOT / "sim/p2_host_regs.h").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-DWD_HOST_CUT", *flags, "-include", str(d / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr[:3000]); sys.exit(1)
    return d / "h"
def run(exe, script): return subprocess.run([str(exe)], input="cut 0\n" + script, capture_output=True, text=True, timeout=120).stdout.strip().splitlines()      # the watchdog model runs the packs unless a script says `cut 1`
def lst(o): return [l for l in o if l.startswith("list")][0]
def flg(o): return [l for l in o if l.startswith("flags")]
def state(out):
    ln = [l for l in out if l.startswith("state")][-1].split(); kv = {}; i = 1
    while i < len(ln) and ln[i] != "present": kv[ln[i]] = int(ln[i + 1], 16) if ln[i] in ("last_hash", "saved", "pend") else int(ln[i + 1]); i += 2
    j = ln.index("present") + 1; pres = []
    while ln[j] != "pstate": pres.append(int(ln[j])); j += 1
    kv["present"] = pres; return kv
def saves(o): return [int(re.search(r"hash ([0-9a-f]+)", l).group(1), 16) for l in o if l.startswith("saved")]
def lastsel(o): return [l for l in o if l.startswith("restarted")][-1]
def mans(k): return mm.load(ROOT / "meters" / k / "meter.json")
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    (tmp / "wk").mkdir(); shipped = bp.build_entries(tmp / "wk")
    exe = build(tmp / "good")
    def binf(entries, name): p = tmp / name; p.write_bytes(mb.compose(entries)); return p
    b7 = binf(shipped, "b7.bin")
    h = {e.name: int(e.id[:8], 16) for e in shipped}
    HS = [int(e.id[:8], 16) for e in shipped]                      # winamp bars, scope, chladni, layered wave, vu master, waterfall, sound map
    # ---------- rows: 7 and 64, TOC order, X wraps, names and previews from the TOC
    o = run(exe, "user %s\nboot\nlist\nstate\n" % b7); s = state(o)
    check("7 rows: the list holds all seven in TOC order", o[-2] == "list 7: 0 1 2 3 4 5 6" and s["rows"] == 7 and s["skipped"] == 0 and s["count"] == 7, o[-3:])
    syn = sy.build(64, False, False, real=shipped); b64 = binf(syn, "b64.bin")
    o = run(exe, "user %s\nboot\nlist\nstate\n" % b64); s = state(o)
    check("64 rows: the list reaches every row, in TOC order (0..63)", o[-2] == "list 64: " + " ".join(map(str, range(64))) and s["rows"] == 64 and s["count"] == 64 and s["mtoc_count"] == 64 and s["mtoc_err"] == 0, o[-3:])
    o = run(exe, "user %s\nboot\nwalk 65\nstate\n" % b64); w = [int(x) for x in [l for l in o if l.startswith("walk")][0].split()[1:]]
    check("X over 64 rows visits 1, 2 ... 63, wraps to 0 and goes on to 1, each a pack loaded or the built-in meter, no error", w == list(range(1, 64)) + [0, 1] and state(o)["err"] == 0 and state(o)["collisions"] == 0, str(w[-4:]) + str(state(o)))
    o = run(exe, "user %s\nboot\nprev\nprev\nstate\n" % b64)
    check("X backwards wraps from row 0 to 63 and goes on to 62", [l for l in o if l.startswith("cur ")] == ["cur 63", "cur 62"])
    o = run(exe, "user %s\nboot\nflags 20\nflags 0\nflags 63\nthumb 20\nthumb 0\n" % b64)
    info = mb.open_toc(b64.read_bytes())["rows"]
    check("names come from the TOC rows (row 20 is TEST 21, row 63 TEST 64, row 0 the built-in Winamp Bars)", ["row 20 17 name TEST 21" in " ".join(o), "row 63 17 name TEST 64" in " ".join(o)] == [True, True] and re.search(r"flags row 0 \d+ name WINAMP BARS", " ".join(o)), o[-5:])
    t = [l for l in o if l.startswith("thumb")]
    check("the preview of row 20 is read from its TOC preview block (length, first run bytes)", int(t[0].split()[4]) == info[20]["preview_len"] - 64 and t[1].split()[4] == "0", t)
    o = run(exe, "user %s\nboot\nswap 130\nstate\n" % b64); s = state(o)
    check("130 selections walking the 64 rows with the music stream mid-read: no refusal, no collision with the stream, every pack load counted", s["err"] == 0 and s["collisions"] == 0 and s["loads"] == 127 and s["mtoc_err"] == 0, str(s))
    # ---------- hidden rows, rows this firmware cannot run
    def mod(es, f): es = [mb.Entry(**e.__dict__) for e in es]; f(es); return es
    def hide3(es): es[3].flags = mb.FLAG_VISIBLE & 0
    hid = binf(mod(shipped, hide3), "hid.bin")
    o = run(exe, "user %s\nboot\nlist\nflags 3\nselect 2\nnext\nnext\nstate\n" % hid)
    check("a hidden row is skipped by the list and its flags say not visible, not selectable (flags 0)", lst(o) == "list 6: 0 1 2 4 5 6" and flg(o)[0].split()[2:4] == ["3", "0"], o)
    cs = [l for l in o if l.startswith("cur ")]
    check("... X from row 2 goes to 4", cs[-2:] == ["cur 4", "cur 5"], cs)
    def abi_up(es): es[2].pack = bytearray(es[2].pack); es[2].pack[4] = 3; es[2].pack = bytes(es[2].pack)
    def tau_up(es): es[4].tau_min = 0x7F00
    un = binf(mod(shipped, lambda es: (abi_up(es), tau_up(es))), "una.bin")
    o = run(exe, "user %s\nboot\nlist\nflags 2\nflags 4\nselect 2\nstate\nwalk 7\n" % un); s = state(o)
    fl = [int(l.split()[3]) for l in o if l.startswith("flags")]
    check("rows that need a newer ABI or TAU are skipped and MARKED: the bin is not refused, the list has 5 rows, MSEL SKIPPED 2, flags carry UNAVAILABLE (2) and not SELECTABLE (16), select refuses them", lst(o) == "list 5: 0 1 3 5 6" and s["skipped"] == 2 and s["mtoc_err"] == 0 and all(f & 2 and not f & 16 for f in fl) and s["cur"] == 0, str(o[:6]) + str(fl) + str(s["cur"]))
    w = [int(x) for x in [l for l in o if l.startswith("walk")][0].split()[1:]]
    check("... and X never lands on them", 2 not in w and 4 not in w and set(w) == {0, 1, 3, 5, 6}, w)
    # ---------- the commit rule
    lim = mans("chladni")["p2"]["max_cycles"]
    CL = "advance 6\ntick 0\n"
    o = run(exe, "user %s\nboot\nselect 2\ntick 0\nsave\nstate\n" % b7); s = state(o)
    check("a newly selected meter is NOT in the saved word at once: the word still holds the previous meter (Winamp Bars), the selection is pending", saves(o)[-1] == FB and s["pend"] == HS[2] and s["commits"] == 0, str(saves(o)) + str(s))
    o = run(exe, "user %s\nboot\nselect 2\nadvance 4\ntick 0\nsave\nstate\nadvance 2\ntick 0\nsave\nstate\n" % b7)
    sv = saves(o); sts = [state(o[:k + 1]) for k, l in enumerate(o) if l.startswith("state")]
    check("after 4 s still the previous meter; after 6 s of clean running the saved word is the new meter (Chladni), committed once, nothing pending", sv == [FB, HS[2]] and sts[1]["commits"] == 1 and sts[1]["pend"] == 0 and sts[0]["clean"] == 4, str(sv) + str(sts))
    o = run(exe, "user %s\nboot\nselect 0\nsave\nselect 2\nadvance 6\ntick 0\nselect 0\nsave\n" % b7)
    check("Winamp Bars (built in, it cannot hang) commits at once: choosing it again after Chladni needs no wait", saves(o)[-1] == FB, saves(o))
    o = run(exe, "user %s\nboot\nselect 1\n%ssave\nselect 2\ntick 0\nselect 1\nstate\nsave\n" % (b7, CL)); s = state(o)
    check("choosing the committed meter again cancels the pending selection (Scope stays saved, nothing pending)", saves(o)[-1] == HS[1] and s["pend"] == 0, str(saves(o)) + str(s))
    # a hang: the watchdog cuts the new meter inside the window
    o = run(exe, "user %s\nboot\ncut 0\nselect 1\n%ssave\ncut 1\nselect 2\ntick 0\nadvance 6\ntick 0\nsave\nstate\nrestart\nstate\n" % (b7, CL)); sts = [state(o[:k + 1]) for k, l in enumerate(o) if l.startswith("state")]; s = sts[0]; sv = saves(o)
    check("a hang inside the window (cut by the watchdog): Winamp Bars takes over, the saved word stays on the LAST CLEAN meter (Scope), the cut is counted as an aborted selection", sv == [HS[1], HS[1]] and s["strikes"] == 1 and s["aborts"] >= 1 and s["pend"] == 0, str(sv) + str(s))
    check("... and the next boot (the host model of Quit and start again) starts on the last clean meter (Scope, row 1), not Winamp Bars and not the hung meter", lastsel(o) == "restarted cur 1" and sts[1]["cur"] == 1, lastsel(o))
    over = "tick %d\n" % (3 * lim)
    o = run(exe, "user %s\nboot\nselect 1\n%sselect 2\ntick 0\n%s%s%sadvance 6\ntick 0\nsave\nstate\n" % (b7, CL, over, over, over)); s = state(o)
    check("three overruns of the cycle limit inside the window strike the new meter out: the saved word stays on the previous clean meter", saves(o)[-1] == HS[1] and s["strikes"] == 1 and s["cur"] == 0 and s["pend"] == 0, str(saves(o)) + str(s))
    bad = bytearray(b7.read_bytes()); off = mb.u32(bad, 40 + 256 * 2 + 92); bad[off + 48 + 100] ^= 0x10; (tmp / "crc.bin").write_bytes(bytes(bad))
    o = run(exe, "user %s\nboot\nselect 1\n%sselect 2\ntick 0\nadvance 6\ntick 0\nsave\nstate\n" % (tmp / "crc.bin", CL)); s = state(o)
    check("a pack refused at selection (CRC): Winamp Bars shows, the saved word stays on the previous clean meter, the refusal counts as an aborted selection and marks the row", saves(o)[-1] == HS[1] and s["err"] == 26 and s["aborts"] == 1, str(saves(o)) + str(s))
    o = run(exe, "user %s\nboot\nselect 1\n%ssave\nstate\n" % (b7, CL))
    # a saved meter that is gone is kept (P2.2 behaviour); the user picking another replaces it
    small = binf([e for e in shipped if e.name != "CHLADNI"], "nochl.bin")
    o = run(exe, "user %s\nboot\nselect 2\n%ssave\nuser %s\nrestart\nsave\nselect 1\nsave\nadvance 6\ntick 0\nsave\n" % (b7, CL, small)); sv = saves(o)
    check("a saved meter that is no longer in the TOC: Winamp Bars runs and the saved word keeps it (twice written back unchanged); it is replaced when the user picks another and that one runs clean", sv[:3] == [HS[2]] * 3 and sv[3] == HS[1], sv)
    # a newly installed meter is never selected automatically
    syn1 = sy.build(8, False, False, real=shipped); new = binf([shipped[0], syn1[7], *shipped[1:]], "ins.bin")          # a new row inserted at position 1, in front of the saved meter
    o = run(exe, "user %s\nboot\nselect 2\n%ssave\nuser %s\nrestart\nstate\n" % (b7, CL, new)); s = state(o)
    check("a newly installed meter (a row never selected before) is never selected by itself: the saved meter (Chladni, now row 3) is selected by its hash, not the new row 1 that took its place in the order", "restarted cur 3" in o and s["count"] == 8 and s["cur"] == 3, lastsel(o))
    # ---------- the manifest id of a row (METR, the decimal preset folder) is the same for the shipped rows of a USER bin as of the core's own bin; a synthetic row has none
    MID = {0: 12, 1: 13, 2: 14, 3: 16, 4: 15, 5: 17, 6: 18}
    for nm, bn, src in (("the core's own bin", b7, "core"), ("a 64-row user bin whose first rows are the shipped ones", b64, "user")):
        o = run(exe, "%s %s\nboot\n" % (src, bn) + "".join("select %d\nmid\n" % r for r in range(7)))
        got = {int(l.split()[2]): int(l.split()[4]) for l in o if l.startswith("mid ")}
        check("the manifest id of the seven shipped rows is 12, 13, 14, 16, 15, 17, 18 in %s (it picks the decimal preset folder and the METR entry)" % nm, got == MID, got)
    o = run(exe, "user %s\nboot\nselect 9\nmid\n" % b64)
    check("a synthetic row has no manifest id (65535): only its hash folder is read", " manifest 65535 " in [l for l in o if l.startswith("mid ")][0], o[-2:])
    # ---------- tuning key collisions
    cb = binf(sy.build(64, False, True, real=shipped), "col.bin")
    o = run(exe, "user %s\nboot\nstate\nselect 9\nstate\nedit 1\nshow\nadvance 6\ntick 0\ntune\nselect 2\nedit 1\ntune\n" % cb)
    sts = [state(o[:k + 1]) for k, l in enumerate(o) if l.startswith("state")]; tn = [list(map(int, l.split()[1:])) for l in o if l.startswith("tune")]
    check("two rows that share the low 16 bits of their id_hash: both flagged (MSEL NOTUNE 2), the meter on screen says so, it runs from its defaults and an edit saves NO tuning; a normal row saves one",
          sts[0]["notune"] == 2 and sts[1]["curnotune"] == 1 and sts[0]["curnotune"] == 0 and not any(tn[0]) and any(tn[1]) and any(l.startswith("C ") for l in o), str(sts[:2]) + str(tn))
    o = run(exe, "user %s\nboot\nflags 8\nflags 9\nflags 2\n" % cb)
    fl = [int(l.split()[3]) for l in o if l.startswith("flags")]
    check("... and the flags of those two rows carry NO_TUNING (8), another row's do not", fl[0] & 8 and fl[1] & 8 and not fl[2] & 8, fl)
    # ---------- the parameter maxima the migration needs, from the generated table
    mx = [tc.maxes(d) for d in sorted((mm.load(f) for f in (ROOT / "meters").glob("*/meter.json") if True), key=lambda d: d.get("p2", {}).get("slot", 99)) if "p2" in d]
    o = run(exe, "".join("maxes %d\n" % i for i in range(8)))
    got = [list(map(int, l.split()[1:])) for l in o]
    check("meters_shipped_max_of(i) returns the parameter maxima of each shipped meter exactly as the manifests give them (tune_capacity.maxes); an index past the seven answers none", [g[1:] for g in got[:7]] == mx and [g[0] for g in got[:7]] == [len(m) for m in mx] and got[7] == [0], str(got[7]))
    # ---------- P2.5: the METER list: one order with X, rows from the TOC, greyed and hidden rows, scroll feedback, the whole page drawn
    sys.path.insert(0, str(ROOT / "tools/player2"))
    def load_adv(name):
        b = (ROOT / "fw2/generated/fonts" / name).read_bytes(); body = b[12:]
        nr = body[20]; off = 24 + 4 * nr; nn = sum(b2 - a + 1 for a, b2 in (struct.unpack_from("<HH", body, 24 + 4 * i) for i in range(nr)))
        return list(body[off:off + nn])[:95]
    FONTS = {"SemiBold": load_adv("SpaceGrotesk-SemiBold.tfnt"), "Bold": load_adv("SpaceGrotesk-Bold.tfnt"), "Tamzen": load_adv("Tamzen8x16r.tfnt"), "Tamzen Bold": load_adv("Tamzen8x16b.tfnt")}
    def uilist(o):
        l = [x for x in o if x.startswith("uilist")][-1]; n = int(l.split()[1].rstrip(":")); return n, [tuple(map(int, t.split("/"))) for t in l.split()[2:]]
    ONEGO = binf(sy.build(64, True, True, real=shipped, hang_row=64, unavailable=20, hidden=21), "onego.bin")       # 64 rows; TEST 20 (row index 19) unavailable, TEST 21 (index 20) hidden, HANG TEST last, TEST 09 and 10 colliding
    S, U, SEL = 1, 2, 16                                                                                             # (flag bits of meters_toc.h: VISIBLE 1, UNAVAILABLE 2, SELECTABLE 16)
    cases = [("7 rows", b7), ("64 rows", b64), ("63 rows", binf(sy.build(63, False, False, real=shipped), "b63.bin")), ("8 rows", binf(sy.build(8, False, False, real=shipped), "b8.bin")), ("a hidden row", hid), ("unavailable rows", un), ("the one-go bin (64 rows, one unavailable, one hidden, HANG TEST last)", ONEGO)]
    for nm, bf in cases:
        o = run(exe, "user %s\nboot\nuilist\nwalk 140\n" % bf)
        n, rows = uilist(o); sel_seq = [r for r, f in rows if f & SEL]; w = [int(x) for x in [l for l in o if l.startswith("walk")][0].split()[1:]]
        exp = [sel_seq[(1 + i) % len(sel_seq)] for i in range(140)]
        check("%s: X walks exactly the SELECTABLE rows of the list, in the list's order (one order for both screens), and wraps" % nm, w == exp and rows[0][0] == 0 and [r for r, _ in rows] == sorted(r for r, _ in rows), (w[:10], exp[:10]))
    o = run(exe, "user %s\nboot\nuilist\n" % b7); n, rows = uilist(o)
    check("7 rows: the list shows 7, all selectable and visible", n == 7 and all(f & SEL and f & S for _, f in rows), rows)
    o = run(exe, "user %s\nboot\nuilist\n" % ONEGO); n, rows = uilist(o); idx = {r: f for r, f in rows}
    check("the one-go bin: 63 rows listed (the hidden TEST 21 is absent), the unavailable TEST 20 is listed (visible, unavailable, NOT selectable), HANG TEST (last) is listed and selectable, Winamp Bars is selectable",
          n == 63 and 20 not in idx and idx[19] & U and idx[19] & S and not idx[19] & SEL and idx[63] & SEL and idx[0] & SEL, (n, idx.get(19), idx.get(63)))
    o = run(exe, "nouser\nnocore\nboot\nuilist\nstep 1\n"); n, rows = uilist(o)
    check("no bin at all: the list is the one built-in meter and X stays on it", n == 1 and rows[0][0] == 0 and rows[0][1] & SEL and "step 1 from 0 -> 0" in o, o[-3:])
    o = run(exe, "user %s\nboot\nselect 30\nuiindex 30\nselect 0\nuiindex 63\nuiindex 19\nuiindex 21\n" % ONEGO)
    ix = [l for l in o if l.startswith("uiindex")]
    check("row -> list position skips the hidden row (row 21 is list position 20 because row 20 is hidden, row 63 is 62); the current meter's position is what the list opens on", ix[0] == "uiindex row 30 -> 29" and ix[1] == "uiindex row 63 -> 62" and ix[2] == "uiindex row 19 -> 19" and ix[3] == "uiindex row 21 -> 20", ix)
    # picking, Configure, unavailable
    o = run(exe, "user %s\nboot\npick 29\nsave\nadvance 6\ntick 0\nsave\nstate\n" % ONEGO); pk = [l for l in o if l.startswith("pick")][0]; sv = saves(o); s_ = state(o)
    h30 = int(mb.open_toc(ONEGO.read_bytes())["rows"][30]["id"][:8], 16) & 0x7FFFFFFF
    check("A on a normal row (list position 29 = row 30, TEST 31) selects it (the same select_row path as X): that meter runs; the saved word follows ONLY after the commit window (5 s of clean running)", pk == "pick 29 guard 2 cur 30 msg 0" and sv[0] == FB and sv[1] == (h30 | 0) and s_["collisions"] == 0, (pk, [hex(x) for x in sv], hex(h30)))
    o = run(exe, "user %s\nboot\nselect 5\npick 19\nstate\n" % ONEGO); pk = [l for l in o if l.startswith("pick")][0]
    check("A on the UNAVAILABLE row (TEST 20): nothing changes (the meter on screen stays), one short message is set; any key clears it", pk == "pick 19 guard 1 cur 5 msg 1" and "msg 0" in run(exe, "user %s\nboot\npick 19\nkeyclear\n" % ONEGO)[-1], pk)
    o = run(exe, "user %s\nboot\ncfg 29\n" % ONEGO)
    check("X on a normal row loads THAT row for the Configure page (the registry loads only one meter), without selecting it: the set meter is remembered", [l for l in o if l.startswith("cfg")][0] == "cfg 29 guard 3 cur 30 msg 0", o[-2:])
    o = run(exe, "user %s\nboot\nselect 5\ncfg 19\n" % ONEGO)
    check("X on the unavailable row refuses with its own short line (msg 2): the meter on screen stays and no Configure opens", [l for l in o if l.startswith("cfg")][0] == "cfg 19 guard 1 cur 5 msg 2", o[-2:])
    o = run(exe, "user %s\nboot\nselect 5\npick 0\n" % ONEGO)
    check("Winamp Bars (list position 0) stays selectable in every bin", [l for l in o if l.startswith("pick")][0] == "pick 0 guard 2 cur 0 msg 0", o[-2:])
    # names, previews, longest name, indicator, markers, coverage
    longn = "W" * 23; lng = binf(mod(sy.build(64, False, False, real=shipped), lambda es: setattr(es[9], "name", longn)), "long.bin")
    for fn, adv in FONTS.items():
        o = run(exe, "adv %s\nuser %s\nboot\ndraw 5 9\n" % (" ".join(map(str, adv)), lng))
        txt = [l.split(" ", 4) for l in o if l.startswith("t ")]; names = [t for t in txt if int(t[1]) == 92 and t[4].startswith("WWW")]
        ok = len(names) == 1 and 92 + int(names[0][3]) <= 302 and (names[0][4] == longn or (names[0][4].endswith(".") and longn.startswith(names[0][4][:-1])))
        check("the longest name (23 'W') fits the row next to the preview and the markers in %s: the name ends before x 302 (the settings icon starts at 304); a name that does not fit is cut and ends with a dot" % fn, ok, names)
        o = run(exe, "adv %s\nuser %s\nboot\ndraw 0 0\n" % (" ".join(map(str, adv)), b64)); txt = [l.split(" ", 4) for l in o if l.startswith("t ")]
        nmx = [t[4] for t in txt if int(t[1]) == 92]
        check("short names are drawn whole (%s): the first rows' names come from the TOC (WINAMP BARS, then the TEST rows' own names)" % fn, nmx[:3] == ["WINAMP BARS", "SCOPE", "CHLADNI"] or nmx[0] == "WINAMP BARS", nmx[:4])
    o = run(exe, "adv %s\nuser %s\nboot\nselect 9\ncurname\n" % (" ".join(map(str, FONTS["Bold"])), lng))
    cn = [l for l in o if l.startswith("curname")][0][8:]
    check("Settings > Appearance > METER: the value is the current meter's name from the TOC, cut to fit by the same rule (a 23 character name in Space Grotesk Bold)", cn == longn or (cn.endswith(".") and longn.startswith(cn[:-1])), cn)
    o = run(exe, "user %s\nboot\nselect 30\ncurname\n" % b64)
    check("... and a normal name is shown whole (TEST 31)", [l for l in o if l.startswith("curname")][0] == "curname TEST 31", o[-1])
    for nm, bf, top, sel in (("7 rows", b7, 0, 0), ("8 rows", cases[3][1], 1, 7), ("63 rows", cases[2][1], 56, 62), ("64 rows", b64, 0, 0), ("64 rows scrolled", b64, 30, 33), ("64 rows at the end", b64, 57, 63), ("the one-go bin", ONEGO, 14, 19), ("one row only", None, 0, 0)):
        o = run(exe, ("user %s\nboot\n" % bf if bf else "nouser\nnocore\nboot\n") + "draw %d %d\n" % (top, sel)); d = [l for l in o if l.startswith("draw")][0].split()
        check("%s: every pixel of the 400 x 360 page is drawn or cleared in the frame (nothing of the previous screen is left: title, rows, the gaps, the hints), no card read" % nm, d[6] == "0" and d[10] == "0", d)
    o = run(exe, "user %s\nboot\nselect 30\ndraw 27 30\n" % b64); txt = [l.split(" ", 4) for l in o if l.startswith("t ")]
    ys = [(int(t[1]), int(t[2]), t[4]) for t in txt]
    star = [y for x, y, tx in ys if tx == "*"]; row30_y = 34 + (30 - 27) * 40 + 12
    check("the current meter keeps its * marker, on its own row (list position 30 at top 27 -> row 3 of the window); no star on any other row", star == [row30_y], (star, row30_y))
    o = run(exe, "user %s\nboot\ndraw 0 0\n" % b64); txt = [l.split(" ", 4) for l in o if l.startswith("t ")]
    check("the current meter (row 0) shows its star while it is in the window; scrolling away removes it", [t for t in txt if t[4] == "*"] != [] and [t for t in run(exe, "user %s\nboot\ndraw 20 20\n" % b64) if t.startswith("t ") and t.endswith(" *")] == [])
    for nm, bf, top, sel, text in (("64 rows", b64, 11, 11, "12 / 64"), ("64 rows, the last", b64, 57, 63, "64 / 64"), ("the one-go bin (63 listed)", ONEGO, 14, 62, "63 / 63"), ("7 rows", b7, 0, 2, "3 / 7")):
        o = run(exe, "user %s\nboot\ndraw %d %d\n" % (bf, top, sel)); txt = [l.split(" ", 4) for l in o if l.startswith("t ")]
        ind = [t for t in txt if t[4] == text and int(t[2]) == 6]
        check("the position indicator reads '%s' in the title line, right aligned inside the screen (%s)" % (text, nm), len(ind) == 1 and int(ind[0][1]) + int(ind[0][3]) <= 380, txt[:3])
    o = run(exe, "user %s\nboot\ndraw 14 19\n" % ONEGO); bl = [l.split() for l in o if l.startswith("b ")]; txt = [l.split(" ", 4) for l in o if l.startswith("t ")]
    check("previews come from the TOC through the stash (one palette blit per listed AVAILABLE row in the window, 56 x 32 at x 20): the window of 7 rows holds the unavailable row, which draws no preview (a greyed placeholder) and its name in the faint colour", len(bl) == 6 and all(b[1] == "20" and b[3:] == ["56", "32"] for b in bl), bl)
    # ---------- the music stream model: scrolling the list never reads the card
    o = run(exe, "user %s\nboot\n" % ONEGO + "".join("draw %d %d\n" % (t, t) for t in range(0, 57)) + "state\n")
    dd = [l.split() for l in o if l.startswith("draw")]
    check("scrolling the whole list (57 window positions) reads nothing from the card: every draw reports 0 reads, previews and names come from the PSRAM copy of the TOC", len(dd) == 57 and all(x[10] == "0" for x in dd), dd[:2])
    # ---------- W1 of release 0.7.5 (D-M25): METERS entry, the settings icon, Configure of ANY meter (a preview of that meter; the set meter comes back), one settings interface
    def kvs(o, tag):
        t = [l for l in o if l.startswith(tag + " ")][-1].split(); i = t.index("cur"); return {k: (int(v, 16) if k in ("saved", "pendhash") else int(v)) for k, v in zip(t[i::2], t[i + 1::2])}
    toc_one = mb.open_toc(ONEGO.read_bytes())["rows"]
    def h31(r): return int(toc_one[r]["id"][:8], 16) & 0x7FFFFFFF
    o = run(exe, "user %s\nboot\nuilist\ncable\n" % ONEGO); n, rows = uilist(o); cable = [int(x) for x in [l for l in o if l.startswith("cable")][0].split()[1:]]
    want = [r for r, f in rows if f & SEL and (r == 0 or toc_one[r]["nparams"] > 0)]
    check("configurable = selectable AND the TOC row has parameters: the one-go bin has 62 such rows; the greyed TEST 20 (row 19) and the hidden TEST 21 (row 20) are not among them", cable == want and len(cable) == 62 and 19 not in cable and 20 not in cable and 0 in cable, (len(cable), want[:5]))
    o = run(exe, "nouser\nnocore\nboot\ncable\n")
    check("no bin: Winamp Bars (built in, it has parameters) is the one configurable row", [l for l in o if l.startswith("cable")] == ["cable 0"], o[-1])
    zp = bytearray(b7.read_bytes()); HD = mb.HDR; zp[HD + mb.TOC * 3 + 82:HD + mb.TOC * 3 + 84] = b"\0\0"          # row 3 (Layered Wave) claims no parameters
    pk_off = mb.u32(zp, 24); struct.pack_into("<I", zp, 32, mb.crc32(bytes(zp[HD:pk_off]))); struct.pack_into("<I", zp, len(zp) - 4, mb.crc32(bytes(zp[HD:len(zp) - 4]))); (tmp / "zp.bin").write_bytes(bytes(zp))
    o = run(exe, "user %s\nboot\ncable\nselect 2\ncfg 3\ncstat\nstate\n" % (tmp / "zp.bin")); c = kvs(o, "cstat"); s_ = state(o)
    check("a visible, runnable row whose TOC row says it has NO parameters is not configurable: no icon row, X says so in one line (msg 3), nothing is loaded, the set meter stays", "3" not in [l for l in o if l.startswith("cable")][0].split()[1:] and [l for l in o if l.startswith("cfg")][0] == "cfg 3 guard 1 cur 2 msg 3" and c["active"] == 0 and c["cur"] == 2, (o[-4:], c))
    # the icon on the right edge of the list rows
    def icon_rows(o, top):
        rs = [tuple(map(int, l.split()[1:5])) for l in o if l.startswith("r ")]; got = {}
        for x, y, w, h, in rs:
            if 304 <= x < 315 and w <= 11 and h == 1 and y >= 34: got.setdefault((y - 34) // 40, []).append((x, y, w))
        return got
    for top, sel in ((14, 19), (0, 0), (57, 63)):
        o = run(exe, "adv %s\nuser %s\nboot\nuilist\ncable\ndraw %d %d\n" % (" ".join(map(str, FONTS["Bold"])), ONEGO, top, sel)); n, rows = uilist(o); cable = {int(x) for x in [l for l in o if l.startswith("cable")][0].split()[1:]}
        got = icon_rows(o, top); expect = {k for k in range(7) if top + k < n and rows[top + k][0] in cable}
        ok = set(got) == expect and all(len({y for _, y, _ in v}) == 11 and min(x for x, _, _ in v) == 304 and max(x + w for x, _, w in v) == 315 and min(y for _, y, _ in v) == 34 + 40 * k + 14 and max(y for _, y, _ in v) == 34 + 40 * k + 24 for k, v in got.items())
        check("list window at top %d: the settings icon (11 x 11 at x 304, centred on the row) is drawn exactly on the rows that can be configured: %s; none on the greyed row or past the end" % (top, sorted(expect)), ok, (sorted(got), sorted(expect)))
    for fn, adv in FONTS.items():
        o = run(exe, "adv %s\nuser %s\nboot\nselect 9\nuser %s\ndraw 5 9\n" % (" ".join(map(str, adv)), lng, lng)); txt = [l.split(" ", 4) for l in o if l.startswith("t ")]
        names = [t for t in txt if int(t[1]) == 92]; marks = [t for t in txt if int(t[1]) >= 316 and t[4] in ("*", "!", "N/A")]
        check("%s: no name reaches the icon column (names end before x 304) and no marker (*, !, N/A) starts inside or before it (markers start at x 316 or later)" % fn, all(92 + int(t[3]) <= 302 for t in names) and all(int(t[1]) >= 316 for t in marks), (names[:2], marks))
    o = run(exe, "adv %s\nuser %s\nboot\nselect 1\nselect 2\n%sdraw 0 1\n" % (" ".join(map(str, FONTS["Tamzen Bold"])), tmp / "crc.bin", CL))
    txt = [l.split(" ", 4) for l in o if l.startswith("t ")]; mk = [t for t in txt if t[4] in ("!", "*")]
    check("a refused row ('!') and the current row ('*') are on their own rows, right of the icon column, in the widest-advance font", [t[4] for t in mk].count("!") == 1 and all(int(t[1]) >= 316 for t in mk), mk)
    # the hint line: A sets, X configures
    vs = (ROOT / "fw2/ui/meterlist_view.c").read_text()
    check("the hint line says what the keys do: A SET and X CONFIGURE (not 'A PICK')", "UP DOWN  A SET  X CONFIGURE" in vs and "A PICK" not in vs)
    # Configure of a row that is not the set one: a preview of THAT meter, the set meter comes back, nothing is committed
    def sc(*cmds): return "user %s\nboot\nselect 5\n%s" % (ONEGO, CL) + "\n".join(cmds) + "\n"
    o = run(exe, sc("cstat", "cbegin 12", "advance 6", "tick 0", "save", "cend", "tick 0", "save", "cstat"))
    c0 = kvs([l for l in o if l.startswith("cstat")][:1], "cstat"); cb = kvs(o, "cbegin"); ce = kvs(o, "cend"); c1 = [kvs([l], "cstat") for l in o if l.startswith("cstat")][-1]; sv = saves(o)
    check("Configure of TEST 12 while TEST 6 is set and committed: the page's meter is row 12 (one pack load), the SET meter is remembered (row 5), the saved word is untouched, no commit and no abort is counted, the stream never collided",
          c0["cur"] == 5 and c0["active"] == 0 and cb["cur"] == 12 and cb["active"] == 1 and cb["set"] == 5 and cb["loads"] == c0["loads"] + 1 and cb["saved"] == c0["saved"] == h31(5) and cb["commits"] == c0["commits"] and cb["aborts"] == c0["aborts"] and cb["collisions"] == 0, (c0, cb))
    check("... while the preview runs, 6 more seconds of clean drawing do NOT commit TEST 12 (the save of the selection never follows a Configure), and Quit would save the SET meter (the register file holds row 5)", sv[0] == h31(5) and cb["saved"] == h31(5), (sv, hex(h31(5))))
    check("leaving the page brings the set meter back: row 5 runs again (the pack is loaded again, quiesced first: no collision), no longer previewing, the saved word, the commits and the aborts are what they were", ce["cur"] == 5 and ce["active"] == 0 and ce["loads"] == cb["loads"] + 1 and ce["saved"] == h31(5) and ce["commits"] == c0["commits"] and ce["aborts"] == c0["aborts"] and ce["collisions"] == 0 and c1["cur"] == 5 and sv[1] == h31(5), (cb, ce))
    o = run(exe, sc("cbegin 5", "cend"))
    c0 = kvs(o, "cbegin"); check("Configure of the set meter itself (Start+X, or X on its row): no preview, no load, nothing to restore", c0["cur"] == 5 and c0["active"] == 0 and c0["loads"] == kvs(run(exe, sc("cstat")), "cstat")["loads"] and kvs(o, "cend")["loads"] == c0["loads"], c0)
    # a selection still waiting for its clean run is paused by the page and resumes after it
    o = run(exe, "user %s\nboot\nselect 5\ncbegin 12\nadvance 6\ntick 0\ncstat\nsave\ncend\ncstat\nadvance 6\ntick 0\nsave\n" % ONEGO); cs = [kvs([l], "cstat") for l in o if l.startswith("cstat")]; sv = saves(o)
    check("a selection of TEST 6 that has not run its 5 clean seconds when Configure of TEST 12 opens: nothing commits during the page (Winamp Bars stays saved), and afterwards the selection is pending again and commits after its own clean run",
          cs[0]["pend"] == 0 and sv[0] == FB and cs[1]["pend"] == 1 and cs[1]["pendhash"] == h31(5) and sv[1] == h31(5) and cs[0]["commits"] == 0, (cs, [hex(x) for x in sv]))
    # the set meter's preset and tuning come back (a pack meter: the built-in meter's preset functions are stubs in this harness)
    o = run(exe, "mph 3\nuser %s\nboot\nselect 2\n%spre 2\nshow\ncbegin 3\ncend\nshow\n" % (b7, CL)); sh = [l for l in o if l.startswith("show")]; pr = [l for l in o if l.startswith("pre ")]
    check("the set meter's preset (preset FILE 2 of 3) is still the preset after Configure of another meter and back: same preset, same parameter values", "preset 2 of 4" in pr[0] and kvs(o, "cend")["preset"] == 2 and kvs(o, "cend")["custom"] == 0 and sh[0] == sh[1] and kvs(o, "cbegin")["preset"] == 0, (pr, sh, o[-4:]))
    o = run(exe, "user %s\nboot\nselect 2\n%sedit 1\nshow\ncbegin 3\ncend\nshow\n" % (b7, CL)); sh = [l for l in o if l.startswith("show")]
    check("an edited (CUSTOM) set meter keeps its edit: the tuning is found again when it comes back", sh[0] == sh[1] and " custom 1 " in sh[1] and kvs(o, "cend")["custom"] == 1, sh)
    # the pool of two tunings: a third meter configured evicts the oldest (known limit until W2)
    o = run(exe, "user %s\nboot\nselect 1\n%sedit 1\ncbegin 2\nedit 1\ncend\ncbegin 3\nedit 1\ncend\nshow\ntune\n" % (b7, CL)); sh = [l for l in o if l.startswith("show")]
    check("KNOWN LIMIT (as built, until the save file of W2): tuning a THIRD meter evicts the oldest tuning, so the set meter (tuned first) is back untuned; the pool still holds exactly two tunings", kvs(o, "cend")["custom"] == 0 and sum(1 for k in range(2) if any(map(int, [l for l in o if l.startswith("tune")][0].split()[1 + 4 * k:5 + 4 * k]))) == 2, (sh, [l for l in o if l.startswith("tune")]))
    o = run(exe, "user %s\nboot\nselect 1\n%sedit 1\nshow\ncbegin 2\nedit 1\ncend\nadvance 6\ntick 0\nsave\nrestart\nshow\ncbegin 2\n" % (b7, CL)); sh = [l for l in o if l.startswith("show")]; c = kvs(o, "cbegin")
    check("two configured meters keep their tunings across a restart (the words of the pool are what the register file carries): the set meter comes back edited, and the other meter, when its Configure page opens, is edited too", len(sh) == 2 and sh[0] == sh[1] and " custom 1 " in sh[1] and "cur 1" in sh[1] and c["custom"] == 1 and c["cur"] == 2 and c["active"] == 1, (sh, c))
    # refusals and strikes
    o = run(exe, "user %s\nboot\nselect 1\n%scbegin 2\nstate\nflags 2\n" % (tmp / "crc.bin", CL)); c = kvs(o, "cbegin"); fl1 = int([l for l in o if l.startswith("flags")][0].split()[3]); code = int(re.search(r"cbegin 2 code (\d+)", [l for l in o if l.startswith("cbegin")][0]).group(1))
    check("a pack that is refused (damaged) when its Configure page opens: cbegin reports the refusal code (26, the CRC), the set meter is back at once (not previewing), the row is marked refused", code == 26 and c["cur"] == 1 and c["active"] == 0 and fl1 & 4 and state(o)["err"] == 26, (code, c, fl1))
    check("... and a refusal touches neither the saved word nor the commit counters (no abort, the stream never collided)", c["saved"] == HS[1] and c["aborts"] == 0 and c["commits"] == 1 and c["collisions"] == 0, c)
    o = run(exe, "user %s\nboot\nselect 1\n%scfg 2\ncstat\n" % (tmp / "crc.bin", CL))
    check("X on that row in the list: one short line (msg 4: CANNOT OPEN), the list stays, the set meter runs", [l for l in o if l.startswith("cfg")][0] == "cfg 2 guard 1 cur 1 msg 4", o[-2:])
    o = run(exe, "user %s\nboot\nselect 2\n%scut 1\ncbegin 1\ntick 0\ncstat\ncut 0\ncend\ncstat\nstate\nsave\n" % (b7, CL)); cs = [kvs([l], "cstat") for l in o if l.startswith("cstat")]; c = kvs(o, "cend")
    check("the watchdog cuts the pack of the page's meter: Winamp Bars takes over (the machinery is the one of any pack), and leaving the page still brings the SET meter back, with the saved word and the commit counters untouched", cs[0]["cur"] == 0 and state(o)["strikes"] == 1 and c["cur"] == 2 and c["active"] == 0 and c["saved"] == HS[2] and c["aborts"] == cs[0]["aborts"] and saves(o)[-1] == HS[2], (cs, c))
    o = run(exe, "user %s\nboot\nselect 2\n%scbegin 1\nsave\nrestart\nstate\n" % (b7, CL))
    check("Quit during a Configure page of another meter: the next boot starts on the SET meter (Chladni), not on the one that was being configured", lastsel(o) == "restarted cur 2", lastsel(o))
    o = run(exe, "user %s\nboot\n" % ONEGO + "".join("cbegin %d\ncend\n" % r for r in (3, 12, 30, 62, 63, 12, 3)) + "state\n"); s_ = state(o)
    check("seven Configure visits to rows of the one-go bin (the music stream mid-read at each load): no collision, no refusal, every visit loads twice (the page, then the set meter back) except a visit to the set row", s_["collisions"] == 0 and s_["err"] == 0 and s_["loads"] > 0 and s_["strikes"] == 0, str(s_))
    # ---------- the settings interface: get and put by id hash and params_rev, sparse, backed by the pool of two tunings
    A, B, C = "AAAA1111", "BBBB2222", "CCCC3333"
    o = run(exe, "sput %s 1 3 3 2 10 5 255 9 0\nsget %s 1\nsget %s 2\nsget %s 1\nsget 1111FFFF 1\n" % (A, A, A, B))
    check("settings round trip: put (hash, params_rev, preset 3, three sparse entries) and get returns exactly them; another params_rev or another meter finds nothing", o[0] == "sput stored 3" and o[1] == "sget found 1 preset 3 n 3 2=10 5=255 9=0" and o[2] == "sget found 0 preset 0 n 0" and o[3] == "sget found 0 preset 0 n 0" and o[4] == "sget found 0 preset 0 n 0", o)
    o = run(exe, "sput %s 1 3 2 2 10 5 7\nsput %s 1 3 0\nsget %s 1\n" % (A, A, A))
    check("an empty put (nothing differs from the preset) drops the meter's record: back to the preset's values", o[1] == "sput stored 0" and o[2] == "sget found 0 preset 0 n 0", o)
    o = run(exe, "sput %s 1 3 2 2 10 5 7\nsput %s 1 4 1 6 1\nsget %s 1\nsget %s 1\n" % (A, A, A, A))
    check("a second put replaces the first (one record per meter and params_rev)", o[2] == "sget found 1 preset 4 n 1 6=1", o)
    o = run(exe, "sput %s 1 0 1 0 1\nsput %s 1 0 1 0 2\nsput %s 1 0 1 0 3\nsget %s 1\nsget %s 1\nsget %s 1\ntune\n" % (A, B, C, A, B, C))
    check("KNOWN LIMIT: the pool holds two meters; the third put evicts the one saved least recently (A), B and C stay", o[3] == "sget found 0 preset 0 n 0" and o[4].startswith("sget found 1") and o[5].startswith("sget found 1"), o)
    o = run(exe, "sput %s 1 0 3 1 300 2 5 3 6\nsget %s 1\n" % (A, A))
    check("a value above the parameter's maximum is not stored (the record would be refused when read): entries 2 and 3 are, entry 1 is not", o[0] == "sput stored 2" and o[1] == "sget found 1 preset 0 n 2 2=5 3=6", o)
    o = run(exe, "sput %s 1 0 20 %s\nsget %s 1\n" % (A, " ".join("%d 1" % i for i in range(20)), A)); got = [l for l in o if l.startswith("sget")][0].split()
    check("the pool's capacity is what the register file has (85 bits of entries: at 8 bits a value, six parameters): a put with more is cut in index order, and what is stored is what get returns", o[0] == "sput stored 6" and got[:6] == ["sget", "found", "1", "preset", "0", "n"] and got[6] == "6", o)
    # ---------- mutants (each rule's test fails without its line of code)
    for m, label, script, bad in (
        ("commit_at_once", "a selection commits at once (no clean run needed)", "user %s\nboot\nselect 2\ntick 0\nsave\n" % b7, lambda o: saves(o)[-1] != FB),
        ("nodrop", "a strike leaves the pending selection alive (it would later commit the fallback)", "user %s\nboot\ncut 0\nselect 1\n%ssave\ncut 1\nselect 2\ntick 0\nadvance 6\ntick 0\nsave\n" % (b7, CL), lambda o: saves(o)[-1] != HS[1]),
        ("refusal_commits", "a refused pack still commits", "user %s\nboot\nselect 1\n%sselect 2\ntick 0\nadvance 6\ntick 0\nsave\n" % (tmp / "crc.bin", CL), lambda o: saves(o)[-1] != HS[1]),
        ("hidden_ok", "hidden rows are selectable", "user %s\nboot\nlist\n" % hid, lambda o: lst(o) != "list 6: 0 1 2 4 5 6"),
        ("unavail_ok", "unavailable rows are selectable", "user %s\nboot\nlist\n" % un, lambda o: lst(o) != "list 5: 0 1 3 5 6"),
        ("collide_ignored", "the collision rule is ignored", "user %s\nboot\nstate\n" % cb, lambda o: state(o)["notune"] != 2),
        ("hidden_listed", "a hidden row is listed", "user %s\nboot\nuilist\n" % hid, lambda o: uilist(o)[0] != 6),
        ("step_all", "X also visits the unavailable rows (the two screens disagree)", "user %s\nboot\nwalk 25\n" % ONEGO, lambda o: [l for l in o if l.startswith("walk")] != [l for l in run(exe, "user %s\nboot\nwalk 25\n" % ONEGO) if l.startswith("walk")]),
        ("nogap", "the gap under the rows is not cleared (a strip of the old screen)", "user %s\nboot\ndraw 0 0\n" % b7, lambda o: [l for l in o if l.startswith("draw")][0].split()[6] != "0"),
        ("unavail_pickable", "an unavailable row can be picked", "user %s\nboot\nselect 5\npick 19\n" % ONEGO, lambda o: "cur 5" not in [l for l in o if l.startswith("pick")][0]),
        ("x_nopreview", "X does not load the row for its Configure page", "user %s\nboot\ncfg 29\n" % ONEGO, lambda o: "cur 30" not in [l for l in o if l.startswith("cfg")][0]),
        ("nofit", "a long name is not cut", "adv %s\nuser %s\nboot\ndraw 5 9\n" % (" ".join(map(str, FONTS["Bold"])), lng), lambda o: any(l.startswith("t 92 ") and l.split(" ", 4)[4].startswith("WWW") and 92 + int(l.split()[3]) > 302 for l in o)),
        ("cfg_commits", "Configure of another row goes through the commit rule (the saved word follows the page)", "user %s\nboot\nselect 5\n%scbegin 12\nadvance 6\ntick 0\nsave\n" % (ONEGO, CL), lambda o: saves(o)[-1] != h31(5)),
        ("cfg_noend", "leaving the page does not bring the set meter back", "user %s\nboot\nselect 5\n%scbegin 12\ncend\n" % (ONEGO, CL), lambda o: kvs(o, "cend")["cur"] != 5),
        ("cfg_nopend", "a selection waiting for its clean run is lost by the page", "user %s\nboot\nselect 5\ncbegin 12\ncend\nadvance 6\ntick 0\nsave\n" % ONEGO, lambda o: saves(o)[-1] != h31(5)),
        ("cfg_nopreset", "the set meter's preset is not restored after the page", "mph 3\nuser %s\nboot\nselect 2\n%spre 2\ncbegin 3\ncend\n" % (b7, CL), lambda o: kvs(o, "cend")["preset"] != 2),
        ("set_rev_ignored", "a record is found whatever the params_rev", "sput AAAA1111 1 3 3 2 10 5 255 9 0\nsget AAAA1111 2\n", lambda o: o[1] != "sget found 0 preset 0 n 0"),
        ("set_nomax", "a value above the maximum is stored", "sput AAAA1111 1 0 3 1 300 2 5 3 6\nsget AAAA1111 1\n", lambda o: o[0] != "sput stored 2"),
        ("gear_all", "the settings icon is drawn on a greyed row too", "adv %s\nuser %s\nboot\ndraw 14 19\n" % (" ".join(map(str, FONTS["Bold"])), ONEGO), lambda o: 5 in icon_rows(o, 14)),
        ("x_noparams", "X on a meter without parameters tries to open it", "user %s\nboot\nselect 2\ncfg 3\n" % (tmp / "zp.bin"), lambda o: [l for l in o if l.startswith("cfg")][0] != "cfg 3 guard 1 cur 2 msg 3"),
        ("noindicator", "no position indicator", "user %s\nboot\ndraw 11 11\n" % b64, lambda o: not any(l.endswith(" 12 / 64") for l in o))):
        with tempfile.TemporaryDirectory() as t2:
            mexe = build(Path(t2), m)
            check("mutant (%s) is caught" % label, bad(run(mexe, script)))
print("PASSED" if not fails else "FAILED (%d)" % fails)
sys.exit(1 if fails else 0)
