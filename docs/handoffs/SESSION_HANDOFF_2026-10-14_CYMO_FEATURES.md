# Session handoff 2026-10-14 (second part): Cymo features, gapless, track details, the click that is not fixed (B-718..B-729)

Read this first, then `docs/handoffs/SESSION_HANDOFF_2026-10-14_CYMO_RECORDINGS.md` (the measurement work this builds on). Everything below is on `main` in the worktree `../tau-alpha-boost` (the shared checkout `tau-alpha` is now on a detached HEAD at `f93711f`: another session moved it, not touched here). Commits after `07f13e1` are LOCAL, not pushed.

## 1. State in one paragraph
Group 8 (Halcyon) of the headphone recordings is closed (flutter fix confirmed, displayed curves match the Pocket to 0.05 dB rms). Decisions D-G02..D-G05 are recorded (16-bit slot not adopted, clipper/dither deferred, ReplayGain boost peak-limited, click cause). Built and fitted: **positive ReplayGain against the peak tag** (gain stage `BOOST`, +6 dB ceiling, fit `rgboost-b720` closed on both seeds). Built on the host: **gapless groundwork** (spec, sizing model, G0 instrumentation), **track details page** (index section, loader, formatter, library page), **art sidecar spec**. On the card: `TAU DEV 114` (`0.6.0-dev.114+bd0c256`, bitstream rgboost-b720 seed 1) and TAU's library index rebuilt WITH the details section (139 tracks). **Open and not understood: the HW GAIN click and a small click at track changes** (owner, DEV 113 and 114, Halcyon off): the B-719 theory was real but is not the whole cause.

## 2. What was done (audit)
| Id | What |
|---|---|
| B-718, B-722 | Group 8 recorded and analysed; displayed curves vs measured: rms 0.03-0.05 dB; the analyser's single-point table gave a false 3 dB dip (use the envelope method) |
| B-719 | HW GAIN ON-to-OFF click: found the FIFO output register's held unscaled sample exposed on disable; firmware waits out the glide (`fw/gain_handover.h`). **Host-proven, hardware: click persists** (KB-169) |
| B-720, B-721 | Positive ReplayGain: `tau_gain_stage.sv` `BOOST` (cap 65535, saturating product), peak tags parsed (ID3 and FLAC), cap 0.944/peak, fit closed; test files `test music/rg_boost_tests/` (RG1..RG5) |
| B-723, B-724 | Gapless spec (`docs/features/GAPLESS_SPEC.md`): trim rule proven on six MP3s (exact FLAC length), deeper RTL FIFO rejected, PSRAM ring 1-2 s, lead build time, I/O check |
| B-725 | G0 instrumentation (Info GAP HIST, SLOT SETTLE) + Info label table to cold data |
| B-726, B-728 | Owner: cover decode blocks everything (16.6 s seen) -> async job G1b in the gapless spec; `docs/features/ART_SIDECAR_SPEC.md` (always sidecars, sizes table) |
| B-727 | Track details page (`docs/features/TRACK_DETAILS_SPEC.md`) |
| B-729 | Owner review of DEV 113/114 (section 4) |

## 3. On the card and the repo
- **Card (`Pock`):** `TAU` (v0.6.0-preview-era stable), `TAU DEV 114`, `TAU Preview`, `TAU Preview Diagnostics`, `TAU_DEV_105`, `TAU_DEV_107`. TAU's media has `rg_boost_tests/` (five files, retitled RG1..RG5, one `tau-art` sidecar) and an index with 139 details records. DEV 113 removed (backup `work/card-backups/20261014-dev114`). Ejected at the end.
- **Package:** `work/diagnostics/tau-dev-114/` (also copied to the main checkout's `work/`), bitstream `work/diagnostics/rgboost-b720/ap_core_s1.rbf` (sha256 `fbdb4d8d...`).
- **Worktree:** `../tau-alpha-boost` (branch main). It needs `toolchain` symlinked from `../tau-alpha/toolchain` (git-ignored). **VM:** stage dirs `tau-local/rgboost-b720-s1/s2` are still there (finished fits), not cleaned.
- **Install command used:** `python3 tools/install_dev_core.py work/diagnostics/tau-dev-114/pocket --card /Volumes/Pock --remove "alfatreze.TAU DEV 113" --backup-dir <main>/work/card-backups/<dir> --no-eject --yes`; media/index: `tools/sync_media.py <folder> --card /Volumes/Pock --core alfatreze.TAU --library --no-playlist [--art-variants]`. Tools default to CARDWRITE: always pass `--card /Volumes/Pock`.

## 4. Owner review of DEV 113/114 (2026-10-14, logged, nothing changed)
1. **HW GAIN click still there**, both directions, all volumes, Halcyon FLAT or OFF. **Small click when changing track** (new: B-717 measured it clean on tone files). The Info screenshot (decoded: `tools/decode_tau_suite.py --grid shot.png --table`) was of DEV 113 and does not show a click; it showed LOAD MS art 16,624 ms (the cover freeze) and ReplayGain `C99` (unity shown as 99: fixed in `bd0c256`, 100 now).
   *Not yet examined (candidates, unverified):* what `pcm_flush()` leaves running downstream (the Cymo resampler history and feed queue (Cymo is ON by default for 44.1 kHz in the Diagnostic Build), the stage fade, the 16-bit path); whether the click at a track change is the same mechanism (both go through flush); the 16 ms decoder pause itself. **Next measurement:** `114_tog_hw_v70` and `114_trk_change_music` (6 toggles starting from ON; one real-music track change), each with Cymo OFF and ON; analyse with the burst method of B-711 (0.5 ms peaks; is it silence then a decaying burst, or something else); `cymo_loopback.py track` events. Consider an ISSP/JTAG look at the stage output and the FIFO register if the recordings do not separate them.
2. **Details page shows the previous track's scrolling animation.** Root cause by reading: `lib_ui_marquee()` is called from the main loop whenever the library is open (`fw/player.c:10246`, `if (lib_ui_open)`) and is not gated by `lib_det_open`; it redraws the selected LIST row over the details page every 1/3 s. Fix: gate it (and any other list-only tick) on the page mode. **Helios question:** the library overlay and the new page draw directly with `ov_draw` brackets and the library's own dirty flag; Helios is used only for the open/close view switch (`library_view`), not for regions or a per-view tick, so the marquee (a list concern) ran outside any view ownership. Using Helios correctly would make the details page its own sub-view whose tick owner is the page (a `helios_view_t` with its own tick), not a flag the list code must remember.
3. **Too much info per row:** change to two stacked lines per field (label on top, value below), longer scroll but readable; 9 rows of 28 px become about 5 two-line fields of about 50 px (to be laid out against the 360 px screen); the 25-characters-per-row wrapping goes away or becomes wider.
4. Not yet requested but pending: the page from the now-playing screen.

## 5. Decisions pending (owner)
Art sidecar sizes and uses (spec section 3: 64/128/360 proposed); ring size 1 s or 2 s (decide from G0 data: play an album through several natural ends on DEV 114, read Info GAP HIST and SLOT SETTLE); details page layout (stacked), key (Y) and whether it opens from now playing; whether to push the local commits.

## 6. Next steps, in order
1. Fix the details page: gate the marquee, stacked two-line rows, re-test on host (`make test-host`), repackage as the next DEV number.
2. Click investigation (section 4.1): recordings first, then read what the flush leaves running; no more guess-fixes before data.
3. Check DEV 114 on the Pocket: boost (Info REPLAYGAIN `C197`..`C200`, RG1 about 5.87 dB louder than REPLAYGAIN Off, RG2 `C100`, RG3 about +0.4 dB), G0 rows, details page.
4. Gapless: G1 (PCM ring) after G0 data; async cover (G1b) before the gapless boundary (G2); sidecar always (`ART_SIDECAR_SPEC.md`).
5. Remaining recordings (groups 9, 10, 12, 13, 14, soaks); G-LOAD waits for the 32 ohm resistor.

## 7. Traps met this session
Slow commands: `make test-host` and `make test-rtl` exceed the foreground limit; run them redirected to a file. `fw/build.sh` is not executable (`bash fw/build.sh`); `TAU_BUILD_OUT=work/x` keeps a measurement build out of `dist/`. `diag_features.json` edits need `python3 tools/gen_diag_features.py` (a stale header was committed once). A worktree has no `toolchain`. The installer backs up into the worktree's `work/` unless `--backup-dir` points at the main checkout (the worktree is removed later). The sandbox could not see `/Volumes/Pock` once while Finder could: replug and retry.
