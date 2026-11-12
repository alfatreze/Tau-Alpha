#!/usr/bin/env python3
"""Reference implementation (Python) of Player 2's persisted-settings layout and of the migration from the legacy layout, for Tau Omega and other tools.

The authority is the firmware: fw2/cores/settings_core.h (the table, `sf_migrate_*`) and fw2/settings/settings.c. This file is a second statement of the same layout, proven equal to the C
code over random register files by sim/test_persist_layout.py, with golden vectors in docs/research/persist_layout/golden_vectors.json. Meaning of the words: docs/features/PERSISTENCE_LAYOUT.md,
what each persist id means by release: tools/persist_registry.json.

A card keeps the settings of each core in its own folder: Settings/<core id>/Interact/_core/interact_persist.json (APF's file: {"interact_persist": {"variables": [{"id", "type", "val"}]}}). A value is
`val` of the variable with that id; the firmware's register file is 32 words, a variable is one word (ID_TO_WORD below, the same addresses as dist/Cores/alfatreze.TAU/interact.json: word = (address - 0x20000000) / 4).
Two layouts exist:  legacy (v0.6.1 and every TAU DEV build up to 181): word 0 is the volume 0..100;  new (next release): word 0 carries the layout version in bits 30..27 (1) and every other word is regrouped by area.
Never copy persisted words from a core of the new layout to a core of the legacy one (it reads defaults and the first save rewrites the legacy layout); legacy -> new is what `migrate` does (the firmware does it
itself at boot, lossless except the old single tuning, which is dropped).

  python3 tools/persist_layout.py FILE.json          decode a persist file: its layout, every named value, and (layout 0 or 1) what the migration would write
  python3 tools/persist_layout.py --stage ROOT       write the staged captures under ROOT/docs/research/persist_layout/staged and print their SHA-256
  python3 tools/persist_layout.py --golden OUT.json  write the golden vectors
"""
import json, random, re, sys
from pathlib import Path

NEW_MARK = 1 << 30                      # bit 30 of words 1 and 2 marks a word already converted from layout 0 (no field reaches it)
MARK6 = 1 << 29                         # bit 29 of word 6 marks "converted to layout 2" (word 6 keeps its layout-1 meaning's bit 30 free of this: a layout-1 word 6 never has bit 29)
MARKS = ((1, 30), (2, 30), (6, 29))
MARKED_WORDS = (1, 2, 6)
# the write order of BOTH migrations (layout 0 -> 2 and layout 1 -> 2), word 0 last: word 6 (the selected preset and, as scratch, the old meter index) BEFORE word 5 (the id hash), so a power loss between them is resumed from
# the index kept in word 6; word 15 is emptied after word 6 has used it; the tuning slots (B: 16, 17, 18, 21; A: 8..11) are converted slot by slot
WRITE_ORDER = (1, 2, 6, 5, 15, 16, 17, 18, 21, 8, 9, 10, 11, 19, 20)
LAYOUT_VERSION = 2
SCRATCH_SHIFT, SCRATCH_WIDTH = 11, 6    # word 6 bits 16..11: the meter index the migration came from (scratch for a resumed migration; ignored by the player)
ID_TO_WORD = {10: 0, 11: 1, 12: 2, 15: 5, 16: 6, 20: 8, 21: 9, 22: 10, 23: 11, 24: 12, 25: 13, 26: 14, 27: 16, 28: 17, 29: 18, 30: 15, 31: 19, 32: 20, 33: 21}
WORD_TO_ID = {w: i for i, w in ID_TO_WORD.items()}
N_WORDS = 32
HASH31 = 0x7FFFFFFF
TUNE_SLOT_WORDS = ((8, 9, 10, 11), (16, 17, 18, 21))     # slot A, slot B
TUNE_BITS, TUNE_HEAD_V1, TUNE_HEAD_V2, TUNE_SEED_V1, TUNE_SEED_V2 = 124, 24, 39, 0x5A, 0x3C


def shipped_meters():
    """[(index, id_hash, params_rev)] of the seven shipped meters, read from fw2/cores/shipped_meter_ids.h (the one place; P2.0 overwrites it with the real ids)."""
    t = (Path(__file__).resolve().parent.parent / "fw2/cores/shipped_meter_ids.h").read_text()
    return [(int(i), int(h, 16), int(r)) for i, h, r in re.findall(r"X\((\d+),\s*0x([0-9A-Fa-f]+)u,\s*(\d+)u\)", t)]


SHIPPED = shipped_meters()


def hash31_of_index(i):
    """The persisted selection (31 bits of id_hash) of a layout-0/1 meter index; 0 (= none, the player falls back to Winamp Bars) for an index that is not a shipped meter."""
    return SHIPPED[i][1] & HASH31 if 0 <= i < len(SHIPPED) else 0


def key_of_index(i):
    """The 20-bit key of a tuning record: the low 16 bits of id_hash and the low 4 bits of params_rev; None for an index that is not a shipped meter."""
    return tune_key(SHIPPED[i][1], SHIPPED[i][2]) if 0 <= i < len(SHIPPED) else None


def tune_key(id_hash, params_rev): return (id_hash & 0xFFFF) | ((params_rev & 15) << 16)


def _fields():
    """(name, word, shift, width, min, max, default) in the firmware's order (sf_table). The fifteen after the tuning fields (METER, MPRE0..6, MPID0..6) are COMPATIBILITY entries with no storage (width 0): they keep fw2/ui/appearance.c
    compiling until P2.4 replaces its call sites; they always read 0 and are never written."""
    t = [("VOLUME", 2, 0, 7, 0, 100, 94), ("REPEAT", 2, 7, 2, 0, 2, 0), ("RGMODE", 2, 9, 2, 0, 2, 0), ("HALCYON", 2, 11, 5, 0, 30, 0),
         ("COLOUR", 1, 0, 5, 0, 18, 1), ("THEME", 1, 5, 6, 0, 31, 0), ("POLARITY", 1, 11, 1, 0, 1, 0), ("METER_HASH", 5, 0, 31, 0, HASH31, 0), ("FONT", 1, 12, 3, 0, 7, 0),
         ("LAYOUT", 0, 27, 4, 0, 15, LAYOUT_VERSION), ("TUNE_NEW", 0, 26, 1, 0, 1, 0),
         ("METER_PRE_POS", 6, 0, 4, 0, 15, 0), ("METER_PRE_ID", 6, 4, 7, 0, 127, 0)]
    for k, w in enumerate((8, 9, 10, 11, 16, 17, 18, 21)):      # two tuning slots of four words (slot A ids 20-23, slot B ids 27-29 and 33)
        t.append(("TUNE%d" % k, w, 0, 31, 0, 0x7FFFFFFF, 0))
    t += [("METER", 0, 0, 0, 0, 0, 0)] + [("MPRE%d" % m, 0, 0, 0, 0, 0, 0) for m in range(7)] + [("MPID%d" % m, 0, 0, 0, 0, 0, 0) for m in range(7)]
    t += [("LAST0", 19, 0, 31, 0, 0x7FFFFFFF, 0), ("LAST1", 20, 0, 31, 0, 0x7FFFFFFF, 0)]       # the last played track (ids 31, 32: the same in every layout)
    return t


FIELDS = _fields()
NAMES = [f[0] for f in FIELDS]
# layout 0 (v0.6.1 .. TAU DEV 181): name -> (word, shift, width, min, max, default); a width of 32 is a whole word
LEGACY = {"VOLUME": (0, 0, 32, 0, 100, 94), "REPEAT": (2, 0, 32, 0, 2, 0), "RGMODE": (17, 4, 2, 0, 2, 0), "HALCYON": (6, 0, 32, 0, 30, 0), "COLOUR": (1, 0, 32, 0, 18, 1),
          "THEME": (16, 0, 32, 0, 31, 0), "POLARITY": (17, 0, 1, 0, 1, 0), "METER": (5, 0, 32, 0, 14, 0), "MPRE0": (18, 0, 4, 0, 7, 0), "MPRE1": (18, 4, 4, 0, 7, 0),
          "MPRE2": (18, 8, 4, 0, 7, 0), "MPRE3": (18, 12, 4, 0, 15, 0), "MPRE4": (18, 16, 4, 0, 7, 0), "FONT": (18, 20, 3, 0, 7, 0), "MPRE5": (18, 23, 4, 0, 7, 0),
          "MPRE6": (15, 0, 4, 0, 15, 0), "LAST0": (19, 0, 31, 0, 0x7FFFFFFF, 0), "LAST1": (20, 0, 31, 0, 0x7FFFFFFF, 0)}
# layout 1 (TAU DEV 182 .. 219, v0.7.0, v0.7.1): the meter parts only (every other word has the same meaning in layout 2): (word, shift, width, min, max)
V1_METER = (5, 0, 6, 0, 62)
V1_MPRE = [(5, 6 + 4 * m, 4, 0, 15) if m < 6 else (6, 0, 4, 0, 15) for m in range(7)]
V1_MPID = [(6, 4, 7, 0, 127), (6, 11, 7, 0, 127), (6, 18, 7, 0, 127), (15, 0, 7, 0, 127), (15, 7, 7, 0, 127), (15, 14, 7, 0, 127), (15, 21, 7, 0, 127)]


def _mask(width): return 0xFFFFFFFF if width >= 32 else (1 << width) - 1


def version(words): return (words[0] >> 27) & 15


def unpack(words):
    """The layout-2 values: name -> value (a field out of range reads as its default, like the firmware)."""
    out = {}
    for name, word, sh, wd, lo, hi, df in FIELDS:
        v = (words[word] >> sh) & _mask(wd)
        out[name] = v if lo <= v <= hi else df
    return out


def pack(words, values):
    """Merge values (name -> value, clamped to the field's range) into a copy of words, keeping every other bit; the converted marks are set (words 1, 2 bit 30 and word 6 bit 29)."""
    w = list(words)
    for name, word, sh, wd, lo, hi, df in FIELDS:
        v = max(lo, min(hi, values.get(name, df)))
        m = _mask(wd) << sh
        w[word] = (w[word] & ~m & 0xFFFFFFFF) | ((v << sh) & m)
    for k, b in MARKS: w[k] |= 1 << b
    return w


def is_legacy(words):
    """Layout version 0 (bits 30..27 of word 0): a legacy volume 0..100 (or an empty word)."""
    return version(words) == 0


def is_v1(words): return version(words) == 1


def needs_migration(words): return version(words) in (0, 1)


def legacy_unpack(words):
    """name -> layout-2 value as a LAYOUT-0 register file holds it; the selected meter index becomes METER_HASH (0 = none for an index that is not a shipped meter) and its preset position METER_PRE_POS;
    fields the legacy layout did not have take their default (the old single tuning is not carried over)."""
    vals = {f[0]: f[6] for f in FIELDS}
    leg = {}
    for name, (word, sh, wd, lo, hi, df) in LEGACY.items():
        v = (words[word] >> sh) & _mask(wd)
        leg[name] = v if lo <= v <= hi else df
    for name in ("VOLUME", "REPEAT", "RGMODE", "HALCYON", "COLOUR", "THEME", "POLARITY", "FONT", "LAST0", "LAST1"): vals[name] = leg[name]
    idx = leg["METER"]
    vals["METER_HASH"] = hash31_of_index(idx)
    vals["METER_PRE_POS"] = leg["MPRE%d" % idx] if idx < 7 else 0
    vals["METER_PRE_ID"] = 0
    return vals


def _get(words, spec):
    word, sh, wd, lo, hi = spec
    v = (words[word] >> sh) & _mask(wd)
    return v if lo <= v <= hi else 0


def v1_selection(words):
    """(meter index, preset position, preset id + 1) the layout-1 words select; an index that is not a shipped meter selects nothing (0, 0, 0 with idx kept for the record)."""
    idx = _get(words, V1_METER)
    if idx >= len(SHIPPED): return idx, 0, 0
    return idx, _get(words, V1_MPRE[idx]), _get(words, V1_MPID[idx])


# ---- the tuning record: v1 (check 8 | meter 5 | preset 7 | count 4 | entries from bit 24, seed 0x5A) and v2 (check 8 | key 20 | preset 7 | count 4 | entries from bit 39, seed 0x3C) ----
def _bits(w4):
    b = []
    for w in w4: b += [(w >> i) & 1 for i in range(31)]
    return b + [0] * 4


def _get_bits(b, pos, n): return sum(b[pos + i] << i for i in range(n)), pos + n


def tune_nbits(mx):
    n = 1
    while n < 16 and (mx >> n): n += 1
    return n


def _check(b, seed):
    by = [sum(b[8 * k + j] << j for j in range(8)) for k in range(16)]
    c = seed
    for k in range(1, 16): c = ((c << 1) | (c >> 7)) & 255; c = (c + by[k]) & 255
    return c


def tune_v2_valid(w4):
    """a record of layout 2 (the check byte matches and a count is stored); the entries are validated against the meter only when the tuning is applied"""
    b = _bits(w4); chk, _ = _get_bits(b, 0, 8); n, _ = _get_bits(b, 35, 4)
    return n != 0 and chk == _check(b, TUNE_SEED_V2)


def tune_v1_parse(w4, max_of):
    """(meter index, preset, [(param index, value)]) of a layout-1 record, or None (zeros, a bad check, an unknown meter, an index past its table, a value past its maximum)"""
    b = _bits(w4); chk, pos = _get_bits(b, 0, 8); meter, pos = _get_bits(b, pos, 5); preset, pos = _get_bits(b, pos, 7); n, pos = _get_bits(b, pos, 4)
    if not n or chk != _check(b, TUNE_SEED_V1): return None
    mx = max_of(meter) if max_of else None
    if mx is None: return None
    out = []
    for _ in range(n):
        i, pos = _get_bits(b, pos, 6)
        if i >= len(mx): return None
        nb = tune_nbits(mx[i])
        if pos + nb > TUNE_BITS: return None
        v, pos = _get_bits(b, pos, nb)
        if v > mx[i]: return None
        out.append((i, v))
    return meter, preset, out


def tune_v2_pack(key, preset, entries, mx):
    """the four words of a layout-2 record for `entries` [(param index, value)] in index order (those that do not fit are skipped, at most 15 are stored); zeros when none is stored"""
    bits = [0] * 128
    def put(pos, v, n):
        for i in range(n): bits[pos + i] = (v >> i) & 1
        return pos + n
    pos, n = TUNE_HEAD_V2, 0
    for i, v in entries:
        if n >= 15: break
        nb = tune_nbits(mx[i])
        if pos + 6 + nb > TUNE_BITS: continue
        pos = put(pos, i, 6); pos = put(pos, v, nb); n += 1
    if not n: return [0, 0, 0, 0]
    put(8, key & 0xFFFFF, 20); put(28, preset & 127, 7); put(35, n, 4)
    put(0, _check(bits, TUNE_SEED_V2), 8)
    return [sum(bits[31 * k + j] << j for j in range(31)) for k in range(4)]


def tune_migrate_slot(w4, max_of):
    """a slot of layout 1 (or one a migration already converted) as layout 2: a converted record is kept; a layout-1 record of a shipped meter is re-keyed (its entries and preset are kept, those that no longer fit are
    dropped); anything else (no record, a meter that is not shipped, no `max_of`) becomes empty. Whether the preset still exists is checked when the tuning is applied (its anchor must equal the preset's)."""
    if tune_v2_valid(w4): return list(w4)
    r = tune_v1_parse(w4, max_of)
    if r is None or key_of_index(r[0]) is None: return [0, 0, 0, 0]
    return tune_v2_pack(key_of_index(r[0]), r[1], r[2], max_of(r[0]))


def migrate(words, max_of=None):
    """The 32 words a layout-0 or layout-1 register file becomes (layout 2). `max_of(meter index)` -> [largest value of each parameter] or None: needed only to keep the tunings of layout 1 (without it they are dropped)."""
    if is_legacy(words):
        vals = legacy_unpack(words)
        out = pack([0] * N_WORDS, vals)
        out[6] |= min(vals_index_legacy(words), 63) << SCRATCH_SHIFT
        return out
    out = list(words)
    idx, pos, pid = v1_selection(words)
    out[5] = hash31_of_index(idx)
    out[6] = pos | pid << 4 | min(idx, 63) << SCRATCH_SHIFT | MARK6
    out[15] = 0
    for slot in TUNE_SLOT_WORDS:
        for k, v in zip(slot, tune_migrate_slot([words[i] for i in slot], max_of)): out[k] = v
    out[0] = (words[0] & ~(15 << 27) & 0xFFFFFFFF) | LAYOUT_VERSION << 27
    return out


def vals_index_legacy(words):
    v = (words[LEGACY["METER"][0]] >> LEGACY["METER"][1]) & _mask(LEGACY["METER"][2])
    return v if LEGACY["METER"][3] <= v <= LEGACY["METER"][4] else LEGACY["METER"][5]


def migrate_resume(words, max_of=None):
    """The same for a register file a migration half wrote (the firmware writes WRITE_ORDER, word 0 last, so the version is still 0 or 1): word 6 carrying MARK6 is kept and word 5 is rebuilt from the index it
    keeps; (layout 0) a word 1 or 2 with its mark is kept; a tuning slot already in layout 2 is kept."""
    out = migrate(words, max_of)
    if version(words) == 0:
        for k in (1, 2):
            if words[k] & NEW_MARK: out[k] = words[k]
    if words[6] & MARK6:
        out[6] = words[6]
        out[5] = hash31_of_index((words[6] >> SCRATCH_SHIFT) & _mask(SCRATCH_WIDTH))
    return out


def resume_lost_slots(resumed, full):
    """The tuning slots in which a resumed migration differs from the uninterrupted one: a power cut INSIDE a slot's four writes leaves a record that mixes the two layouts, which no reader accepts, so that one tuning is lost
    (empty). (The check byte is 8 bits: about one torn record in 256 passes it; its entries are then validated against the meter when applied, and its key must match one.) [] when the two agree, else the names ("A", "B")
    of the slots that differ; None when they differ anywhere else (a bug)."""
    lost = [name for name, slot in zip("AB", TUNE_SLOT_WORDS) if [resumed[k] for k in slot] != [full[k] for k in slot]]
    inside = {k for slot in TUNE_SLOT_WORDS for k in slot}
    return lost if all(resumed[k] == full[k] for k in range(N_WORDS) if k not in inside) else None


# ---- APF's interact_persist.json <-> words ----
def words_from_persist(doc):
    """doc: the parsed interact_persist.json (or a list of {"id", "val"}); a 32-word list, ids this layout does not know are ignored."""
    vs = doc["interact_persist"]["variables"] if isinstance(doc, dict) else doc
    w = [0] * N_WORDS
    for v in vs:
        if int(v["id"]) in ID_TO_WORD: w[ID_TO_WORD[int(v["id"])]] = int(v["val"]) & 0xFFFFFFFF
    return w


def persist_from_words(words, ids=None):
    """[{"id", "type": "slider_u32", "val"}] for the given ids (default: the new layout's 16)."""
    ids = ids or [10, 11, 12, 15, 16, 20, 21, 22, 23, 27, 28, 29, 30, 31, 32, 33]
    return [{"id": i, "type": "slider_u32", "val": words[ID_TO_WORD[i]]} for i in ids]


def describe(doc):
    w = words_from_persist(doc)
    v = version(w)
    r = {"layout": {0: "legacy", 1: "v1"}.get(v, "new"), "words": {str(k): x for k, x in enumerate(w) if x}}
    if v == 0: r["values"] = legacy_unpack(w)
    elif v == 1:
        idx, pos, pid = v1_selection(w)
        r["values"] = {"METER_INDEX": idx, "METER_PRE_POS": pos, "METER_PRE_ID": pid}      # layout 1: the selected meter by table index (the player of layout 2 maps it through shipped_meter_ids.h)
    else: r["values"] = unpack(w)
    if v in (0, 1): r["migrated_words"] = {str(k): x for k, x in enumerate(migrate(w, fixture_max)) if x}
    return r


# ---- fixtures for the tests and the golden vectors: the parameter tables of the seven shipped meters, as a layout-1 tuning needs them to be read. FIXTURE ONLY: the firmware reads the real ones from its meters ----
def fixture_max(i):
    """[largest value of each parameter] of shipped meter i (invented, deterministic: 4 + 3 i parameters); None for any other index"""
    if not 0 <= i < len(SHIPPED): return None
    r = random.Random(1000 + i)
    return [r.choice([1, 3, 7, 15, 100, 255, 1023, 65535]) for _ in range(4 + 3 * i)]


def tune_v1_pack(meter, preset, entries, mx):
    """the four words of a layout-1 record (what TAU DEV 182 .. 219 stored): for the tests and the vectors"""
    bits = [0] * 128
    def put(pos, v, n):
        for i in range(n): bits[pos + i] = (v >> i) & 1
        return pos + n
    pos, n = TUNE_HEAD_V1, 0
    for i, v in entries:
        if n >= 15: break
        nb = tune_nbits(mx[i])
        if pos + 6 + nb > TUNE_BITS: continue
        pos = put(pos, i, 6); pos = put(pos, v, nb); n += 1
    if not n: return [0, 0, 0, 0]
    put(8, meter & 31, 5); put(13, preset & 127, 7); put(20, n, 4); put(0, _check(bits, TUNE_SEED_V1), 8)
    return [sum(bits[31 * k + j] << j for j in range(31)) for k in range(4)]


def random_v1_words(r):
    """a layout-1 register file: random settings, a random selected meter, presets, and (mostly valid) tunings in the two slots"""
    w = pack([0] * N_WORDS, {f[0]: r.randint(f[4], min(f[5], 1000)) for f in FIELDS if f[3] and f[0] not in ("LAYOUT", "METER_HASH", "METER_PRE_POS", "METER_PRE_ID")})
    w[0] = (w[0] & ~(15 << 27)) | (1 << 27)
    for k in (5, 6, 15): w[k] = 1 << 30 if k != 5 else (1 << 30)
    w[5] |= r.choice([r.randint(0, 6)] * 3 + [r.randint(7, 62)]) ; w[6] = 1 << 30; w[15] = 1 << 30
    for m in range(7):
        for spec, hi in ((V1_MPRE[m], 15), (V1_MPID[m], 127)):
            word, sh, wd, lo, mx_ = spec
            w[word] |= r.randint(0, hi) << sh
    for slot in TUNE_SLOT_WORDS:
        c = r.random()
        if c < 0.15: continue
        m = r.randint(0, 6) if c < 0.85 else r.randint(7, 20)
        mx = fixture_max(m) or [255] * 8
        ent = sorted(r.sample(range(len(mx)), r.randint(1, min(len(mx), 9))))
        rec = tune_v1_pack(m, r.randint(0, 127), [(i, r.randint(0, mx[i])) for i in ent], mx)
        if c > 0.93: rec[r.randint(0, 3)] ^= 1 << r.randint(0, 30)             # a damaged record
        for k, v in zip(slot, rec): w[k] = v
    return w


# ---- golden vectors ----
def _nz(w): return {str(k): v for k, v in enumerate(w) if v}


def golden():
    r = random.Random(20261026)
    cases = []
    def case(name, legacy):
        w = [0] * N_WORDS
        for k, v in legacy.items(): w[k] = v
        cases.append({"name": name, "legacy_words": {str(k): v for k, v in legacy.items() if v},
                      "migrated_words": _nz(migrate(w)), "values": legacy_unpack(w)})
    case("empty card (all zero: volume 0 is a valid legacy volume)", {})
    case("defaults of a fresh legacy card", {0: 94, 1: 1})
    case("a TAU DEV 181 card: volume 73, colour 7, meter 6", {0: 73, 1: 7, 2: 0, 5: 6, 6: 0, 8: 0, 15: 1, 16: 0, 17: 0, 18: 0, 19: 1342182458, 20: 1158122160})
    case("volume 100, repeat all, ReplayGain album, polarity light, Halcyon 30", {0: 100, 2: 1, 6: 30, 17: 0x21})
    case("every legacy field at its maximum", {0: 100, 1: 18, 2: 2, 5: 14, 6: 30, 15: 15, 16: 31, 17: 0x21, 18: (7 | 7 << 4 | 7 << 8 | 15 << 12 | 7 << 16 | 7 << 20 | 7 << 23), 19: 0x7FFFFFFF, 20: 0x7FFFFFFF})
    case("out-of-range legacy values read as defaults", {0: 4000, 1: 99, 2: 7, 5: 40, 6: 99, 16: 77, 17: 0x30})
    case("a legacy tuning is dropped, last played kept", {0: 50, 8: 0x1234567, 9: 0x7654321, 10: 5, 11: 6, 19: 0x500014BA, 20: 0x45079DB0})
    case("a legacy card whose meter index is not a shipped meter (14): selects nothing", {0: 60, 5: 14, 18: 0x123})
    for i in range(30):
        legacy = {0: r.randint(0, 100), 1: r.randint(0, 18), 2: r.randint(0, 2), 5: r.randint(0, 14), 6: r.randint(0, 30), 15: r.randint(0, 15), 16: r.randint(0, 31),
                  17: r.randint(0, 1) | (r.randint(0, 2) << 4), 18: r.getrandbits(27), 19: r.getrandbits(31), 20: r.getrandbits(31)}
        case("random legacy card %d" % i, legacy)
    resume = []
    for name_i, legacy in ((0, {0: 73, 1: 7, 5: 6, 6: 3, 15: 1, 16: 2, 17: 0x11, 18: 0x2A5A5A5, 19: 1342182458, 20: 1158122160}), (1, {0: 100, 1: 18, 2: 2, 5: 3, 6: 30, 17: 0x21, 18: 0x7FFFFFF})):
        w = [0] * N_WORDS
        for k, v in legacy.items(): w[k] = v
        full = migrate(w)
        for k in range(len(WRITE_ORDER) + 1):          # the first k writes of the migration happened, word 0 (the version) not yet
            half = list(w)
            for word in WRITE_ORDER[:k]: half[word] = full[word]
            resume.append({"legacy_words": _nz(w), "writes_done": k, "half_words": _nz(half), "resumed_words": _nz(migrate_resume(half)), "equals_full_migration": migrate_resume(half) == full,
                           "lost_tuning_slots": resume_lost_slots(migrate_resume(half), full)})
    # ---- layout 1 -> 2 (TAU DEV 182 .. 219): the selected meter index becomes the id hash, tunings are re-keyed ----
    cases_v1 = []
    def case_v1(name, w):
        idx, pos, pid = v1_selection(w)
        cases_v1.append({"name": name, "v1_words": _nz(w), "migrated_words": _nz(migrate(w, fixture_max)), "selected": {"meter_index": idx, "preset_position": pos, "preset_id_plus_1": pid,
                         "id_hash31": hash31_of_index(idx)}, "tuning_slots_kept": [tune_v2_valid([migrate(w, fixture_max)[i] for i in sl]) for sl in TUNE_SLOT_WORDS]})
    base = pack([0] * N_WORDS, {"VOLUME": 73, "COLOUR": 7, "LAST0": 1342182458, "LAST1": 1158122160})
    base[0] = (base[0] & ~(15 << 27)) | (1 << 27)
    for k in (5, 6, 15): base[k] = 1 << 30
    def sel(meter, pos=0, pid=0):                      # base + the selected meter and the preset of that meter (position, file id + 1)
        w = list(base); w[5] |= meter
        if meter < 7:
            word, sh, wd, lo, hi = V1_MPRE[meter]; w[word] |= pos << sh
            word, sh, wd, lo, hi = V1_MPID[meter]; w[word] |= pid << sh
        return w
    case_v1("meter 6 selected, nothing tuned, no preset (the owner's DEV 185 words 5 and 6 read this way)", sel(6))
    case_v1("meter 0 selected with preset position 3", sel(0, 3))
    case_v1("meter 3 selected with preset position 9 and preset file id 20 (stored 21)", sel(3, 9, 21))
    case_v1("meter 6 selected with preset position 12 and preset file id 126 (stored 127)", sel(6, 12, 127))
    mx = fixture_max(2); w = list(base); w[5] |= 2
    for k, v in zip(TUNE_SLOT_WORDS[0], tune_v1_pack(2, 3, [(0, 1), (2, mx[2] // 2), (5, mx[5])], mx)): w[k] = v
    case_v1("meter 2 selected, tuned (slot A holds a layout-1 tuning of meter 2)", w)
    mx6 = fixture_max(6); w = list(base); w[5] |= 6
    for k, v in zip(TUNE_SLOT_WORDS[1], tune_v1_pack(6, 0x25, [(i, min(mx6[i], 7)) for i in range(0, 18, 2)], mx6)): w[k] = v
    case_v1("meter 6 selected, a tuning with nine parameters in slot B (some may not fit the shorter header)", w)
    w = list(base); w[5] |= 11
    for k, v in zip(TUNE_SLOT_WORDS[0], tune_v1_pack(11, 0, [(0, 1)], [255] * 4)): w[k] = v
    case_v1("selected index 11 is not a shipped meter: selects nothing, its tuning is dropped", w)
    for i in range(24): case_v1("random layout-1 card %d" % i, random_v1_words(r))
    resume_v1 = []
    for ci in (0, 1, 2):
        w = random_v1_words(random.Random(77 + ci))
        for k in TUNE_SLOT_WORDS[0]: w[k] = tune_v1_pack(1 + ci, 5, [(0, 1), (1, 1)], fixture_max(1 + ci))[TUNE_SLOT_WORDS[0].index(k)]
        for k in TUNE_SLOT_WORDS[1]: w[k] = tune_v1_pack(3 + ci, 9, [(2, 1), (3, 1)], fixture_max(3 + ci))[TUNE_SLOT_WORDS[1].index(k)]
        full = migrate(w, fixture_max)
        for k in range(len(WRITE_ORDER) + 1):
            half = list(w)
            for word in WRITE_ORDER[:k]: half[word] = full[word]
            resume_v1.append({"v1_words": _nz(w), "writes_done": k, "half_words": _nz(half), "resumed_words": _nz(migrate_resume(half, fixture_max)), "equals_full_migration": migrate_resume(half, fixture_max) == full,
                              "lost_tuning_slots": resume_lost_slots(migrate_resume(half, fixture_max), full)})
    return {"about": "Golden vectors for tools/persist_layout.py (persist_layout 2): `cases` = a LAYOUT-0 register file (TAU DEV 181 and older; words 0..31, only the non-zero ones listed) and the layout-2 register file the migration writes, with the values "
                     "it held; `resume` = that migration interrupted after k writes (WRITE_ORDER, word 0 last) and what the next boot makes of it; `cases_v1` and `resume_v1` = the same for a LAYOUT-1 register file (TAU DEV 182 .. 219, v0.7.0, v0.7.1), "
                     "(a power cut INSIDE a tuning slot's four writes loses that one tuning: `lost_tuning_slots`); whose selected meter index becomes the id hash of fw2/cores/shipped_meter_ids.h (PLACEHOLDER ids until P2.0) and whose tunings are re-keyed using `tune_max_fixture` (the largest value of each parameter of each shipped "
                     "meter; a FIXTURE: the player reads the real ones). Generated by `python3 tools/persist_layout.py --golden`; checked against the firmware by sim/test_persist_layout.py.",
            "layout_version": LAYOUT_VERSION, "fields": [dict(zip(("name", "word", "shift", "width", "min", "max", "default"), f)) for f in FIELDS],
            "id_to_word": {str(i): w for i, w in sorted(ID_TO_WORD.items())}, "write_order": list(WRITE_ORDER), "marks": [list(m) for m in MARKS], "scratch": {"word": 6, "shift": SCRATCH_SHIFT, "width": SCRATCH_WIDTH},
            "shipped_meters": [{"index": i, "id_hash": h, "params_rev": rv, "id_hash31": h & HASH31, "tune_key": tune_key(h, rv)} for i, h, rv in SHIPPED],
            "tune_max_fixture": {str(i): fixture_max(i) for i in range(len(SHIPPED))},
            "cases": cases, "resume": resume, "cases_v1": cases_v1, "resume_v1": resume_v1}


# ---- staged captures: real files of the owner's card, and what the migration must make of them (for Omega's tests and for the Pocket test of the migration, P2.7) ----
STAGED = "docs/research/persist_layout/staged"
CAPTURES = {"dev181_layout0": "dev181_interact_persist.json", "dev185_layout1": "dev185_after_migration_interact_persist.json"}


def _persist_doc(words):
    return {"interact_persist": {"magic": "APF_VER_1", "variables": persist_from_words(words)}}


def staged_files(root):
    """{file name: text} of the staged captures: the real input (copied byte for byte), the layout-2 file the migration must produce, and the same input as a migration interrupted after k writes leaves it"""
    root = Path(root); out = {}
    for tag, cap in CAPTURES.items():
        text = (root / "docs/research/persist_layout/captures" / cap).read_text()
        w = words_from_persist(json.loads(text)); full = migrate(w, fixture_max)
        out["input_%s_interact_persist.json" % tag] = text
        out["expected_layout2_from_%s_interact_persist.json" % tag] = json.dumps(_persist_doc(full), indent=2) + "\n"
        for k in (3, 4, 10):                                  # 3 = words 1, 2, 6 written (word 5 not yet); 4 = word 5 too; 10 = through the tuning slots, only the version (word 0) missing
            half = list(w)
            for word in WRITE_ORDER[:k]: half[word] = full[word]
            out["interrupted_%s_after_%02d_writes_interact_persist.json" % (tag, k)] = json.dumps(_persist_doc(half), indent=2) + "\n"
    return out


def sha256(text):
    import hashlib
    return hashlib.sha256(text.encode()).hexdigest()


def stage(root):
    root = Path(root); d = root / STAGED; d.mkdir(parents=True, exist_ok=True)
    files = staged_files(root)
    for n, t in files.items(): (d / n).write_text(t)
    return files


def verify_staged(root):
    """problems (empty = ok): a staged file missing or different from a fresh run, an extra file, or a SHA-256 in the README that does not match"""
    root = Path(root); d = root / STAGED; bad = []
    files = staged_files(root)
    for n, t in files.items():
        if not (d / n).exists(): bad.append("missing " + n)
        elif (d / n).read_text() != t: bad.append("differs " + n)
    for f in d.glob("*.json") if d.exists() else []:
        if f.name not in files: bad.append("extra " + f.name)
    rd = d / "README.md"
    txt = rd.read_text() if rd.exists() else ""
    for n, t in files.items():
        if sha256(t) not in txt or n not in txt: bad.append("README lacks the SHA-256 of " + n)
    return bad


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--stage":                  # write the staged captures under ROOT (a repository root)
        files = stage(a[1]); [print(sha256(t), n) for n, t in files.items()]; return 0
    if len(a) == 2 and a[0] == "--golden":
        Path(a[1]).parent.mkdir(parents=True, exist_ok=True)
        Path(a[1]).write_text(json.dumps(golden(), indent=1) + "\n"); print("wrote", a[1]); return 0
    if len(a) == 1:
        print(json.dumps(describe(json.loads(Path(a[0]).read_text())), indent=1)); return 0
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main())
