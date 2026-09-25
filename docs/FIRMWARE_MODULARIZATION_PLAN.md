# Firmware modularization plan

Status: **PLAN ONLY, parked (2026-09-25).** Nothing here is started. Owner decision: finish the key features
first, and the owner will define the architectural approach (section 5) before Phase 1 begins.
Written in B-254. Measurements below are from commit `93ffc84`.

## 1. Where we are (measured, not guessed)

| Fact | Number |
|---|---|
| `fw/player.c` | 11,099 lines, one translation unit |
| Static functions / file-scope statics in it | ~192 / ~216 |
| `main()` alone | ~1,600 lines (from line 9502 to the end): the main loop, input dispatch and most state transitions |
| "Modules" pasted in with `#include` | 15 `.inc` files (playlist, library, art, settings, settingsui, suite, cold, stress, ...) |
| Genuinely separate translation units | `flac.c`, `alloc.c`, `sysio.c`, `psram_diag.c`, the decoders |
| Portable, host-tested logic | `library_core.h`, `cold_core.h`, `suite_core.h`, `qrcode.h` (the pattern to copy) |
| Remaining `#if` blocks in `player.c` | ~61 |
| RTL | `mp3_fb.sv` 1,573 lines (whole draw engine, one state machine); `mp3_soc.v` 1,205 lines |

Function-name prefixes show the real clusters: `ui_` 69, `fb_` 22, `pl_` 12, `target_` 8, `flac_` 7, `slot_` 6,
`refill_` 6, `id3_` 5, `pcm_` 4, `lib_` 4, plus the visualizers (`viz_`, `wviz_`, `tape_`, `eye_`, `vu_`, `spec_`).

The `.inc` files are **not** modules: they share one namespace, can read and write every global in `player.c`,
and only work by textual position (the include order has been a recurring constraint). The B-250/B-251 cleanup
removed dead code and isolated diagnostics; it did not change this structure.

## 2. What "well structured" should mean here (goals)

1. Each module owns its state; other modules use a small header API, never the module's globals.
2. Pure logic (parsers, state machines, layout math) lives in host-testable `*_core.h` with no MMIO.
3. Hardware access (MMIO registers, the SDRAM/PSRAM windows, the draw engine) sits behind one thin layer, so
   the same logic runs in the host harness and on the Pocket.
4. Optional features (diagnostics, stress, profiling) are separate modules with one guarded call site each.
5. The audio path stays fast: nothing moves that costs decode headroom (the L0 record must hold).
6. Cold-code placement stays explicit and checkable (`COLD_FN`, `COLD_READY()`, `tools/check_cold_calls.py`).

## 3. Proposed module map (from the real clusters)

| Module | Owns | Comes from |
|---|---|---|
| `hw` | MMIO register map, cycle counter, SDRAM/PSRAM window access, the `COLD_READY()` / `RRECT_READY()` / `BLIT_READY()` probes | `sysio.c`, register defines, `blit_probe.inc`, `cold.inc` |
| `gfx` | `fb_*` primitives, Talos opcode wrappers, Helios regions and flush, text metrics | `fb_*` (22), `helios.inc`, font code |
| `ui_core` | colours/theme, layout constants, toasts, marquee, overlays, screen blank, the chrome (now-playing frame) | `ui_*` (69) |
| `viz` | all visualizers behind one `viz_ops` table (draw, tick, needs-blend/erase info); the meter config | `viz_`, `wviz_`, `tape_`, `eye_`, `vu_`, `spec_`, `wvcfg_export.inc` |
| `audio` | the decode pump: refill, PCM FIFO feed, decoder glue (MP3, FLAC), speed/EQ/volume, seeking | `refill_`, `pcm_`, `flac_*` glue, `size_`, seek |
| `track` | track load, tags (ID3/FLAC), cover art, resume | `id3_`, `art.inc`, resume/`slot_` |
| `browse` | playlist and library queue, list navigation, history | `playlist.inc`, `library.inc`, `pl_`, `lib_`, `list_` |
| `settings` | persisted state, the settings menu model and pages | `settings.inc`, `settingsui.inc` |
| `diag` (optional) | Check/QR, Tests, Stress pump, profiling | `suite.inc`, `stress*.inc`, `settings_diag.inc` |
| `app` | input dispatch, the main loop as an explicit state machine, boot | `main()` and `controls` |

`app` may call everything; `ui_core`/`viz`/`browse`/`settings` call `gfx` and `hw`; `audio` calls only `hw` and
decoders; nothing calls `app`; `diag` is a leaf. This dependency direction is the rule to enforce.

## 4. Phases (each ends with a gate; do not start the next until it passes)

**Phase 0 - map before moving (no code changes).** Produce (a) a call graph between the clusters above and
(b) a globals ownership table: every file-scope variable assigned to exactly one module, with the cross-module
readers/writers listed. Anything touched by more than two modules is a design flag to resolve first. Gate:
the table exists and the owner agrees the module map and dependency rule.

**Phase 1 - extract pure logic to `*_core.h`, host-tested.** Candidates: playlist/queue navigation, resume
records, list paging/scroll maths, marquee stepping, toast queue, the visualizer easing/peak-cap maths,
settings value encoding, layout maths. Header-only, no MMIO, unit tests in `sim/`. Gate: `make test-host`
covers each extracted core; ROMs stay byte-identical (header-only extraction should not change codegen).

**Phase 2 - promote `.inc` files to real translation units, leaves first.** Order: `diag` (already a leaf),
`viz`, `browse`, `settings`, `track`. Each gets `module.h` (its API) and `module.c` (its state, now `static`
and private). Cross-module reads become accessor calls. Gate per module: builds; heap gap and ROM size
within a stated budget; `tools/check_cold_calls.py` output unchanged or reviewed; `make test-host` passes;
one hardware Check (ENDURANCE) with audio L0 for the first module extracted.
Note: ROMs will no longer be byte-identical here (cross-file calls, link order), so the byte-identity gate of
B-250 is replaced by the budget + hardware gates above.

**Phase 3 - `hw` and `gfx` layers.** Move MMIO defines, window access and the probes under `hw`; move `fb_*`
and Helios under `gfx`. Add a host stub implementation of both so more of the UI logic can run in the harness.
Gate: host renders match the existing snapshot renderer's model for the same inputs.

**Phase 4 - `audio` and the main loop.** Extract the decode pump behind a narrow API, then break `main()`
into an explicit state machine (`idle / loading / playing / paused / overlay / settings / library`) with
input dispatch per state. This is the riskiest phase (audio path, boot order, cold-code readiness), so it
gets the tightest gate: byte-for-byte identical audio counters on the Check (underruns, refill timing) and a
full ENDURANCE run.

**Phase 5 - RTL, optional.** Split `mp3_fb.sv` by opcode group only if timing work keeps being blocked by its
size. Every split re-runs the simulation suite and needs a fresh multi-seed fit (see the timing lessons in
`docs/PHASE_F_SPEC.md`). Not needed for the firmware goals; decide separately.

## 5. Architecture decisions needed from the owner (before Phase 1)

1. **Layering rule:** accept the dependency direction in section 3, or a different one?
2. **State and API style:** plain C modules with header APIs and private statics (recommended: fits rv32 and
   the cold-code model), or something heavier (message queues, an event bus)? An event/state-machine model for
   `app` in Phase 4 is the main open question.
3. **Cross-file optimisation:** allow `-flto` (better inlining, slower/harder-to-debug builds), or keep any
   audio hot path in a single translation unit by convention?
4. **Cold-code policy per module:** which modules are cold by default (UI/settings/library/diag today), which
   must stay hot (audio pump), and should the linker section be assigned per module rather than per function?
5. **Feature switches:** keep the single `TAU_DIAGNOSTIC` switch, or make `diag` a separately linked module
   that release simply does not link?
6. **Where the host harness lives:** grow `sim/` (Python + rv32sim) or add a C host build of the core headers.
7. **Naming and layout:** `fw/<module>/<module>.{c,h}` directories vs. flat `fw/`; naming prefixes; a written
   rule that a new feature must arrive as a module, not as more code in `player.c`.

## 6. Risks and guard rails

- **Codegen and size drift.** Cross-file calls lose inlining; RAM and cold layout are tight. Guard: a size
  budget per phase, `tools/check_cold_calls.py`, and `RAM_192K=1 release` as the stress case.
- **Hidden coupling.** The B-250 work found a build-script test on a flag string that silently changed a
  build. Expect more of these; Phase 0's ownership table exists to find them before code moves.
- **Audio regressions.** The L0 record (no late underruns) is the product's core guarantee. Any phase touching
  the pump needs the hardware ENDURANCE result before it is accepted.
- **Big-bang temptation.** One module per commit, each independently revertible; never move two clusters at once.
- **Timing.** Sequenced after the current key features so that a bitstream and a firmware refactor are never
  both in flight when something breaks.

## 7. Sequencing

Start only when the owner declares the key features done and has signed off section 5. Until then, new code
follows the goals in section 2 where it can (new features as their own `.inc`/module with a small API and a
`*_core.h` for the logic), which makes Phase 2 cheaper.
