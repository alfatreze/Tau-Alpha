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
import math
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
UI_TRACK = c_uint("UI_TRACK")
UI_FAINT = c_uint("UI_FAINT")
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


def rounded_rect_on(frame, x, y, width, height, radius, color, background):
    """Mirror fb_round_rect_on(), whose corner cuts reveal its parent panel."""
    frame.rect(x, y, width, height, color)
    for i in range(radius):
        dy, inner = radius - i, 0
        while (inner + 1) ** 2 + dy ** 2 <= radius ** 2:
            inner += 1
        cut = radius - inner
        if cut:
            for xx in (x, x + width - cut):
                frame.rect(xx, y + i, cut, 1, background)
                frame.rect(xx, y + height - 1 - i, cut, 1, background)


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


def paint_gradient(frame):
    for y in range(FB_H):
        frame.rect(0, y, FB_W, 1, grad_at(y))


def draw_progress(frame, done):
    """Static representative of ui_draw_dynamic()'s progress-bar branch."""
    x, y, width, height = UI_MARGIN, 334, FB_W - 2 * UI_MARGIN, 5
    bg = grad_at(y)
    frame.rect(x, y - 3, width, height + 6, bg)
    if done:
        frame.rect(x, y, done, height, UI_ACCENT)
        frame.rect(x, y, done, 1, blend(UI_WHITE, UI_ACCENT, 5))
    frame.rect(x + done, y, width - done, height, UI_TRACK)
    radius = height // 2
    for i in range(radius):
        dy, inner = radius - i, 0
        while (inner + 1) ** 2 + dy ** 2 <= radius ** 2:
            inner += 1
        cut = radius - inner
        if cut:
            for xx in (x, x + width - cut):
                frame.rect(xx, y + i, cut, 1, bg)
                frame.rect(xx, y + height - 1 - i, cut, 1, bg)
    knob = max(x + 2, min(x + width - 3, x + done))
    frame.rect(knob - 2, y - 3, 5, height + 6, UI_WHITE)


def draw_visualizer(frame, mode):
    """Frozen review instances for the firmware's eleven meter families."""
    x0, y0, width, height = UI_MARGIN, 173, FB_W - 2 * UI_MARGIN, 72
    def background():
        for yy in range(y0, y0 + height):
            frame.rect(x0, yy, width, 1, grad_at(yy))
    def level(index, count=36):
        return 6 + ((index * 19 + 13) % 57)
    background()
    if mode == "bars":
        count, gap = 36, 2
        bar_w = (width - gap * (count - 1)) // count
        for index in range(count):
            x, h = x0 + index * (bar_w + gap), level(index)
            frame.rect(x, y0 + height - h, bar_w, h,
                       blend(UI_ACCENT, UI_TRACK, (index + 1) * 16 // count))
    elif mode == "waterfall":
        for xx in range(width):
            h = 3 + ((xx * 11 + 23) % height)
            color = blend(UI_ACCENT, UI_TRACK, h * 16 // height)
            frame.rect(x0 + xx, y0 + height - h, 1, h, color)
            frame.rect(x0 + xx, y0 + height - h, 1, 1, UI_WHITE)
    elif mode == "levels":
        for i, fraction in enumerate((58, 81)):
            y = y0 + i * 25
            amount = width * fraction // 100
            frame.rect(x0, y, amount, 23, UI_ACCENT)
            frame.rect(x0 + amount, y, width - amount, 23, UI_TRACK)
            frame.rect(x0 + min(width - 1, amount + 15), y, 1, 23, UI_WHITE)
    elif mode == "phase-scope":
        cx, cy = x0 + width // 2, y0 + height // 2
        frame.rect(cx, y0, 1, height, UI_TRACK)
        frame.rect(x0, cy, width, 1, UI_TRACK)
        for i in range(96):
            a = i * math.pi * 2 / 96
            px = cx + int(math.sin(a * 3) * width * .22)
            py = cy - int(math.sin(a * 2) * height * .38)
            frame.rect(px, py, 2, 2, blend(UI_ACCENT, grad_at(py), 12))
    elif mode == "oscilloscope":
        cy = y0 + height // 2
        frame.rect(x0, cy, width, 1, UI_TRACK)
        previous = cy
        for xx in range(width):
            yy = cy - int(math.sin(xx * .15) * 22 + math.sin(xx * .043) * 9)
            top, bottom = min(previous, yy), max(previous, yy)
            frame.rect(x0 + xx, top, 1, bottom - top + 2,
                       blend(UI_ACCENT, UI_TRACK, xx * 16 // width))
            previous = yy
    elif mode == "waveform":
        cy = y0 + height // 2
        for xx in range(width):
            h = 1 + int((math.sin(xx * .10) + 1) * 13)
            frame.rect(x0 + xx, cy - h, 1, h * 2 + 1,
                       blend(UI_ACCENT, UI_TRACK, h * 16 // 28))
    elif mode == "mirrored-bars":
        count, gap, cy = 36, 2, y0 + height // 2
        bar_w = (width - gap * (count - 1)) // count
        for index in range(count):
            h, x = level(index) // 2, x0 + index * (bar_w + gap)
            frame.rect(x, cy - h, bar_w, h * 2 + 1,
                       blend(UI_ACCENT, UI_TRACK, (index + 1) * 16 // count))
    elif mode == "peak-dots":
        count, gap = 36, 2
        bar_w = (width - gap * (count - 1)) // count
        for index in range(count):
            h, x = level(index), x0 + index * (bar_w + gap)
            frame.rect(x, y0 + height - h, bar_w, 2,
                       blend(UI_ACCENT, UI_TRACK, (index + 1) * 16 // count))
    elif mode == "magic-eye":
        tube_w, gap, tx = 46, 24, x0 + (width - 116) // 2
        for channel in range(2):
            x, lit = tx + channel * (tube_w + gap), 42 + channel * 12
            frame.rect(x, y0 + 6, tube_w, 58, blend(UI_ACCENT, UI_TRACK, 5))
            frame.rect(x + 13, y0 + 64 - lit, 20, lit, UI_ACCENT)
            frame.rect(x + 9, y0 + 65, 28, 5, blend(UI_ACCENT, UI_TRACK, 5))
    elif mode == "spectrum":
        columns, rows = 8, 8
        col_w = width // columns
        for col in range(columns):
            lit = 2 + ((col * 5 + 3) % 7)
            for row in range(rows):
                color = (blend(UI_ACCENT, UI_TRACK, min(16, (row + 2) * 16 // rows))
                         if row < lit else UI_TRACK)
                frame.rect(x0 + col * col_w, y0 + height - (row + 1) * 8,
                           col_w - 3, 6, color)
    elif mode == "vu":
        half = width // 2
        for channel, needle in enumerate((.36, .69)):
            ox, pivot_x, pivot_y = x0 + channel * half, x0 + channel * half + half // 2, y0 + height - 4
            radius = height - 14
            for tick in range(81):
                theta = -1.05 + 2.10 * tick / 80
                px = pivot_x + int(math.sin(theta) * radius)
                py = pivot_y - int(math.cos(theta) * radius)
                color = UI_ACCENT if tick >= 60 else blend(UI_ACCENT, grad_at(py), 6)
                frame.rect(px, py, 2, 2, color)
            for tick in range(5):
                theta = -1.05 + 2.10 * tick / 4
                for distance in range(radius - 4, radius):
                    px = pivot_x + int(math.sin(theta) * distance)
                    py = pivot_y - int(math.cos(theta) * distance)
                    frame.rect(px, py, 1, 1, UI_ACCENT if tick >= 3 else blend(UI_ACCENT, grad_at(py), 9))
            frame.text(ox + 6, y0 + 2, "L" if channel == 0 else "R", "TS_1X",
                       UI_ACCENT, grad_at(y0 + 2), 16)
            theta = -1.05 + 2.10 * needle
            for step in range(2, 29):
                distance = (radius - 6) * step // 28
                px = pivot_x + int(math.sin(theta) * distance)
                py = pivot_y - int(math.cos(theta) * distance)
                frame.rect(px, py, 2 if step < 23 else 1, 2 if step < 23 else 1, UI_ACCENT)
            frame.rect(pivot_x - 2, pivot_y - 2, 5, 5, UI_ACCENT)
    else:
        raise ValueError(f"unsupported visualizer mode: {mode}")


def now_playing_base(state="playing", seeking=False, title="NIGHT DRIVE",
                     artist="Tau Test Artist", album="TAU TESTS - 2026",
                     format_line="320 kbps - 44.1 kHz - LAME", toast=None,
                     visualizer="bars"):
    """Deterministic no-art instance of ui_draw_chrome + dynamic UI rows."""
    if state not in {"playing", "paused", "stopped"}:
        raise ValueError(f"unsupported transport state: {state}")
    frame = Frame()
    paint_gradient(frame)
    # ui_draw_chrome's 352px text card.  This no-art fixture keeps the full
    # waveform width, matching art_shown == 0 in the firmware.
    rounded_rect(frame, UI_MARGIN - 8, UI_TITLE_Y - 14, 368, UI_CARD_H, 8, UI_PANEL)
    frame.text(UI_MARGIN, UI_TITLE_Y, title, "TS_2X", UI_WHITE, UI_PANEL, 352)
    info_y = 68
    if artist:
        frame.text(UI_MARGIN, info_y, artist, "TS_15X", UI_DIM, UI_PANEL, 352)
        info_y += 27
    if album:
        frame.text(UI_MARGIN, info_y, album, "TS_1X", UI_DIM, UI_PANEL, 352)
        info_y += 18
    if format_line:
        frame.text(UI_MARGIN, info_y, format_line, "TS_1X", UI_FAINT, UI_PANEL, 352)

    draw_visualizer(frame, visualizer)
    transport_color = (UI_ACCENT if state == "playing" else
                       (UI_WHITE if state == "stopped" else blend(UI_WHITE, UI_PANEL, 16)))
    frame.text(UI_MARGIN, 262, state.upper(), "TS_1X", transport_color, grad_at(262), 80)
    # The three chevrons are the same geometry-driven affordance as the RTL UI;
    # at a frozen review moment, all use the steady accent rather than animation.
    if state == "playing":
        for base in (110, 122, 134):
            for row in range(13):
                inset = abs(6 - row) // 2
                frame.rect(base + inset, 262 + row, max(1, 8 - inset * 2), 1, UI_ACCENT)
    elif state == "paused":
        frame.rect(110, 263, 4, 12, transport_color)
        frame.rect(117, 263, 4, 12, transport_color)
    else:
        frame.rect(110, 264, 10, 10, transport_color)
    frame.text(UI_MARGIN, 288, "1:12 / 3:48", "TS_15X", UI_WHITE, grad_at(288), 360)
    done = 126 if seeking else 113
    draw_progress(frame, done)
    if seeking:
        toast_y = 314
        frame.text(UI_MARGIN, toast_y, "SEEK +10s", "TS_1X", UI_WHITE, grad_at(toast_y),
                   FB_W - 2 * UI_MARGIN)
    elif toast:
        toast_y = 314
        frame.text(UI_MARGIN, toast_y, toast, "TS_1X", UI_WHITE, grad_at(toast_y),
                   FB_W - 2 * UI_MARGIN)
    return frame


def playlist_browser():
    """pl_ui_draw() fixture layered over the same frozen now-playing frame."""
    frame = now_playing_base()
    x, width, y, rows, row_h, list_y, pad_b = 12, 376, 18, 9, 20, 52, 22
    text_x, selected, playing, top, count = x + 10, 3, 1, 0, 12
    entries = ("01 - Welcome Home", "02 - Night Drive", "03 - Sunset Sequence",
               "04 - Ocean Between Us", "05 - Echoes", "06 - Golden Hour",
               "07 - Low Battery", "08 - Neon Rain", "09 - Last Light")
    panel_h = list_y + rows * row_h + pad_b - y
    rounded_rect(frame, x, y, width, panel_h, 8, UI_PANEL)
    frame.text(text_x, y + 10, f"PLAYLIST  {selected + 1} / {count}", "TS_1X",
               UI_ACCENT, UI_PANEL, width - 20)
    track_x, track_y, track_h = x + width - 11, list_y - 2, rows * row_h
    frame.rect(track_x, track_y, 3, track_h, blend(UI_DIM, UI_PANEL, 5))
    thumb_h = track_h * rows // count
    frame.rect(track_x, track_y + (track_h - thumb_h) * top // (count - rows),
               3, thumb_h, UI_ACCENT)
    for i, label in enumerate(entries):
        row_y = list_y + i * row_h
        selected_row = i == selected
        background = UI_ACCENT if selected_row else UI_PANEL
        if selected_row:
            rounded_rect_on(frame, x + 4, row_y - 2, width - 8, row_h, 5,
                            background, UI_PANEL)
        else:
            frame.rect(x + 4, row_y - 2, width - 8, row_h, background)
        foreground = UI_PANEL if selected_row else (UI_WHITE if i == playing else UI_DIM)
        if i == playing:
            frame.text(x + 8, row_y, ">", "TS_1X", foreground, background, 12)
        frame.text(text_x + 8, row_y, label, "TS_1X", foreground, background, width - 40)
    return frame


FIXTURES = {
    "empty-library": lambda: idle(),
    "playlist-error": lambda: idle("No playable tracks in playlist"),
    "now-playing": now_playing_base,
    "playlist-browser": playlist_browser,
    "paused": lambda: now_playing_base("paused"),
    "stopped": lambda: now_playing_base("stopped"),
    "seeking": lambda: now_playing_base("playing", seeking=True),
    "metadata-long": lambda: now_playing_base(
        title="THE EXTREMELY LONG TITLE THAT STARTS A MARQUEE",
        artist="A VERY LONG ARTIST NAME FOR FIXTURE REVIEW",
        album="LONG ALBUM NAME - 2026"),
    "metadata-missing": lambda: now_playing_base(
        title="untagged-demo-track", artist="", album="", format_line=""),
    "toast": lambda: now_playing_base(toast="VOLUME 70%"),
    **{f"visualizer-{name}": (lambda mode=name: now_playing_base(visualizer=mode))
       for name in ("bars", "waterfall", "levels", "phase-scope", "oscilloscope",
                    "waveform", "mirrored-bars", "peak-dots", "magic-eye", "spectrum", "vu")},
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
