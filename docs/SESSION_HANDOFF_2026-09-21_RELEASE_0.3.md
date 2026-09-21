# Session handoff, 2026-09-21 (release v0.3.0 shipped; PSRAM in the product; next: 0.4 media library)

Read first in a fresh session: this file, then `docs/MEDIA_LIBRARY_0.4_BRIEF.md` if the task is the library, `CLAUDE.md` (project rules and execution log),
`docs/CURRENT_STATUS.md`, and the audit entries **B-021 to B-029** in `docs/AUDIT_TRAIL.md` (A-NNN = SDRAM/UI/firmware series, B-NNN = PSRAM series; the next free ids are the
highest of each plus one, check with `grep '^### [AB]-' docs/AUDIT_TRAIL.md`). Older context: `docs/SESSION_HANDOFF_2026-09-21.md` (SDRAM/UI, VM procedures, card
procedure) and `docs/SESSION_HANDOFF_PSRAM_2026-09-21.md` (PSRAM). Use the `analogue-pocket-dev` skill for anything about the Pocket, APF, SDRAM/PSRAM, packaging or the card; its
knowledge base has local entries KB-029 (PSRAM datasheet timing), KB-036 (I/O register packing), KB-037 (PSRAM async on defaults), KB-040 (PSRAM window cost 32/26 cycles),
KB-041 (accented names fail to open), KB-042 (JPEG decode time follows file size).

## 1. State
- **Tau v0.3.0 is built, zipped, installed on the owner's card and committed locally (tag `v0.3.0`, not pushed).** Commits `12cb377` (firmware), `4339fc4` (tools), `e2b4efd` (release files), `0826bf2` (audit/docs).
  The owner skipped the Pocket smoke test of the final release files (the closest tested builds were the numbered test cores 03/05, same source before the version bump).
- **Two zips per release, always** (owner rule), made by `python3 tools/make_release.py --rbf <raw rbf> --rbf-sha256 <hash> --test`:
  `release/alfatreze.TAU_0.3.0_2026-09-21.zip` (normal core: Settings + Info) and `release/alfatreze.TAU_DIAGNOSTIC_0.3.0_2026-09-21.zip` (Diagnostic Build: adds Tests, Stress, ALL SPEEDS).
  Naming follows Analogue's `<Author>.<Core>_<Version>_<Date>.zip`; zips hold only `Cores`, `Platforms`, `Assets`; hashes in `release/SHA256SUMS.txt` (`release/` is not committed).
- **Bitstream = the P4 build** (raw SHA-256 `6db879a70d8af2c344f85ab6efd842a972677a55e78b8af96de558bc76b68e1a`, reversed copy in the zips): SDRAM CPU window + PSRAM mailbox + PSRAM CPU window (`0xA4000000`, 32 MiB).
  Both cores use it. Copy: `work/diagnostics/psram-diag/fpga-b018-diag/ap_core.rbf`. FPGA revision constant unchanged (`4D503317`).
- **Card (`Pock`):** cores `TAU` (release) and `TAU_DIAGNOSTIC` only; `TAU_DIAGNOSTIC` has the test media (3 albums + root playlist, covers embedded, ASCII names). Two hidden `._alfatreze.TAU*` entries in `Cores/` were left (macOS junk); if the menu misbehaves, remount and delete them.
- **What v0.3.0 contains:** PSRAM in the bitstream; album-art working buffer (`art_acc`, 11,040 B) in PSRAM behind `TAU_ART_PSRAM`, proven at first use, art off if PSRAM is missing; new platform artwork; README Diagnostics section; corrected controls text.
  Heap gap release 18,736 B (7,824 B with the buffer in BRAM), diagnostic build 14,464 B.

## 2. What was learned today (details in the audit trail)
1. **PSRAM works and is fast and predictable through the CPU window:** read 32 / write 26 cycles, min = max, 1,000-pass soak 1.05 billion checks 0 failures (B-022, KB-040). The P4 shared RTL did not change SDRAM behaviour or playback (B-023). The product-configuration build was **not bit-identical** to the shipped one (B-021): shared RTL logic changed even with macros off, so any RTL change needs the full gates.
2. **Cover art decode is slow with heavy files, not large pictures.** The player reads art **only from inside the track** (MP3 APIC / FLAC PICTURE), never from `cover.jpg`. Decode time follows compressed bytes: Figma exports were 255 KB for 455 px (9.9 bits/pixel) and 1.5 MB for 1400 px; measured 5.3 s and 15.8 s (B-027) against 2.6 s for a 36 KB, 707 px cover (A-120).
   The PSRAM accumulator's own cost is **unmeasured**: the planned A/B (`TAU PSRAM 06` control, art in BRAM, packaged in `work/diagnostics/tau-psram-06`) was skipped by the owner until the blit engine exists. Re-encode covers before timing (`sync_media.py --cover-quality 75`, about 100 KB). The screen shows 92 px.
3. **Accented file names fail to open** (BUG-001, `docs/issues/001-unicode-playlist-paths.md`, KB-041): macOS stores them decomposed; the firmware's 0192 path template takes the longest printable-ASCII run of the descriptor. Workaround shipped in the tool: ASCII-only names, accents to the plain letter, characters with no equivalent removed, playlist lines rewritten. Firmware fix still open.
4. **The playlist file convention:** bare filenames relative to the playlist's own folder, natural track order, `#` lines ignored; cap 256 tracks or 12 KB text.
5. **Owner-run tests:** the soak loop in the PSRAM diagnostic only polls its stop keys between passes (hold a key for 2 s); Pocket screenshots (Menu + Start) land in `Memories/Screenshots` on the card and may pause playback (KB-031); results decode with `tools/decode_tau_diag_log.py --interact --psram-window`.
6. **Rebuilding changes ROM bytes even for identical behaviour**; a numbered test core must never be repackaged under the same number (B-025): give it the next number.
7. **FAT/macOS card quirks:** `cp`/`rsync` create `._*` files; a decomposed-name folder can leave an undeletable phantom `._` entry; remove the folder to clear it. Writes of about 250 MB take several minutes: run long copies in the background.

## 3. Owner rules (apply always)
- The owner approves every VM launch and every SD-card write explicitly; stage and verify, then ask. Commit and push only when asked (this session's local commits followed "skip smoke tests"; nothing was pushed).
- **Concise chat replies:** outcome first, key decisions, no hashes/paths/code unless asked; put details in `docs/AUDIT_TRAIL.md` and the `CLAUDE.md` execution log (one line per turn that changes something).
- **Numbered test builds "TAU PSRAM NN"**, never reused; **remove superseded test cores on every install** after a verified backup; **every test build gets its own media copy** (`tools/sync_media.py`); the base `TAU` release core keeps its name.
- **Two zips per release**; README's Diagnostics section stays current; the diagnostic build and the release stay in tandem (same bitstream and flags; only the test menus differ). Exception today: none (both have art in PSRAM).
- Predictions are written before hardware results; use the B series for PSRAM work; log KB learnings (`kb.py new --local`, promote only with evidence); do not stage other sessions' files; never modify upstream references.

## 4. Procedures
- **Release:** bump the version in `fw/player.c` (`APP_VER`), `dist/Cores/alfatreze.TAU/core.json`, README ("Current version") and CHANGELOG (`build.sh` refuses a mismatch), then `tools/make_release.py` (builds both ROMs, packages both cores, checks both zips). Platform artwork: `python3 tools/convert_pocket_art.py platform assets/branding/platform-artwork.png dist/Platforms/_images/tau.bin` (works without Pillow; 521x165 PNG).
- **Firmware builds:** `bash fw/build.sh release | player-settings | player-diagnostic` (`ART_PSRAM=0` puts the art buffer back in BRAM; the diagnostic build cannot link that way with ALL SPEEDS: gap 3,584 B under its 4,096 B floor). `make test-host` (fast) and `make test` (27 suites, about 3 minutes; the FAIL lines it prints are injected-fault cases that must be caught).
- **Numbered test core:** `tools/package_sdram_stress.py --playlist-sdram --settings|--diagnostic --number N --rbf ... --rbf-sha256 ...` (`--note` changes the description).
- **Media to the card:** `python3 tools/sync_media.py <folders/files> --core alfatreze.X [--core ...] [--all-tau|--from-core ID] --embed-cover [--cover-quality 75 --cover-max 800] [--dest-suffix " (opt)"] [--mirror] [--dry-run] --manifest FILE`. Converts names to ASCII, embeds covers into the copies, creates missing playlists, verifies every file; never modifies the source.
- **Card install:** back up the replaced cores, their settings, platform files and the five `System/*.bin` indexes to `work/diagnostics/...`, verify with `cmp`, install (`rsync -rt --exclude='._*'` or the sync tool), verify SHA-256/`cmp` against the zip contents, delete the five indexes, `sync`, `diskutil eject /Volumes/Pock`.
- **VM (Quartus):** unchanged from `docs/SESSION_HANDOFF_PSRAM_2026-09-21.md` section 4 and `tools/quartus_fit_summary.py`; no build is pending.

## 5. Open items
1. **Media library 0.4** (next task): `docs/MEDIA_LIBRARY_0.4_BRIEF.md`.
2. **Blit / scaling engine** (owner: compare it side by side with the PSRAM art buffer); the control build `TAU PSRAM 06` and `ART_PSRAM=1` builds are the reference. Also makes pre-scaled thumbnails attractive (see the brief).
3. **BUG-001 in firmware** (byte-transparent or normalised paths; test matrix in the issue).
4. Not pushed: local commits and tag `v0.3.0`; smoke test of the final release files skipped; post-soak playlist check gaps of earlier runs are closed by B-023 (Tests page).
5. PSRAM P5 beyond the art buffer, temperature/second-unit margin for the read timing (default sample index 9 keeps two clocks), cached window and code-in-memory experiments (Phase G) remain optional.
