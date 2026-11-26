#!/usr/bin/env python3
"""Talos engine v2: executable reference model (G1), written from the behavioural contract alone.

Source: docs/TALOS_ENGINE_V2_CONTRACT.md (v0.1, draft for review X1). Every rule implemented below is tagged with
its contract identifier (C-...). Where the contract is silent or ambiguous the chosen reading is tagged GAP-nn and
explained in GAPS.md (folder root). Nothing outside this folder was read to write this file (clean room, D-T07/D-P01).

Level of abstraction (see REPORT.md): a TRANSACTION-level model. It is exact for memory contents, line-buffer
contents and the software-visible counters; it is not cycle accurate. The engine executes queued commands only at
explicit advance points of the action stream ("wait_idle", "fence", "step", the implicit drain at the end), so a
status read between pushes sees the slowest legal engine (C-IDLE-1: a push is visible as busy until it retires).

Public entry point:
    run(params, init_image, command_stream, memory=None, log_values=True)
        -> (memory, write_log, trace)
  memory     : Memory object (memory.a is the 2^25-word uint16 image, word address = index)
  write_log  : list of (cmd_index, op, addresses np.ndarray, values np.ndarray) in write order
  trace      : list of dicts (pushes, drops, retirements, register reads, fills, vblanks, resets)

MIT licence (Tau Alpha). Deterministic; no randomness inside the model.
"""
from collections import deque
import zlib

import numpy as np

MODEL_VERSION = "g1-talos-v2-model-0.1.0 (contract v0.1 2026-11-26)"

# ---- units (contract "Units", C-MAP-1) -------------------------------------------------------------------------------
MEM_WORDS = 1 << 25                 # 25-bit word address: 64 MiB as 32 Mi 16-bit words
MASK25 = MEM_WORDS - 1
MASK19 = (1 << 19) - 1              # frame-offset commands address modulo 2^19 (3.0 "Address classes")
PITCH = 512                         # C-MAP-1 PINNED
DBUF_OFFSET = 1_048_576             # C-DBUF-1: buffer 1 sits 2^20 words above buffer 0
DBUF_LIMIT = 184_320                # C-DBUF-1: rows < 360 (360 * 512)
QUEUE_SLOTS = 255                   # C-Q-2 PINNED
OVF_MAX = 65_535                    # C-Q-3 PINNED (saturating)
FONT_RAM_WORDS = 6_112              # 2.2.2 FONT_CTRL [15:0], C-FONT-1 (191 glyphs x 32 words)
FONT_ROM_WORDS = 3_040              # 2.5 font ROM (95 glyphs x 32 words)

# ---- data: text weight tables (C-CHAR-4, section 3.4; method: text_weights/derive_text_weights.py) ---------------
# Derived by minimising the squared CIE L* error between the linear-light ideal mix (a = c/15) and the engine's
# RGB565 result over the theme text-on-ground pairs; ends fixed (0, 16), never decreasing
# (text_weights/TEXT_WEIGHTS_REDO_2026-11-25.md). The model takes the 16 numbers as data; it does not re-run the fit
# (the themes are not in this package, GAP-24).
TEXT_W_DARK_DERIVED = (0, 4, 6, 7, 8, 9, 10, 11, 12, 12, 13, 14, 14, 15, 15, 16)      # v0.1 default (C-CHAR-4 CHANGE)
TEXT_W_DARK_SHIPPED = (0, 4, 6, 7, 8, 10, 10, 11, 12, 13, 13, 14, 15, 15, 16, 16)     # "current (inherited)", for the IMP-TEXT-DARK class
TEXT_W_LIGHT_IN_USE = (0, 1, 1, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 16)            # v0.1 default (Q2 open)
TEXT_W_LIGHT_DERIVED = (0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 8, 9, 11, 13, 16)            # Q2 alternative

# ---- build parameters (section 1.3) ---------------------------------------------------------------------------------
DEFAULT_PARAMS = {
    "BLIT_BLEND_ENABLE": 0, "DBUF_ENABLE": 0, "FONT_RAM_ENABLE": 0, "FILL_WORDS": 400, "CBLIT_BURST": 1,
    "FEAT_CORNERS": 0, "FEAT_DSBLIT": 0, "FEAT_ESC": 0, "VIEW_ENABLE": 0, "VIEW_DBL_EN": 0, "VIEW_LINE_WORDS": 1200,
    "TEXT_W_DARK": TEXT_W_DARK_DERIVED, "TEXT_W_LIGHT": TEXT_W_LIGHT_IN_USE,
    # SoC-side facts the engine contract refers to (not engine parameters; GAP-03, GAP-04):
    "SOC_VID40": None,          # TAU_VID40 present; None = follow VIEW_ENABLE
    "SOC_DECODE_BITS": None,    # MMIO offset bits decoded; None = 10 with VIEW_ENABLE, else 9 (2.2, C-IF-5)
}
# shipped values of the three base parameters as tau_gfx passes them (1.3 "shipped: 1"); GAP-02: the named-set table
# gives only the feature columns, so every named set takes the shipped base values here.
SHIPPED_BASE = {"BLIT_BLEND_ENABLE": 1, "DBUF_ENABLE": 1, "FONT_RAM_ENABLE": 1}
NAMED_SETS = {                                          # 1.4 (D-T22)
    "TAU": dict(SHIPPED_BASE, FEAT_CORNERS=0, FEAT_DSBLIT=0, FEAT_ESC=0, VIEW_ENABLE=0, VIEW_DBL_EN=0),
    "V": dict(SHIPPED_BASE, FEAT_CORNERS=0, FEAT_DSBLIT=0, FEAT_ESC=0, VIEW_ENABLE=0, VIEW_DBL_EN=0),
    "GALLERY": dict(SHIPPED_BASE, FEAT_CORNERS=1, FEAT_DSBLIT=1, FEAT_ESC=0, VIEW_ENABLE=1, VIEW_DBL_EN=1),
}
# conformance plan section 6: the eight simulation configurations (base parameters at shipped values, GAP-02)
MATRIX = {
    "ALL_OFF": dict(SHIPPED_BASE),
    "CORNERS": dict(SHIPPED_BASE, FEAT_CORNERS=1),
    "DSBLIT": dict(SHIPPED_BASE, FEAT_DSBLIT=1),
    "ESC": dict(SHIPPED_BASE, FEAT_ESC=1),
    "VIEW": dict(SHIPPED_BASE, VIEW_ENABLE=1),
    "VIEW_DBL": dict(SHIPPED_BASE, VIEW_ENABLE=1, VIEW_DBL_EN=1),
    "GALLERY": NAMED_SETS["GALLERY"],
    "CORNERS_DSBLIT": dict(SHIPPED_BASE, FEAT_CORNERS=1, FEAT_DSBLIT=1),
}

# opcodes (3.0)
OP_RUN, OP_RECT, OP_CHAR, OP_COPY, OP_BLIT, OP_BAR, OP_SBLIT, OP_CBLIT, OP_RRECT, OP_FENCE = range(10)
OP_CORNERS, OP_DSBLIT, OP_ESC = 10, 11, 15
OP_NAMES = {0: "RUN", 1: "RECT", 2: "CHAR", 3: "COPY", 4: "BLIT", 5: "BAR", 6: "SBLIT", 7: "CBLIT", 8: "RRECT",
            9: "FENCE", 10: "CORNERS", 11: "DSBLIT", 12: "RSV12", 13: "RSV13", 14: "RSV14", 15: "ESC"}

# scale tables
CHAR_SCALE = {0: (1, 1, 16), 1: (2, 3, 24), 2: (1, 2, 32), 3: (1, 3, 48)}      # C-CHAR-2/-6: (num, den, extent)
SBLIT_RATIO = {0: (1, 1), 1: (2, 3), 2: (1, 2), 3: (1, 3)}                     # 3.7 (num, den)
DSBLIT_RATIO = {0: (3, 4), 1: (2, 3), 2: (1, 2), 3: (1, 3)}                    # 3.12 (num, den) = output:source

# MMIO byte offsets (2.2)
R_FB_ADDR, R_FB_SIZE, R_FB_COLOR, R_FB_GO, R_FB_STALL = 0x048, 0x04C, 0x050, 0x054, 0x058
R_BLT_IDX, R_BLT_DATA, R_CLUT_IDX, R_CLUT_DATA, R_RC_IDX, R_RC_DATA = 0x0C0, 0x0C4, 0x0C8, 0x0CC, 0x0D4, 0x0D8
R_TEXT_MODE, R_DBUF_CPU, R_DBUF_DISP = 0x114, 0x118, 0x11C
R_FONT_CTRL, R_FONT_IDX, R_FONT_DATA = 0x1A0, 0x1A4, 0x1A8
R_GFX_FENCE, R_GFX_STATUS, R_GFX_COUNTS = 0x1B8, 0x1BC, 0x1C0
R_VID_MODE, R_VIEW_BASE, R_VIEW_STRIDE, R_VIEW_CAPS, R_VIEW_CTRL = 0x280, 0x284, 0x288, 0x28C, 0x290

CMD_FIELDS = ("op", "addr", "w", "h", "fg", "bg", "glyph", "sx", "sy", "glyph_hi", "subop")
CMD_WIDTHS = {"op": 4, "addr": 19, "w": 9, "h": 9, "fg": 16, "bg": 16, "glyph": 7, "sx": 2, "sy": 2, "glyph_hi": 2,
              "subop": 6}                                                       # 2.1 port widths

# Mutation hooks (conformance plan section 5 re-expressed for the model; never set outside test_model.py)
MUTANTS = {
    "M1_RECT_SHORT": "RECT one column short",
    "M2_BAR_NOCLAMP": "BAR without the lit <= h clamp (paints above the bar, as canvas.py does)",
    "M2b_BAR_TOP": "BAR lit rows on top",
    "M3_GLYPH_NO_BIT7": "glyph index without bit 7 (glyph_hi[0])",
    "M4_TABLES_SWAPPED": "text weight tables swapped",
    "M5_MIX_ROUND": "text mix rounded instead of floored",
    "M6_BLEND_255": "blend weight 255 instead of 256",
    "M7_KEY_IGNORED": "BLIT key ignored",
    "M7b_KEY_ON_CBLIT": "key applied to CBLIT/SBLIT/DSBLIT",
    "M8_SBLIT_NO_ROWCLAMP": "SBLIT without the row clamp",
    "M8b_SBLIT_SRC_I": "SBLIT source column i instead of floor(i*num/den)",
    "M9_CBLIT_NO_REINDEX": "CBLIT without REINDEX",
    "M10_CUT_K": "RRECT/CORNERS cut index k instead of r-k",
    "M10b_CORNERS_FILL": "CORNERS with the fill",
    "M11_DSB_TRUNC": "DSBLIT truncating average",
    "M11b_DSB_ROW": "DSBLIT wrong neighbour row (same row twice)",
    "M12_DBUF_BLIT": "double-buffer offset applied to blit-class",
    "M13_FLIP_NOW": "flip applied immediately instead of at vblank",
    "M14_FENCE_ALL": "fence counts every command",
    "M15_IDLE_EARLY": "idle reads 1 while commands are queued",
    "M16_QUEUE_256": "a full queue accepting one more push",
    "M16b_OVF_NOCOUNT": "overflow not counted",
    "M17_FILL_399": "fill of FILL_WORDS-1 words",
    "M17b_FILL_HALF": "fill writes the wrong line-buffer half",
    "M19_ROW_INPLACE": "row written word by word before its source row is fully read",
    "M20_DBL_NOHALF": "viewer doubler not halving the line",
    "M21_RSV_DRAWS": "reserved / feature-off opcode draws one RUN row (the shipped behaviour C-OP-1 removes)",
    "M22_ROM_USES_HI": "font ROM index uses glyph_hi[0]",
}


class ElaborationError(Exception):
    pass


class ModelHang(Exception):
    """The action stream waits for something that can never happen in the model (e.g. wait_idle while stalled)."""


# ---- parameters ------------------------------------------------------------------------------------------------------
def resolve_params(params):
    """params: dict, may contain "SET" (a named set or a matrix name) and "MUTANT" (test only)."""
    p = dict(DEFAULT_PARAMS)
    params = dict(params or {})
    s = params.pop("SET", None)
    if s:
        if s in NAMED_SETS:
            p.update(NAMED_SETS[s])
        elif s in MATRIX:
            p.update(MATRIX[s])
        elif s != "DEFAULTS":
            raise ElaborationError("unknown named set %r" % s)
    p.update(params)
    elaborate(p)
    return p


def elaborate(p):
    """1.3: illegal combinations stop elaboration."""
    for k in ("BLIT_BLEND_ENABLE", "DBUF_ENABLE", "FONT_RAM_ENABLE", "FEAT_CORNERS", "FEAT_DSBLIT", "FEAT_ESC",
              "VIEW_ENABLE", "VIEW_DBL_EN"):
        if p[k] not in (0, 1):
            raise ElaborationError("%s must be 0 or 1" % k)
    if p["FILL_WORDS"] not in (400, 512):                         # 1.3 "400 shipped; 512 is accepted for the old A/B only"
        raise ElaborationError("FILL_WORDS must be 400 or 512")
    if p["VIEW_DBL_EN"] and not p["VIEW_ENABLE"]:                 # 1.3 VIEW_DBL_EN "requires VIEW_ENABLE"
        raise ElaborationError("VIEW_DBL_EN requires VIEW_ENABLE")
    vid40 = p["VIEW_ENABLE"] if p["SOC_VID40"] is None else p["SOC_VID40"]
    if p["VIEW_ENABLE"] and not vid40:                            # 1.3 VIEW_ENABLE "needs TAU_VID40 ... error otherwise"
        raise ElaborationError("VIEW_ENABLE needs the 40 MHz video build (TAU_VID40)")
    # 1.3 "a multiple of FILL_WORDS". GAP-01: with the defaults (1200, FILL_WORDS 512) an unconditional check would
    # refuse every FILL_WORDS = 512 build; the model checks it only when VIEW_ENABLE = 1.
    if p["VIEW_ENABLE"] and p["VIEW_LINE_WORDS"] % p["FILL_WORDS"] != 0:
        raise ElaborationError("VIEW_LINE_WORDS must be a multiple of FILL_WORDS")
    if not 0 < p["VIEW_LINE_WORDS"] <= 2048:                      # GAP-22: implied by the 12-bit {n[0], i} address
        raise ElaborationError("VIEW_LINE_WORDS must fit the 11-bit word index of the viewer line buffer")
    for k in ("TEXT_W_DARK", "TEXT_W_LIGHT"):                     # 1.3 "16 weights, 5 bits each"
        t = tuple(p[k])
        if len(t) != 16 or any(not 0 <= v <= 16 for v in t):      # GAP-23: >16 is representable in 5 bits; refused
            raise ElaborationError("%s must be 16 weights in 0..16" % k)
        p[k] = t
    if p.get("MUTANT") and p["MUTANT"] not in MUTANTS:
        raise ElaborationError("unknown mutant %r" % p["MUTANT"])
    if p["SOC_DECODE_BITS"] is None:
        p["SOC_DECODE_BITS"] = 10 if p["VIEW_ENABLE"] else 9      # 2.2 + C-IF-5 ("a VIEW build therefore needs the 10-bit decode")
    return p


def caps_of(p):
    """1.5: capability bits, gfx_caps[13:0], bit 0 = R_GFX_STATUS bit 17; bits 22..30 read 0."""
    return (p["FEAT_CORNERS"] << 0) | (p["FEAT_DSBLIT"] << 1) | (p["FEAT_ESC"] << 2) | (p["VIEW_ENABLE"] << 3) | \
           (p["VIEW_DBL_EN"] << 4)


# ---- memory ----------------------------------------------------------------------------------------------------------
class Memory:
    """The 2^25-word SDRAM image. Initial content: the probe's pattern k * 0x9E37 + 0x1234 (mod 2^16) at word k
    (conformance plan 2.1 b), unless an init image says otherwise. Tracks written addresses so a pooled image can be
    restored cheaply between runs."""
    _base = None

    @classmethod
    def base(cls):
        if cls._base is None:
            k = np.arange(MEM_WORDS, dtype=np.uint32)
            cls._base = ((k * np.uint32(0x9E37) + np.uint32(0x1234)) & np.uint32(0xFFFF)).astype("<u2")
        return cls._base

    def __init__(self):
        self.a = Memory.base().copy()
        self.dirty = []

    def restore(self):
        b = Memory.base()
        for d in self.dirty:
            self.a[d] = b[d]
        self.dirty = []

    def write(self, addrs, vals):
        self.a[addrs] = vals
        self.dirty.append(addrs)

    def crc32(self):
        """CRC-32 (zlib polynomial) over all 2^25 words, little-endian, address order (GAP-27: the plan names no CRC)."""
        return zlib.crc32(self.a.astype("<u2", copy=False).tobytes()) & 0xFFFFFFFF


def pattern_word(k):
    return (k * 0x9E37 + 0x1234) & 0xFFFF


# ---- synthetic fonts (the real atlases are black-box data; vectors carry these generated images) -------------------
def synthetic_font(words, salt):
    """Deterministic 32-bit glyph words; every coverage value 0..15 appears; glyph 0 is not blank (so an unmapped
    code is observable as glyph 0)."""
    out = []
    for i in range(words):
        v = 0
        for p in range(8):
            x = (i * 8 + p) * 2654435761 + salt * 40503
            v |= (((x >> 13) ^ (x >> 7) ^ i) & 15) << (4 * p)
        out.append(v & 0xFFFFFFFF)
    return out


# ---- helpers ----------------------------------------------------------------------------------------------------------
def split565(v):
    v = v.astype(np.int32)
    return (v >> 11) & 31, (v >> 5) & 63, v & 31


def join565(r, g, b):
    return ((r.astype(np.int32) << 11) | (g.astype(np.int32) << 5) | b.astype(np.int32)).astype(np.uint16)


def mask_cmd(cmd):
    c = {}
    for f in CMD_FIELDS:
        c[f] = int(cmd.get(f, 0)) & ((1 << CMD_WIDTHS[f]) - 1)
    return c


def decode_fb_go(word, held_addr, held_size, held_color, feat_esc):
    """C-IF-1 PINNED: op = {[14], [2:0]}, glyph = [9:3], sx = [11:10], sy = [13:12], glyph_hi = [16:15],
    subop = [22:17] (only with FEAT_ESC); addr, w, h, fg, bg from the held registers (2.2.1)."""
    return {
        "op": (((word >> 14) & 1) << 3) | (word & 7), "glyph": (word >> 3) & 0x7F, "sx": (word >> 10) & 3,
        "sy": (word >> 12) & 3, "glyph_hi": (word >> 15) & 3, "subop": ((word >> 17) & 0x3F) if feat_esc else 0,
        "addr": held_addr & MASK19, "w": held_size & 0x1FF, "h": (held_size >> 9) & 0x1FF,
        "fg": held_color & 0xFFFF, "bg": (held_color >> 16) & 0xFFFF,
    }


def encode_fb_go(cmd):
    """The inverse of decode_fb_go for the FB_GO word (the held registers carry addr, size, colours)."""
    op = cmd.get("op", 0)
    return ((op & 7) | (((op >> 3) & 1) << 14) | ((cmd.get("glyph", 0) & 0x7F) << 3) | ((cmd.get("sx", 0) & 3) << 10) |
            ((cmd.get("sy", 0) & 3) << 12) | ((cmd.get("glyph_hi", 0) & 3) << 15) | ((cmd.get("subop", 0) & 0x3F) << 17))


# ---- the engine ------------------------------------------------------------------------------------------------------
class Talos:
    def __init__(self, params, init_image=None, memory=None, log_values=True):
        self.p = resolve_params(params)
        self.mut = self.p.get("MUTANT")
        self.caps = caps_of(self.p)
        if memory is None:
            memory = Memory()
        else:
            memory.restore()
        self.mem = memory
        self.log_values = log_values
        self.write_log = []
        self.trace = []
        self.commands = []            # per accepted push: dict(fields, domain, notes)
        self.t = 0                    # action index for the trace
        init = init_image or {}
        # sticky state, power-up values (2.2.2, C-RST-3)
        self.src_base = 0; self.src_stride = 512; self.dst_base = 0; self.dst_stride = 512
        self.key_en = 0; self.key = 0; self.blend_en = 0; self.blend_mode = 0; self.blend_alpha = 0; self.reindex = 0
        self.blt_idx = 0
        self.clut = np.zeros(256, dtype=np.uint16); self.clut_idx = 0          # "contents not initialised" (GAP-14)
        self.clut_written = np.zeros(256, dtype=bool)
        self.rc = [0] * 16; self.rc_idx = 0                                     # 2.2.2 "all 16 entries 0"
        self.font_ram = np.zeros(FONT_RAM_WORDS, dtype=np.uint32); self.font_idx = 0; self.font_loads = 0
        self.font_rom = np.zeros(FONT_ROM_WORDS, dtype=np.uint32)
        self._glyph_cache = {}
        self.text_light = 0; self.dbuf_cpu = 0; self.disp_buf = 0; self.flip_pending = 0
        self.fb_addr = 0; self.fb_size = 0; self.fb_color = 0                  # 2.2.1 reset 0
        self.queue = deque()
        self.accepted = 0; self.retired = 0; self.fence = 0; self.ovf = 0; self.pushes_while_full = 0
        self.stalled = False
        self.lb = np.zeros(1024, dtype=np.uint16); self.fill_done_line = 0; self.fill_seen = 0
        self.vlb = np.zeros(4096, dtype=np.uint16)
        self.view_in = {"en": 0, "base": 0, "stride": 1200, "dbl": 0}          # VIEW_STRIDE reset 1,200 (2.2.3)
        self.view_lat = {"en": 0, "base": 0, "stride": 0, "dbl": 0}
        self.vid_mode_req = 0
        self.nondeterministic = []    # notes: sticky change while busy (C-ORD-5 FREE), etc.
        self._load_init(init)

    # -- init image -----------------------------------------------------------------------------------------------
    def _load_init(self, init):
        if init.get("fill_pattern", "probe") not in ("probe",):
            raise ValueError("only the probe pattern is defined (conformance plan 2.1 b)")
        for start, words in init.get("regions", []):
            a = (np.arange(len(words), dtype=np.int64) + start) & MASK25
            self.mem.write(a, np.array(words, dtype=np.uint16))
        for start, n, v in init.get("const_regions", []):
            a = (np.arange(n, dtype=np.int64) + start) & MASK25
            self.mem.write(a, np.full(n, v, dtype=np.uint16))
        if "clut" in init:
            c = init["clut"]; self.clut[:len(c)] = c; self.clut_written[:len(c)] = True
        if "rc" in init:
            self.rc = [v & 31 for v in init["rc"]] + [0] * (16 - len(init["rc"]))
        for key, arr, n, salt in (("font_ram", "font_ram", FONT_RAM_WORDS, 1), ("font_rom", "font_rom", FONT_ROM_WORDS, 2)):
            f = init.get(key)
            if f is None:
                continue
            if isinstance(f, dict):
                f = synthetic_font(n, f.get("salt", salt))
            getattr(self, arr)[:len(f)] = np.array(f, dtype=np.uint64).astype(np.uint32)
        st = init.get("sticky", {})
        for k, v in st.items():
            setattr(self, k, v)
        self.text_light = init.get("text_light", 0) & 1
        self.dbuf_cpu = init.get("dbuf_cpu", 0) & 1

    # -- logging ----------------------------------------------------------------------------------------------------
    def _ev(self, ev, **kw):
        d = {"t": self.t, "ev": ev}
        d.update(kw)
        self.trace.append(d)

    def _write(self, ci, op, addrs, vals):
        addrs = np.asarray(addrs, dtype=np.int64)
        vals = np.asarray(vals, dtype=np.uint16)
        if vals.shape == ():
            vals = np.full(addrs.shape, vals, dtype=np.uint16)
        self.mem.write(addrs, vals)
        self.write_log.append((ci, op, addrs, vals.copy() if self.log_values else None))

    def _read(self, addrs):
        return self.mem.a[np.asarray(addrs, dtype=np.int64)].copy()

    # -- addressing ---------------------------------------------------------------------------------------------------
    def _frame_row(self, A, y, x0, n):
        """3.0 frame-offset address: word (A + 512y + x) mod 2^19, then C-DBUF-1 per row. GAP-05: the per-row
        decision is taken on the row's x = 0 address (A + 512y) mod 2^19."""
        row = A + PITCH * y
        a = (row + x0 + np.arange(n, dtype=np.int64)) & MASK19
        if self.p["DBUF_ENABLE"] and self.dbuf_cpu and ((row & MASK19) < DBUF_LIMIT):      # C-DBUF-1
            a = a + DBUF_OFFSET
        return a

    def _frame_rect(self, A, h, x0, n):
        """_frame_row for rows 0..h-1 at once (same rule, vectorised): -> [h, n] addresses."""
        rows = A + PITCH * np.arange(h, dtype=np.int64)[:, None]
        a = (rows + x0 + np.arange(n, dtype=np.int64)[None, :]) & MASK19
        if self.p["DBUF_ENABLE"] and self.dbuf_cpu:
            a = a + np.where((rows & MASK19) < DBUF_LIMIT, DBUF_OFFSET, 0)    # C-DBUF-1, per row
        return a

    def _blit_row(self, base, off, stride, y, cols):
        """3.6 blit-class address: base + offset + y * stride + x, modulo 2^25; never touched by DBUF (C-DBUF-1),
        except under mutant M12."""
        a = (base + off + y * stride + cols) & MASK25
        if self.mut == "M12_DBUF_BLIT" and self.p["DBUF_ENABLE"] and self.dbuf_cpu:
            a = np.where(a < DBUF_LIMIT, a + DBUF_OFFSET, a)
        return a

    # -- queue (2.3) ------------------------------------------------------------------------------------------------
    def full(self):
        cap = QUEUE_SLOTS + (1 if self.mut == "M16_QUEUE_256" else 0)
        return len(self.queue) >= cap                                         # C-Q-2: full when 255 held

    def free(self):
        return max(0, QUEUE_SLOTS - len(self.queue))                           # C-Q-4 (exact in this model)

    def idle(self):
        if self.mut == "M15_IDLE_EARLY":
            return 1 if self.retired + 1 >= self.accepted else 0
        return 1 if self.accepted == self.retired else 0                       # C-IDLE-1

    def push(self, cmd):
        cmd = mask_cmd(cmd)
        if self.full():                                                        # C-Q-3: dropped, counted, saturating
            self.pushes_while_full += 1
            if self.mut != "M16b_OVF_NOCOUNT":
                self.ovf = min(OVF_MAX, self.ovf + 1)
            self._ev("drop", op=cmd["op"], ovf=self.ovf)
            return False
        ci = len(self.commands)
        dom, notes = classify(cmd, self)
        self.commands.append({"cmd": cmd, "domain": dom, "notes": notes})
        self.queue.append(ci)
        self.accepted += 1
        self._ev("push", ci=ci, op=cmd["op"], free=self.free(), full=int(self.full()), idle=self.idle())
        return True

    def step(self, n=1):
        """Dispatch and retire up to n commands (C-Q-1: in push order, one at a time)."""
        done = 0
        while self.queue and done < n:
            if self.stalled:
                break
            ci = self.queue.popleft()
            cmd = self.commands[ci]["cmd"]
            self.execute(ci, cmd)
            self.retired += 1                                                  # C-RET-1: exactly once, in order
            if cmd["op"] == OP_FENCE or self.mut == "M14_FENCE_ALL":
                self.fence = (self.fence + 1) & 0xFFFFFFFF                    # C-FENCE-1, 4.2 (32 bits, wraps)
            self._ev("retire", ci=ci, op=cmd["op"], fence=self.fence)
            done += 1
        return done

    def drain(self):
        if self.queue and self.stalled:
            raise ModelHang("wait for idle while the memory is stalled")
        self.step(len(self.queue))

    # -- command execution (section 3) --------------------------------------------------------------------------------
    def execute(self, ci, c):
        op = c["op"]
        p = self.p
        if op == OP_RUN:
            self._rect(ci, op, c["addr"], c["w"], 1, c["fg"])                  # C-RUN-1: h ignored
        elif op == OP_RECT:
            self._rect(ci, op, c["addr"], c["w"], c["h"], c["fg"])             # C-RECT-1
        elif op == OP_CHAR:
            self._char(ci, c)
        elif op == OP_COPY:
            self._copy(ci, c)
        elif op == OP_BLIT:
            self._blit(ci, c)
        elif op == OP_BAR:
            self._bar(ci, c)
        elif op == OP_SBLIT:
            self._sblit(ci, c)
        elif op == OP_CBLIT:
            self._cblit(ci, c)
        elif op == OP_RRECT:
            self._rrect(ci, c, fill=True)
        elif op == OP_FENCE:
            pass                                                                # C-FENCE-1: draws nothing
        elif op == OP_CORNERS and p["FEAT_CORNERS"]:
            self._rrect(ci, c, fill=(self.mut == "M10b_CORNERS_FILL"))          # C-CORN-1
        elif op == OP_DSBLIT and p["FEAT_DSBLIT"]:
            self._dsblit(ci, c)                                                 # C-DSB-1..3
        elif op == OP_ESC and p["FEAT_ESC"]:
            pass                                                                # C-ESC-1: every sub-opcode is a NOP in v0.1
        else:
            # C-OP-1 CHANGE: reserved opcode or feature off: retires without any memory access
            if self.mut == "M21_RSV_DRAWS":
                self._rect(ci, op, c["addr"], c["w"], 1, c["fg"])

    def _rect(self, ci, op, A, w, h, color):
        if self.mut == "M1_RECT_SHORT" and w > 0:
            w -= 1
        if w == 0 or h == 0:                                                    # C-GEN-1
            return
        self._write(ci, op, self._frame_rect(A, h, 0, w).ravel(), color)        # rows top to bottom, row-major

    def _bar(self, ci, c):
        A, w, h = c["addr"], c["w"], c["h"]
        if w == 0 or h == 0:                                                    # C-GEN-1
            return
        L = (c["glyph_hi"] << 7) | c["glyph"]                                   # C-BAR-1: 9-bit lit count
        lit = min(L, h)
        if self.mut == "M2_BAR_NOCLAMP":
            for y in range(h - L, h):                                            # canvas.py: fill(x, y+h-lit, w, lit)
                if y >= 0:
                    self._write(ci, OP_BAR, self._frame_row(A, y, 0, w), c["fg"])
                else:
                    # a row above the bar: frame offset A + 512*y with y < 0 (mod 2^19)
                    self._write(ci, OP_BAR, (A + PITCH * y + np.arange(w)) & MASK19, c["fg"])
            for y in range(0, max(0, h - L)):
                self._write(ci, OP_BAR, self._frame_row(A, y, 0, w), c["bg"])
            return
        ys = np.arange(h)[:, None]
        on = (ys >= h - lit) if self.mut != "M2b_BAR_TOP" else (ys < lit)
        vals = np.where(on, c["fg"], c["bg"]).astype(np.uint16) * np.ones((1, w), dtype=np.uint16)
        self._write(ci, OP_BAR, self._frame_rect(A, h, 0, w).ravel(), vals.ravel())

    # CHAR -----------------------------------------------------------------------------------------------------------
    def glyph_index(self, c):
        """3.4 glyph index, C-CHAR-1 PINNED."""
        if self.p["FONT_RAM_ENABLE"]:
            hi = 0 if self.mut == "M3_GLYPH_NO_BIT7" else (c["glyph_hi"] & 1)
            code = (hi << 7) | c["glyph"]
            if 0x20 <= code <= 0x7E:
                return code - 0x20
            if code >= 0xA0:
                return code - 0xA0 + 95
            return 0
        code = c["glyph"]
        if self.mut == "M22_ROM_USES_HI":
            code |= (c["glyph_hi"] & 1) << 7
        return code - 0x20 if 0x20 <= code <= 0x7E else 0

    def glyph_coverage(self, g):
        """C-FONT-1: row r of glyph g is words 32g+2r (pixels 0..7) and 32g+2r+1 (pixels 8..15); pixel p of a word
        is bits [4p+3:4p]. Returns a 16 x 16 array of coverage 0..15."""
        src = self.font_ram if self.p["FONT_RAM_ENABLE"] else self.font_rom
        key = (self.p["FONT_RAM_ENABLE"], g)
        if key in self._glyph_cache:
            return self._glyph_cache[key]
        words = src[(32 * g + np.arange(32)) % len(src)].astype(np.uint64)
        cov = np.zeros((16, 16), dtype=np.int32)
        for r in range(16):
            for half in range(2):
                wv = int(words[2 * r + half])
                for p in range(8):
                    cov[r, half * 8 + p] = (wv >> (4 * p)) & 15
        self._glyph_cache[key] = cov
        return cov

    def _char(self, ci, c):
        g = self.glyph_index(c)
        cov = self.glyph_coverage(g)
        nx, dx, ex = CHAR_SCALE[c["sx"]]                                        # C-CHAR-2 / C-CHAR-6
        ny, dy, ey = CHAR_SCALE[c["sy"]]
        cols = (np.arange(ex) * nx) // dx
        rows = (np.arange(ey) * ny) // dy
        cell = cov[np.ix_(rows, cols)]
        light = self.text_light
        if self.mut == "M4_TABLES_SWAPPED":
            light ^= 1
        T = np.array(self.p["TEXT_W_LIGHT"] if light else self.p["TEXT_W_DARK"], dtype=np.int32)
        wt = T[cell]                                                            # C-CHAR-3/-4
        fr, fgg, fb = (c["fg"] >> 11) & 31, (c["fg"] >> 5) & 63, c["fg"] & 31
        br, bgg, bb = (c["bg"] >> 11) & 31, (c["bg"] >> 5) & 63, c["bg"] & 31
        rnd = 8 if self.mut == "M5_MIX_ROUND" else 0
        r = np.minimum(31, (fr * wt + br * (16 - wt) + rnd) // 16)
        gch = np.minimum(63, (fgg * wt + bgg * (16 - wt) + rnd) // 16)
        b = np.minimum(31, (fb * wt + bb * (16 - wt) + rnd) // 16)
        out = join565(r, gch, b)
        # C-CHAR-5: rows top to bottom at A + 512j, each row subject to the double-buffer rule
        self._write(ci, OP_CHAR, self._frame_rect(c["addr"], ey, 0, ex).ravel(), out.ravel())

    # COPY / BLIT / CBLIT --------------------------------------------------------------------------------------------
    @staticmethod
    def src_offset(c):
        return ((c["fg"] & 7) << 16) | c["bg"]                                  # 3.0: S = {fg[2:0], bg[15:0]}

    def _inplace(self, ci, op, src_addrs, dst_addrs, fn):
        """mutant M19: word by word, each source word read just before its own write."""
        for s, d in zip(src_addrs, dst_addrs):
            v = self.mem.a[s:s + 1].copy()
            self._write(ci, op, np.array([d]), fn(v, self.mem.a[d:d + 1].copy()))

    def _copy(self, ci, c):
        w, h, S, A = c["w"], c["h"], self.src_offset(c), c["addr"]
        if w == 0 or h == 0:
            return
        for y in range(h):                                                      # C-ORD-1: rows top to bottom
            sa = self._frame_row(S, y, 0, w)                                    # C-COPY-1: both frame-offset, both DBUF
            da = self._frame_row(A, y, 0, w)
            if self.mut == "M19_ROW_INPLACE":
                self._inplace(ci, OP_COPY, sa, da, lambda v, d: v)
                continue
            vals = self._read(sa)                                               # C-ORD-1: whole row read first
            self._write(ci, OP_COPY, da, vals)

    def blend(self, d, f):
        """3.6 blend, per channel; C-BLIT-3 (mode 0) PINNED, C-BLIT-4 (modes 1..7) MATCH."""
        br, bg, bb = split565(d)
        fr, fg, fb = split565(f)
        m = self.blend_mode
        a = self.blend_alpha
        out = []
        for b_, f_, mx in ((br, fr, 31), (bg, fg, 63), (bb, fb, 31)):
            if m == 0:
                if self.mut == "M6_BLEND_255":
                    o = (f_ * a + b_ * (255 - a)) // 255
                else:
                    o = (f_ * a + b_ * (256 - a)) // 256
            elif m == 1:
                o = (b_ + f_) // 2
            elif m == 2:
                o = np.minimum(mx, b_ + f_)
            elif m == 3:
                o = np.where(f_ <= b_, b_ - f_, 0)
            else:                                                               # modes 4..7
                o = np.minimum(mx, b_ + f_ // 4)
            out.append(o)
        return join565(*out)

    def _blit(self, ci, c):
        w, h, S, A = c["w"], c["h"], self.src_offset(c), c["addr"]
        if w == 0 or h == 0:
            return
        blend_on = bool(self.p["BLIT_BLEND_ENABLE"] and self.blend_en)          # 3.6: with BLIT_BLEND_ENABLE = 0 ignored
        key_on = bool(self.key_en) and self.mut != "M7_KEY_IGNORED"
        cols = np.arange(w, dtype=np.int64)                                     # C-BLIT-6: w 1..127 legal (OOD: full w, GAP-12)
        for y in range(h):
            sa = self._blit_row(self.src_base, S, self.src_stride, y, cols)     # C-BLIT-1
            da = self._blit_row(self.dst_base, A, self.dst_stride, y, cols)
            if self.mut == "M19_ROW_INPLACE":
                def fn(v, d):
                    o = self.blend(d, v) if blend_on else v
                    return np.where(key_on & (v == self.key), d, o)
                self._inplace(ci, OP_BLIT, sa, da, fn)
                continue
            f = self._read(sa)                                                  # C-ORD-1: the row's source first
            if key_on or blend_on:
                d = self._read(da)                                              # C-ORD-1: destination pre-read
                out = self.blend(d, f) if blend_on else f
                if key_on:
                    out = np.where(f == self.key, d, out)                       # C-BLIT-2, C-BLIT-5: keyed word left alone
                # GAP-13: a keyed word is logged as re-written with its own pre-read value
            else:
                out = f                                                         # plain
            self._write(ci, OP_BLIT, da, out)

    def _cblit(self, ci, c):
        w, h, S, A = c["w"], c["h"], self.src_offset(c), c["addr"]
        if w == 0 or h == 0:
            return
        cols = np.arange(w, dtype=np.int64)
        ri = 0 if self.mut == "M9_CBLIT_NO_REINDEX" else self.reindex
        for y in range(h):
            sa = self._blit_row(self.src_base, S, self.src_stride, y, cols)
            da = self._blit_row(self.dst_base, A, self.dst_stride, y, cols)
            f = self._read(sa)
            idx = ((f.astype(np.int32) & 0xFF) + ri) & 0xFF
            if not self.clut_written[idx].all():                                # 2.2.2 "contents not initialised" (GAP-14)
                self.nondeterministic.append((self.t, "CBLIT read an unwritten CLUT entry"))
            out = self.clut[idx]                                                # C-CBLIT-1/-2
            if self.mut == "M7b_KEY_ON_CBLIT" and self.key_en:
                out = np.where(f == self.key, self._read(da), out)
            self._write(ci, OP_CBLIT, da, out)                                  # C-CBLIT-3: no clipping

    def _sblit(self, ci, c):
        nx, dx = SBLIT_RATIO[c["sx"]]
        ny, dy = SBLIT_RATIO[c["sy"]]
        ex = min(127, (c["w"] * dx) // nx)                                      # 3.7 E = min(127, floor(s*den/num))
        ey = (c["h"] * dy) // ny
        if self.mut != "M8_SBLIT_NO_ROWCLAMP":
            ey = min(127, ey)                                                   # C-SBLIT-2 PINNED (row clamp)
        if ex == 0 or ey == 0:
            return
        S, A = self.src_offset(c), c["addr"]
        if self.mut == "M8b_SBLIT_SRC_I":
            X = np.arange(ex, dtype=np.int64)
        else:
            X = (np.arange(ex, dtype=np.int64) * nx) // dx                      # C-SBLIT-1/-3
        dcols = np.arange(ex, dtype=np.int64)
        for j in range(ey):
            Y = (j * ny) // dy
            f = self._read(self._blit_row(self.src_base, S, self.src_stride, Y, X))   # GAP-15: row read before row write
            da = self._blit_row(self.dst_base, A, self.dst_stride, j, dcols)
            if self.mut == "M7b_KEY_ON_CBLIT" and self.key_en:
                f = np.where(f == self.key, self._read(da), f)
            self._write(ci, OP_SBLIT, da, f)                                    # C-SBLIT-4: no key, no blend

    def _dsblit(self, ci, c):
        nx, dx = DSBLIT_RATIO[c["sx"]]
        ny, dy = DSBLIT_RATIO[c["sy"]]
        ex = min(127, (c["w"] * nx) // dx)                                      # 3.12 E = min(127, floor(s*num/den))
        ey = min(127, (c["h"] * ny) // dy)
        if ex == 0 or ey == 0:
            return
        S, A = self.src_offset(c), c["addr"]
        X = (np.arange(ex, dtype=np.int64) * dx) // nx                          # X = floor(i*den/num)
        dcols = np.arange(ex, dtype=np.int64)
        for j in range(ey):
            Y = (j * dy) // ny
            Y2 = Y if self.mut == "M11b_DSB_ROW" else Y + 1
            p0 = self._read(self._blit_row(self.src_base, S, self.src_stride, Y, X))
            p1 = self._read(self._blit_row(self.src_base, S, self.src_stride, Y, X + 1))
            p2 = self._read(self._blit_row(self.src_base, S, self.src_stride, Y2, X))
            p3 = self._read(self._blit_row(self.src_base, S, self.src_stride, Y2, X + 1))
            chans = [split565(q) for q in (p0, p1, p2, p3)]
            rnd = 0 if self.mut == "M11_DSB_TRUNC" else 2
            out = [(chans[0][k] + chans[1][k] + chans[2][k] + chans[3][k] + rnd) // 4 for k in range(3)]   # C-DSB-1
            da = self._blit_row(self.dst_base, A, self.dst_stride, j, dcols)
            res = join565(*out)
            if self.mut == "M7b_KEY_ON_CBLIT" and self.key_en:
                res = np.where(p0 == self.key, self._read(da), res)
            self._write(ci, OP_DSBLIT, da, res)                                 # C-DSB-2: no key, no blend

    def _rrect(self, ci, c, fill):
        A, w, h = c["addr"], c["w"], c["h"]
        op = c["op"]
        if w == 0 or h == 0:                                                    # C-GEN-1, 3.11
            return
        r = min(c["glyph"] & 0x7F, 15)                                          # 3.9: the 7-bit field clamped to 15
        if fill:
            self._rect(ci, op, A, w, h, c["fg"])                                # C-RRECT-1: the fill first
        for k in range(r):
            idx = k if self.mut == "M10_CUT_K" else r - k
            cut = self.rc[idx & 15]
            if cut <= 0:
                continue
            cut = min(cut, w)                                                   # C-GEN-2: OOD (cut > w) stays inside (GAP-17)
            for row in sorted({k, h - 1 - k}):
                if 0 <= row < h:                                                # C-GEN-2: OOD (r > h) stays inside
                    self._write(ci, op, self._frame_row(A, row, 0, cut), c["bg"])
                    self._write(ci, op, self._frame_row(A, row, w - cut, cut), c["bg"])

    # -- scan-out side (2.6, 3.14, 6) ----------------------------------------------------------------------------------
    def fill(self, n):
        n &= 0x3FF
        if self.p["VIEW_ENABLE"] and self.view_lat["en"]:
            return self._view_fill(n)
        fw = self.p["FILL_WORDS"] - (1 if self.mut == "M17_FILL_399" else 0)
        base = PITCH * n + (DBUF_OFFSET if (self.p["DBUF_ENABLE"] and self.disp_buf) else 0)   # C-FILL-1, C-FILL-5
        vals = self.mem.a[(base + np.arange(fw, dtype=np.int64)) & MASK25].copy()
        half = (n & 1) ^ (1 if self.mut == "M17b_FILL_HALF" else 0)
        self.lb[(half << 9) + np.arange(fw)] = vals                             # lb address {n[0], i[8:0]}
        self.fill_done_line = n; self.fill_seen = 1
        self._ev("fill", line=n, buf=self.disp_buf, crc=zlib.crc32(vals.astype("<u2").tobytes()))
        return vals

    def _view_fill(self, n):
        """C-VIEW-2, C-DBL-1: VIEW_LINE_WORDS words from view_base + p * view_stride (GAP-19: modulo 2^25)."""
        dbl = self.view_lat["dbl"] and self.p["VIEW_DBL_EN"] and self.mut != "M20_DBL_NOHALF"
        p = n // 2 if dbl else n
        nw = self.p["VIEW_LINE_WORDS"]
        base = self.view_lat["base"] + p * self.view_lat["stride"]
        vals = self.mem.a[(base + np.arange(nw, dtype=np.int64)) & MASK25].copy()   # as nw/FILL_WORDS consecutive chunks
        self.vlb[((n & 1) << 11) + np.arange(nw)] = vals                        # vlb address {n[0], i} (12 bits)
        self.fill_done_line = n; self.fill_seen = 1
        self._ev("vfill", line=n, plane_line=p, crc=zlib.crc32(vals.astype("<u2").tobytes()))
        return vals

    def vblank(self):
        if self.p["DBUF_ENABLE"] and self.flip_pending:                         # C-DBUF-2
            self.disp_buf ^= 1; self.flip_pending = 0
        if self.p["VIEW_ENABLE"]:                                               # C-VIEW-1: latch at the vblank edge
            self.view_lat = {"en": self.view_in["en"] & 1, "base": self.view_in["base"] & MASK25,
                             "stride": self.view_in["stride"] & 0xFFFF,
                             "dbl": (self.view_in["dbl"] & 1) if self.p["VIEW_DBL_EN"] else 0}
        self._ev("vblank", disp=self.disp_buf, view=self.view_lat["en"])

    def flip_request(self):
        if not self.p["DBUF_ENABLE"]:                                           # 3.14: inputs ignored
            return
        if self.mut == "M13_FLIP_NOW":
            self.disp_buf ^= 1
            return
        self.flip_pending = 1                                                   # C-DBUF-2: several requests, one flip

    def reset(self):
        """C-RST-1: queue emptied (never retire), counts cleared, free 255; C-RST-3: sticky, CLUT, font, corner table
        and memory survive; C-DBUF-3: displayed buffer and pending survive; C-VIEW-1: latched mode to 0."""
        self.queue.clear()
        self.accepted = 0; self.retired = 0; self.fence = 0; self.ovf = 0
        self.fb_addr = 0; self.fb_size = 0; self.fb_color = 0; self.pushes_while_full = 0   # GAP-08 (system reset)
        self.view_lat = {"en": 0, "base": 0, "stride": 0, "dbl": 0}
        self.fill_seen = 0; self.fill_done_line = 0                             # GAP-09
        self.stalled = False
        self._ev("reset")

    # -- sticky / SoC registers (2.2) ------------------------------------------------------------------------------
    def _busy_change(self, what):
        if not self.idle():
            self.nondeterministic.append((self.t, what))                       # C-ORD-4 / C-ORD-5: result is FREE

    def sticky_write(self, field, v):
        """BLT_DATA (2.2.2): writes the selected field, then the selector steps (6 -> 0; 7 writes REINDEX, C-REG-1)."""
        self._busy_change("sticky")
        field &= 7
        if field == 0: self.src_base = v & MASK25
        elif field == 1: self.src_stride = v & 0x3FF
        elif field == 2: self.dst_base = v & MASK25
        elif field == 3: self.dst_stride = v & 0x3FF
        elif field == 4: self.key_en = (v >> 16) & 1; self.key = v & 0xFFFF
        elif field == 5: self.blend_en = v & 1; self.blend_mode = (v >> 1) & 7; self.blend_alpha = (v >> 8) & 0xFF
        else: self.reindex = v & 0xFF                                           # 6 and 7 (C-REG-1 FREE)
        self.blt_idx = 0 if field >= 6 else field + 1                           # GAP-06: 7 steps to 0

    def mmio_write(self, off, v):
        off = self._decode(off)
        v &= 0xFFFFFFFF
        p = self.p
        if off == R_FB_ADDR: self.fb_addr = v & MASK19
        elif off == R_FB_SIZE: self.fb_size = v & 0x3FFFF
        elif off == R_FB_COLOR: self.fb_color = v
        elif off == R_FB_GO:
            return self.push(decode_fb_go(v, self.fb_addr, self.fb_size, self.fb_color, p["FEAT_ESC"]))
        elif off == R_BLT_IDX: self.blt_idx = v & 7
        elif off == R_BLT_DATA: self.sticky_write(self.blt_idx, v)
        elif off == R_CLUT_IDX: self.clut_idx = v & 0xFF
        elif off == R_CLUT_DATA:
            self._busy_change("clut")
            self.clut[self.clut_idx] = v & 0xFFFF; self.clut_written[self.clut_idx] = True
            self.clut_idx = (self.clut_idx + 1) & 0xFF
        elif off == R_RC_IDX: self.rc_idx = v & 15
        elif off == R_RC_DATA:
            self._busy_change("rc")
            self.rc[self.rc_idx] = v & 31; self.rc_idx = (self.rc_idx + 1) & 15
        elif off == R_TEXT_MODE:
            self._busy_change("text_light"); self.text_light = v & 1
        elif off == R_DBUF_CPU:
            if p["DBUF_ENABLE"]:                                                # GAP-10: without DBUF the write is ignored
                self._busy_change("dbuf_cpu"); self.dbuf_cpu = v & 1
        elif off == R_DBUF_DISP:
            if v & 1:
                self.flip_request()
        elif off == R_FONT_CTRL:
            if p["FONT_RAM_ENABLE"] and (v & 1): self.font_idx = 0
        elif off == R_FONT_IDX:
            if p["FONT_RAM_ENABLE"]: self.font_idx = v if v < FONT_RAM_WORDS else 0
        elif off == R_FONT_DATA:
            if p["FONT_RAM_ENABLE"]:
                self._busy_change("font")
                self.font_ram[self.font_idx] = v; self._glyph_cache.clear()
                self.font_idx += 1
                if self.font_idx >= FONT_RAM_WORDS:
                    self.font_idx = 0; self.font_loads = (self.font_loads + 1) & 0xFFF   # GAP-11: 12-bit wrap
        elif p["VIEW_ENABLE"] and p["SOC_DECODE_BITS"] >= 10:
            if off == R_VID_MODE: self.vid_mode_req = v & 1                    # sequencer: out of scope (GAP-20)
            elif off == R_VIEW_BASE: self.view_in["base"] = v & MASK25
            elif off == R_VIEW_STRIDE: self.view_in["stride"] = v & 0xFFFF
            elif off == R_VIEW_CTRL: self.view_in["dbl"] = v & 1
        return None

    def mmio_read(self, off):
        off = self._decode(off)
        p = self.p
        if off == R_FB_GO: return int(self.full()) | (self.idle() << 1)         # 2.2.1; FB_GO[1] == GFX_STATUS[16]
        if off == R_FB_STALL: return None                                       # clk_sys cycles: not modelled (GAP-28)
        if off == R_TEXT_MODE: return (1 << 31) | self.text_light
        if off == R_DBUF_CPU: return ((1 << 31) | self.dbuf_cpu) if p["DBUF_ENABLE"] else 0
        if off == R_DBUF_DISP:
            return ((1 << 31) | (self.flip_pending << 1) | self.disp_buf) if p["DBUF_ENABLE"] else 0
        if off == R_FONT_CTRL:
            return ((1 << 31) | ((self.font_loads & 0xFFF) << 16) | FONT_RAM_WORDS) if p["FONT_RAM_ENABLE"] else 0
        if off == R_FONT_IDX: return self.font_idx if p["FONT_RAM_ENABLE"] else 0
        if off == R_GFX_FENCE: return self.fence
        if off == R_GFX_STATUS:
            return (1 << 31) | (self.caps << 17) | (self.idle() << 16) | self.free()          # 2.2.3, 1.5
        if off == R_GFX_COUNTS: return (0 << 16) | self.ovf                    # late lines come from the scan-out (0 here)
        if p["VIEW_ENABLE"] and p["SOC_DECODE_BITS"] >= 10:
            if off == R_VIEW_BASE: return self.view_in["base"]
            if off == R_VIEW_STRIDE: return self.view_in["stride"]
            if off == R_VIEW_CTRL: return self.view_in["dbl"]
            if off == R_VIEW_CAPS:                                              # GAP-21: [1] and [0] belong to the scan-out side
                return (1 << 31) | (p["VIEW_DBL_EN"] << 3) | (1 << 1) | (1 << 0)
            if off == R_VID_MODE:
                return (self.vid_mode_req << 2) | (int(self.vid_mode_req != self.view_in["en"]) << 1) | self.view_in["en"]
        return 0                                                                # W-only and unknown registers read 0

    def _decode(self, off):
        bits = self.p["SOC_DECODE_BITS"]                                         # 2.2: aliases onto offset & mask
        return off & ((1 << bits) - 1) & ~3

    # -- the action interpreter ---------------------------------------------------------------------------------------
    def act(self, a):
        k = a["a"]
        if k == "push":
            for _ in range(a.get("repeat", 1)):                                 # "repeat": n identical pushes
                self.push(a)
        elif k == "mmio_w":
            self.mmio_write(a["off"], a["v"])
        elif k == "mmio_r":
            v = self.mmio_read(a["off"])
            self._ev("read", off=a["off"], v=v, label=a.get("label"))
        elif k == "sticky":
            self.blt_idx = a["field"] & 7
            self.sticky_write(a["field"], a["v"])
        elif k == "clut":
            self.mmio_write(R_CLUT_IDX, a["idx"])
            for v in a["v"]:
                self.mmio_write(R_CLUT_DATA, v)
        elif k == "rc":
            self.mmio_write(R_RC_IDX, a["idx"])
            for v in a["v"]:
                self.mmio_write(R_RC_DATA, v)
        elif k == "font":
            self.mmio_write(R_FONT_IDX, a["idx"])
            for v in a["v"]:
                self.mmio_write(R_FONT_DATA, v)
        elif k == "text_light":
            self._busy_change("text_light"); self.text_light = a["v"] & 1
        elif k == "dbuf_cpu":
            self.mmio_write(R_DBUF_CPU, a["v"])
        elif k == "flip_req":
            self.flip_request()
        elif k == "vblank":
            self.vblank()
        elif k == "fill":
            self.fill(a["line"])
        elif k == "view":
            for f in ("en", "base", "stride", "dbl"):
                if f in a:
                    self.view_in[f] = a[f]
        elif k == "wait_idle":
            self.drain()
        elif k == "fence":
            ok = self.push({"op": OP_FENCE})
            self.drain()
            self._ev("fence_done", fence=self.fence, accepted=ok)
        elif k == "step":
            self.step(a.get("n", 1))
        elif k == "stall":
            self.stalled = bool(a["v"])
        elif k == "reset":
            self.reset()
        elif k == "cpu_w":                                                      # the CPU's own SDRAM path (mailbox)
            addrs = (np.arange(len(a["v"]), dtype=np.int64) + a["addr"]) & MASK25
            self.mem.write(addrs, np.array(a["v"], dtype=np.uint16))
        elif k == "snapshot":
            self._ev("snapshot", **self.status())
        else:
            raise ValueError("unknown action %r" % k)

    def status(self):
        return {"accepted": self.accepted, "retired": self.retired, "fence": self.fence, "ovf": self.ovf,
                "free": self.free(), "full": int(self.full()), "idle": self.idle(), "disp": self.disp_buf,
                "pending": self.flip_pending, "fill_done_line": self.fill_done_line, "fill_seen": self.fill_seen}


# ---- legal-domain classification (per command) ------------------------------------------------------------------------
def classify(c, eng):
    """-> ("legal" | "ood", [reasons]). Out-of-domain results are not compared (C-GEN-2); the model still produces a
    result that obeys the safety rule (writes stay inside the command's rectangle)."""
    op, w, h, A = c["op"], c["w"], c["h"], c["addr"]
    why = []

    def frame_fits(ww, hh):
        if ww and hh and A + PITCH * (hh - 1) + ww - 1 >= (1 << 19):
            why.append("frame offset beyond 2^19")

    if op in (OP_RECT, OP_RUN, OP_BAR):
        frame_fits(w, 1 if op == OP_RUN else h)                                 # C-RECT-1 (BAR: GAP-16)
    elif op == OP_CHAR:
        frame_fits(CHAR_SCALE[c["sx"]][2], CHAR_SCALE[c["sy"]][2])               # GAP-16
    elif op == OP_COPY:
        if w > 127: why.append("COPY w > 127")                                   # C-COPY-1
        frame_fits(w, h)
    elif op in (OP_BLIT, OP_CBLIT):
        if w > 127: why.append("blit-class w > 127")                             # C-BLIT-6
    elif op in (OP_RRECT, OP_CORNERS):
        frame_fits(w, h)
        r = min(c["glyph"], 15)
        if w and h and r:
            if r > h: why.append("r > h")                                        # C-RRECT-1 legal domain
            for k in range(r):
                if eng.rc[r - k] > w: why.append("cut > w"); break
    # SBLIT, DSBLIT: the contract states no legal domain beyond the field widths (GAP-15); always "legal" here.
    return ("ood" if why else "legal"), why


# ---- the public entry point ------------------------------------------------------------------------------------------
def run(params, init_image, command_stream, memory=None, log_values=True, drain_at_end=True):
    eng = Talos(params, init_image, memory=memory, log_values=log_values)
    for i, a in enumerate(command_stream):
        eng.t = i
        eng.act(a)
    eng.t = len(command_stream)
    if drain_at_end and not eng.stalled:
        eng.drain()
        eng._ev("end", **eng.status())
    run.last_engine = eng
    return eng.mem, eng.write_log, eng.trace


run.last_engine = None


# ---- helpers for vectors ---------------------------------------------------------------------------------------------
def written_regions(memory, write_log, extra_addrs=()):
    """The final words at every address written by the run, as RLE regions: ["c", start, n, value] for constant runs,
    ["w", start, [words]] otherwise."""
    parts = [w[2] for w in write_log] + [np.asarray(x, dtype=np.int64) for x in extra_addrs]
    if not parts:
        return []
    addrs = np.unique(np.concatenate(parts))
    vals = memory.a[addrs]
    out = []
    i = 0
    n = len(addrs)
    # split into contiguous runs
    brk = np.nonzero(np.diff(addrs) != 1)[0] + 1
    starts = np.concatenate([[0], brk]); ends = np.concatenate([brk, [n]])
    for s, e in zip(starts, ends):
        seg = vals[s:e]
        a0 = int(addrs[s])
        # RLE inside the run
        j = 0
        lit_start = None
        while j < len(seg):
            k = j
            while k + 1 < len(seg) and seg[k + 1] == seg[j]:
                k += 1
            if k - j + 1 >= 8:
                if lit_start is not None:
                    out.append(["w", a0 + lit_start, [int(x) for x in seg[lit_start:j]]]); lit_start = None
                out.append(["c", a0 + j, k - j + 1, int(seg[j])])
            elif lit_start is None:
                lit_start = j
            j = k + 1
        if lit_start is not None:
            out.append(["w", a0 + lit_start, [int(x) for x in seg[lit_start:]]])
    return out


def check_regions(memory, regions):
    """-> number of mismatching words."""
    bad = 0
    for r in regions:
        if r[0] == "c":
            seg = memory.a[(r[1] + np.arange(r[2], dtype=np.int64)) & MASK25]
            bad += int(np.count_nonzero(seg != r[3]))
        else:
            seg = memory.a[(r[1] + np.arange(len(r[2]), dtype=np.int64)) & MASK25]
            bad += int(np.count_nonzero(seg != np.array(r[2], dtype=np.uint16)))
    return bad


if __name__ == "__main__":
    import json
    import sys
    if len(sys.argv) > 1:
        # run every vector of a JSON-lines file and report pass/fail against its expectation
        fails = 0
        for line in open(sys.argv[1]):
            v = json.loads(line)
            mem, wl, tr = run(v["params"], v["init"], v["actions"])
            bad = check_regions(mem, v["expect"]["regions"])
            crc = mem.crc32()
            ok = bad == 0 and crc == v["expect"]["crc32"]
            fails += not ok
            print("%-40s %s" % (v["name"], "ok" if ok else "FAIL (%d words, crc %08x)" % (bad, crc)))
        sys.exit(1 if fails else 0)
    print(MODEL_VERSION)
