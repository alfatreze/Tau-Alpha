# Audio metering: what's measured, what's missing, what's worth accelerating

**Status (2026-10-01):** research/analysis only. No firmware, RTL, card or VM change. Verified directly
against `fw/player.c`, `fw/vu_master.inc`, `src/fpga/core/tau_spec_bank.sv`, `src/fpga/core/
tau_wave_meter.sv` and `docs/MMIO_ALLOCATION.md` on the current tree (not transcribed from an earlier
session's notes).

This is deliberately **transversal**, not a per-meter spec: every meter in `docs/features/meters/` draws
a *picture* from a small set of underlying *measurements*, and those measurements are shared plumbing
(`mtr_in_t`, `fw/meter.h`) feeding eleven different draw functions. A gap or an opportunity here affects
every meter at once, which is why it lives here rather than inside any one meter's own doc. Extends
`docs/features/meters/HARDWARE_METER_IDEAS.md` (items 2 and 3 below supersede that doc's rows 2 and 3
with the full picture; items 1, 4, 5, 6 there are unaffected and not repeated here) and
`docs/features/meters/METER_MODULE_SPEC.md` section 3 (`mtr_in_t` is where any new measurement would
have to be added).

## 1. What's measured today

Every meter reads one of four fields of `mtr_in_t` (`fw/meter.h`). There are really only three distinct
underlying measurements; several meters draw different pictures from the *same* number.

| Measurement | Where it's computed | Consumers |
|---|---|---|
| **Sample peak, per channel** (`peak_l`/`peak_r`) | `meters_publish()` (`fw/player.c`): max `\|PCM\|` since the last display frame, scaled by `MTR_HEADROOM_NUM/DEN` = 3/4 (a measured calibration constant, see the comment at that call site) | VU, Master VU (`fw/vu_master.inc`), and — via one derived scalar history, `wave[]`, built from this same peak each frame — Bars, Waterfall, Scrolling Waveform, Peak Dots. Those four are one number drawn four ways, not four independent measurements |
| **16-band octave energy** (`spec_lvl[]`) | Software: a half-octave IIR cascade (8 octaves x 2), log-scaled, decayed, run only while a spectrum-needing meter is shown. Hardware: `tau_spec_bank.sv` (B-263), MMIO `R_SPEC_IDX`/`R_SPEC_DATA`/`R_SPEC_ST` at `0xDC`-`0xE4`, runs continuously at zero CPU cost whenever the bitstream has it | Spectrum (LED), Winamp Bars, Chladni |
| **Raw waveform samples** | Software: decimated PCM (`wav_v[]`). Hardware: `tau_wave_meter.sv` (B-283), MMIO `R_WAVE_CTL`/`R_WAVE_IDX`/`R_WAVE_DATA`/`R_WAVE_PK`/`R_WAVE_ST` at `0xEC`-`0xFC` — per-column min/max capture plus a running `max \|L\|`/`max \|R\|` peak-hold, read and cleared once per frame | Oscilloscope, Winamp Scope (software draw; the hardware capture's own per-pixel draw path is compiled out, B-302, for cost reasons — the *measurement* is cheap, the *draw* wasn't). `meters_publish()` also reads `R_WAVE_PK` directly for `peak_l`/`peak_r` when the block is present, so the hardware peak-hold already doubles as the peak source for the whole first row of this table |
| **L vs R difference** | A rotated Lissajous built from L/R sample pairs (`viz_phase_tick`) | Phase Scope — a picture, not a number; nothing reads a numeric correlation today |

**A naming note worth keeping in mind:** VU and Master VU are **peak**-driven with asymmetric
attack/decay, not RMS-driven. A real VU ballistic integrates over roughly 300 ms of average level; this
applies the ballistic directly to sample peak. Not a bug — it's cheap and was tuned against real tracks
(the 3/4 headroom constant's own comment) — but worth knowing before calling either meter's reading a
true VU value.

## 2. What else would be relevant to measure

None of these exist today. Ranked by how directly they'd extend `mtr_in_t` without a new signal class.

| Quantity | Why it's useful | New signal needed? |
|---|---|---|
| **RMS / short-term loudness** | What a real VU or LUFS-style meter needs; separates "loud" from "peaky" — the gap the naming note above points at directly | A running sum-of-squares accumulator alongside the existing peak one |
| **Stereo correlation** (a number, -1..+1) | Turns the Phase Scope's picture into an actual mono-compatibility reading; a stereo-width indicator | `sum(L*R)`, `sum(L*L)`, `sum(R*R)` per window (`HARDWARE_METER_IDEAS.md` item 3) |
| **Clip/overs counter** | Cheap, meaningful diagnostic — "N samples at full scale this track" | A saturating counter beside the existing peak compare, in either the software or hardware path |
| **Crest factor** (peak/RMS ratio) | Dynamic-range readout; distinguishes mastering styles at a glance | Software-only once RMS exists — one divide, no new signal |
| **Spectral centroid ("brightness")** | Timbre-driven colour or motion (`HARDWARE_METER_IDEAS.md` item 2) | Software-only, one weighted sum over the 16 bands that already exist |
| **Onset/transient strength as a shared signal** | Chladni already computes a spectral-rise trigger internally (`fw/chladni_core.h`'s `chl_detect`) but it isn't exposed through `mtr_in_t`, so no other meter can react to a hit (`HARDWARE_METER_IDEAS.md` item 1 frames this as "Chladni does this in software today," which undersells it — the logic exists but is trapped inside one meter) | Lift the existing detector out to a shared field, no new maths |
| **True/inter-sample peak** | Catches peaks between samples that plain sample-peak misses — matters for clipping detection more than for a VU-style display | Needs oversampling in hardware; the one item here that's genuinely hard, lowest priority |

## 3. Hardware vs software today, and what's easy to move

| Quantity | Status | Notes |
|---|---|---|
| 16-band octave energy | **Hardware** (`tau_spec_bank.sv`) | Continuous, zero CPU cost when present; software cascade is the fallback |
| Sample peak (max `\|L\|`/`\|R\|` since last clear) | **Hardware** (`tau_wave_meter.sv`) | Same block does per-column min/max capture too — a real hardware oscilloscope trigger+capture, not just a peak-hold |
| Raw waveform for the draw path | **Hardware capture exists, software draw** | The hardware min/max path was deliberately not used for Winamp Scope's per-pixel draw (B-302) — the drawing cost was the problem, not the measurement cost |
| RMS/loudness | **Not measured** | **Easy hardware win**: identical pattern to the two blocks above (accumulate continuously, publish once per display frame) — one more sum-of-squares register beside the existing peak one |
| Stereo correlation | **Not measured** | **Easy hardware win**, same pattern: 1-3 multipliers against the 66 DSP blocks (11 used as of `HARDWARE_METER_IDEAS.md`'s own count; re-check against whatever's used once Cymo's resampler lands) |
| Clip counter | **Not measured** | **Trivial either side**: a saturating counter is nearly free inside the existing wave block, or a few cycles in firmware since peak is already computed |
| Crest factor, spectral centroid | **Not measured** | **Software-only, cheap**: both are one divide/weighted-sum over numbers the firmware already has (peak, RMS once it exists, or the 16 spectrum bands) |
| Onset as a shared signal | **Computed, not shared** | No new hardware or software cost — `chl_detect`'s logic just needs to publish through `mtr_in_t` instead of staying private to Chladni |
| True/inter-sample peak | **Not measured** | Needs oversampling in hardware; not a cheap addition |

**The pattern that matters:** `tau_spec_bank.sv` and `tau_wave_meter.sv` already prove out exactly the
shape RMS and correlation would need — accumulate continuously in hardware, publish once per display
frame, firmware reads and clears. Nothing new to invent; the next two measurements are the same block
shape with a different accumulator.

## 4. Where this would land, concretely

- **`mtr_in_t` (`fw/meter.h`) gains fields**, not a parallel struct — RMS/correlation/centroid/clip-count/
  onset all belong beside `peak`/`peak_l`/`peak_r` in the one struct every meter already reads.
- **MMIO**: the 128-slot page (`docs/MMIO_ALLOCATION.md`) has room after Cymo's block (`0x150`-`0x15C`);
  a new RMS/correlation block would follow the same register shape as `tau_wave_meter.sv` (control,
  index/data pair if more than one sub-value, status).
- **Each addition is its own RTL/sim/fit cycle**, same as every other hardware feature in this project —
  this document identifies what's worth building and roughly how, not a shortcut around the usual
  discipline (golden model, mutation tests, synthesis check, real fit, hardware A/B).

## 5. Priority read (not a build order — an owner decision)

1. **Onset as a shared field** — zero new cost, unlocks every other meter reacting to transients the way
   Chladni already can.
2. **Crest factor, spectral centroid** — free once their one input (RMS, or the existing spectrum bands)
   exists; bundle with whichever of the two below ships first.
3. **RMS/loudness** — fixes the VU naming gap, reuses a proven hardware pattern.
4. **Stereo correlation** — same pattern, a genuinely new meter capability (mono-compatibility), not just
   a refinement of an existing one.
5. **Clip counter** — cheap at any point, no reason to sequence it; add whenever convenient.
6. **True/inter-sample peak** — last, and only if clipping detection on a mastering-style meter turns out
   to matter enough to justify oversampling hardware.

Nothing above is built. This is the map; `docs/features/meters/HARDWARE_METER_IDEAS.md` keeps the
items this doesn't touch (beat/onset *detection circuitry* specifically, ballistics-in-hardware, the
Chladni field evaluator, scope decimator reuse).
