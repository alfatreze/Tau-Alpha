#!/usr/bin/env python3
"""The meter-settings save file v1 (fw2/cores/mset_file_core.h, docs/features/meters/METER_SAVE_FILE_SPEC.md section 4) on the host: the firmware's own C code against a RAM model (sim/p2_mset_file_harness.c) and
against the independent Python reference (tools/meter_settings.py), byte for byte:
  * a long random run of edits (new meters, changes, removals, big records that force eviction of the oldest) gives the same 8,192 bytes from C and from Python after EVERY edit, and find agrees;
  * a Quit in the middle of an edit (at sampled write counts, and the last writes): the file still parses and holds either the previous records or the new ones, never a mixture, and the previous generation is untouched;
  * the seed (all zero), all 0xFF, damaged generations (each in turn, both), a newer version (nothing is written), a record of another params_rev (found, but not usable), removal;
  * four mutants of the firmware core must FAIL: the tail not zero-filled, the CRC not inverted, the valid generation overwritten in place, no eviction."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import meter_settings as ms
import toolchain
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def build(td, bug=0):
    exe = Path(td) / f"mset{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DBUG={bug}", "-o", str(exe), str(ROOT / "sim/p2_mset_file_harness.c")], capture_output=True, text=True, env=toolchain.host_env())
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
class H:
    def __init__(self, exe, td): self.exe, self.td = exe, Path(td)
    def _run(self, data, tear, *args):
        i, o = self.td / "in.bin", self.td / "out.bin"
        if data is not None: i.write_bytes(data)
        r = subprocess.run([str(self.exe), str(i) if data is not None else "-", str(o), str(tear), *args], capture_output=True, text=True)
        if r.returncode: return None, None
        return o.read_bytes(), r.stdout.strip()
    def put(self, data, id_hash, rev, preset, entries, tear=-1):
        e = ",".join(f"{i}:{v}" for i, v in entries) or "-"
        return self._run(data, tear, "put", str(id_hash), str(rev), str(preset), e)
    def find(self, data, id_hash, rev):
        _, out = self._run(data, -1, "find", str(id_hash), str(rev))
        d = dict(kv.split("=") for kv in out.split(" ", 3)[:3]) if out else {}
        pairs = []
        if out and " " in out.split("n=")[1]:
            pairs = [tuple(map(int, p.split(":"))) for p in out.split("n=")[1].split(" ", 1)[1].split(",")]
        return int(d.get("found", 0)), int(d.get("rev_ok", 0)), int(d.get("preset", 0)), pairs
    def scan(self, data):
        _, out = self._run(data, -1, "scan")
        return dict(kv.split("=") for kv in out.split())
def rand_entries(rng, n):
    idx = sorted(rng.sample(range(64), n))
    return [(i, rng.randrange(65536)) for i in idx]
def suite(h, quick=False):
    """-> number of byte-for-byte differences between C and Python (plus the checks above when not quick)."""
    rng = random.Random(1129)
    ids = [rng.getrandbits(32) for _ in range(70)]
    cur_c = cur_p = bytes(8192)
    diffs = 0; steps = 60 if quick else 400
    for s in range(steps):
        idh = rng.choice(ids); rev = rng.choice([1, 1, 1, 2]); preset = rng.randrange(8)
        n = rng.choice([0, 1, 2, 3, 5, 8, 16, 40]) if s % 7 else rng.choice([40, 48, 64])
        ent = rand_entries(rng, n)
        cur_c, out = h.put(cur_c, idh, rev, preset, ent)
        cur_p = ms.apply_change(cur_p, idh, rev, preset, ent)
        if cur_c != cur_p: diffs += 1; break
    if quick: return diffs
    check("400 random edits: the C code and the Python reference give the same 8,192 bytes after every one", diffs == 0, f"first difference at a step above")
    p = ms.parse(cur_c)
    check("the final file is sound, holds records, and stays within the generation size", p["status"] == "ok" and 0 < len(p["records"]) and p["gens"][p["active"]]["used"] <= ms.MAX_USED, p["status"])
    # find agrees with the reference for every id and both revisions
    bad = 0
    for idh in ids:
        for rev in (1, 2):
            f, ok, pr, pairs = h.find(cur_c, idh, rev)
            r = ms.find(cur_c, idh, rev)
            if r is None:
                any_rec = [x for x in p["records"] if x["id_hash"] == idh]
                if ok or (f and not any_rec): bad += 1
            else:
                if not (f and ok and pr == r["preset"] and pairs == [tuple(x) for x in r["entries"]]): bad += 1
    check("find agrees with the reference for 70 meters x 2 revisions (a record of another params_rev is found but not usable)", bad == 0, bad)
    # eviction: fill with big records, the oldest go
    big = bytes(8192); order = []
    for k in range(40):
        i = 0xA0000000 + k; big, _ = h.put(big, i, 1, 0, rand_entries(rng, 40)); order.append(i)
    pb = ms.parse(big); have = [r["id_hash"] for r in pb["records"]]
    check("40 meters of 40 changes (128 bytes each, 5,120 in all): the generation stays <= 4,076 bytes and it is the OLDEST that went", pb["gens"][pb["active"]]["used"] <= ms.MAX_USED and have == order[-len(have):] and len(have) < 40 and len(have) >= 31, (len(have), pb["gens"]))
    # a Quit in the middle of an edit
    base = cur_c; before = ms.parse(base)["records"]
    new_id, new_ent = 0xDEADBEEF, rand_entries(rng, 12)
    want = ms.parse(ms.apply_change(base, new_id, 1, 3, new_ent))["records"]
    ok_all = True; seen_old = seen_new = 0; inplace = False
    for k in list(range(0, 1100, 23)) + list(range(1015, 1040)):
        torn, _ = h.put(base, new_id, 1, 3, new_ent, tear=k)
        pt = ms.parse(torn)
        if pt["records"] == before: seen_old += 1
        elif pt["records"] == want: seen_new += 1
        else: ok_all = False
        a = ms.parse(base)["active"]
        if torn[a * 4096:(a + 1) * 4096] != base[a * 4096:(a + 1) * 4096]: inplace = True
    check("a Quit at any point of an edit leaves exactly the old or exactly the new records (never a mixture, never an unreadable file)", ok_all and seen_old and seen_new, (ok_all, seen_old, seen_new))
    check("the valid generation is never modified while the next one is built", not inplace)
    # the seed, 0xFF, damage, newer version
    for name, data in (("the zero seed", bytes(8192)), ("a file of 0xFF", b"\xff" * 8192)):
        sc = h.scan(data); f, *_ = h.find(data, ids[0], 1)
        out, _ = h.put(data, ids[0], 1, 0, [(1, 5)])
        pp = ms.parse(out)
        check(f"{name}: no generation, nothing found, the first edit writes bank A with seq 1", sc["active"] == "-1" and not f and pp["active"] == 0 and pp["seq"] == 1 and len(pp["records"]) == 1, (sc, pp["status"]))
    two = cur_c; pa = ms.parse(two)["active"]
    dmg = bytearray(two); dmg[pa * 4096 + 40] ^= 0x01
    sc = h.scan(bytes(dmg)); pp = ms.parse(bytes(dmg))
    check("the newest generation damaged (one flipped bit): the other, older one is used", sc["active"] == str(1 - pa) and pp["active"] == 1 - pa, (sc, pp["active"]))
    dmg2 = bytearray(dmg); dmg2[(1 - pa) * 4096 + 40] ^= 0x01
    sc = h.scan(bytes(dmg2))
    check("both damaged: no generation, defaults, and the next edit still works (bank A, seq 1)", sc["active"] == "-1" and ms.parse(bytes(dmg2))["status"] == "none" and ms.parse(h.put(bytes(dmg2), ids[0], 1, 0, [(2, 9)])[0])["seq"] == 1)
    newer = bytearray(two); newer[0:4] = b"TMSF"; newer[4:6] = (2).to_bytes(2, "little")
    out, so = h.put(bytes(newer), ids[0], 1, 0, [(2, 9)])
    check("a file of a NEWER version is never overwritten (put refused, 8,192 bytes unchanged)", so == "ok=0" and out == bytes(newer) and ms.apply_change(bytes(newer), ids[0], 1, 0, [(2, 9)]) is None and h.scan(bytes(newer))["locked"] == "1", so)
    # removal
    some = ms.parse(cur_c)["records"][-1]
    gone, _ = h.put(cur_c, some["id_hash"], some["params_rev"], 0, [])
    check("an edit with no entries removes the meter's record and the others stay", ms.find(gone, some["id_hash"], some["params_rev"]) is None and len(ms.parse(gone)["records"]) == len(ms.parse(cur_c)["records"]) - 1 and gone == ms.apply_change(cur_c, some["id_hash"], some["params_rev"], 0, []))
    # seq: a write goes to the other bank with seq + 1
    p0 = ms.parse(cur_c); p1 = ms.parse(gone)
    check("every write goes to the OTHER bank with seq + 1", p1["active"] == 1 - p0["active"] and p1["seq"] == p0["seq"] + 1)
    check("the Python reference's own parser reads what C wrote and rejects a wrong size", ms.parse(b"\0" * 100)["status"] == "size")
    return diffs
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    suite(H(build(td), td))
    for bug, what in ((1, "the tail of the bank not zero-filled"), (2, "the CRC not inverted"), (3, "the valid generation overwritten in place"), (4, "no eviction")):
        mh = H(build(td, bug), td)
        caught = True
        try:
            d = suite(mh, quick=True)
            # in-place / tail / crc / eviction: the byte comparison, or the torn-write and eviction checks, must notice
            f0 = fails
            if d == 0:
                # the quick comparison passed: the mutant must be caught by the targeted checks
                big = bytes(8192); ref = bytes(8192); rng = random.Random(7)
                for k in range(40):
                    e = rand_entries(rng, 40); big, _ = mh.put(big, 0xA0000000 + k, 1, 0, e); ref = ms.apply_change(ref, 0xA0000000 + k, 1, 0, e)
                pb = ms.parse(big)
                base = big; before = ms.parse(base)["records"]; torn_ok = True
                for k in range(0, 1100, 61):
                    torn, _ = mh.put(base, 0xDEADBEEF, 1, 3, rand_entries(rng, 12), tear=k)
                    a = ms.parse(base)["active"]
                    if torn[a * 4096:(a + 1) * 4096] != base[a * 4096:(a + 1) * 4096]: torn_ok = False
                caught = (big != ref) or (not torn_ok)
        except Exception as e:
            caught = True
        check(f"mutant must FAIL: {what}", caught)
print("FAILED" if fails else "all ok")
sys.exit(1 if fails else 0)
