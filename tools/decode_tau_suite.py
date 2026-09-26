#!/usr/bin/env python3
"""Decoder (and reference encoder) for the Tau diagnostics report (fw/suite_core.h, docs/TEST_SUITE_SPEC.md).

  decode_tau_suite.py --text 'TAUD1:...'          full report from the QR text (also reads it from stdin with '-')
  decode_tau_suite.py --qr screenshot.png         decode the QR code in a screenshot (needs OpenCV; use the PNG, not a resized copy)
  decode_tau_suite.py --interact persist.json     the four-word summary from Settings/<core>/Interact/_core/interact_persist.json
  decode_tau_suite.py --words A B C D             the same from four numbers
  decode_tau_suite.py --code 'XXXXXX-...'         the 36-character short code (summary plus bitstream revision)
Add --json for machine output. Exit 0 = record valid, 1 = invalid (bad CRC or format), 2 = usage."""
import argparse
import base64
import json
import struct
import sys
import zlib

FMT = 1
PROFILES = {0: "none", 1: "USER CHECK", 2: "QUICK", 3: "STANDARD", 4: "FULL", 5: "ENDURANCE", 6: "CUSTOM", 7: "RESTART-SET",
            8: "BLIT TEST"}
VERDICTS = {0: "no result", 1: "all checks passed", 2: "some checks failed", 3: "check incomplete"}
RESULTS = {0: "PASS", 1: "FAIL", 2: "SKIPPED", 3: "N/A"}
TESTS = {0: "SDRAM window test", 1: "SDRAM read/write cost", 2: "PSRAM window test", 3: "Cold code test",
         4: "Playlist / library check", 5: "Playback counters", 6: "Timings", 7: "Stress R1 (30 s)",
         8: "Stress R2 (30 s)", 9: "Stress R3 (30 s)", 10: "Soak", 11: "Track changes (10)", 12: "Cold code x20",
         13: "Blit storm (30 s)",   # B-127, CT_BLT: needs live playback like id 5, or reports N/A/SKIP the same way
         14: "Cold frame (30 s)"}   # B-199/B-200, CT_COLDFRAME: worst single-call cost of a synthetic cold probe
                                     # called once per ui_draw_dynamic() (~38 Hz); diagnostic-only, TAU_COLD_FRAME_PROBE builds
TAGS = {1: "build", 2: "memory", 3: "test", 4: "sdram", 5: "psram", 6: "cold", 7: "time", 8: "audio", 9: "library",
        10: "settings", 11: "errors", 12: "notes", 13: "decprof", 14: "decsweep", 15: "blittest", 16: "stack"}
BLIT_OPS = ["RUN", "RECT", "CHAR", "COPY", "BLIT", "BAR", "SBLIT", "CBLIT"]
# meters/*/meter.json (meter module M0, tools/gen_meters.py) index order -- the VIZ_* enum.
VIZ_NAMES = ["BARS", "WATERFALL", "-", "PHASE SCOPE", "OSCILLOSCOPE", "VU", "WAVEFORM", "-", "PEAK DOTS", "-",
             "SPECTRUM", "-", "WINAMP BARS", "WINAMP SCOPE", "CHLADNI"]
B32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"


def crc16(b: bytes) -> int:
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


# ---------------------------------------------------------------- record
def build_record(profile: int, entries: list[tuple[int, bytes]]) -> bytes:
    body = b"TD" + bytes([FMT, profile]) + b"".join(bytes([t, len(v)]) + v for t, v in entries)
    return body + struct.pack("<I", zlib.crc32(body))


def to_text(rec: bytes) -> str:
    return "TAUD1:" + base64.urlsafe_b64encode(rec).rstrip(b"=").decode()


def from_text(txt: str) -> bytes:
    txt = "".join(txt.split())
    if not txt.startswith("TAUD1:"):
        raise ValueError("not a TAUD1 report")
    s = txt[6:]
    return base64.urlsafe_b64decode(s + "=" * (-len(s) % 4))


def parse_record(rec: bytes) -> dict:
    if len(rec) < 8 or rec[:2] != b"TD":
        raise ValueError("bad magic")
    if zlib.crc32(rec[:-4]) != struct.unpack("<I", rec[-4:])[0]:
        raise ValueError("CRC mismatch (record damaged or incomplete)")
    if rec[2] != FMT:
        raise ValueError(f"unsupported format {rec[2]}")
    out = {"format": rec[2], "profile": PROFILES.get(rec[3], f"profile {rec[3]}"), "tests": [], "entries": {}, "unknown": []}
    i, end = 4, len(rec) - 4
    while i < end:
        if i + 2 > end or i + 2 + rec[i + 1] > end:
            raise ValueError("truncated entry")
        tag, n = rec[i], rec[i + 1]
        v = rec[i + 2:i + 2 + n]
        i += 2 + n
        if tag == 3 and n == 6:
            tid, res, val = v[0], v[1], struct.unpack("<I", v[2:])[0]
            entry = {"id": tid, "name": TESTS.get(tid, f"test {tid}"), "result": RESULTS.get(res, str(res)), "value": val}
            if tid == 13:   # CT_BLT (B-127/B-139): value packs busy-permille (low 16 bits) + "audio ran the whole
                            # window" in bit 16, since a PASS/FAIL here only means something if playback didn't drop
                busy = val & 0xFFFF
                entry["busy_permille"] = None if busy == 0xFFFF else busy   # 0xFFFF = TAU_SDRAM_BUSY off for this bitstream
                entry["audio_full"] = bool(val & 0x10000)
            elif tid == 11 and entry["result"] == "FAIL":   # CT_TRK (B-165): a FAIL packs why, not just that it
                                                             # happened -- see CT_TRK's own comment in fw/suite.inc
                entry["changes_done"] = val & 0xFF
                entry["queue_len"] = (val >> 8) & 0xFFFF
                entry["lib_src"] = bool(val & (1 << 24))
                entry["skip_req_pending"] = bool(val & (1 << 25))
            out["tests"].append(entry)
        elif tag == 14 and n >= 10:              # Decode Profile Sweep: one entry per track (B-090/B-091/B-092/B-096), repeatable
            track_idx, speed_pct = v[0], v[1]
            h, i2, s, r = (int.from_bytes(v[k:k + 2], "little") for k in range(2, 10, 2))
            title = v[10:n].decode("ascii", "replace")   # truncated, not NUL-terminated (B-096)
            out["entries"].setdefault("decsweep", []).append(
                {"track": track_idx, "title": title, "speed_pct": speed_pct, "h_pct": h, "i_pct": i2, "s_pct": s, "r_pct": r})
        elif tag == 15 and n == 6:               # Blit Test: one entry per (opcode, level), repeatable (B-166)
            op_id, level, res = v[0], v[1], v[2]
            ops_done = int.from_bytes(v[4:6], "little")
            out["entries"].setdefault("blittest", []).append(
                {"op": BLIT_OPS[op_id] if op_id < len(BLIT_OPS) else f"op{op_id}", "level": level,
                 "result": RESULTS.get(res, str(res)), "stall_pct": v[3], "ops_done": ops_done})
        elif tag == 17 and n == 13:               # SR_T_WVIZCFG (B-218): a one-off export of the Winamp
                                                    # Bars/Scope Configure page's live wviz_cfg, not part
                                                    # of a Check run -- see fw/suite_core.h's own comment
            peak_hold_ms = int.from_bytes(v[8:10], "little")
            out["entries"]["wvizcfg"] = {
                "mode": "scope" if v[0] else "bars",
                "preset": None if v[1] == 0xFF else v[1],
                "bands": v[2],
                "ease_mode": ["instant", "linear", "exponential", "spring"][v[3]] if v[3] < 4 else v[3],
                "attack": v[4], "release": v[5],
                "peak_on": bool(v[6]), "peak_gravity": bool(v[7]),
                "peak_hold_ms": peak_hold_ms, "peak_fall": v[10],
                "scope_smooth": v[11], "scope_trail": v[12],
            }
        elif tag in TAGS:
            w = {1: 4, 2: 2, 3: 4, 4: 2, 5: 2, 6: 2, 7: 4, 8: 2, 9: 4, 10: 1, 11: 1, 12: 1, 13: 2, 14: 1, 16: 4}[tag]
            if tag == 1:
                fw, rev, flags, gap = (struct.unpack("<I", v[k:k + 4])[0] for k in range(0, 16, 4))
                out["entries"]["build"] = {"firmware": f"{fw >> 16 & 255}.{fw >> 8 & 255}.{fw & 255}", "bitstream": f"{rev:08X}",
                                           "flags": flags, "heap_gap": gap}
            else:
                vals = [int.from_bytes(v[k:k + w], "little") for k in range(0, n - n % w, w)]
                if tag in (4, 5) and len(vals) == 6:       # cycles per access, best / average / worst (Check from B-060)
                    vals = dict(zip(("read_min", "read_avg", "read_max", "write_min", "write_avg", "write_max"), vals))
                elif tag in (4, 5) and len(vals) == 4:     # first runs (B-056..B-059): average and worst only
                    vals = dict(zip(("read_avg", "read_max", "write_avg", "write_max"), vals))
                elif tag == 13 and len(vals) == 4:         # decoder stage cost, CT_AUD window (B-088/B-089)
                    vals = dict(zip(("h_pct", "i_pct", "s_pct", "r_pct"), vals))
                elif tag == 8 and len(vals) == 4:          # SR_T_AUDIO, CT_AUD (word[1] repurposed by B-139:
                                                            # 1 if playback ran the whole window, 0 if it never
                                                            # started or dropped out partway through)
                    vals = dict(zip(("late_underruns", "audio_full", "stall_ms", "window_s"), vals))
                    vals["audio_full"] = bool(vals["audio_full"])
                elif tag == 16 and len(vals) == 2:         # SR_T_STACK (B-204): stack high-water mark
                    vals = dict(zip(("peak_bytes", "stack_size"), vals))
                    vals["free_bytes"] = vals["stack_size"] - vals["peak_bytes"]
                out["entries"][TAGS[tag]] = vals
        else:
            out["unknown"].append({"tag": tag, "hex": v.hex()})
    fails = [t for t in out["tests"] if t["result"] == "FAIL"]
    out["verdict"] = VERDICTS[2] if fails else (VERDICTS[1] if out["tests"] else VERDICTS[0])
    return out


# ---------------------------------------------------------------- persisted summary
def unpack_words(w: list[int]) -> dict:
    if len(w) != 4 or any(not 0 <= x <= 0x7FFFFFFF for x in w):
        raise ValueError("need four values in 0..2^31-1")
    if w[0] & 15 != FMT:
        raise ValueError(f"summary format {w[0] & 15} unsupported (empty or another record)")
    pm, fm = w[1] & 0x7FFF, w[1] >> 15 & 0x7FFF
    return {"profile": PROFILES.get(w[0] >> 4 & 7, "?"), "run": w[0] >> 7 & 255, "verdict": VERDICTS[w[0] >> 15 & 3],
            "passed": [TESTS.get(i, f"test {i}") for i in range(15) if pm >> i & 1],
            "failed": [TESTS.get(i, f"test {i}") for i in range(15) if fm >> i & 1],
            "worst_access_cycles": w[2] & 511, "cold_cycles_per_word": (w[2] >> 9 & 511) / 10, "late_underruns": w[2] >> 18 & 63,
            "draw_stall_ms": w[2] >> 24 & 127, "last_load_s": (w[3] & 4095) / 10, "library_error": w[3] >> 12 & 63,
            "cold_error": w[3] >> 18 & 63, "firmware_minor": w[3] >> 24 & 127}


def words_from_interact(doc: dict, ids=(20, 21, 22, 23)) -> list[int]:
    by = {v["id"]: v["val"] for v in doc["interact_persist"]["variables"]}
    return [by[i] for i in ids]


def short_code(w: list[int], bitstream: int) -> str:
    b = b"".join(struct.pack("<I", x) for x in w) + struct.pack("<I", bitstream)
    b += struct.pack("<H", crc16(b))
    acc = nb = 0
    out = ""
    for x in b:
        acc = acc << 8 | x
        nb += 8
        while nb >= 5:
            out += B32[acc >> (nb - 5) & 31]
            nb -= 5
        acc &= (1 << nb) - 1
    if nb:
        out += B32[acc << (5 - nb) & 31]
    return out


def from_short(code: str) -> tuple[list[int], int]:
    c = "".join(code.split()).replace("-", "").upper().replace("O", "0").replace("I", "1").replace("L", "1")
    if len(c) != 36 or any(ch not in B32 for ch in c):
        raise ValueError("a short code has 36 characters from 0-9 A-Z (no I L O U)")
    acc = nb = 0
    b = bytearray()
    for ch in c:
        acc = acc << 5 | B32.index(ch)
        nb += 5
        if nb >= 8:
            b.append(acc >> (nb - 8) & 255)
            nb -= 8
            acc &= (1 << nb) - 1
    b = bytes(b[:22])
    if crc16(b[:20]) != struct.unpack("<H", b[20:])[0]:
        raise ValueError("short code checksum mismatch (typo?)")
    return list(struct.unpack("<4I", b[:16])), struct.unpack("<I", b[16:20])[0]


def qr_text(path: str) -> str:
    import cv2
    img = cv2.imread(path)
    if img is None:
        raise ValueError(f"cannot read {path}")
    # Dense codes (2 px modules) defeat OpenCV's classic detector even for reference symbols; the Aruco-based detector reads
    # them once the pixels are doubled with nearest neighbour (never smoothing). Try it first, then the classic one.
    dets = [cv2.QRCodeDetectorAruco(), cv2.QRCodeDetector()] if hasattr(cv2, "QRCodeDetectorAruco") else [cv2.QRCodeDetector()]
    txt = ""
    for det in dets:
        for scale in (1, 2, 3, 4):
            big = img if scale == 1 else cv2.resize(img, None, fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST)
            txt, _, _ = det.detectAndDecode(big)
            if txt:
                break
        if txt:
            break
    if not txt:
        raise ValueError("no QR code found (screenshot must be the original PNG)")
    return txt


def show(d: dict) -> str:
    return json.dumps(d, indent=2)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--text")
    g.add_argument("--qr")
    g.add_argument("--interact")
    g.add_argument("--words", nargs=4, type=int)
    g.add_argument("--code")
    ap.add_argument("--ids", default="20,21,22,23", help="interact variable ids of the four words")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    try:
        if a.text or a.qr:
            txt = a.text if a.text else qr_text(a.qr)
            if txt == "-":
                txt = sys.stdin.read()
            res = parse_record(from_text(txt))
        elif a.code:
            w, rev = from_short(a.code)
            res = unpack_words(w) | {"bitstream": f"{rev:08X}"}
        elif a.words:
            res = unpack_words(a.words)
        else:
            res = unpack_words(words_from_interact(json.load(open(a.interact)), tuple(int(x) for x in a.ids.split(","))))
    except (ValueError, KeyError, ImportError) as e:
        print(f"invalid: {e}", file=sys.stderr)
        return 1
    print(show(res))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
