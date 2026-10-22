# SOUND MAP: a meter that shows what the sound is (B-820)

One pack-only meter (key `sound_map`, slot 6, the seventh and last slot) with three modes, built on the audio-statistics inputs of `mtr_in_t` ABI 1.1 (`fw2/cores/audio_stats_core.h`; `AUDIO_METERING_RESEARCH.md`).

| Mode | What it shows | Reads |
|---|---|---|
| STEREO FIELD | A sound field of cells: the left channel swells a blob from the centre to the left, the right channel to the right (an ellipse that widens with the level); what the channels share (correlation above 0) glows as a core in the middle. The two top rows are a width gauge: the marker sits at the correlation (far right mono, centre wide, far left and red out of phase). | `rms_l`, `rms_r`, `corr` (peaks and no gauge without the hardware block) |
| CHARACTER | Five bars of cells: LOUD (rms), BRIGHT (spectral centroid), NOISE (a flat spectrum against a tonal one: mean over maximum band), CREST (peak over rms, 1.0 to 4.0), HITS (onsets lately). Each rises fast and falls slowly. | rms or peaks, `centroid`, the bands, `crest`, `onset` |
| LONG MEMORY | One column per SPAN seconds (1 to 10; the preset MEMORY 5 S uses 5): height = loudness, colour = brightness (the centroid, weighted by loudness) from a dim to the accent colour; scrolls left, the newest column is live. A pause stops time. | rms or peaks, `centroid`, `dt_ms` |

Parameters: mode, gain (50 to 200 %), cell size (5 to 12 px), seconds per column (shown in LONG MEMORY only). Presets: STEREO FIELD, CHARACTER, LONG MEMORY, MEMORY 5 S.

**Why one meter with three modes:** the framework holds seven meters (the saved presets have seven fields, the METER list seven rows) and one slot was free. Splitting them into three meters needs the preset field and list scrolling first (`NEW_METER.md`).

**Cost and limits:** pack 4.9 KB, 2.1 KB of the shared 4 KB scratch; only changed cells are drawn (the memory redraws its columns once per pushed column and the live one when it changes); command ceiling 1,500 and the standard cycle limit and strikes apply. Needs ABI minor 1 (older firmware refuses the pack, E22). **Evidence:** host (the pack equals its source on rv32sim in every scenario with and without the statistics, inside its box; rendered frames of the three modes looked right on the host). Not on a Pocket, and RMS/correlation are real only on a bitstream with `TAU_STATS` (not fitted yet): until then the field and loudness fall back to the peaks.
