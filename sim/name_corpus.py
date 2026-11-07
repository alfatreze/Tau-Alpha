"""Shared name corpus (docs/features/NAME_SIDECAR_SPEC.md section 8). Each case is a file STEM plus tags, in a folder of its own category.
  id    unique short id          cat   category            name  the original file stem (an '.mp3' is added)
  tags  original title/artist/album text (None = no tag)   keep  the characters of the stem the card name must still contain
  gone  True when the stem has nothing the Pocket can open (the card name then comes from the libraries or becomes 'track')
Pinned card names live in the test (they depend on the optional libraries); invariants hold for every case."""

N = lambda *cp: "".join(chr(c) for c in cp)

CASES = [
    # --- plain
    dict(id="ascii", cat="plain", name="01 Plain Song", tags=("Plain Song", "An Artist", "An Album")),
    dict(id="digits", cat="plain", name="10 - 2 Pac (1996) [Remix]", tags=("2 Pac", "Pac", "All Eyez")),
    dict(id="dots", cat="plain", name="v1.2.3 final.mix", tags=None),
    # --- accents the Pocket can open (code page 437)
    dict(id="nfc_cafe", cat="cp437", name="Caf" + N(0xE9), tags=("Caf" + N(0xE9), N(0xC9) + "dith Piaf", None), keep="Caf" + N(0xE9)),
    dict(id="nfd_cafe", cat="cp437", name="Cafe" + N(0x301), tags=None, keep="Caf" + N(0xE9)),
    dict(id="umlauts", cat="cp437", name=N(0xF6, 0xFC, 0xE4, 0xEB, 0xEF) + " Stra" + N(0xDF) + "e", tags=None, keep=N(0xF6, 0xFC, 0xE4, 0xEB, 0xEF) + " Stra" + N(0xDF)),
    dict(id="ntilde", cat="cp437", name="A" + N(0xF1) + "o Nuevo " + N(0xA1) + "Hola!", tags=None, keep="A" + N(0xF1) + "o"),
    dict(id="ae_pi_cent", cat="cp437", name="S" + N(0xE6) + "m Pi " + N(0x3C0) + " Cost " + N(0xA2), tags=None, keep="S" + N(0xE6) + "m Pi " + N(0x3C0)),
    dict(id="nordic_lower", cat="cp437", name="Bj" + N(0xF6) + "rk " + N(0xE5) + "ngstr" + N(0xF6) + "m", tags=None, keep="Bj" + N(0xF6) + "rk"),
    # --- Latin letters code page 437 lacks
    dict(id="a_acute", cat="latin_other", name=N(0xC1) + "lvaro Fern" + N(0xE1) + "ndez", tags=(N(0xC1) + "lvaro", None, None)),
    dict(id="o_slash", cat="latin_other", name="S" + N(0xF8) + "ren " + N(0xD8) + "rsted", tags=None),
    dict(id="c_caron", cat="latin_other", name=N(0x10C) + "apek " + N(0x160) + "kroup", tags=None),
    dict(id="polish", cat="latin_other", name=N(0x141) + N(0xF3) + "d" + N(0x17A) + " " + N(0x17C) + N(0xF3) + N(0x142) + "ty", tags=None),
    dict(id="turkish", cat="latin_other", name=N(0x131) + N(0x15F) + N(0x131) + "k " + N(0x130) + "stanbul " + N(0x11F), tags=None),
    dict(id="ligatures", cat="latin_other", name=N(0x152) + "uvre " + N(0xC6) + "ther " + N(0x1E9E), tags=None),
    dict(id="icelandic", cat="latin_other", name=N(0xDE) + "orsteinn " + N(0xD0) + "ei", tags=None),
    # --- other scripts
    dict(id="greek", cat="script", name=N(0x395, 0x3BB, 0x3BB, 0x3AC, 0x3B4, 0x3B1), tags=(N(0x395, 0x3BB, 0x3BB, 0x3AC, 0x3B4, 0x3B1), N(0x39C, 0x3B9, 0x3BA, 0x3AC, 0x3BB, 0x3B7, 0x3C2), None), gone=True),
    dict(id="cyrillic", cat="script", name=N(0x41F, 0x440, 0x438, 0x432, 0x435, 0x442) + " " + N(0x43C, 0x438, 0x440), tags=(N(0x41F, 0x440, 0x438, 0x432, 0x435, 0x442), N(0x426, 0x43E, 0x439), None), gone=True),
    dict(id="chinese_s", cat="script", name=N(0x4E1C, 0x4EAC) + " " + N(0x591C, 0x66F2), tags=(N(0x591C, 0x66F2), None, None), gone=True),
    dict(id="chinese_t", cat="script", name=N(0x7E41, 0x9AD4, 0x4E2D, 0x6587), tags=None, gone=True),
    dict(id="japanese_kana", cat="script", name=N(0x3053, 0x3093, 0x306B, 0x3061, 0x306F) + " " + N(0x30AB, 0x30BF, 0x30AB, 0x30CA), tags=(N(0x3053, 0x3093, 0x306B, 0x3061, 0x306F), N(0x5BAE, 0x672C), None), gone=True),
    dict(id="japanese_mixed", cat="script", name=N(0x6771, 0x4EAC, 0x30BF, 0x30EF, 0x30FC), tags=None, gone=True),
    dict(id="japanese_kanji_only", cat="script", name=N(0x521D, 0x604B), tags=None, gone=True),
    dict(id="korean", cat="script", name=N(0xD55C, 0xAD6D, 0xC5B4) + " " + N(0xB178, 0xB798), tags=(N(0xB178, 0xB798), None, None), gone=True),
    dict(id="arabic", cat="script", name=N(0x645, 0x631, 0x62D, 0x628, 0x627), tags=None, gone=True),
    dict(id="hebrew", cat="script", name=N(0x5E9, 0x5DC, 0x5D5, 0x5DD), tags=None, gone=True),
    dict(id="thai", cat="script", name=N(0xE2A, 0xE27, 0xE31, 0xE2A, 0xE14, 0xE35), tags=None, gone=True),
    dict(id="devanagari", cat="script", name=N(0x928, 0x92E, 0x938, 0x94D, 0x924, 0x947), tags=None, gone=True),
    dict(id="mixed_latin_cyr", cat="script", name="Caf" + N(0xE9) + " " + N(0x41F, 0x440, 0x438, 0x432, 0x435, 0x442) + " 2", tags=None, keep="Caf" + N(0xE9)),
    dict(id="mixed_ascii_cjk", cat="script", name="Tokyo " + N(0x6771, 0x4EAC) + " Night", tags=None, keep="Tokyo"),
    # --- emoji and symbols
    dict(id="emoji_only", cat="emoji", name=N(0x1F600), tags=None, gone=True),
    dict(id="emoji_in_text", cat="emoji", name="Party " + N(0x1F389) + " Time", tags=None, keep="Party"),
    dict(id="emoji_zwj", cat="emoji", name="Family " + N(0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467), tags=None, keep="Family"),
    dict(id="emoji_flag", cat="emoji", name="Flag " + N(0x1F1EF, 0x1F1F5), tags=None, keep="Flag"),
    dict(id="emoji_vs", cat="emoji", name="Heart " + N(0x2764, 0xFE0F), tags=None, keep="Heart"),
    dict(id="music_punct", cat="symbols", name=N(0x2018) + "Quote" + N(0x2019) + " " + N(0x201C) + "double" + N(0x201D) + " " + N(0x2013) + " dash " + N(0x2026) + " " + N(0x266A, 0x266B), tags=None, keep="Quote"),
    dict(id="fullwidth", cat="symbols", name=N(0xFF21, 0xFF22, 0xFF23) + " " + N(0xFF11, 0xFF12, 0xFF13), tags=None),
    dict(id="combining_stack", cat="symbols", name="Z" + N(0x351, 0x35B, 0x346) + "algo", tags=None, keep="lgo"),
    dict(id="superscript", cat="symbols", name="x" + N(0xB2) + " " + N(0xBD) + " " + N(0x2122), tags=None),
    # --- characters a file system or the Pocket may not accept
    dict(id="forbidden", cat="edge", name='A<B>C:D"E|F?G*H', tags=None),
    dict(id="backslash", cat="edge", name="AC\\DC Back", tags=None),
    dict(id="lead_trail_space", cat="edge", name="  padded  ", tags=None),
    dict(id="trailing_dot", cat="edge", name="ends with dot.", tags=None),
    dict(id="only_dots", cat="edge", name="...", tags=None),
    dict(id="reserved_con", cat="edge", name="CON", tags=None),
    dict(id="reserved_nul", cat="edge", name="nul", tags=None),
    dict(id="control_chars", cat="edge", name="tab\there", tags=("tab\there", "line\nbreak", "nul\x00byte")),
    dict(id="hash_percent", cat="edge", name="100% #1 hit & more", tags=None),
    dict(id="dotfile", cat="edge", name=".hidden", tags=None),
    # --- collisions
    dict(id="case_a", cat="collide", name="Caf" + N(0xE9) + " Del Mar", tags=None, group="case"),
    dict(id="case_b", cat="collide", name="CAF" + N(0xC9) + " DEL MAR", tags=None, group="case"),
    dict(id="norm_a", cat="collide", name="Na" + N(0xEF) + "ve", tags=None, group="norm"),
    dict(id="norm_b", cat="collide", name="Nai" + N(0x308) + "ve", tags=None, group="norm", same_as="norm_a"),
    dict(id="fold_a", cat="collide", name=N(0xC1) + "lbum", tags=None, group="fold"),
    dict(id="fold_b", cat="collide", name="Album", tags=None, group="fold"),
    dict(id="fold_c", cat="collide", name=N(0x410) + "lbum", tags=None, group="fold"),
    dict(id="emptyfold_a", cat="collide", name=N(0x1F600), tags=None, group="empty"),
    dict(id="emptyfold_b", cat="collide", name=N(0x1F601), tags=None, group="empty"),
    dict(id="emptyfold_c", cat="collide", name=N(0x1F602), tags=None, group="empty"),
    # --- length
    dict(id="long_ascii", cat="long", name=("Long name " * 30).strip()[:230], tags=None),
    dict(id="long_cjk", cat="long", name=N(0x6771, 0x4EAC) * 80, tags=None, gone=True),
    dict(id="long_cp437", cat="long", name=(N(0xE9) + "t" + N(0xE9) + " ") * 60, tags=None),
]

TAG_ONLY_UNICODE = [c["id"] for c in CASES if c.get("tags") and any(t and any(ord(x) > 0x7E for x in t) for t in c["tags"])]
