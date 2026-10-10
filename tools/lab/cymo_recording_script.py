#!/usr/bin/env python3
"""B-654: the detailed recording script, generated from ONE list so the names, the order and the counts cannot disagree.

    python3 tools/lab/cymo_recording_script.py            # rewrite docs/features/CYMO_RECORDING_SCRIPT.md and .csv
    python3 tools/lab/cymo_recording_script.py --check    # fail on a duplicate name or a malformed row

Naming rule (the handover rule): `<core>_<group>_<what>[_<condition>].wav`, lowercase, underscores, no spaces. <core> is the number of the TAU DEV build under test (105 for the original plan;
the new sessions use the build that carries Halcyon and the new hand-over: written `{c}` below, say 108). Every volume is written into the name: `_vNN` = Tau core volume position (0-100, 0.6 dB each),
`_dNN` = Pocket system volume clicks down from MAXIMUM (maximum = d0; omitted means d0). 48 kHz / 24-bit WAV, mono is enough unless the row says stereo.
"""
import csv, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
STD = "std"            # standard settings, see the script's section 1

# (session, name, test file, set-up beyond the standard, what to do, analysis, question / gate, status)
O, N = "original plan", "NEW (B-654)"
RE = "original plan; RE-RECORD (first take was differential)"   # groups 0-5: the old chain recorded L minus R, which also cancels any click both channels share
R = []
def add(session, name, tfile, setup, do, analysis, gate, status):
    R.append((session, name, tfile, setup, do, analysis, gate, status))

# ---- the original plan (docs/features/CYMO_RECORDING_PLAN_DEV105.md section 3), names unchanged -------------------------------------------------------------------------------------------
add("0 floors", "105_if_short", "-", "interface alone: shorted 3.5 mm plug (or the cable with the Pocket off)", "record 15 s", "cymo_loopback.py analyze", "Q1 / G-FLOOR: interface floor", RE)
add("0 floors", "105_idle", "-", "core loaded, nothing playing", "record 15 s", "cymo_loopback.py analyze", "Q1 / G-FLOOR: Pocket idle floor", RE)
for v in (100, 70, 40, 10):
    add("1 volume", f"105_sil_v{v}", "silence_44100.flac", f"core volume {v}, 15-bit", "start recording, wait 3 s, play, record 15 s", "cymo_loopback.py analyze", "Q1 / G-FLOOR: floor versus volume", RE)
for v in (100, 70, 40, 10):
    add("1 volume", f"105_tone_v{v}", "tone_1k_48000.flac", f"core volume {v}, 15-bit", "start recording, wait 3 s, play, record 12 s", "cymo_loopback.py analyze", "Q1 / G-FLOOR: SINAD versus volume", RE)
for d in (0, 1, 2, 4, 8, 16, 24, 30, 31):
    add("1b system volume", f"105_sysvol_d{d:02d}", "sysvol_48000.flac", f"core volume 100, 15-bit; Pocket system volume d{d} (press + to the end stop first, then - exactly {d} times)", "record the whole file, 18 s", "cymo_isp.py sysvol 105_sysvol_d*.wav (all nine together)", "Q1 / G-FLOOR: is the noise before or after the system volume", RE)
for a, b in (("on", "v94"), ("off", "v94"), ("on", "v70"), ("off", "v70")):
    add("2 HW gain A/B", f"105_hw_{a}_{b}", "tone_1k_48000.flac", f"HW GAIN {a.upper()}, core volume {b[1:]}", "start recording BEFORE pressing play (the 43 ms fade-in must be captured), record 12 s", "cymo_loopback.py analyze / track", "Q2 / G-HWGAIN", RE)
for a in ("on", "off"):
    add("2 HW gain A/B", f"105_hw_{a}_step", "tone_1k_48000.flac", f"HW GAIN {a.upper()}; volume 94, then 70, then 94 DURING the tone", "start recording before play; change the volume at about 4 s and 8 s", "cymo_loopback.py track", "Q2 / G-HWGAIN: step shape", RE)
add("3 16-bit", "105_a16_off_v94", "tone_1k_48000.flac", "16-BIT OUTPUT OFF, core volume 94", "record 12 s", "cymo_loopback.py analyze", "Q3 / G-A16: reference", RE)
add("3 16-bit", "105_a16_on_v85", "tone_1k_48000.flac", "16-BIT OUTPUT ON (lower the volume BEFORE switching it on), core volume 85 (the volume moves in steps of 3, so 84 cannot be selected; 85 is 0.62 dB above level-matched, expect -11.40 dBFS against -12.02 OFF). Recorded as 105_a16_on_v84.wav", "record 12 s", "cymo_loopback.py analyze / compare", "Q3 / G-A16: level-matched", RE)
for v in (70, 40):
    add("3 16-bit", f"105_a16_on_v{v}", "tone_1k_48000.flac", f"16-BIT ON, core volume {v}, interface gain as for 105_a16_on_v94 (lowered, note the knob); the 15-bit reference is 105_tone_v{v}", "record the whole tone", "cymo_loopback.py analyze / compare with 105_tone_v%d" % v, "Q3 / G-A16: does the 6 dB pay where the fixed floor dominates (predicted SINAD +6 dB)", N)
add("3 16-bit", "105_a16_on_v94", "tone_1k_48000.flac", "16-BIT ON, core volume 94; lower the interface gain by about 6 dB first and note the knob", "record 12 s", "cymo_loopback.py analyze / compare", "Q3 / G-A16: same volume", RE)
add("3 16-bit", "105_sil_a16_on_v94", "silence_44100.flac", "16-BIT ON, core volume 94", "record 15 s", "cymo_loopback.py analyze", "Q3 / G-A16: floor with 16-bit", RE)
for n, s in (("screen_on", "screen on, default meter"), ("screen_blank", "Settings > Appearance > SCREEN BLANK active"), ("menu", "a menu open and static"), ("charger", "charger connected, screen on")):
    add("4 spur source", f"105_spur_{n}", "silence_44100.flac", s, "record 15 s", "cymo_loopback.py analyze (spurs)", "Q4 / G-SPUR", RE)
for n, setup, why in (("paused", "play the silence file, then press Pause (the decoder stops; the I2S stream keeps clocking)", "if the floor falls back to idle (-82.5) the extra noise comes from decoding/CPU/SD activity; if it stays at -73, it is the DAC / amplifier state"),
                      ("stopped", "play the silence file, then Stop (back to the idle screen of the player)", "the same, with the player fully stopped"),
                      ("hwoff", "silence file playing, HW GAIN OFF", "excludes the gain stage's activity"),
                      ("mp3sil", "an MP3 silence file playing (silence_44100.mp3)", "FLAC against MP3 decoding load")):
    add("4 spur source", f"{{c}}_spur_{n}", "silence_44100.flac" if n != "mp3sil" else "silence_44100.mp3", setup, "record 15 s", "cymo_loopback.py analyze (band levels and spurs)", "G-SPUR follow-up: why playing digital silence is 9.7 dB noisier than idle (" + why + ")", N)
for v in (94, 100):
    add("load (optional)", f"105_load_v{v}", "tone_1k_48000.flac", f"32 ohm resistor across the jack, 15-bit, core volume {v}", "record 15 s", "cymo_loopback.py analyze", "G-LOAD: distortion under load", O)
for k in ("ladder", "imd", "hot"):
    for st, note in (("a16on", "16-BIT ON, core volume 100, HW GAIN ON, system volume MAXIMUM (press + to the end stop). SET THE INTERFACE GAIN FIRST, ON THE LADDER ITSELF (not on the 1 kHz tone): play the first 10 s of isp_ladder_48000.flac (2 s of silence, then the quietest section, a 12 kHz tone at -12 dBFS) and lower the interface gain until Audition's PEAK meter reads about -18 dBFS on that section. The loudest section is 15 dB above it, so it then peaks near -3 dBFS. Check with `cymo_isp.py peak` on a trial recording of the first 10 s. Record all six ISP takes (ON and OFF) at that one setting and never move the knob during a take. Then run `cymo_isp.py peak` on each take: it must say ok"), ("a16off", "16-BIT OFF, core volume 100, HW GAIN ON (the control)")):
        add("5 inter-sample peaks", f"105_isp_{k}_{st}", f"isp_{k}_48000.flac", note, "start recording, play the file once through (about 60 s for the ladder), stop", f"cymo_isp.py peak (the take is valid only if it says ok), then cymo_isp.py analyze --kind {k}", "G-ISP", RE)

# ---- new sessions ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
for n, f, s, d in (("if_loop_tone", "tone_1k_48000.flac", "play the file from the INTERFACE's own output into its input (no Pocket)", "record 12 s"),
                   ("if_loop_silence", "silence_44100.flac", "interface output muted into its input", "record 15 s"),
                   ("if_loop_sweep", "sweep_20_20k_48000.flac", "play the sweep from the interface's output into its input", "record the whole file")):
    add("6 calibration and repeatability", f"{{c}}_cal_{n}", f, s, d, "cymo_loopback.py analyze; response for the sweep", "chain calibration: subtract the interface's own response and distortion from every later result", N)
for n in ("a", "b", "c"):
    add("6 calibration and repeatability", f"{{c}}_rep_tone_v100_{n}", "tone_1k_48000.flac", "core volume 100, standard settings; leave everything untouched between a, b, c", "record 12 s", "compare level, SINAD, floor of a/b/c", "repeatability: the size of a real difference", N)
add("6 calibration and repeatability", "{c}_rep_tone_v100_cold", "tone_1k_48000.flac", "power the Pocket off and on first, then core volume 100", "record 12 s", "compare with a/b/c", "repeatability: cold boot", N)
add("6 calibration and repeatability", "{c}_rep_idle_b", "-", "core loaded, nothing playing (repeat of 105_idle on the new core)", "record 15 s", "cymo_loopback.py analyze", "repeatability: floor; new core is not worse than 105", N)
add("6 calibration and repeatability", "{c}_smoke_tone_v94_hwon", "tone_1k_48000.flac", "HW GAIN ON, core volume 94 (the new core: Halcyon engine OFF)", "record 12 s", "cymo_loopback.py analyze", "smoke: the new bitstream does not regress sessions 0-2", N)
add("6 calibration and repeatability", "{c}_smoke_tone_v94_hwoff", "tone_1k_48000.flac", "HW GAIN OFF, core volume 94", "record 12 s", "cymo_loopback.py compare with the previous row", "smoke: G-HWGAIN on the bitstream that would ship", N)

add("6 calibration and repeatability", "{c}_cal_ch_balance", "tone_1k_48000.flac", "stereo capture on the EVO 4: Pocket left to input 1, right to input 2, BOTH channels at the same gain (set by hand, Smartgain off); core volume 94", "record 12 s", "cymo_loopback.py analyze --channel 0 and --channel 1; the level difference is the sum of the interface's and the Pocket's channel mismatch", "channel balance of the chain (the stereo rows 9 and the gain-stage balance need it)", N)
add("6 calibration and repeatability", "{c}_cal_ch_swapped", "tone_1k_48000.flac", "as above with the two leads SWAPPED at the interface (left plug in input 2, right plug in input 1)", "record 12 s", "as above; a mismatch that follows the PLUG is the Pocket's, one that stays on the INPUT is the interface's", "separates the Pocket's channel mismatch from the interface's", N)
for n, f, s_, d, an, g in (("cable_rinv", "tone_1k_48000_rinv.flac", "Volt 1 with the new 6.35 mm TS to 3.5 mm TRS cable; INST off; the same tone with the RIGHT channel inverted. Interface gain to MINIMUM first, raise until the peak is about -12 dBFS, then play tone_1k_48000.flac at the SAME gain", "record 12 s each", "cymo_isp.py peak on both takes", "what the TS cable records: the same level as the plain tone = LEFT only (what you want); near silence = L+R summed; about 6 dB louder = still differential"),
                          ("tone_v94", "tone_1k_48000.flac", "Volt 1 and the TS cable (left channel), core volume 94, standard settings", "record 12 s", "cymo_loopback.py analyze --channel 0; compare with the EVO 4's channel 0 (the same take, {c}_rep_tone_v100 is at 100: use v94)", "two independent chains agree: level, floor, THD within 1 dB / a few dB"),
                          ("idle", "-", "Volt 1 and the TS cable, core loaded, nothing playing", "record 15 s", "cymo_loopback.py analyze; compare with {c}_rep_idle_b", "floor: Pocket against interface noise"),
                          ("sweep", "sweep_20_20k_48000.flac", "Volt 1 and the TS cable, Halcyon OFF, core volume 94", "record the whole file", "cymo_loopback.py response; compare with {c}_fr_off_sweep", "the chain's response is the same on both interfaces")):
    add("6x chain cross-check (Volt, optional)", f"{{c}}_xchk_volt_{n}", f, s_, d, an, g, N)

for v in (94, 70, 40):
    add("7 transitions", f"{{c}}_tog_hw_v{v}", "tone_1k_48000_60s.flac", f"core volume {v}; Diagnostics > HW GAIN", "start recording, play; toggle HW GAIN every 4 s starting at 4 s, 6 times (ON/OFF alternate)", "cymo_loopback.py track (events, steps)", "hand-over click at low volume (the B-653 fix)", N)
add("7 transitions", "{c}_tog_a16_v84", "tone_1k_48000_60s.flac", "core volume 84; Diagnostics > 16-BIT OUTPUT", "toggle every 4 s, 6 times (lower the interface gain 6 dB first)", "cymo_loopback.py track", "16-bit toggle step (dip)", N)
add("7 transitions", "{c}_tog_cymo_44k", "tone_1k_44100_60s.flac", "44.1 kHz file, 1.00x; Diagnostics > CYMO RESAMPLER", "toggle every 4 s, 6 times", "cymo_loopback.py track", "resampler toggle step (dip) and settling", N)
add("7 transitions", "{c}_tog_hal_preset", "tone_1k_48000_60s.flac", "Halcyon engine; press Y every 4 s through every preset and back to OFF", "record 60 s", "cymo_loopback.py track", "preset change click (dip)", N)
add("7 transitions", "{c}_tog_hal_slider", "tone_1k_48000_60s.flac", "Settings > Audio > HALCYON EQ; move a slider from 0 to +5 and back at 4 s steps", "record 60 s", "cymo_loopback.py track", "live slider moves are click-free", N)
add("7 transitions", "{c}_tog_vol_step", "tone_1k_48000_60s.flac", "core volume 94; press Down 5 times, then Up 5 times, a press every 2 s", "record 40 s", "cymo_loopback.py track", "volume ramp is click-free", N)
add("7 transitions", "{c}_loop_tone_x10", "tone_1k_48000.flac", "Repeat = one; let it loop 10 times", "record the whole run (about 2 min)", "cymo_loopback.py track (events at every loop boundary)", "the unexplained first-repeat pop; loop boundary", N)
add("7 transitions", "{c}_trk_change_tonesab", "tone_1k_48000.flac + tone_1500_48000.flac", "play A, press Next at 6 s, let B play, press Previous", "record 40 s", "cymo_loopback.py track", "track change pop; priming", N)
add("7 transitions", "{c}_soak_all_on_60m", "tone_1k_48000_60s.flac", "Repeat = one; HW GAIN ON, Cymo ON (44.1 kHz file variant not needed), Halcyon preset WARM, 16-bit OFF, meter running, screen on", "unattended, record 60 min", "cymo_loopback.py track (event list with times)", "issue 023: the rare spike, everything on", N)
add("7 transitions", "{c}_soak_all_off_60m", "tone_1k_48000_60s.flac", "Repeat = one; Halcyon OFF, Cymo OFF, HW GAIN OFF, SCREEN BLANK on", "unattended, record 60 min", "cymo_loopback.py track", "issue 023: the same soak with the new code out of the path", N)

hp = ["off", "flat", "warm", "clear", "bass", "vocal", "speech", "lowvol", "smooth"]
for p in hp:
    add("8 Halcyon", f"{{c}}_hal_{p}_tone", "tone_1k_48000.flac", f"Halcyon {'OFF' if p == 'off' else p.upper()}, core volume 94", "record 12 s", "cymo_loopback.py analyze + track (look for sidebands at +-30 Hz: the flutter)", "the flutter fix; FLAT/OFF transparency; loudness match", N)
add("8 Halcyon", "{c}_hal_off_silence", "silence_44100.flac", "Halcyon OFF", "record 15 s", "cymo_loopback.py analyze", "floor with the engine out", N)
add("8 Halcyon", "{c}_hal_flat_silence", "silence_44100.flac", "Halcyon FLAT (engine on, bypass inside)", "record 15 s", "cymo_loopback.py analyze", "the engine adds no noise", N)
add("8 Halcyon", "107_ctl_oldeq_pop_tone", "tone_1k_48000.flac", "ON TAU_DEV_107 (still has the old EQ): old EQ preset POP (press Y three times), Halcyon OFF, core volume 94", "record 12 s", "cymo_loopback.py track", "control: the flutter exists on the old build (proves the measurement sees it)", N)
add("8 Halcyon", "107_ctl_oldeq_flat_tone", "tone_1k_48000.flac", "ON TAU_DEV_107: old EQ FLAT, core volume 94", "record 12 s", "cymo_loopback.py track", "control: FLAT has none", N)
add("8 Halcyon", "{c}_fr_off_sweep", "sweep_20_20k_48000.flac", "Halcyon OFF, core volume 94", "record the whole file (about 30 s)", "cymo_loopback.py response", "the chain's own response (DAC, amp, interface)", N)
for p in hp[1:]:
    add("8 Halcyon", f"{{c}}_fr_hal_{p}_sweep", "sweep_20_20k_48000.flac", f"Halcyon {p.upper()}, core volume 94", "record the whole file", "cymo_loopback.py response, divided by the OFF sweep; compare with the page's curve", "the displayed curve and the loudness match are true", N)
add("8 Halcyon", "{c}_fr_lf_tones", "lf_tones_48000.flac", "Halcyon FLAT (infrasonic stage on), core volume 94", "play the file (10, 17, 20, 30, 50 Hz in steps)", "cymo_loopback.py analyze per step", "what the 17 Hz high-pass does", N)

add("9 stereo", "{c}_ch_left_only", "ch_left_only_48000.flac", "stereo capture on the EVO 4 (both channels in one take)", "record 12 s", "cymo_loopback.py analyze, both channels", "balance, crosstalk (gain-stage balance targets, crossfeed)", N)
add("9 stereo", "{c}_ch_right_only", "ch_right_only_48000.flac", "stereo capture on the EVO 4 (both channels in one take)", "record 12 s", "cymo_loopback.py analyze, both channels", "balance, crosstalk", N)
add("9 stereo", "{c}_ch_impulse_l", "ch_impulse_l_48000.flac", "Halcyon OFF; then repeat the pair with FLAT", "record 10 s", "impulse position per channel", "no one-sample skew between channels (the pair fix on hardware)", N)
add("9 stereo", "{c}_ch_impulse_r", "ch_impulse_r_48000.flac", "as above", "record 10 s", "impulse position per channel", "no one-sample skew", N)

for lab, f in (("a15", "15-bit (16-BIT OUTPUT OFF)"), ("a16", "16-BIT OUTPUT ON, interface gain lowered 6 dB")):
    add("10 levels and quantisation", f"{{c}}_lvl_{lab}_levels", "levels_48000.flac", f"{f}, core volume 100", "record the whole file (1 kHz at -20, -40, -60, -70, -80, -90 dBFS, 10 s each)", "cymo_loopback.py analyze per section", "G-FLOOR: how much dither / the final quantiser matter", N)
    add("10 levels and quantisation", f"{{c}}_lvl_{lab}_fade24", "fade24_48000.flac", f"{f}, core volume 100", "record the whole file (a 24-bit 1 kHz sine fading to -100 dBFS)", "cymo_loopback.py analyze, harmonics versus level", "G-FLOOR: quantisation distortion at low level", N)

for z in ("open", "300r", "32r", "16r"):
    add("11 load and impedance", f"{{c}}_load_{z}_tone", "tone_1k_48000.flac", f"load: {z} across the jack, 15-bit, core volume 94", "record 12 s", "level versus load gives the output impedance", "G-LOAD", N)
add("11 load and impedance", "{c}_load_iem_sweep", "sweep_20_20k_48000.flac", "your IEM or headphone connected, interface across its terminals through a high-impedance tap", "record the whole file", "cymo_loopback.py response", "G-LOAD: the frequency-dependent effect on correction presets", N)

for n, f in (("tone_1k", "tone_1k_44100.flac"), ("tone_10k", "tone_10k_44100.flac"), ("tone_18k", "tone_18k_44100.flac"), ("sweep", "sweep_20_20k_44100.flac")):
    for m in ("fir", "hold"):
        add("12 44.1 kHz resampler", f"{{c}}_rs_{m}_{n}", f, f"44.1 kHz file at 1.00x; CYMO RESAMPLER {'ON' if m == 'fir' else 'OFF'}, Halcyon FLAT, core volume 94", "record 12 s (the sweep: the whole file)", "cymo_loopback.py analyze (level, SINAD, images); response for the sweep", "resampler on the production path; the 18 kHz tone is where FIR and cubic differ", N)
for r in ("24k", "32k", "22k"):
    add("12 44.1 kHz resampler", f"{{c}}_rs_hold_{r}_tone", f"tone_1k_{ {'24k': '24000', '32k': '32000', '22k': '22050'}[r] }.flac", "CYMO RESAMPLER OFF (hold)", "record 12 s", "cymo_loopback.py analyze", "baseline images before extending the resampler to other rates", N)

add("13 ReplayGain", "{c}_rg_off", "rg_tone_m6_48000.flac", "REPLAYGAIN Off", "record 12 s", "level", "ReplayGain accuracy: the reference", N)
add("13 ReplayGain", "{c}_rg_track", "rg_tone_m6_48000.flac", "REPLAYGAIN Track (the file carries a track gain of -6.00 dB)", "record 12 s", "level should be 6.0 dB below the reference (+-0.1)", "ReplayGain accuracy", N)
add("13 ReplayGain", "{c}_rg_album", "rg_tone_m6_48000.flac", "REPLAYGAIN Album (album gain -3.00 dB in the tags)", "record 12 s", "level should be 3.0 dB below the reference (+-0.1)", "ReplayGain accuracy", N)

add("14 tempo (low priority)", "{c}_tempo_1p5_tone", "tone_1k_44100_60s.flac", "TEMPO on (MP3 variant of the tone), speed 1.50x", "record 20 s", "cymo_loopback.py track: the fundamental stays 1000 Hz", "C7: pitch preservation", N)
add("14 tempo (low priority)", "{c}_tempo_1p5_speech", "speech clip (MP3)", "TEMPO on, speed 1.50x", "record 30 s; listen", "listening, plus the event list", "C7: artifacts", N)

# test files the new rows need (name, how, status)
NEWFILES = [
 ("tone_1k_48000_60s.flac", "tone_1k_48000.flac extended to 60 s", "to build"),
 ("tone_1k_44100_60s.flac", "1 kHz at -6 dBFS, 44.1 kHz, 60 s (an MP3 copy for the tempo rows)", "to build"),
 ("tone_1500_48000.flac", "1.5 kHz at -6 dBFS, 48 kHz, 12 s (the second track of the change test)", "to build"),
 ("tone_1k_44100.flac, tone_10k_44100.flac, tone_18k_44100.flac", "-6 dBFS, 44.1 kHz, 12 s each", "to build"),
 ("tone_1k_24000.flac, tone_1k_32000.flac, tone_1k_22050.flac", "-6 dBFS, 12 s each", "to build"),
 ("sweep_20_20k_48000.flac, sweep_20_20k_44100.flac", "log sweep 20 Hz to 20 kHz, -12 dBFS, 30 s", "to build"),
 ("lf_tones_48000.flac", "10, 17, 20, 30, 50 Hz, 6 s each, -12 dBFS", "to build"),
 ("ch_left_only_48000.flac, ch_right_only_48000.flac", "1 kHz at -12 dBFS in one channel only, 12 s", "to build"),
 ("ch_impulse_l_48000.flac, ch_impulse_r_48000.flac", "single full-scale-ish impulses (-6 dBFS), one every second, in one channel", "to build"),
 ("levels_48000.flac", "1 kHz at -20, -40, -60, -70, -80, -90 dBFS, 10 s each (exists as levels_44100.flac only)", "to build"),
 ("fade24_48000.flac", "24-bit 1 kHz sine fading from -6 to -100 dBFS over 40 s", "to build"),
 ("rg_tone_m6_48000.flac", "1 kHz -6 dBFS with REPLAYGAIN_TRACK_GAIN = -6.00 dB and REPLAYGAIN_ALBUM_GAIN = -3.00 dB (Vorbis comments)", "to build"),
 ("tone_1k_48000, silence_44100, sysvol_48000, isp_ladder/imd/hot_48000", "the original files, on the card in cymo_loopback/", "exist"),
]

def prio(r):
    """P1 = needed to trust the data and to confirm the open fixes; P2 = feeds a gate or a design decision; P3 = nice to have / later."""
    g, n = r[0], r[1]
    if g.startswith("6x"): return "P2"
    if g.startswith(("0 ", "1 ", "1b", "2 ", "6 ", "7 ")): return "P1"
    if g.startswith("8 ") and "_fr_hal_" not in n: return "P1"
    if g.startswith(("3 ", "4 ", "5 ", "8 ", "9 ", "10", "11", "load")): return "P2"
    return "P3"

def check():
    names = [r[1] for r in R]
    dup = sorted({n for n in names if names.count(n) > 1})
    bad = [r[1] for r in R if " " in r[1] or r[1] != r[1].lower() or not (r[1][0].isdigit() or r[1].startswith("{c}"))]
    return dup, bad

def md():
    L = ["# Cymo recording script (handover edition)", "",
         "Generated by `tools/lab/cymo_recording_script.py` (edit the list there and rerun; the CSV beside this file is the tick-sheet). Supersedes section 3 of `CYMO_RECORDING_PLAN_DEV105.md`, whose rationale, predictions and gates (sections 1, 4, 5, 6) still stand.",
         "", f"**{len(R)} recordings** ({sum(1 for r in R if r[7] in (O, RE))} from the original plan, of which {sum(1 for r in R if r[7] == RE)} are re-recordings, {sum(1 for r in R if r[7] == N)} new) in {len(sorted({r[0] for r in R}))} groups, plus two 60-minute unattended soaks.", "",
         "## 1. How to work from this sheet", "",
         "- **Name** every file exactly as the Name column says (lowercase, underscores). `{c}` is the number of the TAU DEV build under test (e.g. `108`); the Halcyon control rows use `107` because they run on the old build. Put the WAVs in `test music/Audio Lab/` and tell me.",
         "- **Standard settings (STD)** unless the row says otherwise: Halcyon OFF (new core) or EQ FLAT (105), REPLAYGAIN Off, HW GAIN ON, CYMO RESAMPLER OFF, 16-BIT OUTPUT OFF, ACCEPT ALL RATES OFF, Pocket system volume at MAXIMUM (press + to the end stop = d0), charger unplugged, screen on with the default meter, nothing in the jack but the capture cable.",
         "- **Per recording:** start recording, wait 3 s, start playback, record the stated time (unless the row says start recording BEFORE play). Interface gain set once so `tone_1k_48000` at core volume 94 reads about -12 dBFS peak; photograph the knob; the 16-bit ON and inter-sample rows say when to lower it and by how much (note it in the file name's notes line, below).",
         "- **Capture chain (decided 2026-10-10): ONE interface for the whole run.** Everything is recorded on the Audient EVO 4 as a STEREO WAV (Pocket left to input 1, right to input 2 through two mono TS leads, or a 3.5 mm TRS to dual 6.35 mm TS splitter; line level, not mic; 48 kHz / 24-bit). Both inputs at the SAME gain, set by hand (check the EVO 4 manual for how its gain dial and Smartgain treat the two inputs); `{c}_cal_ch_balance` and `{c}_cal_ch_swapped` measure what is left. Analyse each file twice, `cymo_loopback.py analyze --channel 0` (left) and `--channel 1` (right); a row that says 'mono' only needs channel 0. A stereo (TRS) plug into one balanced input records LEFT MINUS RIGHT, which also CANCELS any click or tone both channels share: that was the old Volt 1 chain, which is why groups 0-5 are marked RE-RECORD. The Volt 1 with its new 6.35 mm TS to 3.5 mm TRS cable (left channel only) is used for ONE optional cross-check sitting at the very end (group 6x), so the two interfaces are never swapped during the main run.",
         "- **A notes line** per session (in a text file `<core>_notes.txt` beside the WAVs): date, which core is installed, the system volume end-stop check, any interface gain change, anything odd you heard.",
         "- **Priority:** P1 = needed to trust the data and to confirm the open fixes (groups 0, 1, 1b, 2, 6, 7 and the Halcyon tone rows of 8); P2 = feeds a gate or a design decision; P3 = later or optional (per-preset sweeps, other resampler rates, ReplayGain, tempo). Count: P1 %d, P2 %d, P3 %d." % tuple(sum(1 for r in R if prio(r) == p) for p in ("P1", "P2", "P3")),
         "- **Order:** follow the sessions of section 2 (A: groups 6, 7, 8, then 9-14 on the new core; B: 0, 1b, 1, 2, 3, 4, 5 on `TAU_DEV_105`; C: the two controls; D: the optional Volt cross-check). Groups 6 and 7 answer the most open questions (a measurement you can trust, and the clicks).", ""]

    L += ["", "## 2. Sessions: which unit, which core, in which order", "",
          "The EVO 4 stays connected for ALL three sessions; only the Pocket's installed core changes (a card write: ask first). The Volt 1 is touched once, last.", "",
          "| Session | Unit | Pocket core | Groups | Why this order |", "|---|---|---|---|---|",
          "| A | EVO 4 (stereo) | the new build `{c}` (Halcyon, hand-over, noeq): `TAU DEV 111` or `TAU Preview` | 6, 7, 8, 9, 10, 11, 12, 13, 14 | Group 6 first: it proves the chain (calibration, channel balance, repeatability) and the new core does not regress; groups 7 and 8 answer the open clicks and the Halcyon fix |",
          "| B | EVO 4 (stereo) | `TAU_DEV_105` (kept on the card for these) | 0, 1, 1b, 2, 3, 4, 5, load | The original-plan rows, all re-recorded on the stereo chain; they feed the gates G-FLOOR, G-HWGAIN, G-A16, G-SPUR, G-ISP |",
          "| C | EVO 4 (stereo) | `TAU_DEV_107` (old EQ build) | the two `107_ctl_` rows of group 8 | Controls only; do them right after B to keep the card swap to one |",
          "| D (optional) | Volt 1 + TS cable | `{c}` | 6x | Cross-check of the chain against a second interface; the cable test comes first |", "",
          "If time is short: P1 only, in the order A (group 6 then 7), then B (groups 0, 1, 1b, 2). Everything else can wait for the gates that need it.", ""]
    cur = None
    for s, n, f, setup, do, an, gate, st in R:
        if s != cur:
            cur = s
            L += ["", f"### Group {s}", "", "| # | Pri | Name | Test file | Set-up (beyond STD) | Do | Analysis | Answers | Status |", "|---|---|---|---|---|---|---|---|---|"]
        i = [r[1] for r in R].index(n) + 1
        L.append(f"| {i} | {prio(R[i - 1])} | `{n}.wav` | {f} | {setup} | {do} | {an} | {gate} | {st} |")
    L += ["", "## 3. Test files", "", "All in `test music/cymo_loopback/` (copy to the core's media folder and rescan: a card write, ask first). The generator to extend is `tools/lab/cymo_loopback.py gen`.", "", "| File | Content | Status |", "|---|---|---|"]
    for a, b, c in NEWFILES:
        L.append(f"| `{a}` | {b} | {c} |")
    L += ["", "## 4. After the recordings", "", "Run the analysis column's commands, write each gate's result into `AUDIT_TRAIL.md` against the predictions of `CYMO_RECORDING_PLAN_DEV105.md` section 4 BEFORE touching any gated development, and keep the notes file with the WAVs. Group 6's repeat runs decide the tolerance used for every other comparison.", ""]
    return "\n".join(L)

if __name__ == "__main__":
    dup, bad = check()
    if dup or bad:
        sys.exit(f"duplicate names: {dup}; malformed: {bad}")
    if "--check" in sys.argv:
        print(f"ok: {len(R)} recordings, all names unique")
        sys.exit(0)
    out = ROOT / "docs/features"
    (out / "CYMO_RECORDING_SCRIPT.md").write_text(md())
    old = {}                                           # keep the tick-sheet's done / date / notes columns when the list is regenerated
    try:
        with open(out / "CYMO_RECORDING_SCRIPT.csv", newline="") as fh:
            for r in csv.DictReader(fh): old[r["name"]] = (r.get("done", ""), r.get("date", ""), r.get("notes", ""))
    except FileNotFoundError:
        pass
    with open(out / "CYMO_RECORDING_SCRIPT.csv", "w", newline="") as fh:
        w = csv.writer(fh); w.writerow(["#", "priority", "name", "group", "test_file", "setup", "do", "analysis", "answers", "status", "done", "date", "notes"])
        for i, (s, n, f, setup, do, an, gate, st) in enumerate(R, 1):
            w.writerow([i, prio((s, n)), n + ".wav", s, f, setup, do, an, gate, st] + list(old.get(n + ".wav", ("", "", ""))))
    print(f"wrote {len(R)} recordings")
