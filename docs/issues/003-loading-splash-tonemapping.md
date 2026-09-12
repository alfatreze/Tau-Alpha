# BUG-003: Loading artwork loses tonal separation on Pocket hardware

**Status:** Confirmed on hardware; awaiting device reference photo and art retune

## Observed behaviour

The loading image appears on Pocket, but the source's soft glow is crushed into
the same intensity range as the waveform while other areas appear washed out.
Playback itself remains unaffected.

## Likely cause

The shipped image is reduced to a 16-colour palette in RGB565 before firmware
draws it.  This preserves layout but cannot preserve every narrow tonal step in
the authored JPEG; Pocket OLED gamma can further magnify the perceived loss.

## Review workflow

Run `make visual-review` after every loading-art revision.  It writes
`work/previews/tau-loading-framebuffer.png`, the exact palette/RLE framebuffer
that Tau will draw; review it beside the authored source.  This does not
simulate the Pocket screen itself, so a photographed hardware result is still
needed to set the final tone curve.

## Next action

When a Pocket photo is available, tune the source with deliberately separated
luminance bands for background, soft glow, waveform and text/bar regions;
regenerate the asset; inspect the automated capture; then retest on hardware.

## Acceptance criteria

- Glow, waveform and background remain visually distinct on the Pocket panel.
- Status text and the loading bar preserve adequate contrast.
- The generated review capture is checked before each on-device art revision.
