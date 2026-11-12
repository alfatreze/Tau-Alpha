#!/usr/bin/env python3
"""The bin v2 reader (fw2/cores/meterbin_core.h) against the independent Python reference (tools/meter_bin.py); docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2, 3, 7 and EXTERNAL_METERS_P2_PLAN.md P2.1.
  1. the Python composer makes bins the C reader accepts, row for row equal to the reference (random meters: ids, names, flags, versions, ABI, previews, packs), 150 of them, v2 and v1;
  2. a damaged bin is refused with the SAME code by both: 600 random single and double byte damages, half with the end CRC fixed up so the inner checks are reached (the code is the documented MTOC ERR code);
  3. the named refusals: bad magic, version, truncation, trailing bytes, bad CRC, count over 64, duplicate hash, bad id, bad hash, bad name, reserved bytes, origin, window, preview, pack range, oversize (4,456,448 + 1), and that the largest legal bin is accepted;
  4. availability: an ABI above the firmware's, another major, a tau_min above the firmware's mark the row unavailable (not the bin refused); equal and lower are available;
  5. v1 bundles map to TOC rows: the real bundle of the seven current packs equals the reference, the rows keep slot order, a damaged descriptor or a duplicate meter id is refused;
  6. the committed golden vectors (docs/research/meter_bin/) read as their expected.json says, in both readers, and their SHA-256s match;
  7. mutants of the C reader (no duplicate check, no CRC check, count limit off by one, ABI minor compare, no pack range check, no window check) are each caught by a case above.
Host-simulated only."""
import hashlib, json, random, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import meter_bin as mb
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:400]))
    fails += 0 if ok else 1
def build(td, bug=0):
    exe = Path(td) / ("h%d" % bug)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-DBUG=%d" % bug, "-o", str(exe), str(ROOT / "sim/p2_meterbin_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def c_report(exe, td, blobs, fw_abi=0x0102, fw_tau=0x0008):
    """the C reader's report of each blob, as lines (the same text mb.report() makes)"""
    files = []
    for i, b in enumerate(blobs):
        f = Path(td) / ("c%d.bin" % i); f.write_bytes(b); files.append(str(f))
    out = []
    for k in range(0, len(files), 200):
        r = subprocess.run([str(exe), str(fw_abi), str(fw_tau)] + files[k:k + 200], capture_output=True, text=True)
        out += r.stdout.split("--\n")[:-1]
    return out
def refs(blobs, fw_abi=0x0102, fw_tau=0x0008): return [mb.report(b, fw_abi, fw_tau) for b in blobs]

# ---- synthetic packs and bins ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
def rle(rng):
    out, px = bytearray(), 0
    while px < 56 * 32:
        run = min(rng.randint(1, 32), 56 * 32 - px); out.append((rng.randint(0, 7) << 5) | (run - 1)); px += run
    return bytes(out)
def synth_pack(rng, name, origin=mb.WINDOW, abi=0x0102, mid=1, ps=None, bss=None):
    np_ = rng.randint(0, 3); r = rle(rng); code = bytes(rng.randrange(256) for _ in range(rng.randint(1, 40) * 4))
    pal_o = 40 + 20 * np_; rle_o = pal_o + 64; name_o = rle_o + len(r); total = ((name_o + len(name) + 1 + 3) & ~3) + 8
    d = bytearray(total); struct.pack_into("<IHHBBHIIHHHHHHHHI", d, 0, 0x4353444D, 3, total, np_, 0, rng.randint(0, 9), 0, rng.randint(0, 1 << 20), name_o, 40, 0, pal_o, rle_o, len(r), mid, 1, 0)
    for i in range(64): d[pal_o + i] = rng.randrange(256)
    d[rle_o:rle_o + len(r)] = r; d[name_o:name_o + len(name)] = name.encode(); struct.pack_into("<II", d, total - 8, total, 0x31435344)
    image = code + bytes(d); ps_off = (len(image) + 3) & ~3; ps_size = rng.randint(0, 3000) if ps is None else ps
    hdr = struct.pack("<4sHHIIIIIIIIII", b"TMPK", abi, mid, len(image), origin, 0, zlib.crc32(image) & 0xFFFFFFFF, 0, len(code), 0x26C00, rng.randint(0, 2000) if bss is None else bss, ps_off, ps_size)
    return hdr + image
def hexid(rng): return "".join(rng.choice("0123456789abcdef") for _ in range(32))
def rand_names(rng):
    return "".join(chr(rng.randint(0x20, 0x7E)) for _ in range(rng.randint(1, 23))).strip() or "M"
def synth_entries(rng, n):
    es, seen = [], set()
    for _ in range(n):
        i = hexid(rng)
        while i[:8] in seen: i = hexid(rng)
        seen.add(i[:8])
        es.append(mb.Entry(id=i, name=rand_names(rng), version=(rng.randint(0, 9), rng.randint(0, 20), rng.randint(0, 99)), tau_min=rng.choice([0, 0, 0x0008, 0x0007]), flags=rng.choice([1, 1, 1, 0, 3, 5]),
                           params_rev=rng.randint(0, 65535), pack=synth_pack(rng, "M", abi=rng.choice([0x0100, 0x0101, 0x0102, 0x0102])), ))
    return es
def v1_bundle(packs): return b"TPKB" + struct.pack("<HH", 1, len(packs)) + b"".join(packs)
def refix(b):
    """recompute the total size and the end CRC (so a damage inside reaches the checks after the CRC)"""
    b = bytearray(b)
    if len(b) >= 36 and b[:4] == b"TPKB" and struct.unpack_from("<H", b, 4)[0] == 2:
        struct.pack_into("<I", b, 28, len(b)); struct.pack_into("<I", b, len(b) - 4, zlib.crc32(bytes(b[32:-4])) & 0xFFFFFFFF)
    return bytes(b)

with tempfile.TemporaryDirectory() as td:
    exe = build(td)
    rng = random.Random(20261112)
    # 1. random valid bins
    bins = [mb.compose(synth_entries(rng, rng.randint(0, 12))) for _ in range(120)] + [mb.compose(synth_entries(rng, 64))] + [v1_bundle([synth_pack(rng, "V1 %d" % i, origin=mb.WINDOW + 0x10000 * i, mid=i) for i in range(rng.randint(0, 9))]) for _ in range(30)]
    want, got = refs(bins), c_report(exe, td, bins)
    check("%d random bins (120 v2, one with 64 meters, 30 v1): the C reader's report equals the Python reference's, row for row" % len(bins), want == got and all(w.startswith("open 0 ") for w in want), [(i, w, g) for i, (w, g) in enumerate(zip(want, got)) if w != g][:1])
    # 2. random damage
    dmg, kinds = [], []
    for k in range(600):
        b = bytearray(bins[rng.randrange(0, 121)] if k % 5 else bins[121 + rng.randrange(30)])
        if len(b) < 40: continue
        for _ in range(rng.choice([1, 1, 2])):
            region = rng.random()
            i = rng.randrange(0, min(len(b), 32 + 256 * 3 + 64)) if region < 0.7 else rng.randrange(len(b))
            b[i] = rng.randrange(256)
        out = bytes(b)
        if rng.random() < 0.5: out = refix(out)
        if rng.random() < 0.08: out = out[:rng.randrange(len(out))]
        dmg.append(out)
    want, got = refs(dmg), c_report(exe, td, dmg)
    codes = {w.split()[1] for w in want}
    check("%d random damaged bins: both readers give the same code or the same rows (%d distinct outcomes: %s)" % (len(dmg), len(codes), ",".join(sorted(codes, key=int))), want == got and len(codes) >= 9, [(w, g) for w, g in zip(want, got) if w != g][:1])
    # 3. the named refusals
    base_es = synth_entries(random.Random(7), 4)
    for e, nm in zip(base_es, ("Alpha", "Beta", "Gamma", "Delta")): e.name = nm
    base = mb.compose(base_es)
    def code(b): return int(mb.report(b).split()[1])
    def ccode(b): return int(c_report(exe, td, [b])[0].split()[1])
    def row_at(b, i): return 32 + 256 * i
    def put(b, off, data): x = bytearray(b); x[off:off + len(data)] = data; return bytes(x)
    toc = lambda i, off: 32 + 256 * i + off
    cases = [
        ("bad magic", b"NOPE" + base[4:], mb.E_MAGIC), ("bad version", put(base, 4, struct.pack("<H", 3)), mb.E_VERSION), ("version 0", put(base, 4, struct.pack("<H", 0)), mb.E_VERSION),
        ("truncated", base[:-1], mb.E_SIZE), ("one byte over", base + b"\0", mb.E_SIZE), ("shorter than a header", base[:20], mb.E_SIZE),
        ("bad end CRC", put(base, len(base) - 1, bytes([base[-1] ^ 1])), mb.E_CRC), ("a flipped pack byte", put(base, len(base) - 20, bytes([base[-20] ^ 1])), mb.E_CRC),
        ("count 65", put(base, 6, struct.pack("<H", 65)), mb.E_COUNT), ("count 65535", put(base, 6, struct.pack("<H", 65535)), mb.E_COUNT),
        ("toc offset moved", refix(put(base, 8, struct.pack("<I", 36))), mb.E_LAYOUT), ("toc length wrong", refix(put(base, 12, struct.pack("<I", 255))), mb.E_LAYOUT),
        ("packs offset past the end", refix(put(base, 24, struct.pack("<I", 1 << 30))), mb.E_LAYOUT),
        ("duplicate id hash (same first 8 digits, other digits differ)", refix(put(put(base, toc(1, 0), base[toc(0, 0):toc(0, 0) + 4]), toc(1, 4), base[toc(0, 4):toc(0, 4) + 8])), mb.E_DUP),
        ("an upper-case id digit", refix(put(base, toc(2, 10), b"A")), mb.E_ID), ("an id of 31 digits", refix(put(base, toc(2, 35), b"\0")), mb.E_ID), ("an id with a byte after the 32 digits", refix(put(base, toc(2, 36), b"1")), mb.E_ID),
        ("id_hash not the id's first 8 digits", refix(put(base, toc(1, 0), b"\1\2\3\4")), mb.E_HASH),
        ("an empty name", refix(put(base, toc(0, 44), b"\0")), mb.E_NAME), ("a 24-character name", refix(put(base, toc(0, 44), b"X" * 24)), mb.E_NAME), ("a control character in the name", refix(put(base, toc(0, 44), b"\x07")), mb.E_NAME),
        ("a name with junk after its NUL", refix(put(base, toc(0, 66), b"Z")), mb.E_NAME),
        ("a reserved TOC byte set", refix(put(base, toc(1, 200), b"\1")), mb.E_RESERVED), ("a reserved flag bit set", refix(put(base, toc(1, 78), struct.pack("<H", 0x11))), mb.E_RESERVED),
        ("49 parameters", refix(put(base, toc(1, 82), struct.pack("<H", 49))), mb.E_PARAMS),
        ("a pack offset not 4-aligned", refix(put(base, toc(1, 92), struct.pack("<I", struct.unpack_from("<I", base, toc(1, 92))[0] + 1))), mb.E_PACK),
        ("a pack that runs past the end", refix(put(base, toc(1, 96), struct.pack("<I", 1 << 20))), mb.E_PACK), ("a pack offset before the packs", refix(put(base, toc(1, 92), struct.pack("<I", 32))), mb.E_PACK),
        ("a TOC CRC that is not the pack's", refix(put(base, toc(1, 100), b"\1\1\1\1")), mb.E_PACK), ("a TOC abi that is not the pack's", refix(put(base, toc(1, 74), struct.pack("<H", 0x0105))), mb.E_PACK),
        ("a preview past its block", refix(put(base, toc(1, 116), struct.pack("<I", 4000))), mb.E_PREVIEW), ("a preview of 1025 bytes", refix(put(base, toc(1, 116), struct.pack("<I", 1025))), mb.E_PREVIEW),
        ("a preview that is not 56 x 32 pixels", refix(put(base, struct.unpack_from("<I", base, toc(1, 112))[0] + 64, bytes([base[struct.unpack_from("<I", base, toc(1, 112))[0] + 64] ^ 1]))), mb.E_PREVIEW),
    ]
    # window and origin need the pack header changed too: rebuild with a pack the composer is told to accept
    ok_pack = synth_pack(random.Random(9), "S"); slot_pack = synth_pack(random.Random(9), "S", origin=mb.WINDOW + 0x10000); big = synth_pack(random.Random(9), "B", ps=70000)
    cases.append(("a pack linked for another slot (E24 now means not for the window)", mb.compose([mb.Entry(id="a" * 32, name="S", version=(1, 0, 0), pack=slot_pack)], strict=False), mb.E_ORIGIN))
    cases.append(("a pack whose pstate does not fit the window", mb.compose([mb.Entry(id="b" * 32, name="B", version=(1, 0, 0), pack=big)], strict=False), mb.E_WINDOW))
    ok = True
    for name, b, want_code in cases:
        pc, cc = code(b), ccode(b)
        if pc != want_code or cc != want_code: ok = False; print("FAIL refusal: %s -> python %d, c %d, want %d" % (name, pc, cc, want_code))
    check("%d named refusals: both readers give the documented code" % len(cases), ok)
    wr = [mb.Entry(id="A" * 32, name="x", version=(1, 0, 0), pack=ok_pack), mb.Entry(id="a" * 32, name="", version=(1, 0, 0), pack=ok_pack), mb.Entry(id="a" * 32, name="\u00e9", version=(1, 0, 0), pack=ok_pack),
          mb.Entry(id="a" * 32, name="x" * 24, version=(1, 0, 0), pack=ok_pack), mb.Entry(id="a" * 32, name="x", version=(1, 0, 0), pack=slot_pack), mb.Entry(id="a" * 31, name="x", version=(1, 0, 0), pack=ok_pack)]
    def refused(e):
        try: mb.compose([e]); return False
        except ValueError: return True
    check("the composer refuses what the readers would: upper-case id, 31-digit id, empty, non-ASCII and 24-character names, a pack not linked for the window (%d of 6), and takes a sound one" % sum(map(refused, wr)), all(map(refused, wr)) and not refused(mb.Entry(id="a" * 32, name="x", version=(1, 0, 0), pack=ok_pack)))
    es65 = synth_entries(random.Random(3), 65)
    try: mb.compose(es65); r65 = False
    except ValueError: r65 = True
    dup = synth_entries(random.Random(4), 2); dup[1].id = dup[0].id[:8] + dup[1].id[8:]
    try: mb.compose(dup); rd = False
    except ValueError: rd = True
    check("the composer refuses 65 meters and two ids with one hash", r65 and rd)
    # the size limit: the largest legal file is accepted, one byte more is refused (a file of zeros with a good CRC, so only the size can be the reason)
    def padded(n):
        h = bytearray(32); h[:4] = b"TPKB"; struct.pack_into("<HHIIIIII", h, 4, 2, 0, 32, 0, 32, 0, 32, n)
        body = bytes(n - 36); return bytes(h) + body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
    big_ok, big_bad = padded(mb.MAX_BIN), padded(mb.MAX_BIN + 1)
    check("the largest legal bin (4,456,448 bytes) is accepted and 4,456,449 is refused, by both readers (size, not CRC or layout)", code(big_ok) == 0 and code(big_bad) == mb.E_SIZE and ccode(big_bad) == mb.E_SIZE and ccode(big_ok) == code(big_ok) and code(big_ok) != mb.E_SIZE)
    # 4. availability
    es = synth_entries(random.Random(11), 3)
    for e, (abi, tau) in zip(es, [(0x0103, 0), (0x0202, 0), (0x0102, 0x0009)]):
        e.pack = bytearray(e.pack); struct.pack_into("<H", e.pack, 4, abi); e.pack = bytes(e.pack); e.tau_min = tau
    es.append(synth_entries(random.Random(12), 1)[0]); es[3].tau_min = 0x0008; es[3].pack = bytearray(es[3].pack); struct.pack_into("<H", es[3].pack, 4, 0x0102); es[3].pack = bytes(es[3].pack)
    b = mb.compose(es); rep = mb.report(b, 0x0102, 0x0008).splitlines()
    av = [l.split()[-1] for l in rep if l.startswith("avail")]
    check("availability: ABI 1.3 (above 1.2), ABI 2.2 (another major) and tau_min 0.9 (above 0.8) mark the row unavailable; ABI 1.2 and tau_min 0.8 (equal) are available (%s), the same in C" % av, av == ["1", "1", "2", "0"] and c_report(exe, td, [b]) == refs([b]) and mb.report(b, 0x0103, 0x0009).splitlines()[-1].endswith("0"))
    # 5. v1
    v1 = (ROOT / "docs/research/meter_bin/v1_seven.bin").read_bytes()
    r1 = mb.parse(v1)
    check("the v1 bundle of the seven current packs maps to 7 rows in slot order, Winamp Bars flagged fallback, names read from the descriptors, equal in C", [r["name"] for r in r1["rows"]] == ["WINAMP BARS", "WINAMP SCOPE", "CHLADNI", "LAYERED WAVE", "VU MASTER", "WATERFALL", "SOUND MAP"]
          and r1["rows"][0]["flags"] & 2 and r1["version"] == 1 and c_report(exe, td, [v1]) == refs([v1]) and refs([v1])[0].startswith("open 0 7 "), refs([v1])[0][:80])
    pk = []; off = 8
    for i in range(7):
        n = struct.unpack_from("<I", v1, off + 8)[0]; pk.append(v1[off:off + 48 + n]); off += 48 + n
    dup1 = v1_bundle([pk[0], pk[0]]); gone = v1_bundle(pk[:1] + [pk[1][:48] + bytes(len(pk[1]) - 48)]); cut = v1[:-3]
    check("v1: a duplicate meter id (9), a zeroed descriptor (18), a cut bundle (1), 65 packs (5) are refused with the code, in both readers", [code(x) for x in (dup1, gone, cut, v1_bundle(pk * 10))] == [mb.E_DUP, mb.E_DESC, mb.E_SIZE, mb.E_COUNT]
          and [ccode(x) for x in (dup1, gone, cut, v1_bundle(pk * 10))] == [mb.E_DUP, mb.E_DESC, mb.E_SIZE, mb.E_COUNT], [code(x) for x in (dup1, gone, cut)])
    # 6. the golden vectors
    vd = ROOT / "docs/research/meter_bin"
    exp = json.loads((vd / "expected.json").read_text()); good = True; msgs = []
    for v in exp["vectors"]:
        data = (vd / v["file"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != v["sha256"] or len(data) != v["bytes"]: good = False; msgs.append("hash " + v["file"])
        r = mb.report(data, 0x0102, 0x0008).splitlines()[0].split()
        if int(r[1]) != v["code"] or (v["code"] == 0 and int(r[2]) != v["count"]) or c_report(exe, td, [data]) != refs([data]) or v["code_name"] != mb.CODE_NAMES[v["code"]]: good = False; msgs.append("read " + v["file"])
    check("the %d golden vectors: SHA-256 and size as filed, the refusal code or row count as expected.json says, equal in C" % len(exp["vectors"]), good and len(exp["vectors"]) >= 10, msgs)
    import meter_bin_vectors as mbv
    v1v = (vd / "v1_seven.bin").read_bytes(); slot0 = v1v[8:8 + 48 + struct.unpack_from("<I", v1v, 16)[0]]; slot1 = v1v[8 + len(slot0):]; slot1 = slot1[:48 + struct.unpack_from("<I", slot1, 8)[0]]
    regen = mbv.damaged((vd / "v2_seven.bin").read_bytes(), v1v, slot1)
    check("the %d damaged vectors regenerate byte for byte from v2_seven.bin and v1_seven.bin (no toolchain), each with the code it is filed under" % len(regen), all((vd / n).read_bytes() == b and mb.report(b).split()[1] == str(c) for n, (b, _, c) in regen.items()) and len(regen) >= 20)
    pkgs = sorted((vd / "packages").glob("*/meter.json"))
    check("every vector package pins its meter.bin by SHA-256 and the v2 bin's rows are exactly those packages (ids, names, pack bytes)", len(pkgs) == 7 and all(json.loads(p.read_text())["files"]["meter.bin"] == hashlib.sha256((p.parent / "meter.bin").read_bytes()).hexdigest() for p in pkgs)
          and mb.compose_from_dirs([vd / "packages" / n for n in exp["package_order"]], fallback=exp["fallback_id"]) == (vd / exp["v2_main"]).read_bytes())
    # 7. mutants
    mut = {1: ("duplicate hash not checked", None), 2: ("end CRC not checked", put(base, len(base) - 1, bytes([base[-1] ^ 1]))),
           3: ("count limit 65", put(base, 6, struct.pack("<H", 65))), 4: ("ABI minor compared with >=", b), 5: ("pack range not checked", refix(put(base, toc(1, 92), struct.pack("<I", 1 << 20)))), 6: ("window size not checked", mb.compose([mb.Entry(id="b" * 32, name="B", version=(1, 0, 0), pack=big)], strict=False))}
    mut[1] = ("duplicate hash not checked", refix(put(put(base, toc(1, 0), base[toc(0, 0):toc(0, 0) + 4]), toc(1, 4), base[toc(0, 4):toc(0, 4) + 8])))
    for k, (what, data) in mut.items():
        m = build(td, k); rep = c_report(m, td, [data]); check("mutant %d (%s) is caught" % (k, what), rep != refs([data]))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
