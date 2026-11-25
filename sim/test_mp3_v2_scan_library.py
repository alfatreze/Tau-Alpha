#!/usr/bin/env python3
"""MP3 v2 WP-03: tools/mp3_v2/scan_library.py reports exactly what a synthetic stream (tools/mp3_v2/synth_stream.py) contains: ID3v2 with footer,
unsynchronisation and extended-header flags, an Info frame with LAME delay/padding, MS and intensity joint-stereo frames, CRC frames, start/short/stop and
mixed blocks, a frame short of reservoir data, junk between frames, a truncated last frame, an APEv2 and an ID3v1 footer; plus MPEG-2 (LSF) mono,
a VBRI file and a free-format file. The float oracle decodes the same stream and agrees on the frame count. The scan is read-only (file hash and mtime unchanged).
Red: flipping the intensity bit in the stream changes the intensity count; a stream without the reservoir overrun reports zero short frames."""
import hashlib, os, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/mp3_v2"))
import scan_library as L, synth_stream as S, oracle
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1

def main_stream(short=True, intensity=True, junk=True, tail=True):
    out = [S.id3v2(300, footer=True, unsync=True, ext=True), S.xing_frame(kind=b"Info", frames=60, nbytes=60 * 417, delay=576, padding=1000)]
    seed = 1
    def add(n, **kw):
        nonlocal seed
        for _ in range(n): out.append(S.frame(seed=seed, **kw)); seed += 1
    add(10, mode=1, mode_ext=0)
    add(10, mode=1, mode_ext=2)                                  # MS
    add(5, mode=1, mode_ext=1 if intensity else 0)               # intensity
    add(5, mode=1, window=1, block_type=2)                       # short blocks: 5 frames x 4 granule/channels
    add(5, mode=1, window=[1, 1, 0, 0], block_type=[1, 3, 0, 0]) # start (granule 0) and stop (granule 0, channel 1)
    add(5, mode=1, window=1, block_type=2, mixed=1)              # mixed blocks
    add(5, mode=1, crc=True)
    add(5, mode=1, main_data_begin=100)                          # 100 bytes: the previous frame left more than that
    if short: add(1, mode=1, main_data_begin=511)                # 511 bytes: more than any 417-byte frame can have left
    else: add(1, mode=1, main_data_begin=0)
    add(4, mode=1)
    if junk: out.insert(len(out) - 2, bytes(7))                  # 7 zero bytes of junk between two frames
    if tail: out.append(S.frame(seed=999, mode=1)[:100])         # a truncated last frame: 100 bytes of trailing junk
    out += [S.apev2(40), S.id3v1()]
    return b"".join(out)

data = main_stream()
r = L.scan_bytes(data)
n_audio = 10 + 10 + 5 + 5 + 5 + 5 + 5 + 5 + 1 + 4
check("frames: Info frame + audio frames", r["frames"] == n_audio + 1 and r["audio_frames"] == n_audio, f"{r['frames']} {r['audio_frames']}")
check("MPEG-1 Layer III 44.1 kHz joint stereo 128 kbps, CBR", r["versions"] == {"MPEG-1": n_audio} and r["layers"] == {3: n_audio} and r["rates"] == {44100: n_audio} and r["bitrates"] == {128: n_audio} and not r["vbr"] and set(r["modes"]) == {"joint stereo"})
check("MS frames = 10, intensity frames = 5, CRC frames = 5", (r["ms_frames"], r["intensity_frames"], r["crc_frames"]) == (10, 5, 5), str((r["ms_frames"], r["intensity_frames"], r["crc_frames"])))
check("window switching: 15 frames, 50 granules; block types start 5, short 40, stop 5", (r["ws_frames"], r["ws_granules"], r["block_types"]) == (15, 50, [0, 5, 40, 5]), str((r["ws_frames"], r["ws_granules"], r["block_types"])))
check("mixed blocks: 5 frames, 20 granules", (r["mixed_frames"], r["mixed_granules"]) == (5, 20), str((r["mixed_frames"], r["mixed_granules"])))
check("exactly one frame short of reservoir data (main_data_begin 511); the 100-byte references are fine", r["reservoir_short"] == 1 and r["reservoir_use_max"] == 511, str((r["reservoir_short"], r["reservoir_use_max"])))
check("Info frame with LAME delay 576 and padding 1000, counted frames match", r["tag_frame"] == "Info" and r["tag"]["delay"] == 576 and r["tag"]["padding"] == 1000 and r["tag"]["encoder"] == "LAME3.100" and r["tag_frames_vs_counted"] == 60 - n_audio, str(r["tag"]))
check("ID3v2: footer, unsync and extended header flags, 320 bytes", len(r["id3v2"]) == 1 and r["id3v2"][0] == {"major": 3, "unsync": True, "ext": True, "footer": True, "bytes": 320}, str(r["id3v2"]))
check("ID3v1 and APEv2 footers found, not mistaken for frames", r["id3v1"] and r["apev2"] and not r["id3v1_ext"])
check("junk: 0 leading, 7 between frames (one resync), 100 trailing (the truncated frame)", (r["junk_leading"], r["junk_between"], r["resyncs"], r["junk_trailing"]) == (0, 7, 1, 100), str((r["junk_leading"], r["junk_between"], r["resyncs"], r["junk_trailing"])))
check("no side-information inconsistency, no header change", r["side_info_bad"] == 0 and r["header_changes"] == 0 and r["mode_changes"] == 0)

# the float oracle agrees: it decodes every frame except the one short of reservoir data
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    exe = oracle.build(td / "b")
    f = td / "main.mp3"; f.write_bytes(data)
    h0, m0 = hashlib.sha256(f.read_bytes()).hexdigest(), os.stat(f).st_mtime_ns
    rf = L.scan_file(f, peak=True, exe=exe)
    check("scan_file is read-only (hash and mtime unchanged)", hashlib.sha256(f.read_bytes()).hexdigest() == h0 and os.stat(f).st_mtime_ns == m0)
    check("oracle (pinned minimp3) on this stream: decodes frames - short - 1: the frame BEFORE the 7 junk bytes is dropped by its resync (measured upstream behaviour); the truncated last frame returns no samples",
          rf.get("decoded") == rf["frames"] - rf["reservoir_short"] - 1 and rf["peak"] > 0, str((rf.get("decoded"), rf["frames"], rf["peak"])))
    fc = td / "clean.mp3"; fc.write_bytes(main_stream(junk=False, tail=False))
    rc = L.scan_file(fc, peak=True, exe=exe)
    check("clean stream (no junk, no truncated tail): scanner and oracle agree, oracle decodes frames - reservoir_short", rc["junk_between"] == 0 and rc["junk_trailing"] == 0 and rc["decoded"] == rc["frames"] - rc["reservoir_short"] == n_audio, str((rc.get("decoded"), rc["frames"], rc["reservoir_short"])))
    # MPEG-2 LSF mono with mixed short blocks
    f2 = td / "lsf.mp3"; f2.write_bytes(S.stream(8, version=2, br_idx=8, sr_idx=0, mode=3, window=1, block_type=2, mixed=1))
    r2 = L.scan_bytes(f2.read_bytes())
    check("MPEG-2 22.05 kHz mono: 8 frames, 8 mixed granules, 8 short granules", r2["versions"] == {"MPEG-2": 8} and r2["rates"] == {22050: 8} and r2["modes"] == {"mono": 8} and r2["mixed_granules"] == 8 and r2["block_types"][2] == 8 and r2["frames"] == 8, str(r2))
    rf2 = L.scan_file(f2, peak=True, exe=exe)
    check("oracle decodes the LSF stream: 8 frames of 576 pairs", rf2["decoded"] == 8, str(rf2.get("decoded")))
    # VBRI
    f3 = td / "vbri.mp3"; f3.write_bytes(S.vbri_frame(frames=5, nbytes=6 * 417) + S.stream(5))
    r3 = L.scan_bytes(f3.read_bytes())
    check("VBRI header frame recognised (frame count 5), the 5 audio frames counted", r3["tag_frame"] == "VBRI" and r3["tag"]["frames"] == 5 and r3["audio_frames"] == 5 and r3["frames"] == 6, str(r3["tag"]))
    # free format (bitrate index 0): the frame size is the distance to the next header
    f4 = td / "free.mp3"; f4.write_bytes(b"".join(S.frame(br_idx=0, total=417, seed=i, mode=1) for i in range(8)))
    r4 = L.scan_bytes(f4.read_bytes())
    check("free-format file: 8 free-format frames walked (length from the distance to the next header)", r4["free_format_frames"] == 8 and r4["frames"] == 8, str((r4["free_format_frames"], r4["frames"])))
    # leading junk after an ID3v2 and an MPEG-1 stream with a mono/stereo mode change
    f5 = S.id3v2(50) + bytes(417) + S.stream(4, mode=1) + S.stream(4, mode=0)
    r5 = L.scan_bytes(f5)
    check("leading junk (417 zero bytes after an ID3v2) and one channel-mode change", r5["junk_leading"] == 417 and r5["frames"] == 8 and r5["mode_changes"] == 1, str((r5["junk_leading"], r5["frames"], r5["mode_changes"])))

# red: mutated streams are reported differently
rm = L.scan_bytes(main_stream(intensity=False))
check("red: without the intensity bit the intensity count is 0 (the detection is real)", rm["intensity_frames"] == 0 and r["intensity_frames"] == 5)
rs = L.scan_bytes(main_stream(short=False))
check("red: without the main_data_begin overrun no frame is reported short", rs["reservoir_short"] == 0)
# summary and markdown render
S1 = L.summarise([r, r2, r3, r4])
md = L.markdown(S1)
check("summary: 4 files, 1 with intensity, 1 with mixed blocks... table renders", S1["files"] == 4 and S1["files_intensity"] == 1 and S1["files_mixed"] == 2 and "| files / frames" in md, str(S1["files_mixed"]))
sys.exit(1 if fails else 0)
