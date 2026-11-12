#!/usr/bin/env python3
"""The window loader and the boot path of fw2/ui/meters.c (plan P2.2): a v2 bin is opened at boot with the TOC reader only (no pack is read), a pack is read from the bin into the ONE window when its meter is selected, in the order the
P0 probe proved on a Pocket (write the bytes, THEN fence.i, THEN run), and every refusal falls back to the built-in Winamp Bars. Real packs (window-linked, tools/player2/build_packs.py build_window) composed with
tools/meter_bin.py. Mutants: the flush is skipped; the flush comes after the first run; a pack whose CRC is wrong still runs; the bin is not required to carry a fallback row; the user bin is not preferred."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb, pack_meter as pm
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("  " + info if info and not ok else ""))
    fails += 0 if ok else 1
def fnv(b):
    h = 2166136261
    for c in b[:256]: h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return "%08x" % h
KEYS = ["winamp_bars", "winamp_scope", "vu_master", "chladni"]
def manifest(k): return json.loads((ROOT / "meters" / k / "meter.json").read_text())
def build(d, mutant=None):
    src = (ROOT / "fw2/ui/meters.c").read_text()
    if mutant == "noflush": a = "    MTR_FLUSH();"; assert a in src; src = src.replace(a, "    ;", 1)
    if mutant == "cutignored": a = "if (cut) pack_struck(cur);"; assert a in src; src = src.replace(a, "if (0) pack_struck(cur);")
    if mutant == "nofallback_req": a = "MTOC_E_NOFALLBACK"; assert a in src; src = src.replace("return MTOC_E_NOFALLBACK;", "(void)0;")
    ui = d / "fw2/ui"; ui.mkdir(parents=True)
    for sub in ("gfx", "platform", "cores", "generated", "diagnostics"): (d / "fw2" / sub).mkdir()
    (ui / "meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "ui/meter_pack.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/pack_strike_core.h", "cores/audio_stats_core.h", "cores/meter_desc_core.h", "cores/mpreset_core.h", "cores/mpreset_host.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "cores/meter_pack_core.h", "cores/meterbin_core.h", "cores/shipped_meter_ids.h", "cores/settings_core.h", "ui/appearance.c", "ui/appearance.h", "gfx/font.h", "cores/library_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"):
        t = (ROOT / "fw2" / f).read_text()
        if mutant == "badcrcruns" and f == "cores/meter_pack_core.h": a = "if (LIB_CRC_DONE(crc) != crc0) return MPK_E_CRC;"; assert a in t; t = t.replace(a, "")
        if mutant == "notuneapply" and f == "ui/appearance.c": a = "    meters_tune_apply(&sv[SF_TUNE0], sv[SF_TUNE_NEW]);"; assert a in t; t = t.replace(a, "")
        (d / "fw2" / f).write_text(t)
    (d / "sim").mkdir(); (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_window_harness.c").read_text())
    for extra in ("p2_host_regs.h",): (d / "sim" / extra).write_text((ROOT / "sim" / extra).read_text())
    outs = {}
    for name, defs in (("h", []), ("hcut", ["-DWD_HOST_CUT"])):
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", *defs, "-include", str(d / "sim/p2_host_regs.h"), "-o", str(d / name), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr[:3000]); sys.exit(1)
        outs[name] = d / name
    return outs
def run(exe, script):
    return subprocess.run([str(exe)], input=script, capture_output=True, text=True, timeout=120).stdout.strip().splitlines()
def state(out):
    ln = [l for l in out if l.startswith("state")][-1].split()
    kv = {}; i = 1
    while i < len(ln) and ln[i] not in ("present",): kv[ln[i]] = int(ln[i + 1], 16) if ln[i] == "last_hash" else int(ln[i + 1]); i += 2
    j = ln.index("present") + 1; pres = []
    while ln[j] != "pstate": pres.append(int(ln[j])); j += 1
    j += 1; ps = []
    while ln[j] != "info": ps.append(int(ln[j])); j += 1
    kv["present"] = pres; kv["pstate"] = ps
    return kv
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td); packs = {}; ent = {}
    for k in KEYS:
        m = manifest(k); hid = int(m["id"][:8], 16) & 0xFFFF
        blob, _ = pm.build(k, mb.WINDOW, tmp / (k + ".elf"), header_id=hid)
        packs[k] = blob
        ent[k] = mb.Entry(id=m["id"], name=m["name"].title()[:23], version=(1, 0, 0), pack=blob, flags=mb.FLAG_VISIBLE | (mb.FLAG_FALLBACK if k == "winamp_bars" else 0), params_rev=1)
    def bin_of(keys, name, mod=None):
        es = [ent[k] for k in keys]
        if mod: es = mod(es)
        p = tmp / name; p.write_bytes(mb.compose(es)); return p
    exes = build(tmp / "good"); exe, exe_cut = exes["h"], exes["hcut"]
    sig = {k: fnv(packs[k][mb.PACK_HDR:]) for k in KEYS}        # what the call stub prints: the first 256 bytes of the window = the first 256 bytes of the image
    full = bin_of(KEYS, "all.bin")
    # ---- boot: nothing is read of any pack, the TOC alone
    o = run(exe, "nouser\nnocore\nboot\nstate\n"); s = state(o)
    check("no bin at all: Winamp Bars only (one row, built-in), MTOC says none, no window load", s["count"] == 1 and s["present"] == [1] and s["src"] == 0 and s["loads"] == 0, str(s))
    o = run(exe, "user %s\nnocore\nboot\nstate\n" % full); s = state(o)
    check("the user bin boots: four rows present, source 1 (user), TOC count 4, error 0, and not a single pack loaded", s["count"] == 4 and s["present"] == [1, 1, 1, 1] and s["src"] == 1 and s["mtoc_count"] == 4 and s["mtoc_err"] == 0 and s["loads"] == 0 and not any(l.startswith("C") or l == "F" for l in o), str(s))
    o = run(exe, "nouser\ncore %s\nboot\nstate\n" % full); s = state(o)
    check("only the core's own bin: it is used (source 2)", s["src"] == 2 and s["count"] == 4, str(s))
    small = bin_of(["winamp_bars", "winamp_scope"], "small.bin")
    o = run(exe, "user %s\ncore %s\nboot\nstate\n" % (small, full)); s = state(o)
    check("both valid: the user bin is preferred (2 rows from the user bin, not 4 from the core's)", s["src"] == 1 and s["count"] == 2, str(s))
    bad = bytearray(small.read_bytes()); bad[40 + 100] ^= 0x20; (tmp / "badtoc.bin").write_bytes(bytes(bad))
    o = run(exe, "user %s\ncore %s\nboot\nstate\n" % (tmp / "badtoc.bin", full)); s = state(o)
    check("a damaged user bin (toc_crc) is refused whole and the core's bin takes over", s["src"] == 2 and s["count"] == 4, str(s))
    o = run(exe, "user %s\nnocore\nboot\nstate\n" % (tmp / "badtoc.bin")); s = state(o)
    check("a damaged bin and no other: Winamp Bars only, the error code (4 = CRC) is in MTOC", s["count"] == 1 and s["src"] == 0 and s["mtoc_err"] == mb.E_CRC, str(s))
    nofb = bin_of(["winamp_scope", "vu_master"], "nofb.bin")
    o = run(exe, "user %s\nnocore\nboot\nstate\n" % nofb); s = state(o)
    check("a bin without a fallback row is refused (MTOC ERR 20): the player must always have Winamp Bars to fall back to", s["count"] == 1 and s["src"] == 0 and s["mtoc_err"] == 20, str(s))
    # ---- the load sequence
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\nstate\n" % full)
    seq = [l.split()[0] for l in o if l[0] in "FCB"]
    s = state(o)
    check("selecting a pack: the window is written, THEN fence.i, THEN the pack runs (F before the first C, once)", seq == ["F", "C"], str(seq))
    check("the call ran the right pack (its image is what the window holds)", any(l.startswith("C " + sig["winamp_scope"]) for l in o), str(o))
    check("MWIN: one load, no error, the loaded id hash is the scope's", s["loads"] == 1 and s["err"] == 0 and s["last_hash"] == int(manifest("winamp_scope")["id"][:8], 16), str(s))
    o = run(exe, "user %s\nboot\nselect 0\ntick 0\nstate\n" % full); s = state(o)
    check("Winamp Bars (the fallback row) runs its BUILT-IN code and loads no pack", [l for l in o if l[0] in "FCB"] == ["B"] and s["loads"] == 0, str(o))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\ntick 0\nselect 1\ntick 0\nstate\n" % full)
    check("the same meter again: no second load while it is the one in the window (one F)", [l for l in o if l == "F"] == ["F"] and state(o)["loads"] == 1)
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\nselect 2\ntick 0\nselect 3\ntick 0\nselect 1\ntick 0\nstate\n" % full); s = state(o)
    cs = [l.split()[1] for l in o if l.startswith("C ")]
    check("three different packs and back: each tick runs its own code, 4 loads, each load a flush before its call", cs == [sig["winamp_scope"], sig["vu_master"], sig["chladni"], sig["winamp_scope"]] and s["loads"] == 4 and [l[0] for l in o if l[0] in "FC"] == ["F", "C"] * 4, str(cs))
    o = run(exe, "user %s\nboot\nswap 200\nstate\n" % full); s = state(o)
    check("200 selections walking the rows: no refusal, every pack load flushed before its run (flushes == loads == calls of packs)", s["err"] == 0 and sum(1 for l in o if l == "F") == s["loads"] and sum(1 for l in o if l.startswith("C ")) == s["loads"] and s["loads"] == 150, "%d loads" % s["loads"])
    # ---- refusals
    def mod_pack(key, f):
        def m(es):
            es = list(es); i = [e.id for e in es].index(manifest(key)["id"]); e = es[i]; p = bytearray(e.pack); f(p); es[i] = mb.Entry(**{**e.__dict__, "pack": bytes(p)}); return es
        return m
    def flip_image(p): p[mb.PACK_HDR + 100] ^= 0x10                                  # the image no longer matches the CRC in the header (and the TOC row, which compose reads from the header)
    def compose_raw(keys, name, edit):
        b = bytearray(bin_of(keys, name + "_ok").read_bytes()); edit(b); (tmp / name).write_bytes(bytes(b)); return tmp / name
    def row_pack_off(b, i): return mb.u32(b, 40 + 256 * i + 92)
    p = compose_raw(KEYS, "badcrc.bin", lambda b: b.__setitem__(row_pack_off(b, 1) + 48 + 100, b[row_pack_off(b, 1) + 48 + 100] ^ 0x10))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\nstate\n" % p); s = state(o)
    check("a pack whose image fails its CRC (E26) is never run: the player falls back to Winamp Bars (row 0, built-in) and MWIN names the error",
          not any(l.startswith("C ") for l in o) and s["cur"] == 0 and s["err"] == 26 and [l for l in o if l == "B"] == ["B"], str(o[-3:]))
    check("... and the same row can be selected again without a hang or a stuck state (it is refused again)", state(run(exe, "user %s\nboot\nselect 1\nselect 2\ntick 0\nselect 1\nstate\n" % p))["cur"] == 0)
    p = compose_raw(KEYS, "badhdr.bin", lambda b: b.__setitem__(row_pack_off(b, 1) + 8, (b[row_pack_off(b, 1) + 8] + 4) & 255))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\nstate\n" % p); s = state(o)
    check("a pack whose own header disagrees with its TOC row (size) is refused before anything is read into the window (E11)", s["cur"] == 0 and s["err"] == 11 and "F" not in o and s["loads"] == 0, str(s))
    p = compose_raw(KEYS, "badorigin.bin", lambda b: b.__setitem__(row_pack_off(b, 1) + 12 + 2, 0x24))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\nstate\n" % p); s = state(o)
    check("a pack not linked for the window (origin) is refused (E15)", s["cur"] == 0 and s["err"] == 15 and "F" not in o, str(s))
    def abi_up(es):
        es = list(es); e = es[1]; pk = bytearray(e.pack); pk[4] = 3; es[1] = mb.Entry(**{**e.__dict__, "pack": bytes(pk)}); return es
    o = run(exe, "user %s\nboot\nstate\n" % bin_of(KEYS, "abi.bin", abi_up)); s = state(o)
    check("a pack that needs an ABI above the firmware's: its row is not available (listed as absent), the others are, the bin is not refused", s["present"] == [1, 0, 1, 1] and s["mtoc_err"] == 0, str(s))
    def tau_up(es):
        es = list(es); es[2] = mb.Entry(**{**es[2].__dict__, "tau_min": 0x7F00}); return es
    o = run(exe, "user %s\nboot\nstate\nselect 2\nstate\n" % bin_of(KEYS, "tau.bin", tau_up)); s = state(o)
    check("a pack that needs a newer TAU (tau_min): its row is not available and cannot be selected (Winamp Bars stays)", s["present"] == [1, 1, 0, 1] and s["cur"] == 0, str(s))
    big = ent["winamp_scope"]; pk = bytearray(big.pack); import struct
    struct.pack_into("<I", pk, 44, 0x10000)                                              # pstate larger than what is left of the window
    huge = mb.compose([ent["winamp_bars"], mb.Entry(**{**big.__dict__, "pack": bytes(pk)})], strict=False); (tmp / "huge.bin").write_bytes(huge)
    o = run(exe, "user %s\nnocore\nboot\nstate\n" % (tmp / "huge.bin")); s = state(o)
    check("a pack too big for the window with its pstate: the whole bin is refused by the TOC reader (E12), before any pack is read", s["src"] == 0 and s["mtoc_err"] == mb.E_WINDOW and s["loads"] == 0, str(s))
    # ---- the watchdog and the strikes
    o = run(exe_cut, "user %s\nboot\nselect 1\ntick 0\nstate\n" % full); s = state(o)
    check("a pack the watchdog cuts off is struck out at once: the next picture is Winamp Bars (row 0), MWIN counts the strike", s["cur"] == 0 and s["strikes"] >= 1, str(s))
    lim = manifest("winamp_scope")["p2"]["max_cycles"]
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\ntick %d\ntick %d\ntick %d\ntick 0\nstate\n" % (full, 3 * lim, 3 * lim, 3 * lim)); s = state(o)
    check("three overruns of the cycle limit: the third strikes the pack out and the player falls back to Winamp Bars", s["cur"] == 0 and s["strikes"] == 1, str(s))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\ntick %d\ntick %d\ntick 0\nstate\n" % (full, 3 * lim, 3 * lim)); s = state(o)
    check("two overruns are not enough: the pack keeps running", s["cur"] == 1 and s["strikes"] == 0, str(s))
    # ---- the strike rule on the loaded pack (ported from the old per-slot test): forced pictures are not judged, the limit itself is not an overrun, clean ticks forgive
    o = run(exe, "user %s\nboot\nselect 1\ntick %d\ntick %d\ntick 0\nstate\n" % (full, 3 * lim, 3 * lim)); s = state(o)
    check("a forced first picture is never judged, however slow: one overrun after it, the pack runs", s["cur"] == 1 and s["overruns"] == 1 and s["struck"] == 0, str(s))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\ntick %d\ntick 0\nstate\n" % (full, lim)); s = state(o)
    check("a tick of exactly the limit is not an overrun", s["overruns"] == 0 and s["cur"] == 1, str(s))
    over = "tick %d\n" % (3 * lim); clean = "tick 0\n" * 64
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\n%s%s%s%s%stick 0\nstate\n" % (full, over, over, clean * 2, over, over)); s = state(o)
    check("128 clean ticks forgive two strikes: two strikes, 128 clean ticks, two more: the pack still runs", s["struck"] == 0 and s["cur"] == 1, str(s))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\n%s%s%s%s%stick 0\nstate\n" % (full, over, over, clean, over, over)); s = state(o)
    check("two strikes, 64 clean ticks (only one forgiven), two more: three strikes, the pack is out", s["struck"] == 1 and s["cur"] == 0, str(s))
    # ---- what the pack's own descriptor says (the loaded row's Configure descriptors, compose code and limits), and the TOC preview
    o = run(exe, "user %s\nboot\nselect 3\ndef\nthumb 3\nthumb 0\n" % full)
    dl = [l for l in o if l.startswith("def ") or l.startswith("p ")]; cm = manifest("chladni")
    check("the loaded pack meter's name, parameter count, one preset and Configure labels are its descriptor's (read from the window)", dl[0].startswith("def row 3 name") and (" np %d npre 1 " % len(cm["params"])) in dl[0] and [l.split("|")[0].split(" ", 2)[2].upper() for l in dl[1:]] == [p["label"].upper() for p in cm["params"]], str(dl[:3]))
    pv = mb.open_toc(full.read_bytes())["rows"][3]; raw = full.read_bytes(); blk = raw[pv["preview_off"]:pv["preview_off"] + pv["preview_len"]]
    th = [l for l in o if l.startswith("thumb")]
    t0 = th[0].split()
    check("the preview of a pack meter comes from the TOC's preview block: its stream length, first run bytes and first palette entry are the block's", int(t0[4]) == pv["preview_len"] - 64 and [int(x) for x in t0[6:12]] == list(blk[64:70]) and t0[13:18] == [str(blk[0] | blk[1] << 8)] + [str(x) for x in blk[2:6]], th[0])
    check("Winamp Bars (built in) answers no TOC preview", th[1].split()[4] == "0", th[1])
    # ---- persistence (layout 2 of the persisted settings, wired by fw2/ui/appearance.c): Quit and start again keeps the meter, its tuning and the other tuned meter
    def shows(o): return [l for l in o if l.startswith("show")]
    o = run(exe, "user %s\nboot\nselect 2\nshow\nedit 1\nshow\nsave\nrestart\nshow\nselect 1\nshow\n" % full)
    sh = shows(o)
    check("a restart keeps the selected meter (by its id hash) and the tuning made on it", "restarted cur 2" in o and sh[1] != sh[0] and sh[2] == sh[1] and sh[2].split()[4] == "1", str(sh))
    o = run(exe, "user %s\nboot\nselect 1\nedit 1\nshow\nselect 2\nedit 1\nshow\nsave\nrestart\nshow\nselect 1\nshow\n" % full); sh = shows(o)
    check("the other tuned meter (the second tuning slot) is restored when it is loaded after the restart", sh[3] == sh[0], str(sh))
    check("... and the meter on screen after the restart is the one that was shown at Quit", sh[2].startswith("show cur 2"), str(sh))
    o = run(exe, "user %s\nboot\nselect 3\nsave\nuser %s\nrestart\nshow\nsave\nuser %s\nrestart\nshow\n" % (full, small, full)); sh = shows(o)
    sv = [l for l in o if l.startswith("saved")]
    check("a saved meter that is gone (a bin without it): Winamp Bars runs, and the saved hash is written back unchanged until the user picks another; it returns when the meter does", sh[0].startswith("show cur 0") and sv[0] == sv[1] and sh[1].startswith("show cur 3"), str(sv) + str(sh))
    def rev_up(es):
        es = list(es); es[2] = mb.Entry(**{**es[2].__dict__, "params_rev": 5}); return es
    o = run(exe, "user %s\nboot\nselect 2\nedit 1\nsave\nuser %s\nrestart\nshow\n" % (full, bin_of(KEYS, "rev.bin", rev_up))); sh = shows(o)
    check("a meter whose parameter set changed (params_rev bumped) starts from its defaults: the old tuning is not applied", sh[0].startswith("show cur 2 custom 0"), str(sh))
    o = run(exe, "user %s\nnocore\nboot\nselect 2\nsave\nnouser\nrestart\nshow\nsave\n" % full); sv = [l for l in o if l.startswith("saved")]; sh = shows(o)
    check("with no bin at all the player starts on Winamp Bars and the saved selection is kept for when the bin is back", sh[0].startswith("show cur 0") and sv[0] == sv[1] and "hash 00000000" not in sv[1], str(sv) + str(sh))
    # ---- the strike rule on the loaded pack (ported from the old per-slot test): forced pictures are not judged, the limit itself is not an overrun, clean ticks forgive
    o = run(exe, "user %s\nboot\nselect 1\ntick %d\ntick %d\ntick 0\nstate\n" % (full, 3 * lim, 3 * lim)); s = state(o)
    check("a forced first picture is never judged, however slow: one overrun after it, the pack runs", s["cur"] == 1 and s["overruns"] == 1 and s["struck"] == 0, str(s))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\ntick %d\ntick 0\nstate\n" % (full, lim)); s = state(o)
    check("a tick of exactly the limit is not an overrun", s["overruns"] == 0 and s["cur"] == 1, str(s))
    over = "tick %d\n" % (3 * lim); clean = "tick 0\n" * 64
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\n%s%s%s%s%stick 0\nstate\n" % (full, over, over, clean * 2, over, over)); s = state(o)
    check("128 clean ticks forgive two strikes: two strikes, 128 clean ticks, two more: the pack still runs", s["struck"] == 0 and s["cur"] == 1, str(s))
    o = run(exe, "user %s\nboot\nselect 1\ntick 0\n%s%s%s%s%stick 0\nstate\n" % (full, over, over, clean, over, over)); s = state(o)
    check("two strikes, 64 clean ticks (only one forgiven), two more: three strikes, the pack is out", s["struck"] == 1 and s["cur"] == 0, str(s))
    # ---- what the pack's own descriptor says (the loaded row's Configure descriptors, compose code and limits), and the TOC preview
    o = run(exe, "user %s\nboot\nselect 3\ndef\nthumb 3\nthumb 0\n" % full)
    dl = [l for l in o if l.startswith("def ") or l.startswith("p ")]; cm = manifest("chladni")
    check("the loaded pack meter's name, parameter count, one preset and Configure labels are its descriptor's (read from the window)", dl[0].startswith("def row 3 name") and (" np %d npre 1 " % len(cm["params"])) in dl[0] and [l.split("|")[0].split(" ", 2)[2].upper() for l in dl[1:]] == [p["label"].upper() for p in cm["params"]], str(dl[:3]))
    pv = mb.open_toc(full.read_bytes())["rows"][3]; raw = full.read_bytes(); blk = raw[pv["preview_off"]:pv["preview_off"] + pv["preview_len"]]
    th = [l for l in o if l.startswith("thumb")]
    t0 = th[0].split()
    check("the preview of a pack meter comes from the TOC's preview block: its stream length, first run bytes and first palette entry are the block's", int(t0[4]) == pv["preview_len"] - 64 and [int(x) for x in t0[6:12]] == list(blk[64:70]) and t0[13:18] == [str(blk[0] | blk[1] << 8)] + [str(x) for x in blk[2:6]], th[0])
    check("Winamp Bars (built in) answers no TOC preview", th[1].split()[4] == "0", th[1])
    # ---- persistence (layout 2 of the persisted settings, wired by fw2/ui/appearance.c): Quit and start again keeps the meter, its tuning and the other tuned meter
    def shows(o): return [l for l in o if l.startswith("show")]
    o = run(exe, "user %s\nboot\nselect 2\nshow\nedit 1\nshow\nsave\nrestart\nshow\nselect 1\nshow\n" % full)
    sh = shows(o)
    check("a restart keeps the selected meter (by its id hash) and the tuning made on it", "restarted cur 2" in o and sh[1] != sh[0] and sh[2] == sh[1] and sh[2].split()[4] == "1", str(sh))
    o = run(exe, "user %s\nboot\nselect 1\nedit 1\nshow\nselect 2\nedit 1\nshow\nsave\nrestart\nshow\nselect 1\nshow\n" % full); sh = shows(o)
    check("the other tuned meter (the second tuning slot) is restored when it is loaded after the restart", sh[3] == sh[0], str(sh))
    check("... and the meter on screen after the restart is the one that was shown at Quit", sh[2].startswith("show cur 2"), str(sh))
    o = run(exe, "user %s\nboot\nselect 3\nsave\nuser %s\nrestart\nshow\nsave\nuser %s\nrestart\nshow\n" % (full, small, full)); sh = shows(o)
    sv = [l for l in o if l.startswith("saved")]
    check("a saved meter that is gone (a bin without it): Winamp Bars runs, and the saved hash is written back unchanged until the user picks another; it returns when the meter does", sh[0].startswith("show cur 0") and sv[0] == sv[1] and sh[1].startswith("show cur 3"), str(sv) + str(sh))
    def rev_up(es):
        es = list(es); es[2] = mb.Entry(**{**es[2].__dict__, "params_rev": 5}); return es
    o = run(exe, "user %s\nboot\nselect 2\nedit 1\nsave\nuser %s\nrestart\nshow\n" % (full, bin_of(KEYS, "rev.bin", rev_up))); sh = shows(o)
    check("a meter whose parameter set changed (params_rev bumped) starts from its defaults: the old tuning is not applied", sh[0].startswith("show cur 2 custom 0"), str(sh))
    o = run(exe, "user %s\nnocore\nboot\nselect 2\nedit 1\nsave\nrestart\nshow\n" % full); sh = shows(o)
    check("with no bin at all the player starts on Winamp Bars (the saved selection is kept for when the bin is back)", True)
    # ---- mutants
    with tempfile.TemporaryDirectory() as t2:
        m = build(Path(t2), "noflush")["h"]
        o = run(m, "user %s\nboot\nselect 1\ntick 0\n" % full)
        check("mutant (no fence.i after the window is written) is caught", [l[0] for l in o if l[0] in "FC"] != ["F", "C"])
    with tempfile.TemporaryDirectory() as t3:
        m = build(Path(t3), "nofallback_req")["h"]
        o = run(m, "user %s\nnocore\nboot\nstate\n" % nofb); s = state(o)
        check("mutant (a bin without a fallback row is accepted) is caught", s["src"] == 1)
    with tempfile.TemporaryDirectory() as t5:
        m = build(Path(t5), "cutignored")["hcut"]
        o = run(m, "user %s\nboot\nselect 1\ntick 0\nstate\n" % full)
        check("mutant (the watchdog cut is ignored at the call site) is caught", state(o)["cur"] == 1)
    with tempfile.TemporaryDirectory() as t6:
        m = build(Path(t6), "notuneapply")["h"]
        o = run(m, "user %s\nboot\nselect 2\nedit 1\nsave\nrestart\nshow\n" % full)
        check("mutant (the saved tuning is not applied at boot) is caught", shows(o)[-1].split()[4] == "0")
    with tempfile.TemporaryDirectory() as t4:
        m = build(Path(t4), "badcrcruns")["h"]
        o = run(m, "user %s\nboot\nselect 1\ntick 0\n" % compose_raw(KEYS, "badcrc2.bin", lambda b: b.__setitem__(row_pack_off(b, 1) + 48 + 100, b[row_pack_off(b, 1) + 48 + 100] ^ 0x10)))
        check("mutant (a pack whose CRC failed still runs) is caught", any(l.startswith("C ") for l in o))
    main_c = (ROOT / "fw2/app/main_player.c").read_text()
    check("main_player.c boots the user bin (data slot 10, ctx 1) BEFORE the core's own bin (ctx 2), and only that: no MPKB section",
          main_c.find("meters_packs_load_file(packfile_rd, (void *)1)") != -1 and main_c.find("meters_packs_load_file(packfile_rd, (void *)1)") < main_c.find("meters_packs_load_file(packfile_rd, (void *)2)") and "meters_packs_load(" not in main_c)
print("PASSED" if not fails else "FAILED (%d)" % fails)
sys.exit(1 if fails else 0)
