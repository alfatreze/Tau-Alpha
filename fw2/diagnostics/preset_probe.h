/* Preset-read probe (diagnostic only; docs/features/METER_PRESETS_EXTERNAL.md step 0b, B-963). On every new track, right after the cover is loaded and under the same rules (governor NORMAL, the media stream quiesced), it opens a
 * 200-byte file by path through the cover's data slot and reads it, as a one-preset file would be read, and records how long each step took, whether the bytes are the expected ones, how full the audio ring was when it
 * started and whether the audio fell behind (the ring's `dry` and `under_ring` counters moved). The Report rows are "PP ...". The file is `preset-probe.bin` in the core's own Assets folder (the packager writes it). */
#ifndef P2_PRESET_PROBE_H
#define P2_PRESET_PROBE_H
#include <stdint.h>
void preset_probe_run(void);
extern uint32_t pp_runs, pp_ok, pp_bad, pp_err, pp_open_cyc, pp_open_max, pp_read_cyc, pp_read_max, pp_fill_min, pp_dry;       /* pp_err: 1 no path, 2 open failed, 3 read failed (the last failure) */
#endif
