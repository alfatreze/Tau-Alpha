#!/usr/bin/env python3
"""MP3 v2 WP-04: the rail and headroom counter (tools/mp3_v2/rail_count.py with rail_count.c, rail_xr.c, rail_unit_inst.h) on generated streams with known contents.

The streams come from tools/mp3_v2/synth_stream.py: the same bits at two global_gain values, so the second is exactly 2^((g2-g1)/4) louder in every spectral line. That gives
exact expectations: the spectral maximum and the largest window word scale by that factor; the hot stream clips and the quiet one does not; a pre-synthesis gain of -3 dB
removes every rail sample and leaves no error beyond 2 LSB; the same gain applied after the clamp leaves the flat tops (damage) in as many samples as were clipped.
Also: Helix (as Player 2 builds it, window unit as its golden model) and the oracle agree frame for frame (lag 0, same sample count); the probed minimp3 header differs from
the vendored one only by the probe insertions; rail_xr's peak equals the oracle's; the loudness estimate equals an independent implementation of ITU-R BS.1770 written here.
Red checks: a wrong reference is detected (alignment error counted), a probe whose anchor is gone is rejected, a mutated loudness is not equal."""
import math, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/mp3_v2"))
import oracle, rail_count as R, synth_stream as S
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail)))
    if not ok: fails += 1

exes = R.build()
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    def mk(name, gain, mode=1):
        p = td / name; p.write_bytes(S.stream(nframes=30, seed=3, gain=gain, mode=mode, pairs=40, br_idx=14)); return p
    quiet, hot = mk("quiet.mp3", 190), mk("hot.mp3", 194)
    rq, rh = R.analyse_file(quiet, exes), R.analyse_file(hot, exes)
    for nm, r in (("quiet", rq), ("hot", rh)):
        h = r["helix"]
        check(f"{nm}: Helix and the oracle decode the same frames (30) and the same samples", h["helix_frames"] == 30 and r["oracle"]["frames"] == 30 and h["helix_samples"] == h["oracle_samples"] > 0, (h["helix_frames"], h["helix_samples"], h["oracle_samples"]))
        check(f"{nm}: aligned at lag 0 with every sample matched", h["lag"] == 0 and h["unmatched"] == 0, (h["lag"], h["unmatched"]))
        check(f"{nm}: no sample differs from the oracle by more than 2 LSB (Helix and minimp3 agree)", h["align_bad"] == 0, h["align_bad"])
        check(f"{nm}: the window unit model saw 36 slots per frame per call of Subband (slots = frames x 36)", h["slots"] == 30 * 36, h["slots"])
    ih, iq = rh["helix"]["instances"], rq["helix"]["instances"]
    check("quiet stream: no rail sample in either decoder, peak below 1.0", rq["oracle"]["peak"] < 1.0 and rq["oracle"]["rail16"] == 0 and iq[0]["rails"] == 0, (rq["oracle"], iq[0]["rails"]))
    check("hot stream: peak above 1.0 and rail samples in both decoders", rh["oracle"]["peak"] > 1.0 and rh["oracle"]["rail16"] > 0 and ih[0]["rails"] > 0, (rh["oracle"], ih[0]["rails"]))
    h = rh["helix"]
    check("hot stream: Helix and oracle rails coincide (both = oracle's, none created by Helix beyond the boundary samples)", h["both_rail"] >= 0.98 * rh["oracle"]["rail16"] and h["helix_only_rail_clear"] == 0 and h["oracle_only_rail_far"] == 0, h)
    check("hot stream: the float samples beyond 1.0 are the clipped ones", rh["oracle"]["float_over_1"] >= rh["oracle"]["rail16"] - 2, rh["oracle"])
    ratio = rh["xr"]["xr_max"] / rq["xr"]["xr_max"]
    check("spectral maximum scales by exactly 2^((194-190)/4) = 2", abs(ratio - 2.0) < 1e-6, ratio)
    wratio = rh["helix"]["max_word"] / rq["helix"]["max_word"]
    check("largest window word scales by 2 (to the word rounding)", abs(wratio - 2.0) < 0.01, wratio)
    check("rail_xr's float peak equals the oracle's (the probed copy decodes identically)", abs(rh["xr"]["peak"] - rh["oracle"]["peak"]) < 1e-7 and rh["xr"]["pairs"] == rh["oracle"]["pairs"], (rh["xr"]["peak"], rh["oracle"]["peak"]))
    check("granule-channels counted: 30 frames x 2 granules x 2 channels", rh["xr"]["granule_channels"] == 120 and sum(rh["xr"]["hist"]) == 120, rh["xr"]["granule_channels"])
    # pre-gain versus post-clamp (instance order: 0 unity, 1 -1 dB, 2 -3 dB, 3 -6 dB, 4 -9 dB, 5 no-clip, 6 estimated ReplayGain)
    peak_db = 20 * math.log10(rh["oracle"]["peak"])
    check(f"hot stream peak is {peak_db:+.2f} dB (between 0 and +3 dB so the -3 dB gain clears it)", 0 < peak_db < 3.0, peak_db)
    check("pre-gain -3 dB: no rail sample, and no sample off the ideal by more than 2 LSB", ih[2]["rails"] == 0 and ih[2]["err_gt2"] == 0, ih[2])
    check("pre-gain at the no-clip gain: no rail sample", ih[5]["rails"] == 0, ih[5])
    check("gain AFTER the clamp at -3 dB: the flat tops stay (damage in about as many samples as were clipped)", ih[2]["post_err_gt2"] >= 0.5 * ih[0]["rails"] and ih[2]["post_err_max"] > 2, (ih[2]["post_err_gt2"], ih[0]["rails"]))
    check("quiet stream: gain after the clamp costs nothing beyond rounding", all(i["post_err_gt2"] == 0 for i in iq), [i["post_err_gt2"] for i in iq])
    check("unity instance is the shipped decoder: its rails equal the plain count", ih[0]["gain"] == 1.0 and ih[0]["post_err_gt1"] == 0, ih[0])
    # loudness: independent BS.1770 implementation (own code, here), on the oracle's float dump
    f32 = td / "hot.f32"; info = oracle.decode(exes["oracle"], hot, f32=f32)
    xs = struct.unpack("<%df" % (info["pairs"] * info["channels"]), f32.read_bytes())
    def lufs_ref(x, ch, hz):
        def coefs(fs):
            f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
            K = math.tan(math.pi * f0 / fs); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416; a0 = 1 + K / Q + K * K
            sh = ([(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0])
            f0, Q = 38.13547087602444, 0.5003270373238773; K = math.tan(math.pi * f0 / fs); a0 = 1 + K / Q + K * K
            return sh, ([1, -2, 1], [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0])
        n = len(x) // ch; seg = hz // 10; nseg = n // seg
        if nseg < 4: return -70.0
        e = [0.0] * nseg
        for c in range(ch):
            st = [[0.0, 0.0], [0.0, 0.0]]; (sb, sa), (hb, ha) = coefs(hz)
            for i in range(nseg * seg):
                v = x[i * ch + c]
                for (b, a), z in (((sb, sa), st[0]), ((hb, ha), st[1])):
                    y = b[0] * v + z[0]; z[0] = b[1] * v - a[1] * y + z[1]; z[1] = b[2] * v - a[2] * y; v = y
                e[i // seg] += v * v
        bl = [sum(e[b:b + 4]) / (4.0 * seg) for b in range(nseg - 3)]
        L = lambda m: -0.691 + 10 * math.log10(m + 1e-30)
        a = [m for m in bl if L(m) > -70.0]
        if not a: return -70.0
        rel = L(sum(a) / len(a)) - 10.0
        g = [m for m in bl if L(m) > rel and L(m) > -70.0]
        return L(sum(g) / len(g)) if g else -70.0
    mine = lufs_ref(xs, info["channels"], info["hz"])
    check("the C loudness equals an independent BS.1770 implementation (to 0.01 LU)", abs(mine - rh["helix"]["lufs"]) < 0.01, (mine, rh["helix"]["lufs"]))
    sine = [math.sin(2 * math.pi * 997.0 * i / 48000.0) for i in range(48000 * 5)]
    check("reference implementation: a full-scale 997 Hz mono sine measures -3.01 LUFS", abs(lufs_ref(sine, 1, 48000) - (-3.01)) < 0.02, lufs_ref(sine, 1, 48000))
    check("red: a mutated loudness (+1 dB) is not accepted by that comparison", not abs(mine + 1.0 - rh["helix"]["lufs"]) < 0.01)
    # red: a wrong reference is detected (the oracle dump of the quiet stream against the hot stream's Helix decode)
    qf32 = td / "quiet.f32"; oracle.decode(exes["oracle"], quiet, f32=qf32)
    r = subprocess.run([str(exes["rail_count"]), str(hot), "--ref", str(qf32)], capture_output=True, text=True)
    import json
    wrong = json.loads(r.stdout.strip().splitlines()[-1]) if r.returncode == 0 else None
    check("red: Helix of the hot stream against the quiet stream's oracle is flagged (samples off by more than 2 LSB)", wrong is not None and wrong["align_bad"] > 1000, wrong and wrong["align_bad"])
    # the probed header: differs from the vendored one only by the insertions; red: a probe whose anchor has gone is rejected
    src = (ROOT / "third_party/minimp3/minimp3.h").read_text()
    d = R.probed_header(td)
    patched = (d / "minimp3.h").read_text()
    check("probed header minus the two insertions is the vendored header, byte for byte", patched.replace(R.XR_GLOBALS, "").replace(R.XR_CALL, "") == src and patched != src)
    old = R.XR_ANCHOR_2
    try:
        R.XR_ANCHOR_2 = "    for (ch = 0; ch < nch; ch++, gr_info += 7)\n"
        try: R.probed_header(td / "red"); rejected = False
        except AssertionError: rejected = True
    finally: R.XR_ANCHOR_2 = old
    check("red: a probe whose anchor is not in the header is rejected", rejected)
    # a mono stream goes through the mono path of the unit (pushed as both channels, left taken)
    mono = mk("mono.mp3", 194, mode=3)
    rm = R.analyse_file(mono, exes)
    check("mono stream: same frames and samples in Helix and the oracle, no sample off by more than 2 LSB", rm["helix"]["helix_samples"] == rm["helix"]["oracle_samples"] and rm["helix"]["align_bad"] == 0 and rm["helix"]["channels"] == 1, rm["helix"])
    check("mono stream: pre-gain -3 dB stays aligned with the ideal (the extra instances take the left channel too)", rm["helix"]["instances"][2]["err_gt2"] == 0 and rm["helix"]["instances"][2]["samples"] == rm["helix"]["helix_samples"], rm["helix"]["instances"][2])
# a real file when the owner's library is present (skipped otherwise): the Test Album's first MP3 clips in 3 samples, in both decoders
lib = next((Path(d) / "Test Album/02 MacCunn MP3 128 44k.mp3" for d in [ROOT.parent / "test music"] + R.DEFAULT_LIBRARIES if (Path(d) / "Test Album/02 MacCunn MP3 128 44k.mp3").exists()), None)
if lib is not None:
    r = R.analyse_file(lib, exes)
    check("library file: oracle peak 1.0249, 3 rail samples in the oracle and in Helix, 6,694 frames, no alignment error",
          abs(r["oracle"]["peak"] - 1.024874926) < 1e-6 and r["oracle"]["rail16"] == 3 and r["helix"]["instances"][0]["rails"] == 3 and r["helix"]["helix_frames"] == 6694 and r["helix"]["align_bad"] == 0, r["helix"])
else:
    print("skip: the library file is not present")
print("FAILED %d" % fails if fails else "ALL PASSED")
sys.exit(1 if fails else 0)
