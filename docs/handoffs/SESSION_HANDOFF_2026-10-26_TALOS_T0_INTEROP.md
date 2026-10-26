# Session handoff 2026-10-26 (afternoon and evening): the v0.7.0 scope, Talos 2 decisions, T0 on hardware (DEV 186 to 189), the Omega mailbox

Audit **B-847..B-872**. Read this after `docs/handoffs/SESSION_HANDOFF_2026-10-26_MIGRATION_TEST_OMEGA.md`.

## State of the card (`/Volumes/Pock`, ejected)
Stable `alfatreze.TAU` (v0.6.0) and **`TAU DEV 189`** (`0.6.1-dev.189+ed250a97`, `PACKS=1`, `stats-b821`). Owner-tested: all good (Check all 12 rows pass, Report works, icons clean). Settings of a DEV core start empty (per-core folders).

## Done and confirmed on a Pocket this session
* The persist migration legacy -> new (one real file, DEV 181 -> 185): B-847.
* **T0 of the Talos plan (D-T05):** the CPU's cost per draw command is about 132 cycles and the engine keeps up (drain 0-1): the CPU issue path and the end-of-frame **fence spin (about 5 ms per frame)** are the graphics costs, not the engine (B-864, B-867); the BLIT OPS check (RECT, BAR, COPY, palette blit, scaled blit, blend, palette reload order) passes on the shipped bitstream (B-867); fences are counted by reason (Info `FNC *`).
* The Report no longer refuses (record in a 32 KB PSRAM scratch, grid density picked by size; B-867/B-869); the Check page's TRACK CHANGES row now passes (a test bug); the Check no longer corrupts the icon planes (B-870/B-872).

## Decisions recorded (docs/DECISIONS.md)
D-T01..D-T07: the whole graphics path (scanout, Talos engine, SDRAM controller) is replaced by new modules written from the contract and the old code deleted after hardware confirmation (controller last and optional: one Pocket only, warm soak by the owner); one rename inside the v0.7 RTL fit (`tau_gfx`, `tau_scanout`, `tau_talos`, `tau_sdram_ctrl`, `tau_soc`), T2 (split + rename) gets its own baseline fit; T0 is a hard gate. Plan: `docs/features/TALOS2_REIMPLEMENTATION_PLAN.md` (revision block), reviews `docs/research/TALOS2_EXPERT_REVIEW_2026-10-26.md` and `..._PLAN_REVIEW_2026-10-26_EXHAUSTIVE.md`, baseline `docs/research/TALOS_T0_BASELINE_2026-10-26.md` (stats-b821: ALM 13,784/18,480, LAB 93%, M10K 272/308, DSP 23/66, one seed).

## Scope for v0.7.0 (owner, B-853) and where it stands
`docs/V0_7_0_SCOPE.md` (A firmware/data now, B one RTL fit, C risky items). Done from A: issue 025 (B-854), the SDRAM map register + test (B-859, extended in B-870), counters and checks (B-861..B-872), stale spec headers. Issue 020 (title clipped, no marquee in Player 2) rechecked, **owner choice pending** (marquee / ellipsis / both). Not started: the Info/Diagnostics split (design answers pending: About page one or two, CHECK in the normal build?), Unicode names (card test matrix), Halcyon user presets on a Pocket, gapless MP3 acceptance, sidecars, TPG context items, FLAC glue review.

## Omega
`docs/interop/` (README, LEDGER, `to-omega/001`, the v0.7.0 rehearsal package in `rehearsal-v0.7.0/` rebuilt with the capability keys). `min_omega` 0.6.0 (Omega 0.6.0 must exist before v0.7.0 publishes, else back to 0.3.0), `requires_omega.persist_layout` and `packs_abi`, missing optional owned files are a warning, tag 28 is legacy. **The owner passes the prompt to Omega** (given in the conversation); Omega's replies come back by id.

## Open, in order of value (none blocking)
1. **A 4.2 s stream read after every Check** (`ST MAX LAT` about 280 M cycles, three runs in a row: DEV 187, 188, 189): find what the read waits for at a track change (an orphaned read of the old generation? the SD card? a cover load?). Add a latency histogram and the generation/offset of the slowest read. `HW UNDER EV` jumped to 108 once (DEV 188, Layered Wave) and was 1 on DEV 189 (Winamp Bars): attribute per meter. Also `LOOP MAX` 200-650 ms at a track start, `METER MAX LAYERE` 62 ms (Layered Wave tick).
2. **T1** (firmware only): stacked-bar split, clipping in `gfx.c`, the art probe / fence cell collision (B-859: `ART PROBE RAW DEAD1007` confirms it), async fence at frame end (the 5 ms spin), FIFO free-slots polling.
3. T2 + T3 + T4 RTL bundle (split/rename with its own baseline fit, in-queue control opcodes, Gray fence counter, overflow/underflow flags, column-list primitive).
4. The v0.7.0 release: version bump, `make_release.py --player2 --release v0.7.0 --previous release/alfatreze.TAU_0.6.1_2026-10-21.zip --previous-release v0.6.1`, a Pocket run of the release zip itself, Omega 0.6.0, the owner's approval to publish.

## Traps met this session
`make test-host-fast` (about 2.5 min) is the check before any commit; the wasm meter preview hashes `fw2/gfx/gfx.h` (declare new externs elsewhere); the stash map is `tools/sdram_map.json` (a self-test that writes into the stash must take its rows from it: B-870); a Pocket photo of Info pages is not a Report (use Select+Start on Info); `install_dev_core.py` takes the `pocket` folder of the package and refuses `--carry-from` for a core that reads another platform's media; the record of the last Report is in Info `REPORT ROWS/BYTES` (the first read shows the previous report).
