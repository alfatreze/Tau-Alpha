# Cassette meter (archived 2026-09-26)

The cassette meter (`VIZ_TAPE`, enum slot 11) drew a 150x96 cassette: shell, label carrying the playlist name, three stripes, a
window with two hubs that rotate and coast to a stop when paused, wound-tape discs, a rim that took the overall level and a
ribbon between the reels that glowed with bass. It was ported from HarpMudd upstream v1.5.0 / release-1.5.1 (upstream commits
`3404545`, `d76267e`, `154234f`, `f0d6b60`, `4024104`, `42f3853`, `7e9ceec`). The owner disliked its visuals and removed it
(parked 2026-09-25, removed 2026-09-26); a new design may replace it.

## Where it is
- `cassette_meter_code.c.txt` here: the three code parts, verbatim (definitions/state/helpers, the drawing block, the pause-settling
  term). Not compiled.
- `thumbnail_entry_11.txt` here: its Settings preview (palette and stream).
- Git: tag `archive/cassette-meter` is the last commit that still has it in `fw/player.c` (search `VIZ_TAPE`); `git show
  archive/cassette-meter:fw/player.c`.

## What was removed from the build
Enum slot `VIZ_TAPE` -> `VIZ_RETIRED_TAPE` (the enum is append-only because the setting persists an index; a saved cassette
setting is ignored). The definitions block, the `if (viz_mode == VIZ_TAPE)` branch in `ui_draw_dynamic_cold`, the `tape_face`
reset in `ui_meter_faces_invalidate`, the `vu_settling` term, the spectrum-gating mentions, the toast fallback, the Settings
name and thumbnail data (entry 11 is now empty). `UI_WAVE_TOP` (24 extra rows above the meter box, only the cassette used them)
was kept.

## To bring it back
1. Rename `VIZ_RETIRED_TAPE` to `VIZ_TAPE` and add it to `viz_order[]` (raise `VIZ_SEL_COUNT`), and to the Meter slider's max if needed.
2. Paste part 1 before `ui_draw_dynamic_cold`, the drawing block in the meter switch before the plain-bars loop, and the
   `vu_settling` term; re-add `tape_face = 0;` to `ui_meter_faces_invalidate()`; add `VIZ_TAPE` to the two spectrum-gating conditions.
3. Restore `set_viz[11]`, the toast, `meter_thumbs.h` entry 11 (from `thumbnail_entry_11.txt`) and the `VIZ_TAPE == 11` assert.
4. It depends on `spec_lvl[]` (hub tint from the mid bands, bass glow), `peak_amp`, `pl_name_full`, `ui_bg_restore`, `fb_round_rect`
   and `fb_text_boxed`. Cost when it was in: about 2.7 KB of cold code and 0.8 KB of RAM (`tape_hub` rodata 456 B included).
