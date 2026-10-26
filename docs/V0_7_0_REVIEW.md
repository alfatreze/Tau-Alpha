# v0.7.0 review: candidates, recommended cut, everything else pending or parked

**Superseded in scope by `docs/V0_7_0_SCOPE.md` (the owner widened the scope on 2026-10-26); section 1 (what `main` carries), the release gates and the lists in 3 to 5 remain a useful record. Gapless: the ROADMAP says closed for FLAC (DEV 167); only real-music MP3 acceptance is unrecorded.**

Prepared 2026-10-26 for the owner's review. Proposal only: the order and the cut are the owner's (`docs/ROADMAP.md` stays the only ordered list). Evidence labels follow `docs/AUDIT_TRAIL.md`.

## 1. What `main` already carries since v0.6.1 (the base of v0.7.0)
| Item | Evidence |
|---|---|
| Meters on Player 2: every meter but Winamp Bars is a loadable pack (descriptors in the packs, per-pack cycle limits and three strikes D-M20, command budget, `make new-meter`) | DEV 173-184 on a Pocket; DEV 184 tested by the owner: all seven packs run, no underruns (B-837) |
| Meter packs ship as the core's own `tau-packs.bin` (data slot 9), `PACKS=1` release build | host rehearsal of the release scripts (B-834, B-849); **a Pocket run of the release zip itself has not been done** |
| Audio statistics block (`tau_audio_stats.sv`, fit `stats-b821` closed) and the SOUND MAP pack | fit closed; SOUND MAP ran on DEV 184 (owner: all good) |
| Waterfall: horizontal spectrogram, colour maps, SPARKLE, fills the box | DEV 177-184 owner results |
| Persistence by area (one word per area, presets for 20 meters, tuning pool) and the legacy -> new migration | **migration hardware-confirmed for one real file (B-847)**; other legacy files and power loss mid-migration are host-only |
| Last played track and colour restored; seventh meter preset persists | owner Pocket result (B-822) |
| Now Playing shows the Halcyon preset tag | owner Pocket result (B-832) |
| Info rows (statistics, `PACK SRC`, `SET MIGRATED`, `SET LAYOUT`) | on a Pocket |
| Omega interface: `tau-compat.json` capability keys, `min_omega`, interop mailbox | host; Omega side not built |

## 2. Recommended cut for v0.7.0
**Ship what is on `main`; add only small, host-checkable fixes; keep every RTL or fit item out.** Reasons: the base is hardware-tested on DEV 184/185, there is no open fit, and the release needs gates of its own (below).

Add (small, low risk):
1. **Issue 025** (`tau_details.pack` crash on a ReplayGain peak of 2.0 or more): clamp in the index writer, a few lines, host test; nothing on the firmware. Omega already clamps on its side.
2. Re-check **issue 020** (Now Playing title clipped at 45 characters) on Player 2: logged against the old player, may already be gone.
3. Docs: changelog written for owners, README current version, the persist migration described (settings of an older Tau are carried over).

Decide (owner):
4. **BUG-001 Latin-1 / Unicode library names** (`docs/issues/001`): visible to users with accented names; touches the library reader and what Omega writes (index version can stay 1). Recommend **v0.7.1 or later**, unless accented names are a v0.7.0 promise.
5. Do **not** add: the writable font memory (FONT_SPEC, needs an RTL fit), Halcyon CUSTOM persistence, a pack-safety hardware watchdog, more than seven meters, anything needing a new bitstream.

Release gates (not features):
* version bump (core.json, README, CHANGELOG, `APP_VER`) then `make_release.py --player2 --release v0.7.0 --previous release/alfatreze.TAU_0.6.1_2026-10-21.zip --previous-release v0.6.1`; the bitstream is `stats-b821` unless a newer one is chosen (rehearsed with it);
* **a Pocket run of the release package itself** (clean card install, `PACK SRC 2`, all seven packs, the migration from a v0.6.1 settings file) before publishing;
* **Tau Omega 0.6.0 must exist** (it will not publish until its three items are done), else `min_omega` goes back to 0.3.0 for this release;
* owner's approval to publish, assets compared with `release/SHA256SUMS.txt`, `--repo alfatreze/Tau-Alpha` (rule 8).

## 3. Open defects and checks (none blocking, none assigned to v0.7.0 above)
| Item | State |
|---|---|
| DEV 182 underruns (`HW UNDER EV 3`, `DRY 3`, `GOV PANIC 5`) | seen once on DEV 182, not again on DEV 184; watch during the release Pocket run |
| Issue 023: rare audio spike | open, not investigated, not shown to be Cymo |
| Issue 024: MP3 noise/clicks regression | open, plan only |
| Issue 025 | see cut, item 1 |
| `Track changes` Check failure | open since 0.6 work, unexplained (old Check; may differ on Player 2) |
| HW GAIN ON-to-OFF click below full volume, small track-change click | open (old player evidence, B-719 theory insufficient; re-test on Player 2) |
| Tempo clicks at 1.75x and above; FLAC tempo not supported | parked |
| Seek unreliable (item 20) | old player; Player 2 seek has "more issues", acceleration while holding a key tagged for later |
| Asynchronous present; Helios input layer | open structural items |
| Heavy HEAT MAP / FINE GRID / DUST at fullscreen | open |
| Check/Info report items: I2S JITTER average wraps after about 64 s (cosmetic RTL) | for the next RTL fit |
| BUG-001 accented names | see cut, item 4 |

## 4. Pending, waiting on someone
| Item | Waiting on |
|---|---|
| Omega: compare the capability keys, run the rehearsal on CARDWRITE, the two held decisions (Waterfall wasm twin, versioned lab drop), adopt the mirror folder | Omega |
| Omega 0.6.0 (persist decoding, packs left alone, `min_omega` check) | Omega |
| Cymo recordings with a MONO plug (groups 9-14, soaks, 32 ohm load) | owner (hardware) |
| Gapless hardware acceptance and the sidecar work, art sidecar spec | owner / later |
| Report codes (TPG): the pixel grid is already the default (D-R04 was reversed 2026-10-06); Reed-Solomon and extra context items | see `docs/V0_7_0_SCOPE.md` |
| Stable `v0.6.0` follow-ups: edit old preview notes | owner |

## 5. Parked on purpose (do not re-litigate without the owner)
* **Winamp on-device configurator**: may be replaced by Tau Omega; do not extend.
* **PSRAM art** (JPEG fallback buffers `art-overlay`): release keeps art in BRAM until the blit-engine comparison.
* **Firmware modularization** (`FIRMWARE_MODULARIZATION_PLAN`): until key features land.
* **Cover art `TIM1` container**: frozen; the work is making it effective over time.
* **Audiobook tempo extras** (cap list, cold-vs-hot cost, headroom guard, pause shortening, persistence): `AUDIOBOOK_IDEAS_PARKED.md`.
* **DSP review phase 2** (crossfeed, widening, bass boost, multiband, loudness matching) and **Halcyon CUSTOM presets, 24-bit path** (gated by D-S05, D-G01).
* **Cymo output chain extras**: clipper and dither deferred, 16-bit slot not adopted (D-G02..D-G05).
* **Hardware meter helpers** (beat detector, correlation, Chladni evaluator), **batched wave/scope path** (compiled out), removal of redundant software meter paths.
* **RAM headroom** (MP3 ring resize 4-7 KB, meter scratch about 2 KB).
* **Talos 2** reimplementation (frees about 6,500 ALMs), **FLAC decoder v2 follow-ups** (LPC unit, Info rows, hardware A/B), **font spec** (writable font memory).
* **Meter module M6**, public preset gallery (D-M12), blit ideas (B13 gradient bar, B19 flip flags, B10 RLE).
* **Library items waiting for RAM**, tracker/MOD support, CJK/UTF-8 fonts, Chladni presets beyond Lattice/Shimmer, per-channel waveform presets.
* **Resolution 720**: last, by owner decision; keep everything resolution-agnostic.
* **More than seven meters** (list scrolling, 8 pack slots, stats array) and **pack signing**: proposed, not scheduled.
