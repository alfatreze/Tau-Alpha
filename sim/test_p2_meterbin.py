#!/usr/bin/env python3
"""The bin v2 reader (fw2/cores/meterbin_core.h) against the independent Python reference (tools/meter_bin.py); docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2, 3, 7 and EXTERNAL_METERS_P2_PLAN.md P2.1 (as reviewed 2026-11-12).
Two modes of the C reader: `toc` = mb_open_toc, the boot path, given a reader that serves ONLY bytes 0..packs_off (a read of any pack or of the end CRC fails, so the test proves nothing else is read); `file` = mb_check_file, the tools' check.
  1. random v2 bins: both modes equal the reference row for row (ids, names, flags, versions, ABI, previews, packs, tuning collisions); v1 bundles are read by the Python tool only;
  2. random damage (single and double byte, half with the CRCs fixed up so the inner checks are reached, some truncated): same code from C and Python in both modes;
  3. named refusals with their documented codes; the 4,456,448 / 4,456,449 boundary; ABI and tau_min make a row unavailable, not the bin refused;
  4. the boot path does not read packs: a bin with a damaged pack header, a wrong pack_crc, a flipped pack byte or a wrong end CRC is sound at boot and refused only by the full check / at selection (mb_check_pack, mb_check_pack_image);
  5. unique id_hash is found without n^2 reads (the reader's reads are counted); tuning collisions (same low 16 bits of id_hash) are flagged for every row involved;
  6. the golden vectors (docs/research/meter_bin/): SHA-256, the full-check code and the boot code as expected.json says, C equal to Python; the damaged ones regenerate byte for byte; the packages compose to v2_seven.bin;
  7. mutants of the C reader (no duplicate check, no whole-file CRC, count limit off by one, ABI compare, no pack range check, no exact window check, a pack read at open, toc_crc ignored, collisions not reported) are each caught.
Host-simulated only."""
import hashlib, json, random, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import meter_bin as mb
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:600]))
    fails += 0 if ok else 1
def build(td, bug=0):
    exe = Path(td) / ("h%d" % bug)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DBUG=%d" % bug, "-o", str(exe), str(ROOT / "sim/p2_meterbin_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def c_report(exe, td, blobs, mode="file", fw_abi=0x0102, fw_tau=0x0008):
    files = []
    for i, b in enumerate(blobs):
        f = Path(td) / ("c%d.bin" % i); f.write_bytes(b); files.append(str(f))
    out = []
    for k in range(0, len(files), 200):
        r = subprocess.run([str(exe), mode, str(fw_abi), str(fw_tau)] + files[k:k + 200], capture_output=True, text=True)
        out += r.stdout.split("--\n")[:-1]
    return out
def refs(blobs, mode="file", fw_abi=0x0102, fw_tau=0x0008): return [mb.report(b, fw_abi, fw_tau, mode) for b in blobs]
def both(exe, td, blobs):
    """True when C = Python in both modes"""
    return c_report(exe, td, blobs, "file") == refs(blobs, "file") and c_report(exe, td, blobs, "toc") == refs(blobs, "toc")

# ---- synthetic packs and bins ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
def rle(rng):
    out, px = bytearray(), 0
    while px < 56 * 32:
        run = min(rng.randint(1, 32), 56 * 32 - px); out.append((rng.randint(0, 7) << 5) | (run - 1)); px += run
    return bytes(out)
def synth_pack(rng, name, origin=mb.WINDOW, abi=0x0102, mid=1, ps=None, bss=None, ps_off=None):
    np_ = rng.randint(0, 3); r = rle(rng); code = bytes(rng.randrange(256) for _ in range(rng.randint(1, 40) * 4))
    pal_o = 40 + 20 * np_; rle_o = pal_o + 64; name_o = rle_o + len(r); total = ((name_o + len(name) + 1 + 3) & ~3) + 8
    d = bytearray(total); struct.pack_into("<IHHBBHIIHHHHHHHHI", d, 0, 0x4353444D, 3, total, np_, 0, rng.randint(0, 9), 0, rng.randint(0, 1 << 20), name_o, 40, 0, pal_o, rle_o, len(r), mid, 1, 0)
    for i in range(64): d[pal_o + i] = rng.randrange(256)
    d[rle_o:rle_o + len(r)] = r; d[name_o:name_o + len(name)] = name.encode(); struct.pack_into("<II", d, total - 8, total, 0x31435344)
    image = code + bytes(d); po = (len(image) + 3) & ~3 if ps_off is None else ps_off; ps_size = rng.randint(0, 3000) if ps is None else ps
    hdr = struct.pack("<4sHHIIIIIIIIII", b"TMPK", abi, mid, len(image), origin, 0, zlib.crc32(image) & 0xFFFFFFFF, 0, len(code), 0x26C00, rng.randint(0, 2000) if bss is None else bss, po, ps_size)
    return hdr + image
def hexid(rng): return "".join(rng.choice("0123456789abcdef") for _ in range(32))
def rand_names(rng): return "".join(chr(rng.randint(0x20, 0x7E)) for _ in range(rng.randint(1, 23))).strip() or "M"
def synth_entries(rng, n, collide=0):
    es, seen = [], set()
    for k in range(n):
        i = hexid(rng)
        while i[:8] in seen: i = hexid(rng)
        if collide and k % 3 == 1: i = hexid(rng)[:4] + es[-1].id[4:8] + hexid(rng)[8:]; i = i if i[:8] not in seen else hexid(rng)
        seen.add(i[:8])
        es.append(mb.Entry(id=i, name=rand_names(rng), version=(rng.randint(0, 9), rng.randint(0, 20), rng.randint(0, 99)), tau_min=rng.choice([0, 0, 0x0008, 0x0007]), flags=rng.choice([1, 1, 1, 0, 3, 5]),
                           params_rev=rng.randint(0, 65535), pack=synth_pack(rng, "M", abi=rng.choice([0x0100, 0x0101, 0x0102, 0x0102]))))
    return es
def v1_bundle(packs): return b"TPKB" + struct.pack("<HH", 1, len(packs)) + b"".join(packs)
def refix(b):
    """recompute the total size, toc_crc and the end CRC (so a damage inside reaches the checks after them)"""
    b = bytearray(b)
    if len(b) >= 44 and b[:4] == b"TPKB" and struct.unpack_from("<H", b, 4)[0] == 2:
        struct.pack_into("<I", b, 28, len(b)); po = struct.unpack_from("<I", b, 24)[0]
        if po <= len(b): struct.pack_into("<I", b, 32, zlib.crc32(bytes(b[40:po])) & 0xFFFFFFFF)
        struct.pack_into("<I", b, len(b) - 4, zlib.crc32(bytes(b[40:-4])) & 0xFFFFFFFF)
    return bytes(b)
def put(b, off, data): x = bytearray(b); x[off:off + len(data)] = data; return bytes(x)
toc = lambda i, off: 40 + 256 * i + off

with tempfile.TemporaryDirectory() as td:
    exe = build(td)
    rng = random.Random(20261112)
    # 1. random valid bins
    v2s = [mb.compose(synth_entries(rng, rng.randint(0, 12), collide=rng.random() < 0.3)) for _ in range(120)] + [mb.compose(synth_entries(rng, 64))]
    v1s = [v1_bundle([synth_pack(rng, "V1 %d" % i, origin=mb.WINDOW + 0x10000 * i, mid=i) for i in range(rng.randint(0, 9))]) for _ in range(30)]
    check("%d random v2 bins (one with 64 meters, a third with colliding tuning keys): the C reader's report equals the Python reference's, row for row, in the boot mode (TOC copy only) and the full mode" % len(v2s),
          both(exe, td, v2s) and all(w.startswith("open 0 ") for w in refs(v2s)) and any(" 1" in w.splitlines()[-1] for w in refs(v2s)))
    check("%d random v1 bundles are read by the Python tool (inspection only) and the C reader refuses them as version 3, as the firmware must" % len(v1s), all(mb.report(b).startswith("open 0 ") for b in v1s) and all(w.startswith("open 3 ") for w in c_report(exe, td, v1s, "toc")))
    # 2. random damage
    dmg = []
    for k in range(700):
        b = bytearray(v2s[rng.randrange(0, 121)])
        if len(b) < 50: continue
        for _ in range(rng.choice([1, 1, 2])):
            i = rng.randrange(0, min(len(b), 40 + 256 * 3 + 64)) if rng.random() < 0.7 else rng.randrange(len(b))
            b[i] = rng.randrange(256)
        out = bytes(b)
        if rng.random() < 0.5: out = refix(out)
        if rng.random() < 0.08: out = out[:rng.randrange(len(out))]
        dmg.append(out)
    codes = {w.split()[1] for w in refs(dmg)}
    check("%d random damaged bins: C = Python in both modes (%d distinct outcomes: %s)" % (len(dmg), len(codes), ",".join(sorted(codes, key=int))), both(exe, td, dmg) and len(codes) >= 10, codes)
    # 3. the named refusals
    base_es = synth_entries(random.Random(7), 4)
    for e, nm in zip(base_es, ("Alpha", "Beta", "Gamma", "Delta")): e.name = nm
    base = mb.compose(base_es)
    def code(b, mode="file"): return int(mb.report(b, mode=mode).split()[1])
    def ccode(b, mode="file"): return int(c_report(exe, td, [b], mode)[0].split()[1])
    ok_pack = synth_pack(random.Random(9), "S"); slot_pack = synth_pack(random.Random(9), "S", origin=mb.WINDOW + 0x10000)
    far_ps = synth_pack(random.Random(9), "F", ps_off=65000, ps=1000); big_ps = synth_pack(random.Random(9), "B", ps=70000)
    one = lambda i, p: mb.compose([mb.Entry(id=i * 32, name="X", version=(1, 0, 0), pack=p)], strict=False)
    cases = [
        ("bad magic", b"NOPE" + base[4:], mb.E_MAGIC), ("version 3", put(base, 4, struct.pack("<H", 3)), mb.E_VERSION), ("version 0", put(base, 4, struct.pack("<H", 0)), mb.E_VERSION),
        ("truncated", base[:-1], mb.E_SIZE), ("one byte over", base + b"\0", mb.E_SIZE), ("shorter than a header", base[:20], mb.E_SIZE),
        ("bad toc_crc", put(base, 32, bytes([base[32] ^ 1])), mb.E_CRC), ("a TOC byte flipped", put(base, toc(1, 150), b"\1"), mb.E_CRC), ("a preview byte flipped", put(base, struct.unpack_from("<I", base, 16)[0] + 70, b"\xA5"), mb.E_CRC),
        ("count 65", put(base, 6, struct.pack("<H", 65)), mb.E_COUNT), ("count 65535", put(base, 6, struct.pack("<H", 65535)), mb.E_COUNT),
        ("toc offset 36", refix(put(base, 8, struct.pack("<I", 36))), mb.E_LAYOUT), ("toc length wrong", refix(put(base, 12, struct.pack("<I", 255))), mb.E_LAYOUT), ("packs offset past the end", refix(put(base, 24, struct.pack("<I", 1 << 30))), mb.E_LAYOUT),
        ("the reserved header u32", refix(put(base, 36, b"\1")), mb.E_RESERVED),
        ("duplicate id hash (same first 8 digits, other digits differ)", refix(put(put(base, toc(1, 0), base[toc(0, 0):toc(0, 0) + 4]), toc(1, 4), base[toc(0, 4):toc(0, 4) + 8])), mb.E_DUP),
        ("an upper-case id digit", refix(put(base, toc(2, 10), b"A")), mb.E_ID), ("an id of 31 digits", refix(put(base, toc(2, 35), b"\0")), mb.E_ID), ("an id with a byte after the 32 digits", refix(put(base, toc(2, 36), b"1")), mb.E_ID),
        ("id_hash not the id's first 8 digits", refix(put(base, toc(1, 0), b"\1\2\3\4")), mb.E_HASH),
        ("an empty name", refix(put(base, toc(0, 44), b"\0")), mb.E_NAME), ("a 24-character name", refix(put(base, toc(0, 44), b"X" * 24)), mb.E_NAME), ("a control character in the name", refix(put(base, toc(0, 44), b"\x07")), mb.E_NAME),
        ("a name with junk after its NUL", refix(put(base, toc(0, 66), b"Z")), mb.E_NAME),
        ("a reserved TOC byte set", refix(put(base, toc(1, 200), b"\1")), mb.E_RESERVED), ("a reserved flag bit set (bit 3, the old v1 mark)", refix(put(base, toc(1, 78), struct.pack("<H", 0x09))), mb.E_RESERVED),
        ("49 parameters", refix(put(base, toc(1, 82), struct.pack("<H", 49))), mb.E_PARAMS),
        ("a pack offset not 4-aligned", refix(put(base, toc(1, 92), struct.pack("<I", struct.unpack_from("<I", base, toc(1, 92))[0] + 1))), mb.E_PACK),
        ("a pack that runs past the end", refix(put(base, toc(1, 96), struct.pack("<I", 1 << 20))), mb.E_PACK), ("a pack offset past the end of the file", refix(put(base, toc(1, 92), struct.pack("<I", 1 << 20))), mb.E_PACK),
        ("a pack offset before the packs", refix(put(base, toc(1, 92), struct.pack("<I", 40))), mb.E_PACK),
        ("a pstate that cannot fit the window (TOC level)", one("b", big_ps), mb.E_WINDOW),
        ("a preview past its block", refix(put(base, toc(1, 116), struct.pack("<I", 4000))), mb.E_PREVIEW), ("a preview of 1025 bytes", refix(put(base, toc(1, 116), struct.pack("<I", 1025))), mb.E_PREVIEW),
        ("a preview that is not 56 x 32 pixels", refix(put(base, struct.unpack_from("<I", base, toc(1, 112))[0] + 64, bytes([base[struct.unpack_from("<I", base, toc(1, 112))[0] + 64] ^ 1]))), mb.E_PREVIEW),
    ]
    ok = True
    for name, b, want in cases:
        for mode in ("file", "toc"):
            pc, cc = code(b, mode), ccode(b, mode)
            if pc != want or cc != want: ok = False; print("FAIL refusal: %s [%s] -> python %d, c %d, want %d" % (name, mode, pc, cc, want))
    check("%d named refusals, each in the boot mode and the full mode: both readers give the documented code" % len(cases), ok)
    v1mark = put(base, 4, struct.pack("<H", 1))
    check("a bin that says version 1 is refused as VERSION by the boot path of both readers (the firmware reads v2 only; the Python tool's full mode maps v1 bundles instead)", code(v1mark, "toc") == mb.E_VERSION and ccode(v1mark, "toc") == mb.E_VERSION and ccode(v1mark, "file") == mb.E_VERSION)
    lazy = [("a pack linked for another slot (E24: not for the window)", one("a", slot_pack), mb.E_ORIGIN), ("a pstate that fits the TOC's bound but not the window with the pack's own offset", one("c", far_ps), mb.E_WINDOW),
            ("a TOC pack_crc that is not the pack header's", refix(put(base, toc(1, 100), b"\1\1\1\1")), mb.E_PACK), ("a TOC abi that is not the pack's", refix(put(base, toc(1, 74), struct.pack("<H", 0x0105))), mb.E_PACK),
            ("a pack header whose magic is damaged", refix(put(base, struct.unpack_from("<I", base, toc(2, 92))[0], b"XXXX")), mb.E_PACK), ("a flipped pack byte (only the whole-file CRC sees it)", put(base, len(base) - 20, bytes([base[-20] ^ 1])), mb.E_FILECRC),
            ("a wrong whole-file CRC", put(base, len(base) - 1, bytes([base[-1] ^ 1])), mb.E_FILECRC)]
    ok = True
    for name, b, want in lazy:
        if code(b, "file") != want or ccode(b, "file") != want or code(b, "toc") != 0 or ccode(b, "toc") != 0: ok = False; print("FAIL lazy: %s -> python %d/%d, c %d/%d, want %d then 0" % (name, code(b, "file"), code(b, "toc"), ccode(b, "file"), ccode(b, "toc"), want))
    check("%d damages that are NOT in the TOC or the previews: the boot path accepts the bin (no pack is read: the C reader is not even served those bytes), the full check refuses it with the documented code" % len(lazy), ok)
    # the loader's side: mb_check_pack_image is the CRC the loader sums; a flipped pack byte fails it, a good one passes
    r = mb.open_toc(base)["rows"]; bad = put(base, r[2]["pack_off"] + 60, bytes([base[r[2]["pack_off"] + 60] ^ 1]))
    check("pack_crc over the image: good for every pack of a sound bin, bad for a flipped pack byte (the loader's selection-time check)", all(mb.check_pack_image(base, w) for w in r) and not mb.check_pack_image(bad, r[2]) and mb.check_pack_image(bad, r[1]))
    wr = [mb.Entry(id="A" * 32, name="x", version=(1, 0, 0), pack=ok_pack), mb.Entry(id="a" * 32, name="", version=(1, 0, 0), pack=ok_pack), mb.Entry(id="a" * 32, name="é", version=(1, 0, 0), pack=ok_pack),
          mb.Entry(id="a" * 32, name="x" * 24, version=(1, 0, 0), pack=ok_pack), mb.Entry(id="a" * 32, name="x", version=(1, 0, 0), pack=slot_pack), mb.Entry(id="a" * 31, name="x", version=(1, 0, 0), pack=ok_pack),
          mb.Entry(id="a" * 32, name="x", version=(1, 0, 0), pack=ok_pack, flags=8)]
    def refused(e):
        try: mb.compose([e]); return False
        except ValueError: return True
    check("the composer refuses what the readers would: upper-case id, 31-digit id, empty, non-ASCII and 24-character names, a pack not linked for the window, flag bit 3 (%d of 7), and takes a sound one" % sum(map(refused, wr)),
          all(map(refused, wr)) and not refused(mb.Entry(id="a" * 32, name="x", version=(1, 0, 0), pack=ok_pack)))
    es65 = synth_entries(random.Random(3), 65)
    try: mb.compose(es65); r65 = False
    except ValueError: r65 = True
    dup = synth_entries(random.Random(4), 2); dup[1].id = dup[0].id[:8] + dup[1].id[8:]
    try: mb.compose(dup); rd = False
    except ValueError: rd = True
    check("the composer refuses 65 meters and two ids with one hash", r65 and rd)
    def padded(n):
        h = bytearray(40); h[:4] = b"TPKB"; struct.pack_into("<HHIIIIIIII", h, 4, 2, 0, 40, 0, 40, 0, 40, n, 0, 0)
        body = bytes(n - 44); b = bytearray(bytes(h) + body + b"\0\0\0\0"); struct.pack_into("<I", b, 32, zlib.crc32(b"") & 0xFFFFFFFF); struct.pack_into("<I", b, n - 4, zlib.crc32(bytes(b[40:-4])) & 0xFFFFFFFF); return bytes(b)
    big_ok, big_bad = padded(mb.MAX_BIN), padded(mb.MAX_BIN + 1)
    check("the largest legal bin (4,456,448 bytes) is accepted and 4,456,449 is refused (size, not CRC or layout), by both readers, boot mode and full mode", code(big_ok) == 0 and ccode(big_ok) == 0 and ccode(big_ok, "toc") == 0 and code(big_bad) == mb.E_SIZE and ccode(big_bad) == mb.E_SIZE and ccode(big_bad, "toc") == mb.E_SIZE)
    # availability
    es = synth_entries(random.Random(11), 3)
    for e, (abi, tau) in zip(es, [(0x0103, 0), (0x0202, 0), (0x0102, 0x0009)]):
        e.pack = bytearray(e.pack); struct.pack_into("<H", e.pack, 4, abi); e.pack = bytes(e.pack); e.tau_min = tau
    es.append(synth_entries(random.Random(12), 1)[0]); es[3].tau_min = 0x0008; es[3].pack = bytearray(es[3].pack); struct.pack_into("<H", es[3].pack, 4, 0x0102); es[3].pack = bytes(es[3].pack)
    b = mb.compose(es); rep = mb.report(b, 0x0102, 0x0008).splitlines()
    av = [l.split()[-1] for l in rep if l.startswith("avail")]
    check("availability: ABI 1.3 (above 1.2), ABI 2.2 (another major) and tau_min 0.9 (above 0.8) mark the row unavailable; ABI 1.2 and tau_min 0.8 (equal) are available (%s), the same in C" % av, av == ["1", "1", "2", "0"] and both(exe, td, [b]))
    # 5. no n^2 reads; tuning collisions
    class Count:
        pass
    reads = []
    def probe(n):
        bn = mb.compose(synth_entries(random.Random(n), n)); f = Path(td) / "cnt.bin"; f.write_bytes(bn)
        r = subprocess.run([str(exe), "count", "0", "0", str(f)], capture_output=True, text=True)
        return r.stdout
    cnt = {}
    for n in (8, 16, 32, 64):
        o = probe(n); cnt[n] = int(o.split()[-1]) if o.strip() else -1
    check("the boot path's reader calls grow linearly with the meters, not with their square (%s; n^2 would be 64x at 64 vs 8)" % cnt, all(v > 0 for v in cnt.values()) and cnt[64] < 12 * cnt[8], cnt)
    cu = [mb.Entry(id="aaaa1234" + "0" * 24, name="A", version=(1, 0, 0), pack=ok_pack), mb.Entry(id="bbbb5678" + "0" * 24, name="B", version=(1, 0, 0), pack=ok_pack), mb.Entry(id="cccc1234" + "0" * 24, name="C", version=(1, 0, 0), pack=ok_pack),
          mb.Entry(id="dddd5678" + "0" * 24, name="D", version=(1, 0, 0), pack=ok_pack), mb.Entry(id="eeee9999" + "0" * 24, name="E", version=(1, 0, 0), pack=ok_pack)]
    cb = mb.compose(cu)
    check("tuning collisions: rows sharing the low 16 bits of id_hash (1234: A, C; 5678: B, D) are all flagged, E is not, in both readers; none flagged without a collision", mb.report(cb).splitlines()[-1] == "coll 1 1 1 1 0" and both(exe, td, [cb]) and mb.report(base).splitlines()[-1] == "coll 0 0 0 0")
    # 6. the golden vectors
    vd = ROOT / "docs/research/meter_bin"
    exp = json.loads((vd / "expected.json").read_text()); good = True; msgs = []
    for v in exp["vectors"]:
        data = (vd / v["file"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != v["sha256"] or len(data) != v["bytes"]: good = False; msgs.append("hash " + v["file"])
        r = mb.report(data, 0x0102, 0x0008).splitlines()[0].split()
        if int(r[1]) != v["code"] or (v["code"] == 0 and int(r[2]) != v["count"]) or v["code_name"] != mb.CODE_NAMES[v["code"]]: good = False; msgs.append("read " + v["file"])
        if v["file"].startswith("v1_"): continue
        if int(mb.report(data, mode="toc").split()[1]) != v["boot_code"] or not both(exe, td, [data]): good = False; msgs.append("boot/C " + v["file"])
    check("the %d golden vectors: SHA-256 and size as filed, the full-check code, the boot code and the row count as expected.json says, C equal to Python (v1 ones: Python only)" % len(exp["vectors"]), good and len(exp["vectors"]) >= 10, msgs)
    import meter_bin_vectors as mbv
    v1v = (vd / "v1_seven.bin").read_bytes(); slot1 = v1v[8 + 48 + struct.unpack_from("<I", v1v, 16)[0]:]; slot1 = slot1[:48 + struct.unpack_from("<I", slot1, 8)[0]]
    regen = mbv.damaged((vd / "v2_seven.bin").read_bytes(), v1v, slot1, (vd / "packages/winamp_bars/meter.bin").read_bytes())
    check("the %d damaged vectors (and the collision bin) regenerate byte for byte from v2_seven.bin and v1_seven.bin (no toolchain), each with the code it is filed under" % len(regen), all((vd / n).read_bytes() == b and mb.report(b).split()[1] == str(c) for n, (b, _, c) in regen.items()) and len(regen) >= 20)
    r1 = mb.parse(v1v)
    check("the v1 bundle of the seven current packs maps to 7 rows in slot order (Python tool only), Winamp Bars flagged fallback", [r["name"] for r in r1["rows"]] == ["WINAMP BARS", "WINAMP SCOPE", "CHLADNI", "LAYERED WAVE", "VU MASTER", "WATERFALL", "SOUND MAP"] and r1["rows"][0]["flags"] & 2 and r1["version"] == 1)
    pk = []; off = 8
    for i in range(7):
        n = struct.unpack_from("<I", v1v, off + 8)[0]; pk.append(v1v[off:off + 48 + n]); off += 48 + n
    chk = [(v1_bundle([pk[0], pk[0]]), mb.E_DUP), (v1_bundle(pk[:1] + [pk[1][:48] + bytes(len(pk[1]) - 48)]), mb.E_DESC), (v1v[:-3], mb.E_SIZE), (v1_bundle(pk * 10), mb.E_COUNT)]
    check("v1 (Python tool): a duplicate meter id (9), a zeroed descriptor (18), a cut bundle (1), 70 packs (5) are refused with the code", [code(x) for x, _ in chk] == [c for _, c in chk])
    pkgs = sorted((vd / "packages").glob("*/meter.json"))
    ids = [json.loads(p.read_text())["id"] for p in pkgs]
    mans = {p.parent.name: json.loads((ROOT / "meters" / p.parent.name / "meter.json").read_text())["id"] for p in pkgs}
    check("every vector package pins its meter.bin by SHA-256, carries the shipped meter's permanent id, and the v2 bin's rows are exactly those packages (ids, names, pack bytes)", len(pkgs) == 7 and all(json.loads(p.read_text())["files"]["meter.bin"] == hashlib.sha256((p.parent / "meter.bin").read_bytes()).hexdigest() for p in pkgs)
          and sorted(ids) == sorted(mans.values()) and mb.compose_from_dirs([vd / "packages" / n for n in exp["package_order"]], fallback=exp["fallback_id"]) == (vd / exp["v2_main"]).read_bytes())
    # 7. mutants
    dupb = refix(put(put(base, toc(1, 0), base[toc(0, 0):toc(0, 0) + 4]), toc(1, 4), base[toc(0, 4):toc(0, 4) + 8]))
    mut = {1: ("duplicate hash not checked", dupb, "toc"), 2: ("whole-file CRC not checked", put(base, len(base) - 1, bytes([base[-1] ^ 1])), "file"), 3: ("count limit 65", put(base, 6, struct.pack("<H", 65)), "toc"), 4: ("ABI minor compared with >=", b, "toc"),
           5: ("pack range not checked", refix(put(base, toc(1, 92), struct.pack("<I", 1 << 20))), "toc"), 6: ("exact window fit not checked", one("c", far_ps), "file"), 7: ("a pack header read at open", base, "toc"),
           8: ("toc_crc ignored", put(base, toc(1, 150), b"\1"), "toc"), 9: ("tuning collisions not reported", cb, "toc")}
    for k, (what, data, mode) in mut.items():
        m = build(td, k); check("mutant %d (%s) is caught" % (k, what), c_report(m, td, [data], mode) != refs([data], mode))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
