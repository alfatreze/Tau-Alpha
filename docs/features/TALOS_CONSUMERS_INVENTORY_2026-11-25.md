# Talos consumers: who issues which engine commands (2026-11-25)

Input to the Talos v2 scoping (`TALOS_ENGINE_V2_SCOPING_2026-11-25.md`, decision D-T18: every command is kept). **Method:** static search of the owner's projects under `DEV PROJECTS/` for the engine's files and command wrappers (`tau_talos`, `tau_gfx`, `mp3_fb`, `OP_*`, `fb_*` and `gfx_*` calls). **Not done:** no build configuration, runtime trace or opcode histogram of any other core was read or run; counts are call sites in source, not frequency.

## Consumers

| Project | How it has the engine | Commands it issues (call sites) | Status of that project | Will it take the new engine? |
|---|---|---|---|---|
| **Tau Alpha Player 2** (`fw2/gfx`, ships) | the engine itself | RECT, CHAR 1x, BAR, BLIT (plain and blend), SBLIT 1/2/3x, CBLIT, FENCE (the scoping report's grep) | shipping | it is the engine's home |
| **Tau Alpha old player** (`fw/`, not shipping, retiring) | the same RTL | all ten: `fb_rect` 201, `fb_text_clipped` 125, `fb_fence` 31, `fb_blit` 24, `fb_bar` 15, `fb_cblit` 12, `fb_copy` 10, `fb_char` 8, `fb_text_boxed` 7, `fb_rrect` 7, `fb_sblit` 5, `fb_copy_span` 5, `fb_run` 2 | not shipped | no |
| **Tau V** (`tau-nu`) | its own copy of the OLD engine, `src/fpga/core/mp3_fb.sv` (the pre-split text, opcodes RUN, RECT, CHAR, COPY, BLIT, BAR, SBLIT, CBLIT, RRECT), plus the old-player firmware | the same as the old player: `fb_rect` 208, `fb_text_clipped` 123, `fb_fence` 31, `fb_blit` 24, `fb_bar` 15, `fb_cblit` 12, `fb_copy` 10, `fb_char` 8, `fb_text_boxed` 7, `fb_rrect` 7, `fb_sblit` 5, `fb_copy_span` 5, `fb_run` 2 | last commit 2026-11-04 (video decoder work, `tau_tvc_*`) | **Yes, planned:** its `docs/UPSTREAM_TAU_ALPHA.md` (V-288) and `docs/TVC_FRAMEBUFFER_INTEGRATION_PLAN.md` say to base the Tau V core on Tau Alpha's rewritten graphics stack (`tau_gfx`/`tau_talos`/`tau_scanout`) at a tag Tau Alpha declares stable, copied once by hash, never shared as files |
| **Tau Μ** (`Tau Μ`) | its own copy of the old `mp3_fb.sv`, same old-player firmware | identical counts to Tau V's (same lineage) | last commit 2026-10-14; whether it is active was not checked | not stated |
| **Public meter SDK and `Tau alpha meters`** | not the RTL: packs reach the engine through the firmware ABI (rect, rect_stash, blit, text ...); the SDK model `canvas.py` and the lab twin `tools/meters/preview/tau_fb.js` replay it; `tau_fb.js` is also copied into Tau V and Tau Μ | what the pack ABI exposes (see the scoping report section on the frozen ABI) | shipping since v0.7.5 | already depends on the shipped engine |

## Looked at and NOT a consumer
* **Dethrace-Rewired:** it vendors Tau Alpha under `third_party/Tau-Alpha` (a reference copy, one tracked entry) and no file of its own build includes the engine; its decision D-003 (2026-11-17) rejected Talos for reuse (openfpgaOS already has a GPU; `PLAN_REVIEW_2026-11-17.md` R1).
* Tau-blocks, tau-cart, tau-web, Tau codecs (a few files, text only), Tau Reader (1 file, text only), Dethrace-Clash, Oregon Trail Pocket, SpectrumDeck, FreePGA, NeoCalc, PicoDS, the Saturnix projects: no engine file or command call found. Tau Gallery has no working copy under `DEV PROJECTS/` (its archive is on MONSTER); not searched.

## What this means for the rewrite
1. **All ten commands are in real use somewhere in the family.** RUN (2 call sites), COPY (10), RRECT (7) and the CHAR scales are issued by the old-player lineage that Tau V and Tau Μ still run, even though Player 2 does not. This supports D-T18: do not drop any command.
2. **Tau V is a future consumer of the new engine.** Its plan is to import the rewritten graphics stack at a stable tag, so the compatibility policy (no regressions; improvements behind a capability bit or ABI revision) protects a second firmware, not only Tau Alpha's. Its old-player drawing code is the realistic test load for COPY, RRECT, RUN and the CHAR scales: its firmware is the first conformance input to collect.
3. **Copy-once-then-diverge:** Tau V and Tau Μ take files by hash from a tag, so a rewrite does not break them until they choose to import it; the risk is at that import, not now.
4. **Open, cheap to measure:** the real frequency of each command in Tau V's firmware (a trace or the per-opcode counters of the engine's `R_GFX_COUNTS`), whether Tau V's drawing code passes against the new engine in a host test, and whether Tau Μ is still active.
