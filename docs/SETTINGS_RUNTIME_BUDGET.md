# Runtime settings feasibility

## Current result

The first in-core settings-home prototype was deliberately not retained. It
added a simple seven-row menu for existing controls only, but the firmware
linker stopped with `no room left for even a token heap`. No ROM was emitted.

The baseline firmware builds at 152,088 bytes (84.4% of the usable image
budget), and the player is intentionally protected by four independent memory
reservations:

- 24,576-byte fixed decoder arena. Hardware measurement shows Helix reaches
  23,824 bytes, leaving only 752 bytes of arena slack.
- 24 KiB audio DMA ring.
- 4 KiB ID3/art DMA landing zone.
- 16 KiB stack plus a 1 KiB linker heap guard.

Reducing any of these merely to make a menu link is not safe. In particular,
the audio arena and DMA ring must not be exchanged for UI code without a
hardware playback/underrun test matrix.

## Decision

Do not add a runtime settings screen until its code-size budget is made
explicit. Existing direct controls remain the supported way to change theme,
visualizer, EQ, repeat, shuffle, volume, resume, and blank timeout.

## Safe path to implementation

1. Finish the Figma interaction model and reduce the first release to the
   smallest discoverable settings surface: a home list of current values plus
   Appearance, Audio, and Playback entry points. Advanced remains absent.
2. Build an isolated size report for that exact menu, including `.text`,
   `.rodata`, `.data`, and `.bss` deltas. Do not change `link.ld` reservations
   during this measurement.
3. Recover space from non-audio UI/diagnostic code only, or make the settings
   view a separately packaged capability build. Any reclamation must preserve
   the existing standard build byte-for-byte unless explicitly versioned.
4. After a fitting build, run the normal MP3/playlist/artwork/seek/visualizer
   hardware matrix plus a long FLAC playback test and inspect the underrun
   latch. Only then make the settings screen the standard build.

## State and accessibility requirements for the eventual screen

- Entry, navigation, changed-value preview, close, unavailable value, and
  stale persisted-value fallback each need named framebuffer snapshots.
- A selected row must use both high contrast and position/shape, never colour
  alone. Motion previews require a static alternative state.
- Every action must be reversible with B; no confirmation is needed for
  immediately applied ordinary values. Reset remains a later scoped,
  confirmed action.
- The temporary `Select+Start` entry tested in the rejected prototype is not
  adopted. Figma must define the final discoverable entry affordance.
