# On-device automated test suite (Diagnostic Build): specification

**Status:** design only (2026-09-21, audit B-053). No code, card or VM change. Replaces the hand-run scripts (`docs/TEST_SCRIPT_B0xx.md`) with a runner inside the Diagnostic Build.
Labels: **[HW]** Pocket result, **[SRC]** read in source, **[EST]** estimate.

## 1. What can and cannot be automated
| Need | Possible? | How |
|---|---|---|
| Run the existing tests (window suite, read/write cost, playlist check, cold code, library check) | yes | call the same functions the Tests page already calls |
| Stress levels and timed soak with underrun counting | yes | drive the existing `stress_*`/soak state from the runner; poll the counters |
| Screens: open each page and prompt for a screenshot | yes | the runner sets the overlay/page itself; the prompt page carries its own label ("SHOT 07 SETTINGS INFO"), so the screenshot names itself in the picture |
| Press Menu+Start for the screenshot | **no, the user must** | the runner shows "MENU+START for a screenshot, then A to continue" (B skips) |
| Tests that need a restart (settings persist, boot time, resume) | yes, in two phases | phase A stores a pending step in persisted words and asks "Quit the core and start it again"; phase B runs at the next boot, verifies and continues |
| Listening tests (glitches, tone) | partly | the runner plays a track for N minutes and reports underruns and draw stall; "did it sound right? A yes / B no" for the human judgement |
| **Saving results to the card** | **only through the channels that are proven** | see section 2: the core cannot write files (0184 write and nonvolatile-slot save failed on hardware, KB-022/KB-032; earlier they also damaged user files) |

## 2. Where the results go
1. **On screen:** live progress, a per-test result table and a final summary page designed to be screenshotted (Menu+Start).
2. **Persisted record on the card (proven channel, KB-025/KB-035):** a compact machine-readable record in four `interact_persist` words, saved by the Pocket when the core is quit to `Settings/<core>/Interact/_core/interact_persist.json`:
   word A = magic `TSUI`-derived tag, format version, profile id, run counter, step count; word B = pass bitmask (steps 0-30); word C = fail bitmask (steps 0-30); word D = key metrics packed (worst SDRAM window access, cold cycles per word x10, late underruns, boot time in 0.1 s, longest stall in ms). Steps 31+ need a second set of words (the record has a 2-bit "page" field so a long suite can rotate pages). Words 8-11 (the legacy playlist name/hash, unused in library mode) are borrowed in the Diagnostic Build, which therefore stops resuming legacy playlists by name (documented; the release is unaffected).
3. **Screenshots** (`Memories/Screenshots/*.png`, taken by the user at the prompts, self-labelled).
4. **Host report:** `tools/decode_tau_suite.py` (and later Tau Browser) reads the persist JSON and the screenshots and writes one report (Markdown/JSON) per run with the evidence label, comparing results with the recorded predictions.
5. **Later, only with an RTL step:** a nonvolatile results slot so the full text log lands on the card as a file. KB-001/KB-004 say the flush is a bridge read sequence that a core serves from a demuxed address region; the earlier attempts failed at the APF command level and this is an open question. It is a separate phase, not needed for the suite.

## 3. Test catalogue (every entry has a pass rule; durations [EST])
| Id | Test | Kind | Time | Pass rule |
|---|---|---|---|---|
| T01 | SDRAM window test | auto | 5 s | PASS 89 |
| T02 | SDRAM read/write cycles | auto | 5 s | read avg <= 60, write avg <= 50, max <= 400 |
| T03 | PSRAM window test | auto | 5 s | PASS |
| T04 | Cold code test (calls in/out, cached call, 12 KB refill) | auto | 5 s | PASS, 30-34 cycles/word |
| T05 | Cold code repeat x20 | auto | 10 s | 20/20 PASS |
| T06 | Playlist check / library check (walk the index, N random opens) | auto | 10-60 s | PASS |
| T07 | Boot and load timings from the Info values | auto | instant | within the recorded bounds |
| T08 | Playback 3 min with counters | auto+listen | 3 min | late underruns 0, draw stall < 5 ms, human "sounded right" |
| T09 | Stress R1, R2, R3 (30 s each) | auto | 1.5 min | no mismatch, late 0, worst access <= 373 |
| T10 | Timed soak 5/15/30/60 min with the cold test every 60 s | auto | as chosen | 0 failures, late 0 |
| T11 | All speeds sweep (0.85x-2.5x, 10 s each) | auto+listen | 2 min | underrun counts recorded (fast speeds expected to fail, recorded not judged) |
| T12 | Library behaviour: play album, next/prev, shuffle-all order check, playlist play, history restore after restart | auto+restart | 3 min | as the library test script |
| T13 | Settings persistence: change colour, volume, repeat, meter; ask for restart; verify after restart | restart | 1 min + restart | values equal |
| T14 | UI tour: open each screen and prompt a screenshot (about 20: idle, now playing, loading, library home/artists/albums/tracks/playlists, settings pages, Info, Tests, library on/off, help pages, colours list) | prompt | 5-8 min | user presses A on each; screenshots present |
| T15 | Colour edition sweep: cycle all 19 colours with a screenshot of one screen each | prompt | 3 min | screenshots present |
| T16 | Negative cases: needs prepared card copies (missing index, flipped byte, missing cold image); the runner tells the user which copy to launch | guided | 5 min per case | expected Info codes |

## 4. Profiles and selection
* **QUICK (about 2 minutes):** T01-T05, T07, four key screenshots.
* **STANDARD (about 12 minutes):** QUICK + T06, T08, T09, T12, the UI tour.
* **FULL (about 60 minutes):** STANDARD + T10 (30 min) + T11 + T13 + T15.
* **CUSTOM:** a checklist page (A toggles, X selects all, Y none), remembered in a persisted word; each entry shows its time estimate and a total.
* **RESTART-SET:** T13, T12 restart part, boot time (T07 at boot): runs across restarts and resumes by itself.
Runner controls: A next/continue, B skip this step, Start pause/abort (asks to confirm), Select mark this step "look at this" (noted in the record). The runner keeps audio and the main loop alive during long steps (it is a state machine ticked from the main loop, not a blocking function).

## 5. Firmware design
* `fw/suite.inc` behind `TAU_DIAG_SUITE` (Diagnostic Builds only, part of `player-library-diagnostic`): step table `{id, name, kind, est_s, fn}`, a state machine (`IDLE, RUN, WAIT_KEY, WAIT_RESTART, DONE`), the record encoder (section 2), the overlay pages (menu, progress, prompt, summary) using the Settings-style rows.
* Drives the existing test functions and pump; adds a periodic cold-test hook to the soak (the owner's earlier request).
* RAM budget: table and state under 1 KB in fast RAM, prompt strings in the cold image (data), overlay code cold-eligible; the combined build has a 5,008 B heap gap, so this waits for or uses cold code (Phase G4) for its UI part.
* Host: `tools/decode_tau_suite.py`, unit-tested with fabricated persist files; an entry in `docs/TEST_SCRIPT_TEMPLATE.md` so new features add their steps to the catalogue instead of a new script.

## 6. Phases
| Phase | Deliverable | Needs |
|---|---|---|
| S1 | Runner skeleton, QUICK profile (T01-T05, T07), record words, summary page, `decode_tau_suite.py`, fixtures | firmware only |
| S2 | STANDARD and FULL (T06, T08-T12), soak with interleaved cold test, UI tour prompts with self-labelled pages | firmware only |
| S3 | Restart-based tests (T13, history, boot time), CUSTOM selection, negative-case guide (T16) | firmware only |
| S4 | Tau Browser report view (diagnostics tab) | Tau Browser T6 |
| S5 (optional) | Nonvolatile results log file on the card | RTL bridge-read region, its own gates |

## 7. Decisions (owner delegated them on 2026-09-21: "make it simpler for users to run diagnostics that can be easily submitted for analysis")
The goal changed from "convenient for the developer" to "a listener can run it and send us something we can analyse". Consequences, all adopted:
1. **A user-facing check in the normal release, not only in the Diagnostic Build.** Menu > Settings > **Check** (the release has about 17 KB of heap gap, the Diagnostic Build far less). It is one profile, **USER CHECK, about 90 seconds**, no choices, no screenshots required: system and memory self-tests (SDRAM and PSRAM windows, cold code when present), playlist/library check, a 20 s playback counter check (underruns, draw stall), timing values (boot ms, load ms, cold image ms), the settings in force, and any error codes. Result: one summary page.
2. **Submission is one screenshot plus one file, and Tau Browser makes it one click.** The summary page prints a **report code** (36 characters in groups of six, with a checksum) and a plain-language verdict ("All checks passed" / "3 checks failed: see codes"). The user sends (a) a screenshot of that page (Menu+Start) and, if we ask, (b) the `Settings/<core>/Interact/_core/interact_persist.json` file. **Tau Browser "Send diagnostics"** gathers everything into one zip (persist files of every Tau core, screenshots newer than the run, core and index versions, card free space and filesystem, file hashes of the installed core) with nothing uploaded automatically. The code alone is enough for a first analysis (`tools/decode_tau_suite.py --code`).
3. **Four persisted words hold the record** (borrowed: words 12-15 in the release, where they are free; words 8-11 in library and Diagnostic Builds, where the legacy playlist name resume is off). Format section 2, plus a version field so the decoder handles older reports.
4. **Developer profiles stay in the Diagnostic Build:** QUICK, STANDARD, FULL, CUSTOM and RESTART-SET as in section 4, using the same runner and the same record, adding screenshots at prompts and the long soak. USER CHECK is simply the first profile of the same engine.
5. **No results file on the card (S5 dropped for now).** The report code plus the persisted record plus the collector cover the need without touching the SD-write problem again.
6. **Build order:** S1 = runner + USER CHECK + record + decoder + fixtures in the release/combined builds (small: about 1.5 KB of code, fits the release; the developer profiles come after, using cold code once G3 is signed off). S2 developer profiles and soak. S3 restart tests. S4 Tau Browser collector (its spec gains a "Send diagnostics" feature).
7. **What analysis gets:** firmware version and build flags (library, cold, diagnostics), bitstream revision, core name and version, heap gap, memory test results with cycle counts, timing breakdown, audio counters, library size and load time, error codes, settings, and the verdict; enough to tell a card or media problem from a firmware or bitstream problem without a second round trip.

## 8. Report as a QR code (owner idea, adopted 2026-09-21; supersedes the 36-character code as the primary channel)
**Why:** a QR code carries hundreds of bytes to kilobytes, survives a screenshot pixel for pixel, can also be scanned from the screen by any phone, and decoders exist everywhere (zbar, OpenCV, browsers, Rust `rqrr`). The 36-character code stays as a short fallback under the QR for when a full analysis is not needed.
**Capacity and geometry [EST, from the QR standard]:** the frame is 400 x 360. At 3 px per module with a 4-module quiet zone, version 20 (97 modules) is 291 + 24 = 315 px: fits, and holds **666 bytes at error level M (858 at L)**. Version 25 (117 modules) at 2 px per module still fits and holds about 1,000 bytes (M) but is harder to photograph. Default: **version up to 20, level M, 3 px modules**; a report that needs more is split into pages (each page carries "page i of n" and its own checksum); the runner shows them one by one, the user takes one screenshot per page.
**Payload (text, so it survives copy and paste from any scanner app):** `TAUD1:` + base64url of a binary record: header (magic, format version, profile, run counter, firmware build id, bitstream revision, core id and version, build flags), then TLV entries `{tag u8, length u8, value}` for: heap gap and free RAM; per-test result, metric and duration; memory-test cycle counts (SDRAM/PSRAM window read/write avg and max, cold code cycles per word); boot, load, cold-image and library-load times; underrun counters (early/late), draw stall, worst access; library size, error code and state; settings values (colour edition, meter, EQ, speed, repeat, shuffle, resume); the last eight error codes; step notes the user marked; CRC32 of everything. Typical size 250-400 bytes before base64 (one QR); a FULL developer run with many metrics uses two or three pages. Unknown tags are skipped by the decoder, so firmware can add fields without breaking old decoders.
**Firmware:** a small QR encoder (`fw/qrcode.h`, portable C: byte mode, level M, versions 1-25, one fixed mask; about 300 lines and 5-6 KB of code) with its working buffers in the PSRAM data window when present (about 4 KB of scratch kept out of fast RAM); until the encoder moves to cold code (Phase G4) it costs about 6 KB of code in the release build (heap gap 17 KB) and cannot go into the combined Diagnostic Build (gap 5 KB) as resident code. Rendering: one rectangle per horizontal run of dark modules (about 2,400 rectangles at version 20), drawn once when the page opens; the page also shows the verdict line and the short code.
**Tests for the encoder:** host tests build the matrix in the rv32sim harness and compare it with a reference encoder for the same version and mask (and, independently, decode the rendered image with a QR reader and require the exact payload); corrupted-module cases must fail the CRC in the decoder; fixtures render the QR page for the snapshot renderer.
**Host tools:** `tools/decode_tau_suite.py --qr screenshot.png` (uses `pyzbar` or OpenCV when installed, else `zbarimg`), `--text 'TAUD1:...'`, `--code` for the short code; all produce the same JSON and Markdown report.
**Risks:** screenshots are pixel exact but a phone photo of the screen may need a cleaner module size (use level M and version <= 20); the encoder adds code that must be proven bit-exact; anti-aliased scaling by an image viewer can blur 3 px modules (decode from the PNG, not a resized copy).
**Decision:** QR primary, short code fallback, pages for long reports; the encoder is the first cold-code candidate once G3 is signed off.

## 9. Decision 2026-09-21 (owner, B-065): the Check lives only in the Diagnostic Build
Section 7 item 1 (a user-facing Check in the normal release) is **withdrawn**. Reasons: the summary borrows the settings words of the legacy playlist resume (resume is lost once a Check has run), the feature needs the cold image and the newer bitstream, it adds support surface to the everyday core, and the Diagnostic Build already installs beside it with its own settings. The release keeps the Info page. The switch `TAU_CHECK`, the target `fw/build.sh player-library-check` and the packager option stay in the source so the release can gain it later without rework. Status: S1 (USER CHECK, record, QR, decoder) and S2 (STANDARD, FULL) are built for the Diagnostic Build; S3 (restart-based tests) is not.

## 10. Profiles as built (B-066)
Diagnostic Build, `fw/suite.inc`: **USER CHECK** (ids 0-6), **STANDARD** (+ track changes id 11, cold code x20 id 12, stress R1-R3 ids 7-9), **FULL** (STANDARD + soak id 10), **ENDURANCE** (ids 0-6 + soak id 10 with a cold-code test each minute; runs and failures in the record's cold entry). Options on the start page: soak length (5/15/30/60 min) and stress level. Record profile ids: 1, 3, 4, 5. A soak passes only with no mismatch, 0 late underruns and 0 failed cold tests. Not built: all-speeds sweep, library random-open, playback listening gate, restart-based tests (S3).
