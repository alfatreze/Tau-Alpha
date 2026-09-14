# SDRAM contention diagnostic

## Purpose

This is the gate after the bounded Phase 1 mailbox test. It verifies that the
corrected SDRAM arbitration path remains reliable while the actual Tau player
does the work future cold data must coexist with: MP3 decoding, audio FIFO
servicing, the 400×360 SDRAM framebuffer, and every visualizer.

It is a separate developer core, `alfatreze.TAU_SDRAM_STRESS`, not a normal
TAU release. It uses the exact RBF that passed A-032 and a player ROM compiled
with `TAU_SDRAM_STRESS=1`. The normal TAU core, its player ROM, and its data
files are not modified.

## Workload and safety boundary

- The stress pump is a one-outstanding MMIO client of the existing Phase 1
  bridge. It does not expose mapped SDRAM or alter the linker map.
- It writes and reads 32-bit pattern words only at controller word addresses
  `0x00080000–0x000FFFFE`: byte offsets 1–2 MiB. This preserves the first
  1 MiB framebuffer/guard and the current off-screen artwork area.
- A deterministic address-derived pattern is checked on every read. A CRC-32
  is accumulated over each 1 MiB pass as an additional summary; an individual
  mismatch stops the stress pump and is surfaced immediately.
- The pump yields between every bridge operation. Framebuffer ownership remains
  first at each arbitration boundary; no CPU burst can monopolise the port.
- The initial interval is 3,750 `clk_sys` cycles per 32-bit operation (16,000
  operations/sec maximum, approximately one 1 MiB write/read pass per minute).
  This is intentionally measurable sustained traffic rather than an
  unbounded CPU loop that would only benchmark firmware starvation.

## Developer controls and evidence

The stress core adds only compile-gated controls:

- **Select + X** toggles stress on/off, with an on-screen toast.
- Each completed 1 MiB write/read pass displays an `SDRAM PASS n` toast.
- Plain **X** continues to cycle visualizers; ordinary controls remain intact.

**Pocket behavior:** The user confirms Select + Start displays diagnostic
summary information on the running Pocket build, but it stops playback; press
A to resume. The current source review does not explain this behavior, so
reconciling source and packaged ROM remains open under
[issue 011](issues/011-stress-summary-telemetry-missing.md). Record the exact
summary values and whether they continue to increment after resuming playback.
The user has proposed a persistent compact progress/status HUD; see
[issue 012](issues/012-stress-progress-hud.md).

## 2026-09-14 Pocket visualizer matrix — first HUD run

The user completed ten named visualizer runs while audio, artwork, and the
player UI remained active. No SDRAM mismatch, timeout, audible dropout, or
display corruption was reported. Eye is intentionally excluded because its
attempt occurred after playback stopped; LED was repeated, not substituted for
Eye. The user visually reviewed Eye and elected to waive a repeated loaded
stress pass because it is not materially distinct enough to block this SDRAM
gate. This is substantial **Pocket** evidence for the bounded contention
workload, subject to the timing qualification below.

| Run | Visualizer | Reported result | HUD last-pass text | Qualification |
|---:|---|---|---|---|
| 1 | Bars | PASS | `L1 00:25` | Clean named run. |
| 2 | Water | PASS | `L2 00:31` | Clean named run. |
| 3 | Levels | PASS | `L3 00:25` | Clean named run. HUD photo also shows `P3 28% 01:09 L2 00:31`. |
| 4 | Phase | PASS | `L4 00:10` | Handwriting is legible but this short duration is not a valid throughput measurement. |
| 5 | Wave | PASS | `L5 00:41` | Clean named run. |
| 6 | VU | PASS | `L6 00:56` | Clean named run. |
| 7 | Scroll | PASS | `L7 00:32` | Clean named run. |
| 8 | Mirror | PASS | `L8 00:10` | Clean named run. |
| 9 | Dots | PASS | `L9 00:47` | Clean named run. |
| 10 | Eye | Not accepted | `L10 00:01` | Playback had stopped; user correctly repeated testing rather than treating this as a loaded run. |
| 11 | LED | PASS | `L11 00:26` | First clean LED run after the stopped-playback Eye run. |
| 12 | LED (repeat) | PASS | `L12 00:27` | Deliberate second LED run, not an unnamed visualizer. |

The handwritten notes and two Pocket photos are sufficiently clear for this
record; they are user-supplied evidence, not repository assets. The two HUD
screens confirm the strip was legible and that audio continued: `ST BARS P1
61% 01:09 L-- --:--` while the player showed `02:24 / 04:38`, and `ST LEVELS
P3 28% 01:09 L2 00:31` while it showed `02:04 / 03:13`.

### Timing qualification and correction

Those raw `mm:ss` HUD values are **not valid pass durations**. The HUD's first
implementation subtracted two reads of `R_CYCLES`, a 32-bit free-running
`clk_sys` counter at 60 MHz. It wraps every 71.582788 seconds. A multi-minute
pass therefore loses each full 71.58-second interval; it cannot be compared
to the audio-frame player clock or used for bandwidth conclusions. The player
elapsed time is also not a stopwatch for a pass because music may have started
before the stress run and can pause independently.

The staged correction accumulates each short, unsigned cycle delta into a
software whole-second counter, preserving intervals across counter wraps. It
will be built and re-staged as a stress-only ROM before any timing claim is
accepted. The current clean/fail observations remain useful; the raw durations
are retained only as evidence of the faulty HUD behavior.

For each visualizer, use a known high-bitrate MP3, start stress, leave it
running through at least one completed 1 MiB pass, exercise seek/pause/resume,
artwork and track/playlist changes, then record the pass count and any observed
playback/display symptoms. First run a stress-off baseline for that visualizer
for comparison. One full pass is the minimum per visualizer; five passes on one
visualizer exceed that mode's pass-count minimum but do not replace the other
visualizers or interactions. Any mismatch, timeout, audible underrun,
corruption, or persistent display stall is a failure and stops Phase 2
planning. Capture summary values and reconcile counters/source provenance under
issue 011.

## Evidence limits and acceptance

A completed pass proves only this bounded MMIO traffic under the selected
player workload. It does not prove cached/uncached aliases, mapped cold
workspace, or execute-in-place code. Those remain Phase 2 gates.

The functional visualizer coverage is accepted with the explicit Eye waiver
above: all exercised modes had zero SDRAM mismatches/timeouts, no audible audio
dropouts, and no display corruption. The timing correction is staged for a
future stress session; it is not a blocker for this contention result, but no
pass-duration performance claim may be made until it is observed past 72
seconds.
