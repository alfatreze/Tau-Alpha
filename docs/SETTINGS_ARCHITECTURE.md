# Tau in-app settings architecture

**Status:** Functional model and information architecture approved for technical
scaffolding. Screen layout waits for Figma and the framebuffer snapshot harness.

## 1. Functional model

### Actors and goals

- **Listener:** adjust Tau without memorising button combinations, understand
  the current choice, preview visual options, and recover defaults.
- **Advanced listener/tester:** opt into deeper tuning, diagnostics, or
  experimental behaviour without exposing that complexity to everyone.
- **Developer:** add settings without changing existing persisted meanings or
  allowing development diagnostics to leak into a standard build.

### Objects

- **Setting:** stable ID, label, value type, allowed values, default,
  persistence rule, availability, and apply behaviour.
- **Group:** Appearance, Audio, Playback, and Advanced.
- **Build capability:** whether Advanced functionality was compiled into this
  firmware (`TAU_ADVANCED_BUILD`).
- **Advanced mode:** future user-controlled setting that reveals compiled
  Advanced options. It cannot enable code absent from the build.
- **Preset preview:** the visible or audible result of a tentative selection.

### Rules

1. Everyday controls stay shallow. Advanced mode is opt-in and off by default.
2. `TAU_ADVANCED_BUILD=0` removes advanced functionality at compile time.
3. An advanced-capable build still hides its Advanced section until the user
   explicitly enables Advanced mode in Tau's own settings UI.
4. Desktop framebuffer snapshots are developer tooling and never depend on the
   runtime Advanced setting.
5. Existing persisted indices are append-only. Reordering visualizers, themes,
   or EQ presets would silently change a saved preference and is forbidden.
6. A changed value previews immediately. Back keeps the applied value; Reset
   requires confirmation and identifies the affected group.
7. Volume remains a direct playback control, not a buried settings choice.
8. Standard builds show no debug overlay, internal saved-position words, raw
   numeric preset indices, or experimental feature labels.

## 2. Information architecture

### Appearance

- **Colour theme** — named swatches rather than numeric indices.
- **Visualizer** — named modes with a live thumbnail/framebuffer preview.
- **Album artwork** — show automatically, always hide, or always show when
  available. Keep the direct shortcut only if hardware testing proves useful.
- **Screen blank timeout** — Off, 1, 5, 10, or 30 minutes.

### Audio

- **EQ mode** — named preset with a compact response-curve preview.
- **EQ bypass** — represented by Flat/Off, not as a separate conflicting
  switch.
- **Volume** — visible as status and adjustable during playback; link to help,
  but do not duplicate it as the primary settings interaction.

### Playback

- **Repeat** — Off, All, One.
- **Shuffle** — On/Off.
- **Resume playlist position** — On/Off with an explanation that standalone
  files do not replace the saved playlist position.
- **Playback speed** — Normal or 1.2x belongs here only if retained; it should
  be explicit rather than dependent on a long, easy-to-trigger gesture.

### Advanced

Visible only when both build capability and future Advanced mode allow it.

- **Performance overlay** — frame cadence, draw pressure, audio FIFO low-water
  mark, and decoder headroom.
- **Load diagnostics** — most recent header, size-probe, artwork, prefill, and
  total load timings.
- **Visualizer tuning** — smoothing, peak hold, and response intensity after
  measurements establish safe ranges.
- **Metadata compatibility** — future filename/tag compatibility diagnostics;
  do not promise normalization until the Unicode test matrix is complete.
- **Efficiency logging** — optional session instrumentation for decoder load,
  framebuffer work, SD traffic, FIFO margin, and active visual mode. Label it
  as an optimization log, not a battery reading, unless Analogue exposes real
  battery telemetry to openFPGA cores.
- **Battery status** — show only if a documented host API becomes available;
  never estimate a percentage from runtime or FPGA activity and present it as
  measured battery state.
- **Experimental features** — individually labelled with impact and an easy
  return to defaults. Avoid a single ambiguous global “experimental” switch.
- **Reset advanced settings** — does not erase ordinary preferences or resume
  position.

Runtime screenshot export is deliberately excluded for now: Pocket/APF file
writing needs a dedicated safe output slot and must never target the music
library. The desktop snapshot harness supplies design review without that risk.
Battery/power logging has the same storage rule: any future log writes to a
dedicated nonvolatile slot under `/Saves/tau/`, never into `/Assets/tau/`.

## 3. Flow inventory

1. Open Tau settings from a discoverable player action.
2. Choose a group; the current value is always visible in the row.
3. Open a setting; move through named options with live visual/audio preview.
4. Confirm or go back; applied choices persist using their stable IDs.
5. Enable Advanced mode through an explanatory confirmation; the Advanced
   group appears only on capable builds.
6. Reset one group or all ordinary settings through a scoped confirmation.
7. If a saved value is unavailable in the current build, fall back to the
   documented default and retain no misleading label.

## 4. Screen and state inventory

- Settings home
- Appearance list
- Theme gallery/detail
- Visualizer gallery/detail and live preview
- Audio list
- EQ preset list/detail with response preview
- Playback list
- Advanced-mode explanation and confirmation
- Advanced list (capable builds only)
- Reset confirmation and completion feedback
- Unsupported/stale saved-value recovery state

Every state above requires a named 400x360 framebuffer snapshot before its UI
implementation is considered complete, following `AGENTS.md`.

## 5. Open interaction decisions for Figma

- The settings entry gesture/button is not fixed yet; avoid adding another
  opaque Select chord merely because it is technically free.
- Decide whether galleries replace the player or overlay it after testing text
  density and live visualizer performance.
- Decide whether X/Y/L/R remain direct cycling shortcuts after the full settings
  views exist. Recommendation: retain them during alpha, then simplify only
  from observed use rather than preference alone.
