# Meter / visualizer interface: specification

**As of:** 2026-09-25, tau-alpha commit `952d403` plus uncommitted work through audit trail entries
B-208–B-221 (the enum-hole fix, `VIZ_WINAMP_BARS`/`VIZ_WINAMP_SCOPE`, the Configure editor page, and
the QR config export — none of it hardware-tested yet as of this writing).

This is the authoritative description of Tau's visualizer ("meter") system: what a meter *is* to the
firmware, how one is identified, what a Winamp-style meter's configuration looks like, and how that
configuration can currently leave the device. It exists so **Tau Omega can build its own consuming
spec by inspecting real behaviour against this document**, not by guessing — see
`docs/CROSS_PROJECT_INTERFACE.md` for the general rule this follows. Nothing here should be copied
into Tau Omega verbatim; treat every section as "here is where to look and what to expect," and
verify against a real captured artifact (a real `interact_persist.json`, a real decoded QR) before
trusting a number.

## 1. Meter identity: the `VIZ_*` enum

Every meter is one value of an **append-only** enum in `fw/player.c` (`VIZ_BARS = 0, VIZ_WATER, ...`).
"Append-only" is load-bearing, not a style preference: `viz_mode` persists on the SD card as a raw
integer index (`interact_persist.json`, `SW_VIZ`/persist id 15 — see `docs/CROSS_PROJECT_INTERFACE.md`
§5). Reordering or inserting a value would silently repoint every already-saved card's meter choice
at a different, unrelated meter after a firmware update. **A consuming tool must never assume the
enum order is meaningful beyond "whatever the current firmware source says," and must never write a
`viz_mode` value the current firmware doesn't recognise.**

Current full list, in on-disk index order (0-13):

| Index | Enum name | Status |
|---|---|---|
| 0 | `VIZ_BARS` | Ships, hardware-blit-accelerated (`OP_BAR`) |
| 1 | `VIZ_WATER` | Ships |
| 2 | `VIZ_LEVELS` | Ships |
| 3 | `VIZ_SCOPE` | Ships |
| 4 | `VIZ_WAVE` | Ships |
| 5 | `VIZ_VU` | Ships |
| 6 | `VIZ_SCROLL` | Ships |
| 7 | `VIZ_MIRROR` | Ships |
| 8 | `VIZ_DOTS` | Ships |
| 9 | `VIZ_EYE` | Ships |
| 10 | `VIZ_LED` | Ships |
| 11 | `VIZ_TAPE` | **Parked, not deleted** — excluded from selection (see §2), kept for reference |
| 12 | `VIZ_WINAMP_BARS` | New, firmware-complete, not yet hardware-tested |
| 13 | `VIZ_WINAMP_SCOPE` | New, firmware-complete, not yet hardware-tested |

## 2. Selectable vs. persisted: the one exception (`VIZ_TAPE`)

`VIZ_TAPE` (index 11) is a real, complete meter — its drawing code and its choice-list thumbnail
still exist — but it is deliberately excluded from selection (the owner dislikes its visuals and
plans to design a replacement later). Because it sits in the *middle* of an append-only enum rather
than at the end, "selectable" is not simply "index < N": `fw/player.c`'s `viz_sel_to_mode()` /
`viz_mode_to_sel()` do a row↔mode remap around that one gap for the Settings choice list and the
X-button cycle. **A saved `viz_mode` of 11 is still a value the firmware recognises and will not
error on** — the persisted-setting restore path only rejects it from being silently re-adopted as
the *active* choice, it does not treat it as invalid data. If more meters are ever parked the same
way, this becomes a proper exclusion list rather than a single hardcoded skip — not built, because
there has only ever been one.

## 3. The two new meters: what they are, in firmware terms

`VIZ_WINAMP_BARS` / `VIZ_WINAMP_SCOPE` are the classic Winamp-style bars/scope, built with a fluid
easing layer instead of an instant snap (`docs/AUDIT_TRAIL.md` B-215). Bars render as one hardware
`OP_BAR` command per column, reading the existing 16-band octave filter (`spec_lvl[]`, shared with
`VIZ_LED`); Scope reuses `VIZ_WAVE`'s draw path with added temporal smoothing.

Both share one configuration struct, `wviz_cfg_t` (`fw/player.c`):

| Field | Type | Range | Meaning |
|---|---|---|---|
| `bands` | u8 | 4–16 | Bar columns. 16 is the octave filter's real resolution; fewer merges adjacent bands, more would mean interpolating fake data — 16 is a hard ceiling, not a rounding choice |
| `ease_mode` | u8 | 0–3 | 0 instant, 1 linear, 2 exponential, 3 spring (approximated critical damping, not exact — see the code comment in `fw/player.c`'s `wviz_ease_step()`) |
| `attack` | u8 | 1–100 | Rise speed toward a louder target |
| `release` | u8 | 1–100 | Fall speed toward a quieter target |
| `peak_on` | u8 (bool) | 0/1 | Falling peak-cap marker enabled |
| `peak_gravity` | u8 (bool) | 0/1 | 0 = linear fall, 1 = accelerating fall |
| `peak_hold_ms` | u16 | 0–800 | How long a new peak holds before it starts falling |
| `peak_fall` | u8 | 1–100 | Fall speed once it starts |
| `scope_smooth` | u8 | 0–90 (percent) | Temporal smoothing on the scope trace |
| `scope_trail` | u8 | 0–80 (percent) | **Accepted but has no visible effect yet** — a real soft trail needs alpha blend, which is built but shelved project-wide (timing risk, see the README's Tradeoffs section); a release build always does a plain full erase regardless of this value |

Five built-in presets exist in firmware (`wviz_presets[]`, `fw/player.c`), each a full `wviz_cfg_t`:
`FLUID` (index 0, also the boot default), `CLASSIC`, `BOUNCY`, `SLOW FADE`, `SNAPPY`. **These are
hardcoded in firmware today** — there is no data slot yet through which a different preset list can
be supplied (§5).

## 4. Where a chosen configuration lives today: nowhere, on purpose

There is currently **no persistence** for `wviz_cfg_t` across a restart. This was a deliberate,
recorded decision (`docs/AUDIT_TRAIL.md` B-216), not an oversight, for two concrete reasons checked
before building anything:

1. The obvious channel — `fw/settings.inc`'s `SW_*` persist register — is a **hardwired 4-bit index
   in RTL** (`mp3_soc.v`'s `set_idx[3:0]`), already fully used (16 of 16 slots) with the media
   library enabled. Adding one more persisted value needs an RTL change and a Quartus fit, not a
   firmware-only change.
2. A *nonvolatile data slot* (the APF mechanism that writes a slot's contents back to the SD card on
   Quit) has real, documented failure history in this exact project — it hung the Pocket on Quit and
   on boot, and separately destroyed three libraries, before being retired
   (`docs/A088_UPSTREAM_CHECKS.md`, `docs/MEDIA_LIBRARY_0.4_BRIEF.md`). It is not used for anything,
   deliberately, and this feature does not reopen that door.

**What exists instead:** the on-device Configure editor (Settings > Appearance > Meter > Configure)
edits a live, in-RAM `wviz_cfg` directly — changes take effect immediately and are explicitly
"session-only, for playing around" (the owner's own framing). Getting a tuned configuration *out* of
the device uses the QR export (§6) rather than any file the device itself writes.

## 5. Planned, not built: a Tau-Omega-synced preset/config data slot

The forward plan (recorded, not yet started) is a new **read-only, `deferload` data slot** — same
proven-safe pattern as `tau-library.tdb`/`tau-cold.bin`, explicitly *not* the nonvolatile mechanism
§4 rules out — tentatively named `tau-meters.cfg`, holding:

- Per-field min/max/default ranges (so both the on-device editor and any future Tau Omega editor
  derive their bounds from one shared source instead of two hand-maintained copies)
- A preset list, structurally like §3's five built-ins but editable from Tau Omega and synced onto
  the card the same way the library index is
- A version/magic/CRC32 header, same convention as `tau-library.tdb` (`tools/tau_library.py`'s
  `MAGIC`/`VERSION`/CRC pattern) — **whatever the real header ends up being, follow that precedent
  exactly rather than inventing a new one**, since the CRC-and-version discipline is what makes the
  format's own failure modes (a truncated file, an old build reading a newer format) safe.

**No slot id has been assigned. No file format has been fixed.** Slot 6 is already known to be
double-booked (`docs/CROSS_PROJECT_INTERFACE.md` §5) — whichever slot this eventually claims must be
checked against `tools/tau_data_slots.py`'s current state at the time, not against this document,
since this document will not be kept in lockstep with that decision in real time.

## 6. What can leave the device today: the QR export

Settings > Appearance > Meter > Configure > **Export QR** builds a report using the *exact* existing
Check/Blit-Test pipeline (`fw/suite_core.h`'s `sr_init`/`sr_tlv`/`sr_finish`/`sr_text`/`qr_encode`,
the `CHK_REC`/`CHK_TXT`/`CHK_QR` SDRAM staging buffers) rather than a second channel, and shows it as
a QR code. It is **Diagnostic-Build-only**, matching every other QR feature in this project — a
plain release core does not have this row do anything (`docs/CROSS_PROJECT_INTERFACE.md`'s standing
trap about Check being Diagnostic-Build-only applies identically here).

The record carries one new tag, `SR_T_WVIZCFG` (value 17, `fw/suite_core.h`), a fixed 13-byte
payload:

| Byte(s) | Field | Notes |
|---|---|---|
| 0 | `mode` | 0 = bars, 1 = scope |
| 1 | `preset_idx` | 0–4 = one of §3's five presets, `0xFF` = hand-edited ("custom") |
| 2 | `bands` | §3 |
| 3 | `ease_mode` | §3 |
| 4 | `attack` | §3 |
| 5 | `release` | §3 |
| 6 | `peak_on` | §3 |
| 7 | `peak_gravity` | §3 |
| 8–9 | `peak_hold_ms` | little-endian u16 |
| 10 | `peak_fall` | §3 |
| 11 | `scope_smooth` | §3 |
| 12 | `scope_trail` | §3 |

Decodable today by `tools/decode_tau_suite.py` (the same tool that decodes a Check report), whose
own field-by-field unpack is the reference implementation — **read that tool's tag-17 branch, not
this table, if the two ever disagree**, since the tool is checked against a real host-side round-trip
test (`sim/test_suite.py`, "decode wvizcfg") and this document is not automatically re-verified
against the code the way that test is. A `SR_T_WVIZCFG` record is a one-off export, not a Check
report — decoding one produces `verdict: "no result"` and an empty `tests` list, which is correct,
not a bug: there is nothing to pass or fail, only a config to read back.

## 7. Meter modularity: current state, and a proposed cleaner design

**Honest current state:** adding `VIZ_WINAMP_BARS`/`VIZ_WINAMP_SCOPE` this pass touched roughly six
separate places by hand: the enum, the choice-list name array (`set_viz[]`), the meter-thumbnail
tables (`meter_thumb_pal`/`meter_thumb_off`/`meter_thumb_rle`), the X-button toast-message chain, the
per-frame draw dispatch (`ui_draw_dynamic_cold()`'s `if (viz_mode == ...)` chain), and — for this
specific pair — the octave-cascade CPU-budget gate (`meter_afford()`'s caller list). That is not a
modular system; it is a working, hand-maintained one, and every future meter pays the same six-place
tax. This is worth fixing before many more meters get added, not after.

**Proposed direction (design only, not started):** a small const descriptor table, one entry per
meter —

```c
typedef struct {
    const char *name;              /* choice-list label, e.g. "WINAMP BARS" */
    uint8_t     selectable;        /* 0 for VIZ_TAPE's case -- replaces viz_sel_to_mode()'s special-case remap */
    uint8_t     needs_spectrum;    /* replaces the hand-maintained meter_afford() caller list */
    void      (*tick)(uint32_t x0, uint32_t y, uint32_t w, uint32_t h, uint16_t bg);
    uint8_t     thumb_idx;         /* index into the existing meter_thumb_* tables, unchanged */
} viz_desc_t;
static const viz_desc_t viz_table[VIZ_COUNT] = { ... };
```

This would let the choice list, the X-cycle, the toast message, and the per-frame dispatch all become
a single lookup into `viz_table[viz_mode]` instead of five separate hand-written switches — adding a
meter becomes "add one table row and one `tick()` function," and *removing* one (parking it, the way
`VIZ_TAPE` is parked today) becomes flipping `selectable` to 0 instead of writing a bespoke remap
function. `wviz_bars_tick()`/`wviz_scope_tick()` (`fw/player.c`) are already shaped to fit this
exactly — they were factored out with explicit `(x0, y, w, h)` arguments specifically so the
Configure page could call them directly, which is the same signature this table's `tick` function
pointer would want.

**Not done.** This would touch every existing meter's call site, is a real refactor of working,
hardware-verified code, and deserves its own dedicated build-and-reverify pass rather than being
folded into an unrelated feature change — the same discipline this project applies to every
non-trivial RTL or firmware restructuring. Recorded here so it is designed before it is needed again,
not because two new meters justify it on their own.

## 8. What Tau Omega should actually do with this document

Per `docs/CROSS_PROJECT_INTERFACE.md`: don't copy this file. Read it, then verify the specific facts
your feature needs against a real artifact — a real decoded `SR_T_WVIZCFG` QR (once one exists; none
has been captured from real hardware as of this writing, see §6's own caveat), the actual current
`fw/player.c` enum if a persisted `viz_mode` value needs interpreting, `tools/decode_tau_suite.py`'s
own tag-17 branch if this table and that code ever disagree. Record what you verify, and what's still
open, in your own `docs/FIRMWARE_SYNC.md` — that is the correct home for an Omega-side tracking
checklist, not a second copy of this file.
