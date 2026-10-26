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

  python3 tools/persist_layout.py FILE.json          decode a persist file: its layout, every named value, and (legacy) what the migration would write
  python3 tools/persist_layout.py --golden OUT.json  write the golden vectors
"""
import json, random, sys
from pathlib import Path

NEW_MARK = 1 << 30                      # bit 30 of words 1, 2, 5, 6, 15 marks a word already converted (no field reaches it)
MARKED_WORDS = (1, 2, 5, 6, 15)
WRITE_ORDER = (1, 2, 5, 6, 15, 16, 17, 18, 21, 8, 9, 10, 11, 19, 20)       # then word 0 last: a legacy word is overwritten only after every new word built from it
LAYOUT_VERSION = 1
ID_TO_WORD = {10: 0, 11: 1, 12: 2, 15: 5, 16: 6, 20: 8, 21: 9, 22: 10, 23: 11, 24: 12, 25: 13, 26: 14, 27: 16, 28: 17, 29: 18, 30: 15, 31: 19, 32: 20, 33: 21}
WORD_TO_ID = {w: i for i, w in ID_TO_WORD.items()}
N_WORDS = 32


def _fields():
    """(name, word, shift, width, min, max, default) in the firmware's order (sf_table)."""
    t = [("VOLUME", 2, 0, 7, 0, 100, 94), ("REPEAT", 2, 7, 2, 0, 2, 0), ("RGMODE", 2, 9, 2, 0, 2, 0), ("HALCYON", 2, 11, 5, 0, 30, 0),
         ("COLOUR", 1, 0, 5, 0, 18, 1), ("THEME", 1, 5, 6, 0, 31, 0), ("POLARITY", 1, 11, 1, 0, 1, 0), ("METER", 5, 0, 6, 0, 62, 0), ("FONT", 1, 12, 3, 0, 7, 0),
         ("LAYOUT", 0, 27, 4, 0, 15, LAYOUT_VERSION), ("TUNE_NEW", 0, 26, 1, 0, 1, 0)]
    for m in range(20):                  # the selected preset of meter m, 4 bits: word 5 (meters 0..5), word 6 (6..12), word 15 (13..19)
        w, s = (5, 6 + 4 * m) if m < 6 else (6, 4 * (m - 6)) if m < 13 else (15, 4 * (m - 13))
        t.append(("MPRE%d" % m, w, s, 4, 0, 15, 0))
    for k, w in enumerate((8, 9, 10, 11, 16, 17, 18, 21)):      # two tuning slots of four words (slot A ids 20-23, slot B ids 27-29 and 33)
        t.append(("TUNE%d" % k, w, 0, 31, 0, 0x7FFFFFFF, 0))
    t += [("LAST0", 19, 0, 31, 0, 0x7FFFFFFF, 0), ("LAST1", 20, 0, 31, 0, 0x7FFFFFFF, 0)]       # the last played track (ids 31, 32: the same in both layouts)
    return t


FIELDS = _fields()
NAMES = [f[0] for f in FIELDS]
# the legacy layout (v0.6.1 .. TAU DEV 181): name -> (word, shift, width, min, max, default); a width of 32 is a whole word
LEGACY = {"VOLUME": (0, 0, 32, 0, 100, 94), "REPEAT": (2, 0, 32, 0, 2, 0), "RGMODE": (17, 4, 2, 0, 2, 0), "HALCYON": (6, 0, 32, 0, 30, 0), "COLOUR": (1, 0, 32, 0, 18, 1),
          "THEME": (16, 0, 32, 0, 31, 0), "POLARITY": (17, 0, 1, 0, 1, 0), "METER": (5, 0, 32, 0, 14, 0), "MPRE0": (18, 0, 4, 0, 7, 0), "MPRE1": (18, 4, 4, 0, 7, 0),
          "MPRE2": (18, 8, 4, 0, 7, 0), "MPRE3": (18, 12, 4, 0, 15, 0), "MPRE4": (18, 16, 4, 0, 7, 0), "FONT": (18, 20, 3, 0, 7, 0), "MPRE5": (18, 23, 4, 0, 7, 0),
          "MPRE6": (15, 0, 4, 0, 15, 0), "LAST0": (19, 0, 31, 0, 0x7FFFFFFF, 0), "LAST1": (20, 0, 31, 0, 0x7FFFFFFF, 0)}


def _mask(width): return 0xFFFFFFFF if width >= 32 else (1 << width) - 1


def unpack(words):
    """The new layout: name -> value (a field out of range reads as its default, like the firmware)."""
    out = {}
    for name, word, sh, wd, lo, hi, df in FIELDS:
        v = (words[word] >> sh) & _mask(wd)
        out[name] = v if lo <= v <= hi else df
    return out


def pack(words, values):
    """Merge values (name -> value, clamped to the field's range) into a copy of words, keeping every other bit; the converted mark is set on words 1, 2, 5, 6, 15."""
    w = list(words)
    for name, word, sh, wd, lo, hi, df in FIELDS:
        v = max(lo, min(hi, values.get(name, df)))
        m = _mask(wd) << sh
        w[word] = (w[word] & ~m & 0xFFFFFFFF) | ((v << sh) & m)
    for k in MARKED_WORDS: w[k] |= NEW_MARK
    return w


def is_legacy(words):
    """Layout version 0 (bits 30..27 of word 0): a legacy volume 0..100 (or an empty word)."""
    return (words[0] >> 27) & 15 == 0


def legacy_unpack(words):
    """name -> value as a legacy register file holds it; fields the legacy layout did not have take their default (the old single tuning is not carried over)."""
    vals = {f[0]: f[6] for f in FIELDS}
    for name, (word, sh, wd, lo, hi, df) in LEGACY.items():
        v = (words[word] >> sh) & _mask(wd)
        vals[name] = v if lo <= v <= hi else df
    return vals


def migrate(words):
    """The 32 words a legacy register file becomes: every word zero, every field packed from the legacy values."""
    return pack([0] * N_WORDS, legacy_unpack(words))


def migrate_resume(words):
    """The same for a register file a migration half wrote (the firmware writes WRITE_ORDER, word 0 last): a marked word is kept, the others are built from the legacy words still there."""
    out = migrate(words)
    for k in MARKED_WORDS:
        if words[k] & NEW_MARK: out[k] = words[k]
    return out


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
    legacy = is_legacy(w)
    r = {"layout": "legacy" if legacy else "new", "words": {str(k): v for k, v in enumerate(w) if v}}
    r["values"] = legacy_unpack(w) if legacy else unpack(w)
    if legacy: r["migrated_words"] = {str(k): v for k, v in enumerate(migrate(w)) if v}
    return r


# ---- golden vectors ----
def golden():
    r = random.Random(20261026)
    cases = []
    def case(name, legacy):
        w = [0] * N_WORDS
        for k, v in legacy.items(): w[k] = v
        cases.append({"name": name, "legacy_words": {str(k): v for k, v in legacy.items() if v},
                      "migrated_words": {str(k): v for k, v in enumerate(migrate(w)) if v}, "values": legacy_unpack(w)})
    case("empty card (all zero: volume 0 is a valid legacy volume)", {})
    case("defaults of a fresh legacy card", {0: 94, 1: 1})
    case("a TAU DEV 181 card: volume 73, colour 7, meter 6", {0: 73, 1: 7, 2: 0, 5: 6, 6: 0, 8: 0, 15: 1, 16: 0, 17: 0, 18: 0, 19: 1342182458, 20: 1158122160})
    case("volume 100, repeat all, ReplayGain album, polarity light, Halcyon 30", {0: 100, 2: 1, 6: 30, 17: 0x21})
    case("every legacy field at its maximum", {0: 100, 1: 18, 2: 2, 5: 14, 6: 30, 15: 15, 16: 31, 17: 0x21, 18: (7 | 7 << 4 | 7 << 8 | 15 << 12 | 7 << 16 | 7 << 20 | 7 << 23), 19: 0x7FFFFFFF, 20: 0x7FFFFFFF})
    case("out-of-range legacy values read as defaults", {0: 4000, 1: 99, 2: 7, 5: 40, 6: 99, 16: 77, 17: 0x30})
    case("a legacy tuning is dropped, last played kept", {0: 50, 8: 0x1234567, 9: 0x7654321, 10: 5, 11: 6, 19: 0x500014BA, 20: 0x45079DB0})
    for i in range(30):
        legacy = {0: r.randint(0, 100), 1: r.randint(0, 18), 2: r.randint(0, 2), 5: r.randint(0, 14), 6: r.randint(0, 30), 15: r.randint(0, 15), 16: r.randint(0, 31),
                  17: r.randint(0, 1) | (r.randint(0, 2) << 4), 18: r.getrandbits(27), 19: r.getrandbits(31), 20: r.getrandbits(31)}
        case("random legacy card %d" % i, legacy)
    resume = []
    for name_i, legacy in ((0, {0: 73, 1: 7, 5: 6, 6: 3, 15: 1, 16: 2, 17: 0x11, 18: 0x2A5A5A5, 19: 1342182458, 20: 1158122160}), (1, {0: 100, 1: 18, 2: 2, 6: 30, 17: 0x21, 18: 0x7FFFFFF}) ):
        w = [0] * N_WORDS
        for k, v in legacy.items(): w[k] = v
        full = migrate(w)
        for k in range(len(WRITE_ORDER) + 1):          # the first k writes of the migration happened, word 0 (the version) not yet
            half = list(w)
            for word in WRITE_ORDER[:k]: half[word] = full[word]
            resume.append({"legacy_words": {str(i): v for i, v in enumerate(w) if v}, "writes_done": k, "half_words": {str(i): v for i, v in enumerate(half) if v},
                           "resumed_words": {str(i): v for i, v in enumerate(migrate_resume(half)) if v}, "equals_full_migration": migrate_resume(half) == full})
    return {"about": "Golden vectors for tools/persist_layout.py: a legacy register file (words 0..31, only the non-zero ones listed) and the register file the migration writes, with the values it holds; `resume` shows a migration "
                     "interrupted after k writes (tools/persist_layout.py WRITE_ORDER, word 0 last) and what the next boot makes of it. Generated by `python3 tools/persist_layout.py --golden`; checked against the firmware by sim/test_persist_layout.py.",
            "layout_version": LAYOUT_VERSION, "fields": [dict(zip(("name", "word", "shift", "width", "min", "max", "default"), f)) for f in FIELDS],
            "id_to_word": {str(i): w for i, w in sorted(ID_TO_WORD.items())}, "write_order": list(WRITE_ORDER), "marked_words": list(MARKED_WORDS), "cases": cases, "resume": resume}


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "--golden":
        Path(a[1]).parent.mkdir(parents=True, exist_ok=True)
        Path(a[1]).write_text(json.dumps(golden(), indent=1) + "\n"); print("wrote", a[1]); return 0
    if len(a) == 1:
        print(json.dumps(describe(json.loads(Path(a[0]).read_text())), indent=1)); return 0
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main())
