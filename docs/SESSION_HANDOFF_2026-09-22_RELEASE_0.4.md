# Session handoff, 2026-09-22 (release v0.4.0 shipped: media library, Phase G cold code, on-device diagnostics)

Read first in a fresh session: this file, then `CLAUDE.md` (project rules and execution log), `docs/CURRENT_STATUS.md`, and audit entries
**B-031 to B-081** in `docs/AUDIT_TRAIL.md` (A-NNN = the earlier SDRAM/UI/firmware series, ended at A-137; B-NNN continues from the PSRAM
work through the media library, Phase G and this release; next free id: check `grep '^### [AB]-' docs/AUDIT_TRAIL.md | tail -1`).
Superseded: `docs/SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md` (still correct for the SDRAM/PSRAM P0-P4 history it covers).
Use the `analogue-pocket-dev` skill for anything about the Pocket, APF, SDRAM/PSRAM, packaging or the card.

## 1. State

- **Tau v0.4.0 is built, zipped, and installed on the owner's card as the base `TAU` and `TAU_DIAGNOSTIC`.** Not committed, not tagged, not
  pushed (owner has not asked). Working tree has the whole of this session's work uncommitted (`git status --short` lists about 80 files);
  `.claude/`, `docs/vendor/`, `work/`, `release/`, `UniClaudeProxy/` are intentionally untracked (see `.gitignore`/session convention).
- **What shipped that v0.3.0 didn't:**
  1. **A media library.** `tools/sync_media.py --library` builds a browsable index (`tau-library.tdb`) from the user's own music;
     Select opens Artists/Albums/Tracks/Shuffle All plus imported playlists, with history (loaded, not started, after a restart).
     Optional: no library present means the player works exactly as before, now called **Legacy Playlist Mode**.
  2. **Phase G ("cold code"): about 24 KB of firmware moved from on-chip RAM into PSRAM**, executed through the instruction-fetch
     alias proven in Phase G2/G3. Free RAM rose from the 0.3.0 release's ~18.7 KB (with the library added, briefly ~4-11 KB) to
     **34,752 B** in the shipped release and **~29-34 KB** in the Diagnostic Build. Fails safe: without PSRAM instruction fetch
     (an older bitstream), the library, settings menus, playlist and cover art all switch off cleanly; single-file playback and the
     core menu still work (proven on hardware, B-070/B-071/B-077).
  3. **An on-device diagnostics tool, "Check"** (Diagnostic Build only, by design - see decisions below): Settings > Diagnostics >
     Check runs a self-test (USER CHECK, ~30 s) or longer developer profiles (STANDARD, FULL, ENDURANCE up to 60 min), and produces
     a plain verdict, a **QR code** carrying the full report, and a 36-character fallback code. `tools/decode_tau_suite.py` decodes
     any of: the QR (screenshot), the short code, or the four persisted words APF saves on Quit.
  4. **Left/Right consistency**: in every menu, list and the playlist, Right opens/selects (like A), Left goes back (like B); on a
     switch or Volume they still change the value. Menus and the playlist now scroll continuously on a held Up/Down. The playlist
     list is now visually styled like the settings/library rows.
  5. **Two real bugs fixed** that were present in the shipped 0.3.0: Left/Right in the settings menus (incl. Volume) briefly sought
     the playing track; album art could go blank after a track change within an album. Both root-caused, not just patched (see
     section 3 below - the story of each is worth reading before touching that code again).
- **Bitstream: the G3 build** (seed 1, PSRAM window + PSRAM instruction fetch; raw SHA-256 of the rbf tracked as `work/diagnostics/
  psram-ifetch-g3/ap_core-s1.rbf`, `rbf_r` in the zips `c81b33f9...`). FPGA revision constant unchanged (`4D503317`).
- **Card (`Pock`):** `alfatreze.TAU` and `alfatreze.TAU_DIAGNOSTIC`, both v0.4.0, plus `alfatreze.TAU_DEV_34` (a numbered test build
  on the same firmware, kept separate from the release cores for testing without touching the owner's daily-use install). No other
  test cores remain; `TAU_PSRAM_12` and every earlier `TAU_DEV_NN`/`TAU_PSRAM_NN` were retired with verified backups along the way
  (all under `work/diagnostics/library-0.4/`).
- **Open smoke-test items from the first v0.4.0 boot** (B-080, fixes now installed, not yet re-confirmed on hardware):
  1. Fixed: closing the library with nothing loaded showed a blank screen with "UNKNOWN TRACK"; now shows "Select a track from
     your library" and reuses the same screen at boot, so there is one "nothing loaded" screen everywhere, not two.
  2. **Parked (B-082, owner: UX friction, not blocking):** `TAU` booted to the idle card instead of restoring the last-played
     album, while `TAU_DIAGNOSTIC` restored correctly - a real behavioural difference between builds that share the same source.
     Info's LIBRARY row still ends `R1`/`R0` (boot restore opened something, or not) so the evidence isn't lost whenever this
     is picked back up.
  3. **Parked (B-082, owner: UX friction, not blocking):** selecting an album does not show the loading spinner over the cover,
     although the code path is identical to an ordinary track skip (which does show it, as far as anyone has confirmed). Needs
     a description or screenshot of exactly what appears during that gap whenever this is revisited.
  4. **Not a bug, a design question:** the library does not remember which folder/list you had open (browse position); only
     *playback* history (what was playing) is ever persisted. Flag if browse-position memory is wanted as a feature.

## 2. What v0.4.0 actually contains (firmware map)

Everything below is gated by build-time macros in `fw/build.sh`; the release (`bash fw/build.sh release`) and the Diagnostic Build
(`player-library-diagnostic`) both carry `TAU_LIBRARY=1 TAU_COLD=1 TAU_COLD_CODE=1 TAU_G4=${G4:-2} TAU_ART_PSRAM=1 TAU_SETTINGS_UI=1
TAU_PL_SDRAM=1 TAU_DIAG_INFO=1 TAU_METER_THUMBS=1`; the Diagnostic Build additionally has `TAU_DIAG_TESTS=1 TAU_SDRAM_STRESS=1
TAU_SDRAM_STRESS_WINDOW=1 TAU_STRESS_HUD=1 TAU_CHECK=1`. `TAU_CHECK` is deliberately **not** in the release (owner decision, B-065/
B-073: it borrows the legacy-playlist-resume settings words, needs the cold image, and is developer/tester surface the everyday
core doesn't need - the release keeps the Info page instead).

- **Media library** (`fw/library.inc`, `fw/library_core.h`, `tools/tau_library.py`): index format `tau-library.tdb` (data slot 5,
  optional, never shipped - built by the user's own sync run), PSRAM-resident (queue, dead-track bitmap, the index image itself),
  browse stack Artists/Albums/Tracks/Lists, Shuffle All (Fisher-Yates), history in persist words 24-27 (see `tools/tau_data_slots.py`
  for the exact declarations - **shared** between the release packager and the test-build packager so they can't drift).
- **Phase G / cold code** (`fw/cold.inc`, `fw/cold_core.h`, `tools/pack_cold.py`, RTL `tau_psram_bus`/arbiter in `mp3_soc.v` behind
  `TAU_PSRAM_IFETCH`): a second, read-only PSRAM port + line-fill adapter feeds the CPU's instruction cache from a second PSRAM
  alias (`0x2480_0000`+); a boot-time check (bitstream feature bit + a probe call) decides `COLD_READY()`, and every entry from hot
  code into cold code is behind either that check or state that implies it (library loaded, menu open) - see `tools/check_cold_calls.py`
  for the full list, and `docs/PHASE_G_SPEC.md` section 8 for what moved at each step:
  - G1 (data only): meter previews, help text.
  - G2/G3 (RTL + bring-up): the instruction-fetch path itself, proven on hardware (31.6 cycles/instruction-word on a cache miss,
    no SDRAM regression, 0 late underruns through a 30-min soak).
  - G4 step 1: the whole library UI and settings-menu code (including the old Diagnostic Tests/Stress menus).
  - G4 step 2-3: the playlist loader and overlay, and the cover-art glue (`art_find_apic`, `art_decode`, `art_find_flac_picture`,
    `art_flush_row`). **Deliberately not moved:** the track-open path used by every load, `read_track_head`/`load_track` themselves,
    boot/idle/splash code, the picojpeg byte-feed callback (`art_need_bytes`, called too often per cover to afford a cold miss), and
    `ui_draw_dynamic` (the meters - per-frame code, waiting on the blit engine to make the split worthwhile; still ~17 KB hot).
- **The Check** (`fw/suite_core.h` portable record/QR-text/persist-word format + `fw/qrcode.h` QR encoder, both cold code;
  `fw/suite.inc` the runner and pages; `tools/decode_tau_suite.py` the host decoder, byte-exact against the firmware - see
  `sim/test_suite.py` and `sim/test_qr.py`, both in `make test-host`). QR is level L (not M - owner decision, B-073 addendum:
  screenshots are pixel-exact, so error correction buys little against extra capacity), versions up to 38, drawn at 2-4 px/module
  depending on report size. Profiles: USER CHECK (7 tests), STANDARD (+track changes, cold x20, stress R1-R3), FULL (+a soak),
  ENDURANCE (soak alone, 5/15/30/60 min, with a cold-code test every 60 s during it).
- **Everything else** (MP3/FLAC/JPEG decode, the SDRAM playlist window, PSRAM album art, the settings menus' look) is the same
  system documented in `docs/SESSION_HANDOFF_2026-09-21_RELEASE_0.3.md` and the SDRAM/PSRAM architecture docs.

## 3. Two bug stories worth reading before touching this code again

**The album-art stash race (B-075).** The library loader (`ui_loader_begin_ex`, shows the loading spinner) used to clear the
off-screen art buffer on every track load. Further down `load_track()`, an optimisation ("most tracks of an album share one
picture, so if the signature matches, skip the redecode and just reuse what's there") assumed that buffer still held the previous
track's picture. It did, before the loader existed; once the loader started clearing it first, every track after the first in an
album found nothing to reuse and stayed blank. Two investigations (B-073, B-074) chased a *different* file's cover before this was
found by re-reading the actual sequence rather than the individual symptom - the lesson: when something works for track 1 and
breaks for track 2+ of the *same source*, look at what runs between loads, not at the file being loaded.

**The cover-finder "bug" that wasn't (B-073).** Before finding the above, a specific cover (a v2.3-tagged MP3, 455x455, non-
progressive JPEG) appeared not to load. A byte-exact copy of the frame-finding code, run natively against the file's real bytes
pulled off the card, found the picture correctly - proving the *parsing logic* was never at fault, before the real cause (above)
was found. Worth remembering: when a host-side replica of firmware logic agrees with the file but the Pocket doesn't, look
elsewhere in the pipeline (timing, redraw ordering, caching) rather than re-reading the same parsing code a third time.

## 4. Decisions made this session (ask before reversing any of these)

- **The Check stays Diagnostic-Build-only** (B-065/B-073): not worth the resume-word conflict and code-space cost in the release;
  the switch (`TAU_CHECK`) and the release-style build target (`player-library-check`) are kept in source so it can be turned on
  later without rework.
- **QR density: level L, not M** (B-073 addendum): screenshots are pixel-exact, so error correction was trading away capacity for
  nothing. Revisit if a future report genuinely needs more room than level L's larger versions give.
- **Numbered test builds are `TAU DEV NN`**, not `TAU PSRAM NN` (owner, B-060 addendum): numbering continues from the last one used
  (currently 34), never reused, never restarted.
- **Left/Right are Back/Forward everywhere** (owner, B-072): the playlist's old "page a screenful" behaviour on Left/Right moved to
  the shoulder buttons (L1/R1); library lists lost their own page-by-screenful the same way (hold Up/Down and the letter jump
  remain).
- **Playlist rows restyled to match settings/library** (owner, B-073 clarifying question: "Settings-style rows"): own constants
  `PLIST_ROW_H`/`PLIST_ROWS` (36 px / 7 rows), asserted equal to `SET_MENU_ROW_H`/`LIB_ROW_H`/`LIB_ROWS` at compile time so the
  three list styles can't drift apart again; the other, denser list pages (Info, Check, Stress Status, Help) were **not** touched -
  out of scope, still 22 px / 12 rows via `PL_UI_ROW_H`/`PL_UI_ROWS`.
- **`TAU_PSRAM_12` and other stale test cores get removed on the next card write that touches the card anyway** - not worth a
  dedicated card write purely for cleanup (owner, this session).

## 5. Owner rules (apply always - unchanged from 0.3.0, repeated because they matter)

- The owner approves every VM launch and every SD-card write explicitly; stage and verify, then ask. Commit and push only when asked.
- **Concise chat replies:** outcome first, key decisions, no hashes/paths/code unless asked; details go in `docs/AUDIT_TRAIL.md` and
  the `CLAUDE.md` execution log (one line per turn that changes something).
- **`TAU DEV NN`**, never reused; **remove superseded test cores on every install** after a verified backup; **every test build
  gets its own media copy** (`tools/sync_media.py --library`); the base `TAU`/`TAU_DIAGNOSTIC` keep their names.
- **Two zips per release** (`tools/make_release.py`); README's Diagnostics section stays current.
- Predictions are written before hardware results; do not stage other sessions' files; never modify upstream references;
  `bash -n fw/build.sh` and `make test-host` (or `make test` for anything touching RTL) before anything is called done.

## 6. Procedures

- **Release:** bump `fw/player.c` `APP_VER`, `dist/Cores/alfatreze.TAU/core.json`, README ("Current version"), `CHANGELOG.md`
  (`build.sh` refuses a version mismatch), then `python3 tools/make_release.py --rbf <raw rbf> --rbf-sha256 <hash> --test`. This
  now builds `release` (the library+Phase G release firmware) and `player-library-diagnostic` (adds Check + the old Tests/Stress
  menus), packages the release with `package.py --release-library` (declares the library and cold-image data slots via
  `tools/tau_data_slots.py`) and the Diagnostic Build with `package_sdram_stress.py --playlist-sdram --diagnostic --library --cold`.
- **Firmware builds:** `bash fw/build.sh release | player-library-diagnostic | player-library-check` (the last is the release
  firmware *with* Check added, for experiments - not shipped). `G4=1` reproduces Phase G step 1 only, `G4=0` disables Phase G
  entirely, for bisecting a regression back to a specific step. `make test-host` (fast) and `make test` (host + RTL, ~3 min; the
  FAIL lines it prints for the PSRAM/G3 mutation tests are injected-fault cases that must be caught, not real failures).
- **The Check's own tests:** `make test-qr` (~90 s, needs `work/venv-qr`, not in `test-host`) checks the QR encoder against segno
  for every version; `sim/test_suite.py` (in `test-host`) checks the report record/decoder.
- **Numbered test core:** `tools/package_sdram_stress.py --playlist-sdram --diagnostic --settings --library --cold --number N
  --note "..." --rbf ... --rbf-sha256 ...`.
- **Media to the card:** `python3 tools/sync_media.py <folders> --core alfatreze.X [--core ...] --library [--mirror] [--dry-run]`.
- **Card install:** back up the replaced cores (core, assets, platform files, settings) and the five `System/*.bin` indexes to
  `work/diagnostics/library-0.4/card-...`, verify with SHA-256, install, verify again, delete the five indexes, clear `._` files,
  `sync`, `diskutil eject /Volumes/Pock`. A card mounted through the Pocket's own USB connection is much slower than a card reader
  for a full media copy (20+ minutes observed for ~200 MB) - prefer a reader when one is available.
- **Reading results:** `tools/decode_tau_suite.py --qr <screenshot.png>` (needs OpenCV; tries its Aruco detector first for dense
  codes), `--interact <persist.json>`, `--code <short code>`. `tools/check_cold_calls.py fw.elf` lists every hot-to-cold call for
  review after touching what's cold vs hot.

## 7. Open items, in the order they're likely to matter

1. **Smoke-test the rest of v0.4.0 properly**: this release has only been tested as separate `TAU DEV` pieces plus one very short
   first boot; a fuller pass (the `docs/TEST_SCRIPT_B071.md` shape, adapted for the release build rather than a numbered test core)
   hasn't been run against the actual shipped zips.
2. **Committing, tagging, pushing v0.4.0** - not done; ask before any of it, per standing rule.
3. **Test coverage still missing** (`docs/TEST_SUITE_SPEC.md` sections 4/10): restart-based Check tests (settings persistence,
   history resume, boot time - "S3"), a library random-open/stress test, a "browse while playing" case in the Check (the real risk
   G4 introduced and the one case that isn't automatically regression-tested), the all-speeds sweep.
4. **BUG-001** (accented file names fail to open) - workaround shipped in the sync tool since B-026, firmware fix still open;
   `docs/issues/001-unicode-playlist-paths.md`.
5. **Tau Browser** ("Send diagnostics" collector spec written, `../Tau Browser/DIAGNOSTICS_COLLECTOR.md`; implementation is
   Codex's, per the earlier prompt set in `../Tau Browser/`).
6. **Further Phase G**: the meter/visualizer split (~17 KB hot, deferred to the blit engine per the roadmap - splitting it twice,
   once now and once for blit commands, isn't worth it) and the picojpeg decoder (~8 KB, currently hot for the `art_need_bytes`
   reason above).
7. **Parked, revisit later**: per-colour text/dark palette entries, a library page in the Diagnostic Build, resume-by-second - all
   cheap now that Phase G freed real memory, none started.
8. **Longer-term roadmap order** (`docs/ARCHITECTURE_ROADMAP.md`): JTAG/SignalTap bring-up (Phase F0, procedure already works -
   `docs/JTAG_DEBUG_ACCESS.md`; the SignalTap proof was synthesis-only, so it didn't spend a real Quartus build), then the blit
   engine (Phase F) - which now also carries the SDRAM busy-cycle counter (owner decision 2026-09-22: deliberately left out of
   every RTL build so far so each one's timing changes stayed attributable; the blit-engine build already changes RTL and
   firmware together, so it rides along there instead of spending its own slot) - and unlocks item 6 and a real PSRAM-vs-BRAM
   album-art comparison (parked since B-028).
9. **Parked (B-082, owner: UX friction, not blocking)**: the `TAU` vs `TAU_DIAGNOSTIC` boot-restore mismatch (Info `R0`/`R1`
   evidence already on the card, read whenever this is picked back up) and the missing loading-message on an album pick
   (needs a description/screenshot of the gap when revisited).

## 8. Where to look for anything specific

- **Full narrative history**: `docs/AUDIT_TRAIL.md`, A-001 through A-137 (SDRAM/UI/firmware, ends at v0.2.2) then B-001 onward
  (PSRAM, then the media library from B-031, Phase G from B-045, the Check from B-053, this release from B-078). Every entry has
  an evidence label (host-verified / build-verified / hardware-observed) and, for hardware results, a prediction recorded first.
- **Design docs**: `docs/MEDIA_LIBRARY_0.4_SPEC.md` (index format, what was deferred - thumbnails, non-ASCII search, etc.),
  `docs/PHASE_G_SPEC.md` (address map, RTL design, phase-by-phase progress log), `docs/TEST_SUITE_SPEC.md` (the Check's design,
  including the QR report format), `docs/PSRAM_IMPLEMENTATION_PLAN.md`/`docs/PSRAM_TIMING_CONTRACT.md` (PSRAM P0-P4 history),
  `docs/ARCHITECTURE_ROADMAP.md` (ordering of everything not yet started).
- **The `analogue-pocket-dev` skill's knowledge base** has local entries through KB-044 (cold-image/instruction-fetch findings);
  `KB-041` (accented names), `KB-042` (JPEG decode follows file weight), `KB-040` (PSRAM window cost).
