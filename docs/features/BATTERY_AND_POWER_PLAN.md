# Battery status and power-efficiency plan

**Status:** Future feature; battery telemetry blocked by the current documented
openFPGA API. Internal efficiency instrumentation remains feasible.

## Product goal

Help listeners understand remaining battery state and help Tau development find
which workloads consume the most energy: decoding, SD access, framebuffer work,
album art, visualizers, screen brightness, speaker output, or idle behaviour.

These are two different features and must not be conflated:

1. **Battery meter:** a user-facing measurement of the Pocket battery.
2. **Efficiency log:** engineering data that helps compare Tau workloads.

## Current platform boundary

The documented Analogue Pocket openFPGA host/target interface does not provide
a battery percentage, voltage, current, charge/discharge state, or total-system
power reading to a running core.

Developer Edition units can display the Aristotle FPGA's 1.1 V fabric power in
the Analogue OS Statistics overlay. Analogue explicitly notes that this excludes
other loads including RAM, I/O banks, level translators, and cartridge power;
it also does not account for the display, audio output, or the entire device.
It is therefore useful for FPGA comparisons but cannot support an honest Tau
battery meter or whole-device drain log.

## Recommended staged implementation

### Stage A — controlled battery benchmark, no firmware feature

Use repeatable hardware runs and record externally:

- Pocket model, firmware version, battery age/condition if known
- Starting and ending OS battery percentage
- Test duration and ambient temperature
- Display brightness and display mode
- Headphones, speaker volume, or Dock state
- Codec, bitrate, sample rate, playlist, artwork visibility, visualizer, EQ,
  screen-blank state, and Advanced-build status

Run long A/B sessions because the OS battery percentage is coarse. Change one
variable at a time. Repeat each condition rather than treating a single drain
run as authoritative.

### Stage B — optional Tau efficiency log

Under `TAU_ADVANCED_BUILD`, add an explicit **Efficiency logging** setting. Log
monotonic session data and optimization proxies:

- Firmware/core version and session duration
- Codec, sample rate, bitrate class, mono/stereo
- Time spent playing, paused, loading, seeking, and screen-blanked
- Active visualizer and time per visualizer
- Album-art state and art decode/load duration
- Decoder cycles or headroom samples
- Framebuffer command count and draw-FIFO stall cycles
- SD read count, bytes read, and measured load/refill latency
- PCM FIFO low-water mark and underrun count
- EQ mode and playback-speed mode

This log answers “which Tau workload is heavier?” It does not answer “how much
battery remains?” without an external start/end battery measurement.

Write only through a dedicated nonvolatile data slot under `/Saves/tau/`, using
a bounded append/ring format and an explicit user opt-in. Never write beside or
through the music assets. Validate shutdown, power-loss, full-log, and corrupt-
log recovery on a throwaway card before enabling the feature in distributed
builds.

### Stage C — real in-core battery meter

Implement only if Analogue documents a supported telemetry command. At minimum
it must expose a trustworthy percentage or voltage plus charging state. Then:

- Show a compact status icon in the player chrome.
- Provide percentage and charging state in Settings > Advanced > Power.
- Sample slowly to avoid pointless host traffic.
- Treat unavailable/stale values as unavailable, never as zero.
- Add snapshots for full, medium, low, charging, unavailable, and critical
  states and hardware-test the thresholds.

## Alternatives while the API is unavailable

- Use the Pocket OS battery indicator outside the core for user status.
- Use the Developer Edition Statistics overlay for relative FPGA-fabric power
  comparisons, while clearly labelling its incomplete scope.
- Use controlled battery rundown tests for whole-device comparisons.
- Use an external USB power meter only for bench experiments and remember that
  USB-powered/charging behaviour is not identical to normal battery discharge.
- Let a future Pocket Sync companion import Tau efficiency logs and combine
  them with manually recorded start/end battery values for analysis.

## Optimization questions the log should answer

1. Which visualizers cause the most framebuffer commands or draw stalls?
2. Does hiding artwork or blanking the framebuffer reduce measurable drain?
3. How much decoder headroom changes by codec, bitrate, EQ, or 1.2x playback?
4. Are SD reads bursty enough to justify larger or differently timed refills?
5. Does a lower UI refresh cadence preserve appearance while reducing activity?
6. Which improvements reduce FPGA-rail power on a Developer Edition and also
   improve controlled whole-device battery runs?

## Go/no-go criteria

- Do not build the user-facing battery meter without documented telemetry.
- Build the internal efficiency counters only after the snapshot harness and
  renderer state fixtures are established.
- Add file logging only after a dedicated safe save slot and failure tests exist.
- Promote an optimization only when repeated measurements show improvement and
  audio underrun, UI responsiveness, and image quality remain acceptable.
