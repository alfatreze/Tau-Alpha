# Runtime settings feasibility

> **Update 2026-09-21:** the settings screen shipped in v0.2.0 after the playlist buffers moved to SDRAM (A-105); measured cost of the grouped menu about 4.4 KiB, the choice lists and
> previews about 1-6 KiB more. See `docs/CURRENT_STATUS.md`; the text below is the original budget analysis.

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

## Measured size of a minimal menu (A-096, 2026-09-20)

A seven-row flat settings home over the existing controls adds 2,996 bytes
(+2,988 `.text`/`.rodata`, +8 `.bss`) and leaves a 416-byte heap gap against the
1,024-byte minimum: it fails the link by 608 bytes, reproducing the earlier
"no room left for even a token heap". Freeing the 13,312-byte playlist buffers
(A-095) would leave a 13,724-byte gap with the menu in place. A grouped design
with previews and confirmation will be larger and needs its own size report.
Details and caveats: `AUDIT_TRAIL.md` A-096.
