# BUG-002: Pocket OS does not render Tau's superscript alpha metadata glyph

**Status:** Confirmed on hardware; resolved with ASCII OS metadata

## Observed behaviour

The platform metadata name was packaged as `TAUᵅ`, but Pocket displays it as
`TAU`.  Platform artwork and the author icon both load correctly.

## Cause and impact

Pocket's openFPGA metadata font does not render the Unicode superscript alpha.
This is a display-font limitation, distinct from the earlier playlist filename
encoding issue.  The filesystem name is already safe ASCII (`tau` and
`alfatreze.TAU`), so launch and asset lookup are unaffected.

## Recommendation

Treat `TAU` as the OS metadata name and carry the `ᵅ` mark in graphical
branding (platform artwork, author artwork and in-core UI) where the glyph is
under Tau's control.  If a textual OS-visible qualifier is wanted, use ASCII
`TAU Alpha` rather than a Unicode glyph.

## Resolution

The platform name is now `TAU` and the core description is `TAU Music Player`.
The graphical brand remains free to use the alpha mark.

## Acceptance criteria

- The metadata label uses only glyphs verified on Pocket hardware.
- Tau's alpha mark remains represented consistently in its graphical brand.
- No core, asset or settings path contains the Unicode glyph.
