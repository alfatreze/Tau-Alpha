#!/usr/bin/env python3
"""Software reference renderer for the mp3_fb blit engine (PHASE_F_SPEC.md
section 12: "a software reference renderer implementing each opcode, plus
pixel-diff fixtures").

This is a from-scratch reimplementation of mp3_fb.sv's pixel semantics in
Python, not a wrapper around the RTL -- the point is an independent model to
diff the RTL's simulated output against, the same way the library loader was
checked against a Python reference (fw/library_core.h vs tools/tau_library.py)
before it was trusted. Every formula here is transcribed directly from
mp3_fb.sv with a comment pointing at the RTL construct it mirrors, so a
future reader can verify by inspection rather than by trusting this file.

Source-word model: COPY/BLIT/SBLIT read from a plain memory abstraction, not
real SDRAM content, matching the exact convention sim/tb_mp3_fb.v's own SDRAM
stub already uses ("word N reads back as N+1") -- this lets the reference and
the RTL simulation agree on source content without either one modelling real
SDRAM storage.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FONT_ROM = ROOT / "src" / "fpga" / "core" / "font_rom.v"

STRIDE = 512  # words/line, matches mp3_fb.sv's STRIDE localparam

# scale_nd(): sel -> (num, den). mp3_fb.sv function scale_nd.
SCALE_ND = {0: (1, 1), 1: (2, 3), 2: (1, 2), 3: (1, 3)}
# scale_ext(): sel -> painted extent of a 16px cell. mp3_fb.sv function scale_ext.
SCALE_EXT = {0: 16, 1: 24, 2: 32, 3: 48}
# cov_weight(): 4-bit coverage -> 5-bit blend weight. mp3_fb.sv function cov_weight.
COV_WEIGHT = [0, 4, 6, 7, 8, 10, 10, 11, 12, 13, 13, 14, 15, 15, 16, 16]


def sblit_ext(src: int, sel: int) -> int:
    """mp3_fb.sv function sblit_ext: multiply-by-small-constant scaled extent,
    clamped to 127 (the glyphbuf row-buffer width limit OP_COPY/OP_BLIT share)."""
    if sel == 0:
        scaled = src
    elif sel == 1:
        scaled = (src * 3) >> 1
    elif sel == 2:
        scaled = src << 1
    else:
        scaled = src * 3
    return min(scaled, 127)


def blend_ch(b: int, f: int, mode: int, alpha: int, chmax: int) -> int:
    """mp3_fb.sv function blend_ch, one channel. mode 0 = DSP approx (>>8, not
    /255, a documented pragmatic trade); 1..4 = PSX shift-add ratios."""
    if mode == 0:
        dsp = f * alpha + b * (256 - alpha)
        return (dsp >> 8) & 0xFF
    if mode == 1:
        return (b + f) >> 1
    if mode == 2:
        s = b + f
        return chmax if s > chmax else s
    if mode == 3:
        return 0 if f > b else (b - f)
    s = b + (f >> 2)
    return chmax if s > chmax else s


def blend_px(bg: int, fg: int, mode: int, alpha: int) -> int:
    """mp3_fb.sv function blend_px: per-channel blend_ch on RGB565, channels
    zero-extended to 8 bits so one function serves 5-bit R/B and 6-bit G."""
    r = blend_ch((bg >> 11) & 0x1F, (fg >> 11) & 0x1F, mode, alpha, 31)
    g = blend_ch((bg >> 5) & 0x3F, (fg >> 5) & 0x3F, mode, alpha, 63)
    b = blend_ch(bg & 0x1F, fg & 0x1F, mode, alpha, 31)
    return ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F)


def load_font_words(path: Path = FONT_ROM) -> list[int]:
    """Parse the shipped font_rom.v directly (the non-TAU_FONT_REPACK, single
    32-bit-array path -- what a macro-less `make test-rtl` build compiles)
    rather than re-rasterising with tools/gen_font_rom.py's render_words(),
    which needs PIL and the TTF file: this way the reference tests against
    the actual committed ROM content, with no extra dependency and no risk
    of a fresh render disagreeing with what shipped."""
    text = path.read_text()
    else_part = text.split("`else", 1)[1]
    words: dict[int, int] = {}
    for m in re.finditer(r"mem\[\s*(\d+)\]\s*=\s*32'h([0-9A-Fa-f]+);", else_part):
        words[int(m.group(1))] = int(m.group(2), 16)
    if not words:
        raise RuntimeError(f"no mem[N] = 32'hXXXXXXXX; entries found in {path}")
    n = max(words) + 1
    return [words.get(i, 0) for i in range(n)]


@dataclass
class Renderer:
    """Destination framebuffer as a sparse {word_addr: rgb565} map, matching
    how sim/tb_blit_scene.v captures only the words actually written."""

    font_words: list[int]
    mem: dict[int, int] = field(default_factory=dict)

    @staticmethod
    def src_read(addr: int) -> int:
        """mp3_fb.sv's SDRAM stub (sim/tb_mp3_fb.v): p0_q <= rd_addr[15:0] + 1."""
        return ((addr & 0xFFFF) + 1) & 0xFFFF

    def run(self, addr: int, w: int, fg: int) -> None:
        """OP_RUN: default case in mp3_fb.sv's dispatch -- a one-row RECT."""
        self.rect(addr, w, 1, fg)

    def rect(self, addr: int, w: int, h: int, fg: int) -> None:
        """OP_RECT."""
        for r in range(h):
            for c in range(w):
                self.mem[addr + r * STRIDE + c] = fg

    def copy(self, addr: int, w: int, h: int, src_addr: int) -> None:
        """OP_COPY: fixed FB_BASE=0/STRIDE addressing for both source and dest."""
        for r in range(h):
            for c in range(w):
                self.mem[addr + r * STRIDE + c] = self.src_read(
                    src_addr + r * STRIDE + c
                )

    def blit(
        self,
        dst_addr: int,
        src_addr: int,
        w: int,
        h: int,
        dst_stride: int = STRIDE,
        src_stride: int = STRIDE,
        key_en: bool = False,
        key: int = 0,
        blend_en: bool = False,
        blend_mode: int = 0,
        blend_alpha: int = 0,
    ) -> None:
        """OP_BLIT: B1 (sticky base/stride) + B2 (colour key) + B5 (blend).
        Key wins over blend when both could apply (mp3_fb.sv's pixel_keyed
        comment: "Key takes priority over blend where both could apply").

        A keyed pixel does not simply skip its write: A_KEYDST pre-reads the
        real destination into glyphbuf first, and a keyed source word just
        never overwrites that pre-read value there -- the burst still streams
        the SAME value back out to the same address. Modelled here as
        "re-write the existing destination", not "no write", so this only
        gives the right answer when the destination was already written by
        an earlier command in the same scene (real usage always keys against
        a real background, never an untouched word)."""
        for r in range(h):
            for c in range(w):
                s = self.src_read(src_addr + r * src_stride + c)
                d_addr = dst_addr + r * dst_stride + c
                if key_en and s == key:
                    s = self.mem.get(d_addr, 0)
                elif blend_en:
                    b = self.mem.get(d_addr, 0)
                    s = blend_px(b, s, blend_mode, blend_alpha)
                self.mem[d_addr] = s

    def cblit(
        self,
        dst_addr: int,
        src_addr: int,
        w: int,
        h: int,
        clut: list[int],
        dst_stride: int = STRIDE,
        src_stride: int = STRIDE,
        reindex: int = 0,
    ) -> None:
        """OP_CBLIT (B8, PHASE_F_SPEC.md section 5 "B8 detailed design"): one
        palette index per source word (low byte), looked up in a 256-entry
        CLUT. Reuses OP_BLIT's addressing exactly; no key/blend interaction,
        mirroring OP_COPY's own established "never keys" precedent.

        `reindex` is B9 (Tier 2): an 8-bit offset added to the index before
        the lookup (wrapping mod 256, matching mp3_fb.sv's plain 8-bit adder),
        the "re-index to a shadow/highlight palette" trick instead of a blend."""
        for r in range(h):
            for c in range(w):
                s = self.src_read(src_addr + r * src_stride + c)
                idx = ((s & 0xFF) + reindex) & 0xFF
                self.mem[dst_addr + r * dst_stride + c] = clut[idx]

    def bar(self, addr: int, w: int, h: int, fg: int, bg: int, lit_rows: int) -> None:
        """OP_BAR (B6): two chained RECT fills, unlit segment on top, lit on
        the bottom -- the convention mp3_fb.sv's own comment states."""
        lit = min(lit_rows, h)
        unlit = h - lit
        self.rect(addr, w, unlit, bg)
        self.rect(addr + unlit * STRIDE, w, lit, fg)

    def sblit(
        self,
        dst_addr: int,
        src_addr: int,
        src_w: int,
        src_h: int,
        sx: int,
        sy: int,
        dst_stride: int = STRIDE,
        src_stride: int = STRIDE,
    ) -> None:
        """OP_SBLIT (B4): nearest-neighbour scaled blit via the same Bresenham
        stepper CHAR uses, against a variable source extent."""
        out_w = sblit_ext(src_w, sx)
        out_h = sblit_ext(src_h, sy)
        num_x, den_x = SCALE_ND[sx]
        num_y, den_y = SCALE_ND[sy]
        ey, acc_y = 0, 0
        for row in range(out_h):
            ex, acc_x = 0, 0
            for col in range(out_w):
                s = self.src_read(src_addr + ey * src_stride + ex)
                self.mem[dst_addr + row * dst_stride + col] = s
                acc_x += num_x
                if acc_x >= den_x:
                    acc_x -= den_x
                    ex += 1
            acc_y += num_y
            if acc_y >= den_y:
                acc_y -= den_y
                ey += 1

    def char(self, addr: int, glyph: int, fg: int, bg: int, sx: int, sy: int) -> None:
        """OP_CHAR: 4bpp coverage atlas lookup + gamma-fitted anti-aliasing
        blend (cov_weight), then the same EPX-scale Bresenham stepper SBLIT
        reuses. Address = (glyph-0x20)*32 + row*2 + half, per font_rom.v's
        own header comment."""
        char_base = (glyph - 0x20) * 32 if 0x20 <= glyph <= 0x7E else 0
        ext_w = SCALE_EXT[sx]
        ext_h = SCALE_EXT[sy]
        num_x, den_x = SCALE_ND[sx]
        num_y, den_y = SCALE_ND[sy]
        fg_r, fg_g, fg_b = (fg >> 11) & 0x1F, (fg >> 5) & 0x3F, fg & 0x1F
        bg_r, bg_g, bg_b = (bg >> 11) & 0x1F, (bg >> 5) & 0x3F, bg & 0x1F
        ey, acc_y = 0, 0
        for row in range(ext_h):
            rowlo = self.font_words[char_base + ey * 2 + 0]
            rowhi = self.font_words[char_base + ey * 2 + 1]
            rowbits = (rowhi << 32) | rowlo
            ex, acc_x = 0, 0
            for col in range(ext_w):
                cov = (rowbits >> (ex * 4)) & 0xF
                w16 = COV_WEIGHT[cov]
                inv16 = 16 - w16
                mix_r = fg_r * w16 + bg_r * inv16
                mix_g = fg_g * w16 + bg_g * inv16
                mix_b = fg_b * w16 + bg_b * inv16
                # px_color = {mix_r[8:4], mix_g[9:4], mix_b[8:4]}
                r5 = (mix_r >> 4) & 0x1F
                g6 = (mix_g >> 4) & 0x3F
                b5 = (mix_b >> 4) & 0x1F
                px = (r5 << 11) | (g6 << 5) | b5
                self.mem[addr + row * STRIDE + col] = px
                acc_x += num_x
                if acc_x >= den_x:
                    acc_x -= den_x
                    ex += 1
            acc_y += num_y
            if acc_y >= den_y:
                acc_y -= den_y
                ey += 1
