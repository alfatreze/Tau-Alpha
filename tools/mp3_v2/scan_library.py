#!/usr/bin/env python3
"""MP3 v2 WP-03: read-only header scan of an MP3 library (own code, written from the frame layout of ISO/IEC 11172-3 / 13818-3 and the public
Xing/Info/LAME and Fraunhofer VBRI tag layouts; no third-party code). Nothing in the library is written or modified.

Per file: MPEG version / layer / bitrates / sample rate / channel mode (with mode extension: intensity and MS), CRC (protection) frames, free format,
CBR or VBR, Xing/Info/VBRI header (frame count, TOC, LAME delay and padding), ID3v2 (size, unsynchronisation, extended header, footer flag), ID3v1 and
APEv2 footers, junk (leading, between frames, trailing), side information of every frame: window switching, block types, mixed blocks, and the frames
that would fall short of bit-reservoir data (decoder semantics: a frame's main_data_begin must not exceed the unconsumed bytes the previous frame left,
at most 511); the Xing TOC error against the true frame offsets (plan unknown U7); clipping potential (peak and rail samples) from the float oracle
(tools/mp3_v2/oracle.py) when it can be built.

  scan_library.py [--json OUT.json] [--markdown OUT.md] [--no-peak] [--quiet] [PATH ...]
PATH is a directory (searched recursively for .mp3) or a file; without a PATH the owner's two known locations are used when they exist.
Library use: scan_bytes(data) -> dict for one file's bytes; summarise(list of dicts) -> aggregate counts; markdown(summary) -> table text."""
import json, math, os, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

BR = {1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
      2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]}
SR = {1: [44100, 48000, 32000], 2: [22050, 24000, 16000], 25: [11025, 12000, 8000]}
VERSION_NAME = {1: "MPEG-1", 2: "MPEG-2", 25: "MPEG-2.5"}
MODE_NAME = ["stereo", "joint stereo", "dual channel", "mono"]
DEFAULT_LIBRARIES = ["/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/test music", "/Users/abel.santos/Downloads/DEV PROJECTS/test music"]


# ---------------------------------------------------------------- frame header
def parse_header(b):
    """4 bytes -> dict, or None when they are not a valid MPEG audio frame header (any layer)."""
    if len(b) < 4 or b[0] != 0xFF or (b[1] & 0xE0) != 0xE0: return None
    vbits = (b[1] >> 3) & 3
    if vbits == 1: return None                                   # reserved
    version = {3: 1, 2: 2, 0: 25}[vbits]
    layer = 4 - ((b[1] >> 1) & 3)
    if layer == 4: return None                                   # reserved
    br_idx, sr_idx = b[2] >> 4, (b[2] >> 2) & 3
    if br_idx == 15 or sr_idx == 3: return None
    mode = b[3] >> 6
    h = {"version": version, "layer": layer, "crc": (b[1] & 1) == 0, "br_idx": br_idx, "sr_idx": sr_idx, "hz": SR[version][sr_idx],
         "padding": (b[2] >> 1) & 1, "mode": mode, "mode_ext": (b[3] >> 4) & 3, "emphasis": b[3] & 3, "channels": 1 if mode == 3 else 2,
         "free": br_idx == 0, "copyright": (b[3] >> 3) & 1, "original": (b[3] >> 2) & 1}
    h["intensity"] = layer == 3 and mode == 1 and bool(b[3] & 0x10)      # MPEG-1 and LSF Layer III: bit 4 of the mode extension
    h["ms"] = layer == 3 and mode == 1 and bool(b[3] & 0x20)
    if layer == 3 and not h["free"]:
        h["kbps"] = BR[1 if version == 1 else 2][br_idx]
        h["len"] = (144 if version == 1 else 72) * h["kbps"] * 1000 // h["hz"] + h["padding"]
    elif layer == 3:
        h["kbps"] = 0; h["len"] = 0
    else:
        h["kbps"] = None; h["len"] = 0                            # Layer I/II: recorded, not walked
    h["samples"] = 1152 if version == 1 else 576
    return h


def compatible(a, b):
    """same stream: version, layer and sample rate (bitrate, mode and padding may change from frame to frame)"""
    return a is not None and b is not None and a["version"] == b["version"] and a["layer"] == b["layer"] and a["sr_idx"] == b["sr_idx"]


def side_info_size(h): return (17 if h["channels"] == 2 else 9) if h["version"] != 1 else (32 if h["channels"] == 2 else 17)


class Bits:
    def __init__(self, b): self.b, self.pos = b, 0
    def get(self, n):
        v = 0
        for _ in range(n):
            byte = self.b[self.pos >> 3] if (self.pos >> 3) < len(self.b) else 0
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1); self.pos += 1
        return v


def parse_side_info(h, si):
    """side information of one Layer III frame -> {main_data_begin, grs: [[(part23, ws, block_type, mixed), ...per channel] per granule]}"""
    r = Bits(si); v1 = h["version"] == 1; ch = h["channels"]
    mdb = r.get(9 if v1 else 8); r.get((5 if ch == 1 else 3) if v1 else (1 if ch == 1 else 2))
    if v1:
        for _ in range(ch): r.get(4)
    grs = []
    for _ in range(2 if v1 else 1):
        row = []
        for _ in range(ch):
            part23 = r.get(12); r.get(9); r.get(8); r.get(4 if v1 else 9)
            ws = r.get(1); bt = 0; mixed = 0
            if ws:
                bt = r.get(2); mixed = r.get(1); r.get(10); r.get(9)
            else:
                r.get(15); r.get(4); r.get(3)
            if v1: r.get(1)
            r.get(2)
            row.append((part23, ws, bt, mixed))
        grs.append(row)
    return {"main_data_begin": mdb, "grs": grs}


# ---------------------------------------------------------------- tags
def synchsafe(b): return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]


def id3v2_at(data, p):
    if data[p:p + 3] != b"ID3" or len(data) < p + 10: return None
    flags = data[p + 5]
    if any(x & 0x80 for x in data[p + 6:p + 10]): return None
    size = synchsafe(data[p + 6:p + 10]) + 10 + (10 if flags & 0x10 else 0)
    return {"major": data[p + 3], "unsync": bool(flags & 0x80), "ext": bool(flags & 0x40), "footer": bool(flags & 0x10), "bytes": size}


def vbr_tag(data, p, h):
    """Xing/Info (+LAME) or VBRI in the frame at p -> dict or None"""
    off = p + 4 + (2 if h["crc"] else 0) + side_info_size(h)
    end = p + h["len"]
    tag = data[off:off + 4]
    if tag in (b"Xing", b"Info") and off + 8 <= end:
        flags = data[off + 7]; q = off + 8; t = {"kind": tag.decode(), "frames": None, "bytes": None, "toc": None, "delay": None, "padding": None, "encoder": None}
        if flags & 1: t["frames"] = struct.unpack(">I", data[q:q + 4])[0]; q += 4
        if flags & 2: t["bytes"] = struct.unpack(">I", data[q:q + 4])[0]; q += 4
        if flags & 4: t["toc"] = list(data[q:q + 100]); q += 100
        if flags & 8: q += 4
        if q + 24 <= end and data[q] != 0:
            t["encoder"] = bytes(data[q:q + 9]).decode("latin-1", "replace").rstrip("\x00 ")
            b = data[q + 21:q + 24]
            t["delay"] = (b[0] << 4) | (b[1] >> 4); t["padding"] = ((b[1] & 15) << 8) | b[2]
        return t
    if data[p + 36:p + 40] == b"VBRI" and p + 36 + 18 <= end:
        ver, delay, quality, nbytes, nframes = struct.unpack(">HHHII", data[p + 40:p + 54])
        return {"kind": "VBRI", "frames": nframes, "bytes": nbytes, "toc": None, "delay": None, "padding": None, "encoder": None, "vbri_delay_field": delay}
    return None


# ---------------------------------------------------------------- one file
def later_sync(data, p0, end, h, window=65536):
    """is there another confirmed compatible frame start within `window` bytes after p0?  (decides whether a frame is the last one)"""
    for q in range(p0, min(end - 4, p0 + window)):
        if data[q] != 0xFF: continue
        g = parse_header(data[q:q + 4])
        if g and g["layer"] == 3 and compatible(h, g) and not g["free"] and (q + g["len"] == end or compatible(g, parse_header(data[q + g["len"]:q + g["len"] + 4]))):
            return True
    return False


def scan_bytes(data):
    r = {"size": len(data), "id3v2": [], "id3v1": False, "id3v1_ext": False, "apev2": False, "lyrics3": False, "frames": 0, "audio_frames": 0, "other_layer_frames": 0,
         "versions": {}, "layers": {}, "rates": {}, "modes": {}, "bitrates": {}, "crc_frames": 0, "free_format_frames": 0, "intensity_frames": 0, "ms_frames": 0,
         "padding_frames": 0, "emphasis_frames": 0, "header_changes": 0, "mode_changes": 0, "ws_frames": 0, "ws_granules": 0, "block_types": [0, 0, 0, 0],
         "mixed_granules": 0, "mixed_frames": 0, "reservoir_short": 0, "reservoir_use_max": 0, "side_info_bad": 0,
         "junk_leading": 0, "junk_between": 0, "junk_trailing": 0, "resyncs": 0, "tag_frame": None, "tag": None, "peak": None}
    p = 0
    while True:
        t = id3v2_at(data, p)
        if not t: break
        r["id3v2"].append(t); p += t["bytes"]
    end = len(data)
    if end - p >= 128 and data[end - 128:end - 125] == b"TAG":
        r["id3v1"] = True; end -= 128
        if end - p >= 227 and data[end - 227:end - 223] == b"TAG+": r["id3v1_ext"] = True; end -= 227
    if end - p >= 32 and data[end - 32:end - 24] == b"APETAGEX":
        size = struct.unpack("<I", data[end - 20:end - 16])[0]; flags = struct.unpack("<I", data[end - 12:end - 8])[0]
        r["apev2"] = True; end -= size + (32 if flags & 0x80000000 else 0)
        if end - p >= 128 and data[end - 128:end - 125] == b"TAG": r["id3v1"] = True; end -= 128
    r["audio_bytes"] = end - p
    first = True; prev = None; next_expected = -1; reserv = 0; free_base = None; junk_run = 0; toc_positions = []; start_of_audio = None; prev_mode = None
    while p + 4 <= end:
        h = parse_header(data[p:p + 4])
        ok = False; L = 0
        if h and h["layer"] == 3:
            if h["free"]:
                if free_base is None:                                # free format: the frame size is the distance to the next compatible header
                    for d in range(24, 2304 + 2):
                        if p + d + 4 <= end and compatible(h, parse_header(data[p + d:p + d + 4])) and parse_header(data[p + d:p + d + 4])["free"]:
                            free_base = d - h["padding"]; break
                if free_base: L = free_base + h["padding"]
            else:
                L = h["len"]
            if L:
                if p + L == end: ok = True
                elif prev is not None and p == next_expected and p + L <= end: ok = True                     # confirmed by the frame before it (its next header is this one)
                elif p + L + 4 <= end and compatible(h, parse_header(data[p + L:p + L + 4])): ok = True
                elif p + L <= end and not later_sync(data, p + L, end, h): ok = True        # the last frame: only junk follows
        if not ok:
            junk_run += 1; p += 1; continue
        if junk_run:
            if start_of_audio is None: r["junk_leading"] += junk_run
            else: r["junk_between"] += junk_run; r["resyncs"] += 1
            junk_run = 0
        if start_of_audio is None: start_of_audio = p
        r["frames"] += 1
        if first:
            r["tag"] = vbr_tag(data, p, h)
            if r["tag"]: r["tag_frame"] = r["tag"]["kind"]
        is_tag = first and r["tag"] is not None
        key = lambda d, k: d.__setitem__(k, d.get(k, 0) + 1)
        if not is_tag:                                              # the Xing/Info/VBRI frame is not audio: it is left out of every distribution
            key(r["versions"], VERSION_NAME[h["version"]]); key(r["layers"], h["layer"]); key(r["rates"], h["hz"]); key(r["modes"], MODE_NAME[h["mode"]])
            if h["free"]: r["free_format_frames"] += 1
            else: key(r["bitrates"], h["kbps"])
            if h["crc"]: r["crc_frames"] += 1
            if h["padding"]: r["padding_frames"] += 1
            if h["emphasis"]: r["emphasis_frames"] += 1
            if h["intensity"]: r["intensity_frames"] += 1
            if h["ms"]: r["ms_frames"] += 1
        if prev is not None and (prev["version"], prev["sr_idx"], prev["channels"]) != (h["version"], h["sr_idx"], h["channels"]): r["header_changes"] += 1
        if prev_mode is not None and not is_tag and prev_mode != h["mode"]: r["mode_changes"] += 1      # the tag frame is not audio: its mode does not count
        # side information, reservoir
        si_off = p + 4 + (2 if h["crc"] else 0)
        sis = side_info_size(h)
        si = parse_side_info(h, data[si_off:si_off + sis])
        payload = L - 4 - (2 if h["crc"] else 0) - sis
        if payload < 0: r["side_info_bad"] += 1
        part_bits = sum(g[0] for gr in si["grs"] for g in gr)
        mdb = si["main_data_begin"]
        if not is_tag:
            r["audio_frames"] += 1
            r["reservoir_use_max"] = max(r["reservoir_use_max"], mdb)
            for gr in si["grs"]:
                for (p23, ws, bt, mx) in gr:
                    if ws:
                        r["ws_granules"] += 1; r["block_types"][bt] += 1
                        if mx: r["mixed_granules"] += 1
            if any(g[1] for gr in si["grs"] for g in gr): r["ws_frames"] += 1
            if any(g[1] and g[3] for gr in si["grs"] for g in gr): r["mixed_frames"] += 1
        ok_res = reserv >= mdb
        if not ok_res and not is_tag: r["reservoir_short"] += 1
        if part_bits > 8 * (mdb + max(payload, 0)): r["side_info_bad"] += 1
        total = min(reserv, mdb) + max(payload, 0)                  # bytes the decoder holds for this frame
        consumed = (part_bits + 7) // 8 if ok_res else 0
        reserv = min(511, max(0, total - consumed))
        toc_positions.append(p)
        prev = h; first = False; p += L; next_expected = p
        if not is_tag: prev_mode = h["mode"]
    leftover = junk_run + max(0, end - p)
    if leftover:
        r["junk_leading" if start_of_audio is None else "junk_trailing"] += leftover
    r["end_unwalked"] = max(0, end - p)
    r["start_of_audio"] = start_of_audio
    # Xing/VBRI: counted frames vs the header, TOC error against true offsets
    t = r["tag"]
    if t and t.get("frames") is not None:
        r["tag_frames_vs_counted"] = t["frames"] - (r["frames"] - 1)       # the header frame itself is not counted by the Xing/Info frame field
    if t and t.get("toc") and t.get("bytes") and r["frames"] > 2:
        n = r["frames"] - 1; base = toc_positions[0]
        errs = []
        for i in range(100):
            k = min(n - 1, int(i / 100 * n))
            true = (toc_positions[1 + k] - base) / t["bytes"] * 256
            errs.append(t["toc"][i] - true)
        maxe = max(abs(e) for e in errs)
        avg_rate = t["bytes"] / max(1e-9, n * (1152 if toc_positions and r["versions"].get("MPEG-1") else 576) / (max(r["rates"], key=r["rates"].get) if r["rates"] else 44100))
        r["toc_max_err_bytes"] = maxe / 256 * t["bytes"]
        r["toc_max_err_seconds"] = (maxe / 256 * t["bytes"]) / avg_rate
    r["vbr"] = len(r["bitrates"]) > 1
    if r["audio_frames"]:
        tot = sum(k * v for k, v in r["bitrates"].items()); cnt = sum(r["bitrates"].values())
        r["kbps_avg"] = tot / cnt if cnt else 0
    return r


def scan_file(path, peak=False, exe=None):
    data = Path(path).read_bytes()
    r = scan_bytes(data)
    if peak and exe:
        import oracle
        try:
            o = oracle.decode(exe, path)
            r["peak"] = o["peak"]; r["float_over_1"] = o["float_over_1"]; r["rail16"] = o["rail16"]; r["decoded"] = o["decoded"]
        except Exception as e:                                       # a stream the oracle rejects is a finding, not a crash
            r["peak"] = None; r["oracle_error"] = str(e)[:80]
    return r


# ---------------------------------------------------------------- summary
def summarise(results):
    S = {"files": len(results), "bytes": sum(r["size"] for r in results), "frames": sum(r["frames"] for r in results), "audio_frames": sum(r["audio_frames"] for r in results)}
    def count(pred): return sum(1 for r in results if pred(r))
    def dist(field):
        d = {}
        for r in results:
            for k in r[field]: d[k] = d.get(k, 0) + 1
        return dict(sorted(d.items(), key=lambda kv: str(kv[0])))
    S.update({
        "versions_files": dist("versions"), "layers_files": dist("layers"), "rates_files": dist("rates"), "modes_files": dist("modes"),
        "bitrates_files": dist("bitrates"),
        "files_crc": count(lambda r: r["crc_frames"] > 0), "frames_crc": sum(r["crc_frames"] for r in results),
        "files_free_format": count(lambda r: r["free_format_frames"] > 0),
        "files_intensity": count(lambda r: r["intensity_frames"] > 0), "frames_intensity": sum(r["intensity_frames"] for r in results),
        "files_ms": count(lambda r: r["ms_frames"] > 0), "frames_ms": sum(r["ms_frames"] for r in results),
        "files_ws": count(lambda r: r["ws_frames"] > 0), "frames_ws": sum(r["ws_frames"] for r in results), "granules_ws": sum(r["ws_granules"] for r in results),
        "granules_start": sum(r["block_types"][1] for r in results), "granules_short": sum(r["block_types"][2] for r in results), "granules_stop": sum(r["block_types"][3] for r in results),
        "files_mixed": count(lambda r: r["mixed_granules"] > 0), "granules_mixed": sum(r["mixed_granules"] for r in results),
        "files_reservoir_short": count(lambda r: r["reservoir_short"] > 0), "frames_reservoir_short": sum(r["reservoir_short"] for r in results),
        "files_vbr": count(lambda r: r["vbr"]), "files_cbr": count(lambda r: not r["vbr"] and r["audio_frames"]),
        "files_xing": count(lambda r: r["tag_frame"] == "Xing"), "files_info": count(lambda r: r["tag_frame"] == "Info"), "files_vbri": count(lambda r: r["tag_frame"] == "VBRI"),
        "files_no_tag_frame": count(lambda r: r["tag_frame"] is None),
        "files_lame_delay": count(lambda r: r["tag"] and r["tag"].get("delay") is not None),
        "files_id3v2": count(lambda r: r["id3v2"]), "files_id3v2_footer": count(lambda r: any(t["footer"] for t in r["id3v2"])),
        "files_id3v2_unsync": count(lambda r: any(t["unsync"] for t in r["id3v2"])), "files_id3v2_ext": count(lambda r: any(t["ext"] for t in r["id3v2"])),
        "files_id3v1": count(lambda r: r["id3v1"]), "files_ape": count(lambda r: r["apev2"]),
        "files_junk_leading": count(lambda r: r["junk_leading"]), "files_junk_between": count(lambda r: r["junk_between"]), "files_junk_trailing": count(lambda r: r["junk_trailing"]),
        "files_header_change": count(lambda r: r["header_changes"]), "files_mode_change": count(lambda r: r["mode_changes"]),
        "files_emphasis": count(lambda r: r["emphasis_frames"] > 0), "files_side_info_bad": count(lambda r: r["side_info_bad"]),
        "files_tag_count_mismatch": count(lambda r: r.get("tag_frames_vs_counted") not in (None, 0)),
        "id3v2_max_bytes": max([t["bytes"] for r in results for t in r["id3v2"]] or [0]),
        "reservoir_use_max": max([r["reservoir_use_max"] for r in results] or [0]),
    })
    toc = sorted(r["toc_max_err_seconds"] for r in results if "toc_max_err_seconds" in r)
    S["toc_files"] = len(toc)
    if toc: S["toc_err_s_max"] = toc[-1]; S["toc_err_s_median"] = toc[len(toc) // 2]; S["toc_err_s_p90"] = toc[int(0.9 * (len(toc) - 1))]
    pk = [r for r in results if r.get("peak") is not None]
    S["peak_files"] = len(pk)
    if pk:
        S["peak_max"] = max(r["peak"] for r in pk)
        S["files_peak_over_1"] = count(lambda r: (r.get("float_over_1") or 0) > 0)
        S["float_over_1_total"] = sum(r.get("float_over_1") or 0 for r in pk)
        S["files_rail16"] = count(lambda r: (r.get("rail16") or 0) > 0); S["rail16_total"] = sum(r.get("rail16") or 0 for r in pk)
    return S


def markdown(S):
    rows = [("files / frames / audio frames", f"{S['files']} / {S['frames']:,} / {S['audio_frames']:,}"),
            ("MPEG version (files)", ", ".join(f"{k} {v}" for k, v in S["versions_files"].items())),
            ("layer (files)", ", ".join(f"L{k} {v}" for k, v in S["layers_files"].items())),
            ("sample rate Hz (files)", ", ".join(f"{k} {v}" for k, v in S["rates_files"].items())),
            ("channel mode (files)", ", ".join(f"{k} {v}" for k, v in S["modes_files"].items())),
            ("bitrates kbps seen (files)", ", ".join(f"{k} {v}" for k, v in S["bitrates_files"].items())),
            ("CBR / VBR files", f"{S['files_cbr']} / {S['files_vbr']}"),
            ("CRC (protection) frames: files / frames", f"{S['files_crc']} / {S['frames_crc']}"),
            ("free-format files", S["files_free_format"]),
            ("joint-stereo MS frames: files / frames", f"{S['files_ms']} / {S['frames_ms']:,}"),
            ("intensity-stereo frames: files / frames", f"{S['files_intensity']} / {S['frames_intensity']}"),
            ("window-switched frames: files / frames / granules", f"{S['files_ws']} / {S['frames_ws']:,} / {S['granules_ws']:,}"),
            ("block types, granules: start / short / stop", f"{S['granules_start']:,} / {S['granules_short']:,} / {S['granules_stop']:,}"),
            ("mixed-block granules: files / granules", f"{S['files_mixed']} / {S['granules_mixed']}"),
            ("frames short of bit-reservoir data: files / frames", f"{S['files_reservoir_short']} / {S['frames_reservoir_short']}"),
            ("largest main_data_begin seen (bytes)", S["reservoir_use_max"]),
            ("first frame: Xing / Info / VBRI / none", f"{S['files_xing']} / {S['files_info']} / {S['files_vbri']} / {S['files_no_tag_frame']}"),
            ("LAME delay and padding present (files)", S["files_lame_delay"]),
            ("tag frame count differs from the counted frames (files)", S["files_tag_count_mismatch"]),
            ("ID3v2: files / with footer / unsync / extended header / largest bytes", f"{S['files_id3v2']} / {S['files_id3v2_footer']} / {S['files_id3v2_unsync']} / {S['files_id3v2_ext']} / {S['id3v2_max_bytes']:,}"),
            ("ID3v1 footer / APEv2 footer (files)", f"{S['files_id3v1']} / {S['files_ape']}"),
            ("junk: leading / between frames / trailing (files)", f"{S['files_junk_leading']} / {S['files_junk_between']} / {S['files_junk_trailing']}"),
            ("sample-rate, version or channel-count change inside a file (files)", S["files_header_change"]),
            ("channel-mode change inside a file (files)", S["files_mode_change"]),
            ("emphasis flagged (files)", S["files_emphasis"]),
            ("side information inconsistent with the frame size (files)", S["files_side_info_bad"])]
    if S.get("toc_files"):
        rows.append(("Xing TOC worst error vs true frame offsets, seconds: median / 90th percentile / max over %d files" % S["toc_files"],
                     f"{S['toc_err_s_median']:.2f} / {S['toc_err_s_p90']:.2f} / {S['toc_err_s_max']:.2f}"))
    if S.get("peak_files"):
        rows.append(("float oracle: files with a sample beyond +-1.0 / total such samples / largest peak", f"{S['files_peak_over_1']} of {S['peak_files']} / {S['float_over_1_total']:,} / {S['peak_max']:.4f}"))
        rows.append(("16-bit oracle output at the rail: files / samples", f"{S['files_rail16']} / {S['rail16_total']:,}"))
    return "| Item | Value |\n|---|---|\n" + "\n".join(f"| {a} | {b} |" for a, b in rows) + "\n"


def by_folder(results, roots):
    """per top-level folder (relative to the scanned root): files, frames, window-switched frames, files with samples beyond 1.0 in the float oracle, ID3v2 unsync files"""
    groups = {}
    for r in results:
        rel = None
        for root in roots:
            try: rel = Path(r["path"]).relative_to(root); break
            except ValueError: continue
        key = rel.parts[0] if rel and len(rel.parts) > 1 else "(top level)"
        g = groups.setdefault(key, {"files": 0, "frames": 0, "ws": 0, "over1": 0, "unsync": 0, "vbr": 0, "mono": 0})
        g["files"] += 1; g["frames"] += r["frames"]; g["ws"] += r["ws_frames"]; g["vbr"] += 1 if r["vbr"] else 0
        g["over1"] += 1 if (r.get("float_over_1") or 0) > 0 else 0; g["unsync"] += 1 if any(t["unsync"] for t in r["id3v2"]) else 0
        g["mono"] += 1 if "mono" in r["modes"] else 0
    return groups


def folder_markdown(groups):
    rows = ["| Folder | Files | Frames | Window-switched frames | VBR files | Mono files | ID3v2 unsync files | Files with samples beyond 1.0 |", "|---|---|---|---|---|---|---|---|"]
    for k in sorted(groups):
        g = groups[k]; rows.append(f"| {k} | {g['files']} | {g['frames']:,} | {g['ws']:,} | {g['vbr']} | {g['mono']} | {g['unsync']} | {g['over1']} |")
    return "\n".join(rows) + "\n"


def find_mp3(paths):
    out = []
    for p in paths:
        p = Path(p)
        if p.is_file(): out.append(p)
        elif p.is_dir(): out.extend(sorted(q for q in p.rglob("*") if q.suffix.lower() == ".mp3" and q.is_file()))
    return out


def main():
    a = sys.argv[1:]
    def opt(name):
        if name in a:
            i = a.index(name); v = a[i + 1]; del a[i:i + 2]; return v
    jout, mout = opt("--json"), opt("--markdown")
    peak = "--no-peak" not in a; a = [x for x in a if x != "--no-peak"]
    quiet = "--quiet" in a; a = [x for x in a if x != "--quiet"]
    paths = a or [p for p in DEFAULT_LIBRARIES if Path(p).exists()]
    files = find_mp3(paths)
    if not files: print("no MP3 files found in", paths or DEFAULT_LIBRARIES); return 0
    exe = None
    if peak:
        try:
            import oracle; exe = oracle.build()
        except Exception as e:
            print("oracle not available, peak skipped:", str(e)[:80]); peak = False
    results = []
    for f in files:
        r = scan_file(f, peak, exe); r["path"] = str(f); results.append(r)
        if not quiet:
            print(f"{f.name[:48]:48s} {r['frames']:6d} fr  {','.join(r['versions']):8s} {','.join(str(k) for k in r['rates']):7s} {','.join(r['modes'])[:12]:12s} "
                  f"{'VBR' if r['vbr'] else 'CBR'} tag={r['tag_frame'] or '-':5s} ws={r['ws_frames']:5d} mixed={r['mixed_granules']} i={r['intensity_frames']} crc={r['crc_frames']} "
                  f"short={r['reservoir_short']} junk={r['junk_leading']}/{r['junk_between']}/{r['junk_trailing']} peak={r['peak'] if r['peak'] is None else round(r['peak'], 3)}")
    S = summarise(results)
    print(markdown(S))
    if jout: Path(jout).write_text(json.dumps({"summary": S, "files": results}, indent=1))
    roots = [p for p in paths if Path(p).is_dir()]
    G = by_folder(results, roots)
    if len(G) > 1: print(folder_markdown(G))
    if mout: Path(mout).write_text(markdown(S) + "\n" + folder_markdown(G))
    return 0


if __name__ == "__main__":
    sys.exit(main())
