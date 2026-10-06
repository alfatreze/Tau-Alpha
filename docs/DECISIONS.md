# Decisions register

Numbered decisions with reasons, so an assessed option is never re-litigated. Rejected or parked options are recorded
too, with why. Newest last. Detail lives in the spec each entry points at.

## Meter module structure (2026-09-25, B-274) -- `docs/features/meters/METER_MODULE_SPEC.md` section 25

| # | Decision | Status |
|---|---|---|
| D-M01 | One `tau-assets.bin` container (themes, meters, later icons and fonts) instead of one slot per asset type | Decided |
| D-M02 | Generic on-device Configure page kept, Diagnostic-Build-only | Decided |
| D-M03 | Omega consumes a versioned preview bundle from a Tau-Alpha release and never forks it | Decided |
| D-M04 | Lab maths: JS port + golden vectors by default; WASM per meter only if a `wasm32` toolchain is proven | Decided |
| D-M05 | `role[]` theme roles are part of the meter contract from day one | Decided |
| D-M06 | A function enters the shared meter core when a second meter needs it | Decided |
| D-M07 | Legacy meters stay exactly as they are; any look change is opt-in per meter with owner approval | Decided |
| D-M08 | Cost-guard fallback is per-meter declared, defaulting to the `VIZ_BARS`-equivalent | Decided |
| D-M09 | Preset sharing formats: `.tmeter` JSON, `.tmeterpack` zip, `tau-meter:` text; binary only on device | Decided |
| D-M10 | Templates live in the firmware registry only; user variety comes from presets | Decided |
| D-M11 | Licence for shared presets and packs is MIT | Decided (owner) |
| D-M12 | Public preset gallery | **Parked** (owner) |
| D-M13 | Build order M0 to M6 as in the spec | Decided |
| D-M14 | A meter's persistent mutable state (1 KB or more, append-and-scan) lives in PSRAM via `MTR_PSRAM`, as a ring unwrapped into a hot scratch; this is the framework's strategy for all future meters | Decided (owner, 2026-10-02) |

### D-M12 detail: public preset gallery (parked)

What it is: a hosted, browsable collection of user-submitted `.tmeter` / `.tmeterpack` presets that Omega could search
and install from.

Why parked: it needs hosting, identity, moderation and takedown handling, none of which improve the device, and the
formats already support file and text-string exchange without any of it. Nothing built today depends on it.

Residual value kept so a later gallery is cheap: the content-addressed preset `id` (dedupe), the `licence` field
(MIT default), the optional `preview` block (trace + frame hash), and the compatibility fields (`meter`, `schema`,
`requires`, `min_firmware`). Revisit after M4 has real users; if revisited, start as a static, curated index in a git
repository (reviewed pull requests) rather than a service.

## Cover / thumbnail image format (2026-09-26, B-284) -- `docs/features/IMAGE_FORMATS.md`

| # | Decision | Status |
|---|---|---|
| D-I01 | Cover art variant is palette 256 (CLUT + 8-bit indices, `TIM1` container) at 128 px on the long side | Decided (owner) |
| D-I02 | Non-square covers scale proportionally, no crop and no letterbox; the file carries the real width and height | Decided (owner) |
| D-I03 | Better on-device codecs (WebP, AVIF, JPEG XL, HEIF) rejected: entropy decoding, loop filters and RAM compete with audio on a 60 MHz core without an FPU | Rejected |
| D-I04 | BC1 and pre-scaled JPEG kept as named variants, not defaults: on the owner's nine real covers palette beat BC1 on every one and pre-scaled JPEG is smudged at small sizes and 5x slower to show | Parked |
| D-I05 | Container `TIM1` and the `tau-art/` sidecar path stay unfrozen until a firmware reader exists | Open |


## Helios architecture review follow-ups (2026-09-28/29) -- `docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md`

| # | Decision | Status |
|---|---|---|
| D-H01 | `helios_excl[]` stays a one-caller mechanism (fullscreen's CPU%/hint label) rather than gaining a synthetic second caller to "prove it generalizes." Checked for a natural second use case (VU Master's overlay -- sits in its own reserved space below the ladders, no overlap; the stress HUD row -- occupies documented "otherwise unused" screen space, no conflict; Chladni's corner -- the mechanism's own comment already rules this out, tile-replication needs the source offsets adjusted too, separate larger work) and found none currently broken in the shape the mechanism exists to fix (a fill redrawing across a persistent on-top label, a beam-race glitch). Revisit when a real second need appears -- e.g. a future meter redesign adding a persistent corner element to the normal (non-fullscreen) player screen. | Parked (owner) |

### D-M14 detail: meter state in PSRAM (2026-10-02, B-509)

Trigger: merging the Layered Wave branch left the 192 KB Diagnostic Build 368 B under its 4,096 B heap-gap floor, and 2,424 B of that was one meter's
history buffer. Chosen: move it to PSRAM as a ring (`MTR_PSRAM`, `mtr_psram_ready()`, `.psram_state` region at `0xA4009000`), with a 404 B hot scratch. Result:
heap gap 3,728 B to 5,744 B. Full mechanism, rules, cost model, risks and suggestions: `docs/features/meters/METER_MODULE_SPEC.md` section 27.

Why it is a framework decision rather than a Layered Wave fix: nearly all meter state is history that is appended and scanned, which a ~32-cycle window serves
well and on-chip RAM is wasted on; and each meter owning its own PSRAM state removes any buffer sharing between meters.

Rejected, so it is not re-litigated: **shrink the buffer** (removes a capability to fix a layout problem; three places to change), **alias onto another meter's
buffers** (works because meters are exclusive, but every switch must invalidate the other meter's initialised flag, an invisible coupling), **gate the meter out of
the build that needs the RAM** (that build exists to carry it), **cold data** (read-only; this buffer is written every frame).

Open (not decided): the window-access budget of about 700 per frame is a judgement pending Meter Sweep data; migrating Chladni's `chl_half` is a candidate that
needs an access-pattern review and owner approval (D-M07).

## Report codes (2026-10-04, B-594) -- `docs/features/BARCODE_STUDY.md`

- **D-R01: carry reports as a pixel grid (TPG), not JAB Code or a 3-plane QR.** The path is a lossless screenshot read on a PC, so the area is better spent on data than on camera robustness. JAB: large implementation (ISO 23634, LDPC, palettes, docking), reference code under a licence that looks unfriendly to an MIT repo (unverified, from memory, check before any reuse), no Rust decoder, estimated 4-5 KB on this screen. 3-plane QR: about 3x a format that already fails at 2 px modules. Not built. Revisit only if a phone-camera path for large reports becomes a requirement.
- **D-R02: robust grid is the default view, lossless the automatic fallback, QR the third view.** Owner decision 2026-10-04. Robust (4x4 cells, 2 bits per channel, up to 6,059 B) survives JPEG q80+ and common resizing; lossless (up to 259,184 B) only a lossless PNG. A bad channel fails loudly (CRC32), never silently. **No error correction in v1**: Reed-Solomon in robust mode is the open option if the shareable path matters.
- **D-R03: one drawing path and one caption set for every code page** (`rep_draw()`): block centred, frame, title / view / detail at the top, controls at the bottom. Owner decision 2026-10-04 (captions had drifted apart across the seven pages).
- **D-R04 (REVERSED 2026-10-06 by the owner: the pixel grid is now the default, no build property needed; `fw/build.sh` defaults `TPG=1`, `TPG=0` gives the old QR-only pages; the Diagnostic Build only, the release ROM has no report pages). Original text: `TAU_TPG` stays off by default (hardware-tested only in the `TAU DEV BARCODE NN` builds).** Turning it on in the shipping Diagnostic Build is a one-line default flip (as `LPC_FW` was) and an owner decision; so is `TAU_QR_MAXV=14` (QR phone-scannable only), to wait until Tau Omega decodes grids. The QR encoder tables are not trimmed: that saves cold code only, not RAM.
- **D-R05: record tag numbers are claimed on `main`.** `SR_T_LOAD2` (25) was taken on `main` while this branch used 25 for `SR_T_INFOTEXT`; the branch moved to 26/27. Before adding a tag on a branch, read the enum on `main`. Captures made with the old numbers are container fixtures only.
- **Assessed and not done:** a unique device id in every report (privacy: makes all of one person's screenshots linkable; the APF gives cores no device id, the Cyclone V chip id needs RTL and a fit); wall-clock time from APF command 0x0090 (needs RTL; the capture time is already in the screenshot file name).

