# Meter module structure: one manifest, one contract, one preview stack

**Status (2026-09-25, B-274, part 2 added the same day):** design only; sections 12 to 17 (capability governance, shared meter core, operations) were added after owner review. No firmware, RTL, tool or card change. Extends
`docs/features/meters/METER_CONFIG_SPEC.md` section 7 (the `viz_desc_t` idea), `docs/features/THEME_SPEC.md` (roles, theme file) and
`docs/features/meters/CHLADNI_METER_SPEC.md` (the portable-core + lab pattern). Owner decisions needed are listed in section 10.

## 1. The constraint that shapes everything

Tau is a soft RISC-V core with no dynamic loader and no MMU. Code cannot be plugged in at runtime; every meter's
code is compiled into `tau.rom` or the cold image. So a "plugin API" here has two halves, and they must not be
confused:

| Half | What varies | Who can change it | Mechanism |
|---|---|---|---|
| **Code module** | The drawing and maths | A developer, at build time | A C module obeying a fixed contract (section 3) |
| **Data plugin** | Parameters, presets, palette roles, ordering, which meters are selectable | Anyone, at any time, through Omega | A validated read-only data file (section 6) |

Omega only ever touches the data half. It never runs, ships or generates meter code. That is what keeps it safe
(a bad file can select a bad value, never crash the CPU) and it matches the fail-safe rule used everywhere else
in this project: bad CRC, bad version or unknown id falls back to the built-in default, never a blank screen.

## 2. What exists today (audit)

Fourteen `VIZ_*` values in `fw/player.c`. Eleven native meters are inline `if (viz_mode == VIZ_x) { ... }` blocks in
`ui_draw_dynamic_cold()` (`VIZ_BARS` 0 to `VIZ_LED` 10, shared state in file-scope statics), `VIZ_TAPE` is parked,
and `VIZ_WINAMP_BARS/SCOPE` are the only ones already factored into `(x0, y, w, h, bg)` functions with their own
config struct. Adding one meter touches six hand-maintained places (enum, name list, thumbnail tables, X-cycle
toast, dispatch chain, the `meter_afford()` spectrum gate). Parameters are hard-coded per meter. The configurator
page and its QR layout are hand-written for one meter pair. The lab tools (`tools/lab/chladni_lab.html`, the Fluid
Bars and Copper/Geiss sandboxes) each re-implement the framebuffer, the ballistics and the audio sources.

Three lessons already paid for, which the design below encodes rather than rediscovers:

- A shared settings struct across two modes made them fight each other (B-234). **State is per meter.**
- Contexts changing (preset, mode, page) left stale pixels and a stale redraw cache (B-234). **`force` is part of
  the contract**, not an ad hoc flag.
- Raw pointers to SDRAM hang (B-192). **Modules draw only through engine primitives**, never by writing memory.

## 3. The on-device contract (C)

One header, `fw/meter.h`, no dependency on `player.c` internals. A module supplies a descriptor; the core supplies
an input struct and a small primitive set. Nothing else crosses the boundary.

```c
typedef struct {                 /* everything a meter may read, refreshed once per display frame */
    const uint8_t  *spec;        /* 16 bands, 0..255, after the shared ballistics (spec_lvl)       */
    const int8_t   *wave;        /* WAVE_COLS signed samples (wav_v)                               */
    uint16_t        peak, peak_l, peak_r;   /* headroom-scaled, as MTR_HEADROOM already gives them */
    uint32_t        frame;       /* display frame counter                                          */
    uint16_t        dt_ms;       /* ms since the last tick                                         */
    uint16_t        x, y, w, h;  /* the rect the meter owns; it never draws outside it            */
    uint16_t        bg;          /* flat background when the host says so                          */
    const uint16_t *role;        /* 12 theme roles, RGB565 (THEME_SPEC.md); never hard-code colours */
    uint8_t         force;       /* 1 = repaint the whole rect and drop every redraw cache         */
} mtr_in_t;

typedef struct {
    const char *key;             /* "winamp_bars": stable, lower_snake, the manifest key           */
    uint8_t     id;              /* on-disk index, append-only (METER_CONFIG_SPEC.md section 1)    */
    char        name[16];        /* choice-list label                                              */
    uint8_t     flags;           /* MTR_SELECTABLE | MTR_NEEDS_SPEC | MTR_NEEDS_HW_SPEC | MTR_NEEDS_WAVE |
                                    MTR_STEREO | MTR_GRADIENT_BG                                   */
    uint8_t     cost_class;      /* 0 = <=40 cmds/frame (VIZ_BARS), 1 = <=100, 2 = <=400          */
    uint8_t     nparams;
    const mtr_param_t *params;   /* section 4                                                      */
    uint8_t    *value;           /* nparams live values, owned by the module, one array per meter  */
    void      (*open)(const mtr_in_t *);   /* once on entry; must leave the rect fully painted      */
    void      (*tick)(const mtr_in_t *);   /* once per frame; never blocks, never allocates         */
    void      (*close)(void);              /* optional; release nothing you did not take            */
    uint8_t     thumb;           /* index into the shared thumbnail tables                         */
} mtr_desc_t;
```

Rules a module must meet (each is a Check-able property, section 8):

1. **No blocking.** `tick` returns within its declared cost class. Waiting for the engine is the host's job
   (`fb_wait` policy, beam gating, `meter_afford()`), not the module's.
2. **Primitives only.** `mtr_bar`, `mtr_rect`, `mtr_blit`, `mtr_cblit`, `mtr_sblit`, `mtr_rrect`, `mtr_text` are thin
   inlines over the existing `fb_*` calls. Their only added job is counting commands, so cost is measured centrally
   and identically for every meter.
3. **Own your state, clamp your parameters.** Statics live inside the module. Every parameter is clamped against the
   table on every load, whatever its source (section 6 says why).
4. **`open` and `force` both mean "assume nothing about the pixels".**
5. **No colour literals.** Colours come from `role[]` or from a fixed-tagged palette declared in the manifest.
6. **Cold by default.** Modules are `COLD_FN3`. A module that must stay hot says so in its manifest and pays for it in
   the heap-gap ledger.

The host (a new `fw/meter_host.inc`, the only file that knows about `viz_mode`) does the rest: selection, X-cycle,
the toast, spectrum-source gating from `MTR_NEEDS_*`, `force` on every context change, per-meter parameter storage,
and the generic Configure page. Parking a meter is clearing `MTR_SELECTABLE`; the `viz_sel_to_mode()` remap goes away.

## 4. The manifest: single source of truth

Each meter is `fw/meters/<key>/` with:

```
meter.json      the manifest (below)
<key>.inc       the C module (desc + tick)
<key>_core.h    optional portable maths (no MMIO, no float; the chladni_core.h pattern)
meter.js        the preview implementation (section 7)
thumb.png       56x32 source for the choice-list thumbnail
golden/         recorded input traces and expected frames (section 8)
```

```jsonc
{
  "schema": 1,
  "key": "winamp_bars", "id": 12, "name": "WINAMP BARS",
  "flags": ["selectable", "needs_spec"], "cost_class": 0,
  "params": [
    { "key": "bands",   "label": "Bands",  "type": "u8",   "min": 4, "max": 16, "step": 1, "default": 16 },
    { "key": "ease",    "label": "Easing", "type": "enum", "values": ["INSTANT","LINEAR","EXPONENTIAL","SPRING"], "default": 2 },
    { "key": "hold_ms", "label": "Peak hold", "type": "u16", "min": 0, "max": 800, "step": 10, "default": 300, "unit": "ms",
      "when": { "peak_on": 1 } }
  ],
  "presets": [ { "name": "FLUID", "values": { "bands": 16, "ease": 2, "hold_ms": 300 } } ],
  "roles_used": ["accent", "accent-2", "surface-track"]
}
```

`tools/gen_meters.py` reads every manifest and emits, deterministically and checked in CI like `check_cold_calls.py`:

| Output | Replaces |
|---|---|
| `fw/meters_gen.h`: the `VIZ_*` enum, the `mtr_desc_t` table, per-meter param tables, `VIZ_COUNT` static asserts | enum, name list, `viz_sel_to_mode()` and its asserts |
| `fw/meter_thumbs.h` (existing generator, now driven by manifest order) | the hand-kept thumbnail tables |
| `tools/meters_schema.json` (the whole registry, one file) | nothing today; this is what Omega and the lab read |
| `docs/METER_REGISTRY.md` | the hand-written table in `METER_CONFIG_SPEC.md` section 1 |

The generator refuses: a duplicate or non-append-only `id` (compared with the previous released registry kept in
`tools/meters_released.json`), a default outside its own range, a preset that violates the parameter table, a
`when` clause naming a missing parameter, and any `roles_used` not among the 12 roles. This is the same idea as the
audit-trail checker: the class of mistake that has actually bitten this project is caught by a script, not by review.

Parameters are deliberately limited to `u8`, `u16`, `bool`, `enum`. That keeps the QR wire format, the file format
and the Configure page all generic, and it means no meter can require a UI widget nobody built.

## 5. The generic Configure page and the QR export

The hand-written Winamp Configure page and its 13-byte `SR_T_WVIZCFG` layout become one generated page and one
generic record. The page walks `params[]`, honours `when`, and shows presets from the same table. It is the same
page for every meter, so a new meter gets an on-device editor for free, and the Winamp-specific page is deleted.

New QR tag `SR_T_METERCFG` (next free value in `fw/suite_core.h`):

```
meter_id u8 | schema u8 | preset u8 (0xFF = custom) | nparams u8 | value[nparams]  (each param = 1 byte, u16 = 2, LE)
```

Omega decodes it generically from `meters_schema.json` (which gives the order and widths for that `schema`), so a
new meter needs no decoder change on either side. `SR_T_WVIZCFG` (17) stays decodable forever, as an old-firmware
record. The on-device editor stays a Diagnostic-Build feature exactly as today; per the parked-configurator note,
Omega is the intended editor of record and the on-device page is the fallback for people without it.

## 6. Data plugin file and how Omega uses it

One read-only `deferload` data slot, same proven-safe class as `tau-library.tdb` and `tau-cold.bin`; never the
nonvolatile write-back mechanism (`METER_CONFIG_SPEC.md` section 4 has the history). **Do not spend a second slot on
themes**: slots are scarce and slot 6 is already double-booked. Instead one container, `tau-assets.bin`:

```
header  : magic "TAUA" | version u16 | section_count u16 | crc32 of the section table u32
section : tag[4] | offset u32 | length u32 | crc32 u32          (repeat)
tags    : "THEM" theme file (THEME_SPEC.md section 5, unchanged)   "METR" meter config   (later: "ICON", "FONT")
```

`METR` section, little-endian:

```
meter_count u16
meter[]: id u8 | schema u8 | flags u8 (bit0 selectable override) | order u8 (list position) |
         preset_count u8 | preset[]: name[16] | value[nparams]
         default_preset u8
```

Firmware rules, all following the existing precedents:

- Verify container CRC, section CRC and version before reading anything; any failure means built-in defaults,
  silently, and the Info page shows a code (as the cold image does with its E-codes).
- Unknown meter id: skip. Unknown schema newer than firmware: skip that meter only. Fewer params than the firmware
  knows: the rest take defaults. More: ignore the tail. **Every value is clamped against the compiled table**; the
  file's own ranges are advisory for editors, never trusted by the device.
- The file can reorder and hide meters and replace presets. It cannot add a meter, because meters are code.

What Omega does: reads `tools/meters_schema.json` (from a tagged Tau-Alpha release, verified by hash), shows a
parameter editor generated from it, embeds the preview stack (section 7) for live tuning, validates, and writes
`METR` into `tau-assets.bin` next to `THEM`. Because both sides are driven by the same registry, "which meters and
parameters exist" is never hand-copied. This is the surface to add to `CROSS_PROJECT_INTERFACE.md` section 5, and per
its rule Omega verifies against a real captured container from a card, not a fixture invented from this text.

Persistence, honestly: the active *meter* still persists as `viz_mode` (persist id 15). On-device parameter edits
remain session-only, because the persist register is full. Omega writing `METR` is what makes tuned values durable
across restarts, which is a cleaner story than widening RTL for a setting few people will touch.

## 7. The preview stack

Goal: a new meter is previewed in the browser in an hour, with the same look, ballistics, colours and cost numbers
the device will produce, and the same code path for Omega.

```
tools/meters/preview/
  index.html            shell: picks a meter from the registry, renders controls, cost panel, source picker
  tau_fb.js             RGB565 400x110 framebuffer + the mtr_* primitives, with Tau's quantisation and cost counters
  tau_theme.js          12 roles, Dark/Light, ui_grad_at() background exactly as ui_grad_set builds it
  tau_audio.js          sources: synthetic demo (deterministic), sweep, sine, file (FFT folded to 16 half-octave bands),
                        mic, and TRACE (replay of band levels recorded from real firmware)
  tau_ballistics.js     the firmware's spec_lvl attack/release, headroom 3/4, log mapping: the source of the 16 bands
  tau_controls.js       builds sliders/selects/toggles from params[]; honours `when`; preset dropdown; QR-config paste
  tau_cost.js           commands/frame, estimated SDRAM %, CPU model, RAM held, against the declared cost class
  build.py              inlines everything into ONE html file (publishable as an artifact, or loaded in Omega's webview)
tools/meters/preview/fixtures/    recorded traces (levels.json) captured on hardware, provenance recorded
```

A meter's `meter.js` is small and has the same shape as the C module:

```js
export default {
  key: 'winamp_bars',
  open(ctx)  { ctx.fb.rect(ctx.x, ctx.y, ctx.w, ctx.h, ctx.bg); },
  tick(ctx)  { /* reads ctx.spec[], ctx.p (params by key), ctx.role; draws with ctx.fb.bar/rect/... */ }
};
```

Design decisions that make it reusable rather than a sixth copy of the same lab:

- **Same primitive names, counted centrally.** `ctx.fb.bar` in JS and `mtr_bar` in C are one vocabulary, so a port is a
  transliteration and the cost panel is comparable between meters and against `VIZ_BARS`' 36 commands.
- **Parameters come from the manifest, not from hand-written HTML.** Adding a parameter is editing `meter.json`.
- **Audio is a trace first.** The lab's live sources are good for feel; regression needs determinism. Recording
  `spec_lvl` per frame from a real Tau (a Diagnostic-Build dump, or the existing test album through the profile
  build) into a fixture means the same input can be replayed in the browser, in the native harness and in the
  rv32sim run. `tau_ballistics.js` exists so file/mic sources still look like the device.
- **Maths lives once, in C.** Where a meter has real maths (Chladni), its portable `*_core.h` is the source. Preferred
  route is compiling that header to WASM for the lab so lab and firmware cannot drift; if the toolchain has no
  `wasm32` target (Apple's clang usually lacks `wasm-ld`), fall back to a JS port that is proven equal to the native
  harness by golden vectors (section 8). Decide this per meter, not globally.
- **One file out.** `build.py` output is the artifact to share and the bundle Omega embeds. Omega treats it as a
  versioned vendor drop from a Tau-Alpha release (hash recorded), consistent with the no-shared-files rule.
- **Honesty flags built in.** The cost panel labels every figure "model estimate" until a hardware Check has measured
  that meter, and marks features gated on shelved hardware (alpha blend) as unavailable rather than silently drawing
  them, so the lab cannot promise what a release build will not do.

## 8. Verification: what "done" means for any meter

A meter is finished when all of these pass, in this order of cheapness:

1. `gen_meters.py` clean (ids, ranges, presets, roles).
2. Native harness (`tools/host/<key>_harness.c`, the chladni pattern) runs `<key>_core.h` and matches its own invariants.
3. **Golden frames:** the JS module and the C module, fed the same trace and parameters, are diffed frame by frame
   (C side through the same reference-renderer route as `blit_reference.py`, or the rv32sim firmware harness).
   Exact match for solid-colour meters; a stated tolerance and the reason for gradient ones.
4. `make test-host` includes the meter suite; `check_cold_calls.py` passes; heap gap stays above its floor.
5. Diagnostic-Build **meter sweep** (a small extension of the existing Blit Test / Decode Sweep shape): for each
   selectable meter, open it, run 10 s over the Test Album, record commands/frame, late underruns and yield time.
   Reported in the Check QR. This turns "the new meter is fine" into a measured line per meter, the same discipline as
   the blit-storm test, and closes the "cost vs classic" TBD in the README.
6. **If the meter keeps persistent mutable state of 1 KB or more** (a history ring, an accumulator): it uses `MTR_PSRAM` and follows section 27.3, with a
   ring-versus-reference test in the style of `sim/test_lw_ring.py` (far more pushes than the ring length, a mutation check) in addition to golden frames,
   and a hardware label in the audit trail before it is called done.
7. Only then a hardware install and a look.

## 9. Existing and new meters mapped onto the structure

| Meter | Today | Module route | Notes |
|---|---|---|---|
| `VIZ_BARS` | inline, `OP_BAR` | wrap as descriptor, block moved verbatim | Cost baseline (36 cmds) |
| WATER, LEVELS, SCOPE, WAVE, VU, SCROLL, MIRROR, DOTS, EYE | inline | wrap verbatim, one at a time, only when touched | Each already reads shared statics; move those into the module when it moves |
| `VIZ_LED` | inline, hardware spectrum | wrap; `MTR_NEEDS_HW_SPEC` | Gate moves from `meter_afford()`'s caller list to the flag |
| `VIZ_TAPE` | inline, parked | descriptor with `selectable` = 0 | Code kept, no remap needed |
| `VIZ_WINAMP_BARS/SCOPE` | factored, own config | first real modules, params in manifest | Replaces `wviz_cfg_*` and the bespoke page |
| Chladni (planned) | core + lab | first *new* module through the whole pipeline | Its `chl_preset_t` becomes manifest presets |
| Copper, Geiss, Fluid variants | labs only | same route if ever built | Geiss' RLE-run cost is measured by the same cost panel |

Wrapping legacy meters must be **behaviour-neutral and proven so**: the verification is that `release` and both
diagnostic ROMs are byte-identical, or that any difference is explained line by line. That is the same check used
for the build-target cleanup (B-248..B-251).

## 10. Build order and open decisions

| Step | Content | Proof |
|---|---|---|
| **M0** | `fw/meter.h`, `meters/*/meter.json` for the existing 14, `gen_meters.py`, descriptor table replacing enum/name list/remap; dispatch chain unchanged | ROMs byte-identical, `make test-host` |
| **M1** | Preview stack v1 (`tau_fb`, `tau_theme`, `tau_audio`, `tau_ballistics`, `tau_controls`, `build.py`) with Winamp pair and `VIZ_BARS` as the first meters | Lab renders match the sandbox they came from |
| **M2** | Winamp pair become real modules; generic Configure page and `SR_T_METERCFG`; delete the bespoke page | Golden frames, host QR round-trip test |
| **M3** | Trace recorder (Diagnostic Build) and golden-frame diff in `make test-host` | A recorded real trace reproduces bit-exact |
| **M4** | `tau-assets.bin` container, `METR` and `THEM` loaders with fail-safe fallbacks, Omega writer, registry hand-off | Real captured container decoded by Omega |
| **M5** | Chladni through the full pipeline; meter sweep test on hardware | Measured cost row per meter |
| **M6** | Legacy meters wrapped opportunistically | Byte-identical ROM each time |

Decisions I need from the owner before M0:

1. **Container vs separate slots.** Recommend one `tau-assets.bin` (themes, meters, later icons and fonts). It needs one
   new slot number, and the slot-6 conflict has to be settled first.
2. **Is the on-device Configure page kept?** Recommend keep it, generic and Diagnostic-Build-only, as the no-Omega fallback.
3. **Preview hand-off to Omega:** versioned bundle from a Tau-Alpha release (recommended) versus Omega owning a fork of
   the stack. A fork reintroduces the drift the cross-project rules exist to prevent.
4. **Lab maths route per meter:** WASM from the C core where the toolchain allows, otherwise JS plus golden vectors.
5. **Do the theme roles land before M2?** `role[]` is in the contract from day one, but until the role table exists the
   host passes today's constants through it, so nothing blocks.

## 11. Risks

- **Descriptor indirection cost.** One function-pointer call per frame is negligible against a frame budget of tens of
  thousands of microseconds; it is not on the audio path.
- **Heap gap.** `release` has room since G4=3, the diagnostic builds do not (floors of 4 KiB). Manifests carry a
  `hot_bytes` estimate and the generator sums it, so a module cannot land unnoticed.
- **Refactor risk on hardware-proven meters.** Mitigated by wrapping verbatim and proving byte-identical ROMs before any
  behaviour change; no legacy meter is rewritten as part of this plan.
- **Preview drift.** The one way a lab lies is by diverging from firmware. Golden frames are the answer; a meter without
  them is labelled "preview unverified" in the lab.
- **Scope creep into a general UI framework.** This structure is for meters. Icons, fonts and themes reuse the container
  and the manifest idea, not the `tick` contract.

---

# Part 2 (added after owner review): capability governance, the shared meter core, and operations

Owner intent: **every meter uses the common interface** (faster development, fewer bugs, consistent behaviour). A
meter that needs something the interface does not offer must not bolt it on privately: it goes through the
capability process below and is judged against the technical stack and the feasibility spec.

## 12. Capability requests: how a meter gets something new

A meter's manifest declares everything it uses beyond the base contract. The base contract is the set every meter
gets for free: spectrum, waveform, peaks, rect, roles, `mtr_bar`/`mtr_rect`. Everything else is a named
**capability** in a registry (`docs/METER_CAPABILITIES.md`, generated from `tools/meter_capabilities.json`):

| Field | Meaning |
|---|---|
| `key` | e.g. `sblit`, `cblit`, `rrect`, `hw_spectrum`, `alpha_blend`, `sdram_plane`, `psram_table`, `vsync_beam`, `stereo`, `beat_detect` |
| `provider` | RTL opcode, firmware helper, or shared-core function that supplies it |
| `status` | `hardware-proven`, `sim-only`, `fit-timing-open`, `shelved`, `design-only` (the project's evidence vocabulary) |
| `ready_probe` | the fail-safe check that says whether the running bitstream has it (`BLIT_READY()`, `RRECT_READY()`, feature bit) |
| `cost` | commands, CPU cycles, SDRAM %, RAM bytes per use, each labelled measured or model |
| `fallback` | what a meter draws when the probe says no |

Rules:

1. **A manifest may only list capabilities that exist in the registry.** The generator fails otherwise. This is what
   stops a meter quietly depending on alpha blend (shelved) or a raw SDRAM pointer (B-192).
2. **`status` gates the build.** A `shelved` or `design-only` capability is rejected for a selectable meter. A
   `sim-only` or `fit-timing-open` one is allowed only for a parked or Diagnostic-Build-only meter.
3. **Every non-trivial capability needs a declared fallback**, and the lab renders the fallback too, so the
   degraded look is seen before it ships.
4. **The new-capability path is a fixed checklist**, the same discipline the blit engine followed:
   a) write the need as a registry entry (`design-only`) with the cost model and a feasibility note against
      `PHASE_F_SPEC.md` / `HELIOS_SPEC.md` (opcode budget, M10K, timing margin, the shared glyphbuf write network);
   b) if it is firmware or shared-core only, build it in `fw/meter_core` (section 13), with a native test;
   c) if it needs RTL, it becomes a Talos item with its own sim testbench, mutation hook, reference-renderer case and
      Quartus fit, and the meter waits on that, as Chladni's B18 to B20 do today;
   d) promote `status` only on evidence (sim, fit, then a hardware Check), logged in `AUDIT_TRAIL.md`.
5. **Prefer composing existing capabilities over a new one.** The manifest has a `considered` field where the author
   states which existing primitives were tried and why they are insufficient. This is the cheap way to catch "we
   already have that": most meters reduce to bars, spans, scaled blits or CLUT blits.
6. **Budget is a capability too.** `cost_class` (section 3) plus a per-meter `budget` block (commands per frame, CPU
   microseconds, RAM) are checked by the meter sweep (section 8, step 5); a meter over budget fails its own Check line.

## 13. The shared meter core: transversal functions

Yes, it is worth it, and the code already shows why. Five things are written more than once, or written once inside
one meter and needed by the next:

- Winamp's `wviz_ease_step()` (four easing curves), peak-hold-then-fall with optional gravity, and band merging are
  locked inside `VIZ_WINAMP_BARS`. `VIZ_BARS`, `VIZ_LEVELS`, `VIZ_LED`, `VIZ_MIRROR` each carry their own attack/fall
  arithmetic and peak markers.
- `ui_mix()` is called 37 times in `player.c`, each meter building its own colour ramps.
- Every meter keeps its own "what did I draw last frame" cache and its own force/clear logic (the source of the B-234
  bugs).
- Chladni needs onset/trigger detection and slow-slew mode weights; a future meter will want the same beat signal.
- Cost counting, headroom scaling and yield behaviour live at call sites, not in one place.

Proposal: `fw/meter_core.h` (portable, no MMIO, no float, like `chladni_core.h`, so it is natively tested and
WASM/JS-comparable) plus `fw/meter_core.inc` for the few pieces that must touch the engine. Cold code, shared by all
modules. Candidate contents, in order of value:

| Group | Functions | Replaces / enables |
|---|---|---|
| **Ballistics** | `mtr_ease(cur, target, mode, attack, release)`, `mtr_slew`, `mtr_decay` | Winamp ease; per-meter fall code in BARS/LEVELS/LED/MIRROR |
| **Peak cap** | `mtr_peak_t`, `mtr_peak_update(state, level, dt_ms, cfg)` (hold, linear or gravity fall) | Winamp caps; a cap for any other bar meter for free |
| **Band mapping** | `mtr_bands_merge(spec, n_out, out)`, `mtr_band_to_mode`, log/linear level mapping, headroom | Winamp merge; Chladni band-to-mode; consistent loudness across meters |
| **Colour** | `mtr_ramp(role_a, role_b, t)`, `mtr_ladder(level)` (ok/warn/danger), `mtr_lum`, `mtr_on_accent` | The 37 `ui_mix` sites; theme-correct by construction (THEME_SPEC.md) |
| **Redraw cache** | `mtr_delta_t`: per-column last-drawn value, returns "changed", honours `force` | The hand-rolled `0xFF` cache that broke in B-234; one tested implementation of skip-if-unchanged |
| **Signals** | `mtr_onset` (spectral-flux trigger with refractory time, from `chl_detect`), `mtr_energy`, `mtr_silence`, `mtr_stereo_balance` | Chladni triggers; idle look during silence for every meter |
| **Geometry** | `mtr_lit_rows`, `mtr_span_split`, `mtr_grid`, `mtr_scale_q8`, safe clip to rect | OP_BAR/BAR-family arguments, no per-meter off-by-one |
| **Random / time** | `mtr_rng` (seeded, deterministic), `mtr_ms` from `dt_ms` | Reproducible previews and golden frames |
| **Cost** | `mtr_cost_begin/end`, command counter in the `mtr_*` wrappers | One measurement method for every meter |

Design rules for the core:

1. **Pure functions and small POD state structs the module owns.** No hidden globals, so two meters (or the Configure
   preview and the player screen) can never share state by accident, which was B-234's root cause.
2. **Fixed point only, documented Q formats**, each with a native test against a reference (the Python reference
   pattern used for the blit engine and the spectrum bank).
3. **A function enters the core when a second meter needs it, not before.** First meter keeps it local; the second
   promotes it. This stops the core becoming speculative infrastructure and keeps each promotion backed by a real
   caller and a test.
4. **Every core function has a JS twin in the preview stack** (`tau_core.js`) with shared golden vectors, so lab and
   device use identical ballistics. This is what makes "tune it in the browser" trustworthy.
5. **Behaviour-neutral migration.** Moving an existing meter onto core functions is allowed only where its output is
   unchanged (byte-identical frames on the recorded trace) or where the owner approves the visible change, meter by
   meter. Consistency improvements (a common peak cap, common headroom) are an explicit, reviewed look change, never a
   side effect of a refactor.
6. **Size is accounted for.** Cold-only, and the manifest's `hot_bytes` sum includes core functions a meter pulls in.

What deliberately stays out of the core: anything specific to one visual idea (Chladni's field maths, the eye's
geometry), and anything that draws pixels outside the `mtr_*` primitives.

## 14. Structural additions

- **Layers, with one-way dependencies.** `Helios/engine primitives` <- `meter_core` <- `meter modules` <- `meter host`.
  A module never includes another module, never reaches `player.c` state, and never calls the engine directly. A
  script (`tools/check_meter_deps.py`, in the style of `check_cold_calls.py`) greps the includes and fails on a
  violation.
- **Identity and lifetime.** Ids append-only forever; a retired meter keeps its id, its manifest and `selectable = 0`
  (VIZ_TAPE precedent) so old saved cards still resolve. `tools/meters_released.json` records the id set of each
  release and is the reference for the generator's append-only check.
- **Manifest schema versioning.** `schema` per manifest and `registry_version` for the whole set; the container and QR
  tag both carry the schema so Omega and old firmware degrade per meter, never globally.
- **Templates.** `tools/new_meter.py <key>` scaffolds the folder (manifest, `.inc`, core header stub, `meter.js`,
  golden stub, thumbnail placeholder) so a new meter starts compliant. Development speed comes from this, not from
  documentation.
- **One place for rect and layout.** The meter rect comes from the host (`UI_WAVE_Y`, height from the now-playing
  design), never from constants in a module. Changing the now-playing layout then cannot break a meter.
- **Beam and audio policy stays in the host.** Vsync/beam gating (Helios), `meter_afford()` yield, the 1/6, 1/3, 2 s
  latch rules and `force` on context change are applied uniformly by the host. A module cannot opt out, so the
  audio-first guarantee cannot be weakened by one meter's author.
- **Mutable state placement.** Persistent mutable buffers of 1 KB or more live in PSRAM through `MTR_PSRAM`, never shifted through the window, never scatter-read; the rules,
  cost model and risks are section 27 (D-M14).
- **Failure containment.** The host wraps `tick` with a command-count guard: a module that exceeds its cost class for N
  consecutive frames is switched to its declared fallback (or a plain bars meter) and the Info page shows a code. The
  audio path never waits on a meter.

## 15. Operational additions

- **Meter sweep as a standing regression.** The Diagnostic Build gains a sweep over every selectable meter on the Test
  Album (10 s each): commands/frame, worst tick time, late underruns, meter-yield seconds. One QR carries all of it
  (repeatable tag like `SR_T_DECSWEEP`). A release is blocked by a red line, not by opinion.
- **Traces as test assets.** A recorder in the Diagnostic Build dumps `spec_lvl`, `wav_v` and peaks per frame to the
  card or QR; recordings of the Test Album tracks live in the preview fixtures with provenance. Golden frames, the lab,
  the native harness and rv32sim all replay the same file.
- **Screenshots as evidence.** The lab's "compare to device" mode overlays a Pocket screenshot (Memories/Screenshots)
  on the lab frame for the same trace position, which closes the loop on colour and geometry without guessing.
- **Field diagnostics.** The Info page gains a METER row: active id, parameter source (built-in, file, edited), guard
  trips, worst tick. A user report then says which meter and which configuration.
- **Release checklist entry.** `make_release.py` runs the registry checks, the meter suite and the sweep result check;
  the release notes list added, parked and changed meters straight from the registry diff.
- **Docs are generated where they can be.** `METER_REGISTRY.md` and `METER_CAPABILITIES.md` come from the JSON, so the
  tables in `METER_CONFIG_SPEC.md` and `CROSS_PROJECT_INTERFACE.md` stop being hand-maintained copies.

## 16. Maintenance additions

- **Ownership and change rules.** Interface changes (`mtr_in_t`, `mtr_desc_t`, core function signatures) are
  append-only within a schema version and logged in `AUDIT_TRAIL.md` with an "Omega impact" line, per the
  cross-project rule.
- **Deprecation path.** A meter is parked (selectable 0), later removed from the container's default order, and its id
  is never reused. The code stays until a release confirms nobody's card depends on it, then is deleted with the id
  still reserved in `meters_released.json`.
- **Dead capability sweep.** The generator reports capabilities no meter uses and meters whose `considered` note names a
  capability that has since become proven, prompting a rewrite onto the cheaper path.
- **Toolchain drift.** Golden frames pin the compiler-sensitive maths; a compiler or `-O` change that shifts a frame is
  caught in `make test-host`, not on a card.
- **Documentation debt guard.** The generator refuses a meter with no thumbnail, no preset marked default, or no
  golden trace, so "we will add the preview later" cannot ship.
- **Cost model calibration.** After each hardware sweep, measured commands, SDRAM % and CPU us are written back into the
  capability registry's `cost` fields (model -> measured), so the lab's estimates converge on reality and stale
  estimates are visible as such.

## 17. Revised build order

| Step | Content | Proof |
|---|---|---|
| **M0** | `meter.h`, manifests for the 14, generator, descriptor table; dispatch unchanged | ROMs byte-identical |
| **M0.5** | Capability registry (`meter_capabilities.json`) seeded from the blit/Helios specs, generator checks, dependency checker | Registry matches `PHASE_F_SPEC.md` statuses; generator rejects a shelved capability |
| **M1** | Preview stack v1 with `tau_core.js` twins for ballistics, peak cap, colour ramp | Lab equals the Fluid Bars sandbox |
| **M1.5** | `fw/meter_core.h` first tranche (ballistics, peak cap, band merge, delta cache) extracted from the Winamp code, native test, JS twin, golden vectors | Winamp frames byte-identical before and after |
| **M2** | Winamp pair as modules on the core; generic Configure page; `SR_T_METERCFG` | Golden frames, QR round trip |
| **M3** | Trace recorder, golden-frame diff in `make test-host`, `new_meter.py` | Recorded hardware trace reproduces exactly |
| **M4** | `tau-assets.bin`, `METR`/`THEM` loaders, Omega writer | Real captured container decoded by Omega |
| **M5** | Chladni as the first new meter, promoting `mtr_onset` and the tile helpers into the core on its second user; meter sweep on hardware | Measured cost row per meter |
| **M6** | Legacy meters wrapped and, where approved, moved onto the core one at a time | Byte-identical ROM, or an approved look change |

Additional owner decisions (add to section 10): (6) approve the "promote to core on the second user" rule; (7) whether a
consistency pass (shared peak caps and headroom across the legacy meters) is wanted as a visible look change after M6, or
legacy meters stay exactly as they are; (8) whether the guard's fallback is "plain bars" or a per-meter declared fallback.

---

# Part 3 (added after owner review): property sets, templates and shareable presets

Owner intent: the interface must define how properties are set, and what templates are, so Omega can configure meters
and create and share new presets without knowing anything meter-specific. Everything below is data: it lives in the
manifest, the preset file and the container, and adds no code to any meter.

## 18. The property model

Section 4 gave parameters a type, range and default. To let a generic editor (Omega, the on-device page) do a good job,
each property carries the full description below. All of it is in `meter.json`; none of it is compiled into the device
except what the device needs to clamp and apply (type, range, step, enum count).

| Field | Purpose | On device? |
|---|---|---|
| `key`, `label`, `help` | Stable id, display name, one-line explanation | key index only |
| `type` | `u8`, `u16`, `bool`, `enum`, plus two derived kinds: `role` (a theme role index 0-11, never a raw colour) and `pct` (0-100 shown as %) | yes |
| `min`, `max`, `step`, `default`, `unit` | Range, granularity, unit label | min/max/step/default |
| `group` | Editor section: `Shape`, `Motion`, `Peaks`, `Colour`, `Advanced` (fixed vocabulary so every meter looks alike) | no |
| `level` | `basic` or `advanced`; the editor hides advanced by default | no |
| `when` | Visibility and validity condition on other properties; hidden properties keep their value but are not saved in a preset unless applicable | evaluated by the clamp step |
| `requires` | Capability keys (section 12) the property needs; the editor greys it out for a target bitstream that lacks them | no |
| `affects` | `cost`, `look`, `both`: lets the editor warn "this raises draw cost" and the cost panel weight it | no |
| `links` | Constraints between properties, for example `{ "peak_fall": ">= 1 when peak_on" }` or `{ "bands": "divides 16 or is 16" }` | clamp step |
| `random` | `{min,max}` sub-range used by "randomise", so random results stay tasteful and inside budget | no |
| `lock` | `false` by default; a template may lock it (section 19) | clamp step |

Two rules keep this robust: a **property never holds a raw RGB value** (colours are theme roles, so a shared preset
looks right in every theme and polarity), and a **property never changes meaning** once released (a new meaning is a
new key; an old key is deprecated, never repurposed). Both come from lessons already paid for (theme roles, append-only
enums).

## 19. Layers and templates

A meter's effective configuration is resolved in a fixed order, later layers overriding earlier ones only for the keys
they set:

```
1. property default            (manifest)
2. template                    (a named base for a family of looks; may lock or hide properties)
3. preset                      (built-in, from the container, or imported)
4. session edit                (on-device Configure page; not persisted, section 6)
```

**Template** = a manifest-level definition, shipped with the firmware registry, not user data:

```jsonc
"templates": [
  { "key": "classic",  "label": "Classic",  "base": { "ease": 0, "peak_on": 0 },
    "lock": ["ease"], "hide": ["hold_ms","peak_fall"] },
  { "key": "fluid",    "label": "Fluid",    "base": { "ease": 2, "peak_on": 1, "hold_ms": 300 } }
]
```

A template is what an editor offers first ("start from Classic, Fluid, Minimal"). It fixes the shape of the property
form (which controls are shown, which are locked) and gives the defaults; it never changes what the device can do.
Templates are also the answer to "same code, different meter": the bars family can expose Winamp-style, plain and
stepped looks as templates of one module instead of three modules.

**Preset** = a named set of values for one meter, optionally pinned to a template. Only keys that differ from the
template need be present, so presets stay small and survive the addition of new properties (missing keys take the
layer below). A preset whose values violate a `lock` or a `link` is rejected at validation, never silently adjusted.

## 20. The preset file: the unit of sharing

One preset or a bundle of them is a small JSON document, **`.tmeter`**, produced and consumed by Omega and the lab,
and compiled to the binary `METR` section for the device (section 6). JSON is the human and sharing format; binary is
only what the device reads.

```jsonc
{
  "format": "tau-meter-preset", "format_version": 1,
  "meter": "winamp_bars", "schema": 1,            // which meter and which manifest schema this was authored against
  "template": "fluid",
  "name": "Slow Tide", "author": "abel", "created": "2026-09-25",
  "description": "Long release, low peak cap",
  "values": { "bands": 12, "release": 20, "hold_ms": 500 },
  "requires": ["needs_spec"],                      // capability keys, derived not typed by hand
  "min_firmware": "0.5.0",                         // derived from the schema and capabilities used
  "theme_hint": "any",                             // "any" or a theme key; roles keep it portable
  "preview": { "trace": "test-album-1", "frame_png": "sha256..." },   // optional, for galleries
  "id": "sha256 of the canonical (sorted-key) values + meter + schema"   // content address
}
```

Properties of the format that matter for sharing and safety:

- **Content-addressed id.** The id is the hash of the canonical values, so the same preset from two people is
  recognisably the same, edits produce a new id, and a gallery can dedupe.
- **Self-describing compatibility.** `meter`, `schema`, `requires`, `min_firmware` let Omega say "this preset needs
  firmware 0.5 and hardware spectrum" before it goes anywhere near a card. On the device, an unknown meter or a newer
  schema is skipped, per section 6.
- **Migration.** When a schema changes, the manifest carries `migrations` (rename, remap an enum, rescale a range, add
  a default). Omega upgrades old presets on import and records the migration; the device never migrates, it only
  clamps.
- **No code, no paths, no colours.** A preset can only contain manifest-declared keys with typed values. That keeps
  shared files harmless: the worst a malicious or corrupt file can do is select ugly in-range values.
- **Bundles.** `.tmeterpack` is a zip of `.tmeter` files plus a `pack.json` (name, author, licence, ordering, which meter
  is default). Omega imports a pack, validates every preset, and writes the chosen ones into `METR`.
- **Licence field.** Presets are values, not code, but a pack states a licence, and the default is MIT (decided, section 25), the same
  licence as the rest of the project, so sharing has clear terms.

## 21. Ways a preset moves around

| Route | Format | Use |
|---|---|---|
| Omega export/import | `.tmeter` / `.tmeterpack` | Main route: share files, sync to card |
| Device to Omega | `SR_T_METERCFG` QR (section 5) | Capture a value set tuned on the Pocket; Omega turns it into a `.tmeter`, asks for a name |
| Text share | `tau-meter:1:<meter>:<schema>:<base64url values>:<crc16>` | One preset in a chat message or a QR from Omega; short, checksummed, pastes into Omega or the lab |
| Lab to Omega | same `.tmeter` (the lab exports and imports it) | Tune in the browser against a trace, then save |
| Omega to card | `METR` section in `tau-assets.bin` | The only way the device receives presets |

The lab and Omega use the same validator (one JS module generated with the registry), so "valid in the lab" and "valid
in Omega" cannot differ. The device's clamp is the last line and is never bypassed.

## 22. Operating it from Omega

Everything Omega needs is derivable from `meters_schema.json` (properties, templates, links, capabilities) plus the
firmware version it is targeting:

1. **Meter list** from the registry, with thumbnail, cost class, capabilities.
2. **Editor** generated from properties: grouped, basic/advanced, `when` and `links` enforced live, locked properties
   shown as read-only, capability-missing properties greyed with the reason.
3. **Templates picker**, then a preset gallery (built-ins, imported, mine), duplicate/rename/delete, randomise within
   `random` ranges, reset to template.
4. **Live preview** with the embedded stack (section 7), any trace, any theme and polarity, with the cost panel warning
   when a value pushes the meter over its budget.
5. **Compatibility check** against the target card's core version (Omega already reads `core.json`), listing what will
   be skipped and why.
6. **Build and sync**: choose which presets go on the card, order, default; write `METR`; verify by reading back and
   comparing the CRC.
7. **Capture from device**: scan a `SR_T_METERCFG` QR (Omega's QR pipeline already exists), review, save as a preset.

Omega owns presets as user data (its library, naming, galleries); Tau-Alpha owns what a valid preset is. That split keeps
the cross-project rule: the registry is the contract, Omega verifies it against a real captured container.

## 23. Firmware-side consequences (small)

- The device stores, per meter, only: property table (type, min, max, step, default, lock flag, link ids), template
  defaults, and its built-in presets. Labels, help, groups, random ranges and migrations are not compiled in, which
  keeps the heap-gap cost near zero.
- `mtr_apply(meter, values[])` is the single entry that loads a preset: clamp each value, enforce locks and links, then
  `open(force=1)`. The Configure page, the container loader and the QR export all go through it and its inverse
  `mtr_snapshot(meter, out[])`.
- `SR_T_METERCFG` (section 5) gains the template index so a captured tune reopens in the right form.

## 24. Additions to build order and decisions

- **M2.5:** property model in the manifest and generator (groups, levels, links, templates, random), `mtr_apply` and
  `mtr_snapshot`, validator module shared by lab and Omega, `.tmeter` JSON schema in `tools/`. Proof: a preset round
  trips lab -> `.tmeter` -> `METR` -> device -> QR -> `.tmeter` unchanged, on the host harness first.
- **M4 extension:** Omega preset library, pack import/export, capture-from-QR, compatibility check.
- **Decisions:** (9) `.tmeter` JSON plus `tau-meter:` text form as the sharing formats, binary only for the device;
  (10) templates live in the firmware registry (recommended) versus user-definable ones in Omega, which would also need a
  file format and a policy for locks; (11) licence default for shared packs; (12) whether a public preset gallery is in
  scope for Omega or presets are only exchanged as files.

---

# Decisions (resolved 2026-09-25, owner delegated: choose what is best technically and for performance)

All open questions from sections 10, 17 and 24, with the reason. Recorded in `docs/DECISIONS.md` as D-M01 to D-M13.

## 25. The register

| # | Question | Decision | Reason |
|---|---|---|---|
| 1 | One container or separate slots | **One `tau-assets.bin`** (`THEM`, `METR`, later `ICON`, `FONT`) | Slots are scarce and slot 6 is double-booked; one CRC-checked loader is one code path to prove. Needs one new slot number, settled with the slot-6 conflict first |
| 2 | Keep the on-device Configure page | **Keep, generic, Diagnostic-Build-only** | Costs nothing once generated from the manifest; it is the no-Omega fallback and the source of `SR_T_METERCFG` captures. Not in the release core, where heap is spent on features |
| 3 | Preview hand-off to Omega | **Versioned bundle from a tagged release, hash recorded; Omega never forks it** | A fork reintroduces the drift the golden frames exist to prevent |
| 4 | Lab maths route | **JS port plus golden vectors by default; WASM only where a meter's maths is heavy and a `wasm32` toolchain is proven to exist** | No toolchain dependency for the common case; golden vectors already prove equality with the C core. WASM is a per-meter optimisation, not a rule |
| 5 | Theme roles before M2 | **`role[]` in the contract from day one**, host passes today's constants through it | Avoids a contract change later; nothing blocks on the theme work |
| 6 | Promote to core on the second user | **Yes** | Every core function has a real caller and a test; no speculative code in a size-constrained image |
| 7 | Consistency pass on legacy meters | **No. Legacy meters stay exactly as they are** | They are hardware-proven under the audio-never-glitches guarantee; a look change risks the one thing that must not move and buys nothing measurable. Any later change is opt-in per meter, owner-approved, with before/after frames |
| 8 | Guard fallback | **Per-meter declared fallback, defaulting to the `VIZ_BARS`-equivalent** (hardware-proven, 36 commands) | A declared fallback keeps the meter recognisable; the default guarantees a safe, cheap result when none is declared |
| 9 | Sharing formats | **`.tmeter` JSON, `.tmeterpack` zip, `tau-meter:` text form; binary only on device** | Human-diffable and validated in one shared JS module; device stays minimal |
| 10 | Where templates live | **Firmware registry (manifest) only; no user-defined templates** | A template can lock and hide properties, which is a shape-of-the-form decision tied to the code. User templates would need their own format, lock policy and migration for no performance or safety gain. User variety comes from presets |
| 11 | Licence for shared presets and packs | **MIT** | Owner decision; same as the project. `pack.json` `licence` defaults to `MIT` |
| 12 | Public preset gallery | **Parked** (D-M12), presets are exchanged as files and text strings | Owner decision; needs hosting, moderation and identity, none of which serve the device. Revisit after M4 has real users |
| 13 | Build order | **As sections 17 and 24: M0, M0.5, M1, M1.5, M2, M2.5, M3, M4, M5, M6** | M0 is byte-identical and risk-free, so it comes first and unblocks everything |
| 14 | Where a meter's persistent mutable state lives | **PSRAM (`MTR_PSRAM`), as a ring unwrapped into a small hot scratch** (section 27, D-M14) | On-chip RAM is the scarcest resource and almost all meter state is append-and-scan history that a ~32-cycle window serves well; each meter owning its state removes buffer sharing between meters |

## 26. Consequences of the decisions

- **Manifest gains** `fallback` (a meter key or `plain_bars`) and a `licence` field is only on packs, not meters.
- **Generator checks** that a fallback exists and is `selectable`, and that no template locks a property the fallback path needs.
- **`make_release.py`** ships `meters_schema.json` and the preview bundle as release assets with hashes; Omega pins to them.
- **No gallery work** in any milestone. The `.tmeter` `id` (content hash) is kept because it is useful for dedupe in a local library, not because of a gallery.

## 27. Mutable meter state lives in PSRAM (D-M14, B-509)

**Decision (owner, 2026-10-02): a meter's long-lived mutable buffers live in PSRAM, behind one framework mechanism, and this is the strategy every new
meter follows.** First user: Layered Wave's history (2,424 B), which did not fit the 192 KB link next to the rest of the merged build (heap gap 3,728 B
against a 4,096 B floor) and now costs the on-chip RAM only a 404-byte scratch row (heap gap 5,744 B, +2,016 B).

### 27.1 Why this is the structural answer

On-chip RAM is the scarcest resource in the project (the whole RAM-shrink track exists because of it) and every meter's state competes with the audio
path for it. Almost all of a meter's state is *history*: written once per push, read a few times per frame, never touched per pixel. That access
pattern is exactly what a ~32-cycle window can serve, whereas the on-chip RAM is wasted holding it. PSRAM has 32 MiB of window and the project uses well
under 1% of it. Moving state out also removes the "which meter's buffer do we alias onto whose" problem for good: each meter owns its state, nothing is
shared or time-multiplexed, so there is no init-on-switch coupling between meters.

Rejected alternatives (kept so they are not re-litigated):

| Option | Why not |
|---|---|
| Shrink the buffer (fewer layers / lower max resolution) | Takes a capability away from the user to solve a memory-layout problem, and must be changed in three places (C, JS twin, golden test) |
| Alias one meter's buffers onto another's (e.g. Chladni's `chl_half`) | Meters are exclusive at runtime, so it works, but every switch must invalidate the other meter's "initialised" state or it draws stale frames; the coupling is invisible in the code |
| Gate the meter out of the build that needs the RAM | The Diagnostic Build is the build meant to carry the experimental settings |
| Cold data | Cold data is read-only (loaded once from `tau-cold.bin`); a per-frame buffer is written constantly |

### 27.2 The mechanism (all in `fw/meter.h`, `fw/link.ld`)

- **`MTR_PSRAM`** is a storage attribute. Declaring `static uint32_t my_ring[..] MTR_PSRAM;` places the object in the `.psram_state` linker section, region
  `psram_state` = `0xA4009000..0xA400FFFF` (28 KB). The linker assigns the addresses, so no module picks one, and an overflow is a **link error**, not silent
  corruption. The region starts after every fixed-address Check buffer and ends where the media library image begins (`+0x10000`).
- **`mtr_psram_ready()`** proves the window once per boot and caches the verdict for every meter: the PSRAM expansion ID must read back, the build must
  report the CPU window present, and the first and last word of the region must hold two test patterns (the same proof `art_psram_prove()` uses for the album-art
  accumulator, applied to the state region). A module calls it **before its first store**. On failure the module draws a flat background and returns, never
  a write to an address that is not RAM.
- **Host builds:** the firmware branch is enabled only by `MTR_PSRAM_FW` (defined in `fw/player.c`). Host harnesses leave it undefined, so `MTR_PSRAM` is empty and
  `mtr_psram_ready()` is a constant 1. The *same module source* therefore compiles on the host and is compared against its JS twin with plain arrays.

### 27.3 The rules a module must follow

1. **Classify first.** *Hot RAM*: anything touched per pixel or per row, or smaller than about 512 B. *PSRAM*: a persistent mutable buffer of about 1 KB or
   more whose access is "append at one end, scan sequentially". A random-access plane written per pixel stays in hot RAM (or is not a candidate).
2. **Ring, never shift.** Shifting a buffer through the window costs one read and one write per byte moved. A ring moves an index instead; a push is one write.
3. **Unwrap into a hot scratch with word reads.** One window read returns four samples. A frame reads the arc it will draw into a small hot scratch (one layer,
   404 B for Layered Wave) and the existing draw code reads the scratch unchanged.
4. **Word-aligned 32-bit access only.** Declare the storage as `uint32_t`. A sub-word store is a read-modify-write on the whole word. Never assume byte-lane
   write support in the window, and never rely on a byte layout (use shifts and masks, so host and firmware agree).
5. **Initialise after `mtr_psram_ready()`.** Contents at power-up are undefined, and the proof deliberately overwrites the region's first and last word. This
   is safe only because every user calls `mtr_psram_ready()` before its first store and then initialises its own state.
6. **Compile-time guards for the ring invariants** (`_Static_assert`): the ring length is a multiple of 4 and is at least as long as the longest span a frame
   reads. If either is broken, old samples alias new ones and the picture is wrong in a way no crash reveals.

### 27.4 Cost model (measured numbers, not estimates, except where marked)

An uncached window read costs about 32 cycles and a write about 26 [HW, B-022 / KB-040]; at 66.667 MHz one millisecond is about 2,080 reads. Layered Wave's worst
case per redraw is 6 layers x 101 words = 606 reads (about 19 k cycles, 0.29 ms) plus 600 stores only on a reset (about 16 k cycles). A push is 6 word
read-modify-writes. **The frame-time figure is arithmetic from those per-access numbers, not a measurement of the finished meter**: the Meter Sweep
(Diagnostics) is the place to confirm it on a card. A working budget for any one meter is **at most about 700 window accesses per frame** (about 0.35 ms); this
is a judgement, chosen to leave the audio loop clear of the B-299-style regression, and should be revised once the sweep has real data.

The cautionary precedent is B-027: the album-art accumulator in PSRAM came out about **4x slower than predicted**. Predict, then measure.

### 27.5 Risks, and what to do about each

| # | Risk | Likelihood / impact | Remediation or mitigation |
|---|---|---|---|
| 1 | **The window is absent or fails the proof** (old bitstream, or a build without the P4 window) | Low, because cold code (which every meter here is) already requires the PSRAM instruction alias, and the bundles ship both together. Impact: that meter is blank | Fail-safe is built (flat background). **Not yet exercised on hardware**: add the meter to the old-bitstream fail-safe test (the J1-J8 style) the next time that test runs |
| 2 | **Frame time grows** (a meter scatter-reads the window per pixel, or shifts through it) | Medium for a new meter written without these rules. Impact: the B-299 failure mode, audio jitter from a meter that spends the CPU | Follow 27.3. Add window accesses to the cost accounting (suggestion S1). Fall back by widening the draw stride or lowering resolution, the same self-scaling the command-count guard uses |
| 3 | **Contention with other PSRAM clients** (cold code instruction fetch, the media library index, the art accumulator) | Medium. The worst single access measured is about 380 cycles [HW, B-054], and the CPU stalls while it waits | Real-time masters never use PSRAM, so audio is not directly delayed; keep per-frame accesses bounded (27.4); rely on the existing `meter_afford()` yield. If the Check blit-storm or the sweep ever shows late underruns tied to a PSRAM-heavy meter, cut the unwrap to the columns actually drawn (stride) before anything else |
| 4 | **Address-map collision** with the fixed-address Check buffers (`CHK_QR 0xA4003000`, `CHK_REC 0xA4007000`, `CHK_TXT 0xA4007800`, `CKM 0xA4008400`) | Low today (the state region starts at `0xA4009000`). Impact: silent corruption of a persistent buffer | The linker region is placed clear of them and is checked at link time only against *itself*. **Suggestion S2**: a small script that scans `fw/` for `0xA4xxxxxx` literals and the linker regions and fails on any overlap. **Pre-existing finding while mapping this:** `art_acc` (`0x0000..0x3C00`) already overlaps `CHK_QR` (`0x3000..0x6398`). It is time-shared today (cover decode versus the Check page) and has not been observed to matter, but it is exactly what S2 would flag |
| 5 | **The proof overwrites the region's first and last word** | Low; safe only by convention | Rule 5. Any future user that stored to the region before calling `mtr_psram_ready()` would be corrupted. Keep the call as the first line of the module's tick |
| 6 | **Hot scratch is still needed** | Certain; small | One layer's worth (404 B). A meter needing per-pixel random access keeps that data in hot RAM and should not use this mechanism for it |
| 7 | **Correctness is no longer visible in the data** (a wrong ring index draws a plausible but wrong picture) | Medium for a new ring | Golden-frame equality against the JS twin covers it (Layered Wave: 1,866,565 commands identical) and a dedicated ring-versus-shift-register test (`sim/test_lw_ring.py`) drives far more pushes than the ring length. Any meter adopting this must have both |
| 8 | **Host tests cannot see PSRAM behaviour** (timing, window presence, contention) | Certain | Hardware verification is separate: the Meter Sweep and the Check on a card. Record the result as a hardware label in the audit trail before calling a PSRAM-state meter done |
| 9 | **Region is 28 KB, hard-limited above by the library image** | Low (Layered Wave uses 2.4 KB) | A link error appears first. If it ever fills, move the library image's start in a coordinated change; do not reach into it |
| 10 | **Heap margin is judged on the gap only** (heap-peak instrumentation is still parked, B-230) | Unchanged | Not made worse by this change; the gap went up 2,016 B. Revisit if the parked instrumentation is built |
| 11 | **Stack margin on the 192 KB link** (a separate change on the Layered Wave branch reduced the stack) | Unverified on hardware | Not caused by this mechanism, noted here because the headroom it buys is easy to spend. Run a worst-case Check and read the stack peak |

### 27.6 Suggestions (not built)

- **S1.** Extend `tools/meter_cost_estimate.py` so it counts accesses to `MTR_PSRAM` objects per redraw and gates them like draw commands (the 700-access budget above).
- **S2.** `tools/check_psram_map.py`: fail on any overlap between linker regions and fixed `0xA4xxxxxx` literals (see risk 4).
- **S3.** Show the proof verdict (`mtr_ps_state`) and the last frame's window-access count on the Diagnostics Info page, next to the existing meter rows.
- **S4.** Candidates to migrate when hot RAM is next needed, each needing its own access-pattern review (D-M07: legacy meters stay as they are until the owner approves): Chladni's `chl_half` (1,600 B) and its two `K x RX` coefficient tables (320 B each).
- **S5.** If the draw engine ever gains a PSRAM-sourced blit, a ring could feed it directly and the unwrap scratch would go away.
- **S6.** A second proof-style fail-safe test: run a PSRAM-state meter on a bitstream without the window and confirm the flat-background path, since the host build cannot reach it.

### 27.7 Consequences

- `fw/meter.h` gains `MTR_PSRAM`, `mtr_psram_ready()` and the proof; `fw/link.ld` gains the `psram_state` region and `.psram_state` section; `fw/player.c` defines
  `MTR_PSRAM_FW`. No existing meter changes (D-M07).
- `meters/*/meter.json` does not need to declare state placement: it is a property of the module source, enforced by the linker and the spec's review checklist.
- The module-addition checklist (section 8) gains one line: *does it keep persistent mutable state of 1 KB or more? then 27.3, with a ring test and a hardware label.*
