#!/usr/bin/env python3
"""Render reproducible Tau framebuffer fixtures from production UI inputs.

This host-side model deliberately has no dependency on Pillow.  It reads the
same 4bpp font ROM and advances used by ``mp3_fb.sv``, reproduces RGB565
blending and the fractional type scale, then writes the native 400x360 frame.
It is a fixture renderer, not a claim to capture the Pocket OLED: the packaged
loading asset remains the only asset-exact capture, while these screens model
the firmware's framebuffer commands from its source of truth.
"""

import argparse
import re
from pathlib import Path

from capture_splash_frame import WIDTH, HEIGHT, write_capture


ROOT = Path(__file__).resolve().parent.parent
PLAYER = (ROOT / "fw/player.c").read_text(encoding="utf-8")
METRICS = (ROOT / "fw/font_metrics.h").read_text(encoding="utf-8")
ROM = (ROOT / "src/fpga/core/font_rom.v").read_text(encoding="utf-8")

FB_W, FB_H = WIDTH, HEIGHT
HALF = {"TS_1X": 2, "TS_15X": 3, "TS_2X": 4, "TS_3X": 6}
COV_WEIGHT = (0, 4, 6, 7, 8, 10, 10, 11, 12, 13, 13, 14, 15, 15, 16, 16)


def c_uint(name):
    match = re.search(r"#define\s+" + re.escape(name) + r"\s+0x([0-9A-Fa-f]+)u", PLAYER)
    if not match:
        raise RuntimeError(f"could not read {name} from fw/player.c")
    return int(match.group(1), 16)


def c_dec(name):
    match = re.search(r"#define\s+" + re.escape(name) + r"\s+\(?([0-9]+)u?\)?", PLAYER)
    if not match:
        raise RuntimeError(f"could not read {name} from fw/player.c")
    return int(match.group(1))


def read_advances():
    match = re.search(r"font_adv\[95\]\s*=\s*\{(.*?)\};", METRICS, re.S)
    values = [int(n) for n in re.findall(r"\d+", match.group(1))]
    if len(values) != 95:
        raise RuntimeError("font metrics are not the expected 95 glyph advances")
    return values


def read_rom():
    words = {}
    for index, value in re.findall(r"mem\[\s*(\d+)\]\s*=\s*32'h([0-9A-Fa-f]{8})", ROM):
        words[int(index)] = int(value, 16)
    if len(words) != 3040:
        raise RuntimeError("font ROM is incomplete")
    return words


ADV = read_advances()
FONT = read_rom()


def glyph(ch):
    code = ord(ch)
    return code if 0x20 <= code <= 0x7E else 0x20


def rgb565_parts(color):
    return ((color >> 11) & 31, (color >> 5) & 63, color & 31)


def blend(fg, bg, weight):
    fr, fgc, fb = rgb565_parts(fg)
    br, bgc, bb = rgb565_parts(bg)
    return (((fr * weight + br * (16 - weight)) >> 4) << 11 |
            ((fgc * weight + bgc * (16 - weight)) >> 4) << 5 |
            ((fb * weight + bb * (16 - weight)) >> 4))


class Frame:
    def __init__(self):
        self.pixels = [0] * (FB_W * FB_H)

    def rect(self, x, y, width, height, color):
        x0, x1 = max(0, x), min(FB_W, x + width)
        y0, y1 = max(0, y), min(FB_H, y + height)
        for yy in range(y0, y1):
            self.pixels[yy * FB_W + x0:yy * FB_W + x1] = [color] * (x1 - x0)

    def text(self, x, y, message, scale, fg, bg, max_width):
        """Mirror fb_text_clipped: advance budget and painted-cell edge differ."""
        half = HALF[scale]
        cell = 16 * half // 2
        limit = x + max_width
        for ch in message:
            code = glyph(ch)
            advance = ADV[code - 0x20] * half // 2
            if x + advance > limit or x + cell > FB_W:
                break
            self.char(x, y, code, half, fg, bg)
            x += advance

    def char(self, x, y, code, half, fg, bg):
        out = 16 * half // 2
        base = (code - 0x20) * 32
        for oy in range(out):
            sy = min(15, oy * 2 // half)
            lo = FONT[base + sy * 2]
            hi = FONT[base + sy * 2 + 1]
            row = lo | (hi << 32)
            for ox in range(out):
                px, py = x + ox, y + oy
                if not (0 <= px < FB_W and 0 <= py < FB_H):
                    continue
                sx = min(15, ox * 2 // half)
                coverage = (row >> (sx * 4)) & 15
                self.pixels[py * FB_W + px] = blend(fg, bg, COV_WEIGHT[coverage])

    def png_pixels(self):
        result = []
        for color in self.pixels:
            r, g, b = rgb565_parts(color)
            result.append((r * 255 // 31, g * 255 // 63, b * 255 // 31))
        return result


UI_PANEL = c_uint("UI_PANEL")
UI_WHITE = c_uint("UI_WHITE")
UI_DIM = c_uint("UI_DIM")
UI_RED = c_uint("UI_RED")
UI_ACCENT = c_uint("UI_ACCENT")
UI_MARGIN = c_dec("UI_MARGIN")
UI_TITLE_Y = c_dec("UI_TITLE_Y")
UI_CARD_H = c_dec("UI_CARD_H")
UI_SPL_VER_Y = UI_TITLE_Y - 14 + UI_CARD_H - 14 - 16
APP_VER_MATCH = re.search(r'#define\s+APP_VER\s+"([^"]+)"', PLAYER)
if not APP_VER_MATCH:
    raise RuntimeError("could not read APP_VER from fw/player.c")
APP_VER = APP_VER_MATCH.group(1)


def gradient_top(accent=UI_ACCENT):
    r, g, b = rgb565_parts(accent)
    r, g, b = r * 255 // 31, g * 255 // 63, b * 255 // 31
    luma = (2126 * r + 7152 * g + 722 * b) // 10000 or 1
    r, g, b = ((r * 45 + luma // 2) // luma,
               (g * 45 + luma // 2) // luma,
               (b * 45 + luma // 2) // luma)
    r, g, b = ((r + 46) // 2, (g + 46) // 2, (b + 46) // 2)
    return (min(31, (min(255, r) * 31 + 127) // 255) << 11 |
            min(63, (min(255, g) * 63 + 127) // 255) << 5 |
            min(31, (min(255, b) * 31 + 127) // 255))


GRAD_TOP = gradient_top()


def grad_at(y):
    y = min(FB_H - 1, y)
    rem, den, threshold = FB_H - 1 - y, FB_H - 1, (1, 5, 3, 7)[y & 3]
    values = []
    for level in rgb565_parts(GRAD_TOP):
        num = level * rem
        base = num // den
        values.append(base + int((num - base * den) * 8 > threshold * den))
    return values[0] << 11 | values[1] << 5 | values[2]


def rounded_rect(frame, x, y, width, height, radius, color):
    frame.rect(x, y, width, height, color)
    for i in range(radius):
        dy, inner = radius - i, 0
        while (inner + 1) ** 2 + dy ** 2 <= radius ** 2:
            inner += 1
        cut = radius - inner
        if cut:
            for xx in (x, x + width - cut):
                frame.rect(xx, y + i, cut, 1, grad_at(y + i))
                frame.rect(xx, y + height - 1 - i, cut, 1, grad_at(y + height - 1 - i))


def source_idle_lines():
    body = PLAYER.split("static void ui_idle_screen(", 1)[1].split("\n}", 1)[0]
    colors = {"UI_WHITE": UI_WHITE, "UI_DIM": UI_DIM, "ui_accent": UI_ACCENT}
    return [(int(y), message, colors[color], scale)
            for y, message, color, scale in re.findall(
                r'ui_gs_line\(\s*(\d+)u,\s*"([^"]*)"\s*,\s*(\w+)\s*,\s*(TS_\w+)\s*\)', body)]


def idle(reason=None):
    frame = Frame()
    for y in range(FB_H):
        frame.rect(0, y, FB_W, 1, grad_at(y))
    rounded_rect(frame, UI_MARGIN - 8, UI_TITLE_Y - 14, FB_W - 2 * UI_MARGIN + 16,
                 UI_CARD_H, 8, UI_PANEL)
    frame.text(UI_MARGIN, UI_SPL_VER_Y, "v" + APP_VER, "TS_1X", UI_DIM, UI_PANEL,
               FB_W - 2 * UI_MARGIN)
    frame.text(UI_MARGIN, UI_TITLE_Y, "TAU", "TS_2X", UI_ACCENT, UI_PANEL, FB_W - 2 * UI_MARGIN)
    if reason:
        frame.text(UI_MARGIN, 144, reason, "TS_1X", UI_RED, grad_at(144), FB_W - 2 * UI_MARGIN)
    for y, message, color, scale in source_idle_lines():
        frame.text(UI_MARGIN, y, message, scale, color, grad_at(y), FB_W - 2 * UI_MARGIN)
    return frame


FIXTURES = {
    "empty-library": lambda: idle(),
    "playlist-error": lambda: idle("No playable tracks in playlist"),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("fixture", choices=FIXTURES)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    write_capture(args.output, FIXTURES[args.fixture]().png_pixels())
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
