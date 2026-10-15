# Session handoff 2026-10-16: Player 2 (new player) S0 to S3 built and run on a Pocket; old-player gapless findings

Audit B-752 to B-760 (`docs/AUDIT_TRAIL.md`). **Everything is committed locally and NOT pushed.** Work in the worktree **`../tau-alpha-player2`, branch `player2`** (head `a60d752`); it contains everything from `../tau-alpha-boost` (branch `main`, head `1284d02`: the spec, decisions D-P2-01..10, the DEV 123 to 128 firmware work). The primary checkout `tau-alpha` is a detached old HEAD: do not work there. Card tools need `--card /Volumes/Pock` (the tools default to CARDWRITE). The vendored toolchain is untracked: `tau-alpha-player2/toolchain` is a symlink to `../tau-alpha-boost/toolchain` (recreate it in any new worktree).

## 1. What the owner asked for, in order
(1) Fix the DEV 123 problems of the old player's gapless ring; (2) make reports easier than several screenshots; (3) **rebuild the player** around Cymo's audio architecture, dropping the inherited HarpMudd application code; spec it as a senior embedded audio / Analogue Pocket engineer; (4) build and test it stage by stage on the Pocket.

## 2. The spec and the decisions (read these first)
* **`docs/features/PLAYER_V2_SPEC.md`**: the whole design (audio is hardware-paced and the firmware a producer with a fill target; immutable `track_t`; a 4-entry **timeline** of segments and one position formula on a single hardware number, `heard = R_RING_CNT - FIFO level`; a **sample-accounting invariant** checked on the device; one target request in flight with generations; hot paths with no indirect calls into cold code; a **counter registry** that generates Info and reports; one typed capability struct). Sections 14 (what HarpMudd code is retired and when), 16 (stages S0 to S9) and 17 (decisions).
* **Decisions (`docs/DECISIONS.md`)**: D-P2-01..08 and 10 agreed as recommended (new `player2` target beside the old; drop pre-ring bitstreams; replace the inherited FLAC decoder LAST; keep Helix; gapless same rate+codec first, prepared from the library index details; tempo with the ring on; optional CAPS register + free-running underrun counter as one small fit before S3 without blocking; UI parity first, Figma later; manual change keeps flush + fade-in). **D-P2-09: no LTO now, revisit with measurements at S6.**
* Supporting evidence: `docs/research/PLAYER2_GLOBALS_MAP.md` (736 file-scope variables, 601 functions, **163 written by more than one cluster**, 74 touched by more than three), `docs/research/player2/BASELINE.json` (pinned hashes; `tools/player2/baseline.py --check` is in `make test-host`), `docs/research/HARPMUDD_COMPARISON_AND_REWRITE_2026-10-12.md` (28% HarpMudd-origin firmware/RTL source; the rewrite plan R0 to R7).

## 3. What exists in `fw2/` (stage status)
| Stage | State |
|---|---|
| S0 baselines, ownership map, decisions | DONE |
| Pure cores (`fw2/cores/`): `timeline_core.h` (9 mutants), `governor_core.h` (3 mutants + scheduling simulation), `counters_core.h` (6 mutants), `monoclock_core.h` (2 mutants); `tools/check_deps.py` enforces the module dependency rule | DONE, in `make test-host` |
| S2 platform skeleton (`platform/` start.S, link.ld, mmio.h, caps, interlock, markers; `gfx/`; `diagnostics/` counters table + Info page) | DONE, **hardware-confirmed** on `TAU DEV 130` (B-756): boots first time, all nine capability bits 1, worst loop 0.40 ms |
| S3 `aout` + `transport/tone.c` (a continuous 1 kHz tone in 12 s segments through the real ring, timeline, governor, accounting) | DONE, **hardware-confirmed** on `TAU DEV 131` (B-757/B-758/B-759): across two segment boundaries the phase residual is max 0.14 degrees (a one-sample slip is 8.16), level flat to 0.02 dB; device counters `SWAPS 4`, `ACCT ERR 0`, `UNDER RING 0`, `DRY 0`. Host-proven first against a ring/FIFO model (`sim/test_p2_aout.py`, 7 mutants) |
| Report page (START draws every counter as a pixel grid; `decode_tau_suite.py --grid shot.png --table` reads one screenshot back into every row) | BUILT, host-tested through the codec and decoder (`sim/test_p2_report.py`), **NOT yet run on a Pocket** (the SDRAM-mailbox pixel writes and the screenshot decode are the checks) |
| S1 probes (`0x008A` slot-update probe core, optional CAPS register, free-running underrun counter) | NOT started |
| S4 stream + queue, S5 settings, S6 MP3 transport, S7 views/meters, S8 FLAC, S9 retire | NOT started |

**On the card (`/Volumes/Pock`):** `TAU DEV 133` (Player 2, report page, uptime fix), `TAU DEV 128` (OLD player, ring + gapless diagnostics, kept for the open boundary investigation), `TAU`, `TAU Preview`, `TAU Preview Diagnostics`, owner cores. Test builds are numbered `TAU DEV NN` (numbers keep increasing; remove the superseded one on each install). Package/install commands: `python3 tools/package_dev_build.py --number NN --variant player2 --rbf work/diagnostics/pcmring-b745/ap_core_s1.rbf --rbf-sha256 c7423a54d55cbbd89181fa27a12c9fd6804e6630be552543eb4ec3b66b50249e --build-flags PLAYER2=1` then `python3 tools/install_dev_core.py work/diagnostics/tau-dev-NN/pocket --card /Volumes/Pock --remove "alfatreze.TAU DEV <previous>" --backup-dir "$PWD/work/card-backups/<stamp>" --yes` (dry run first; the owner approves card writes; the bitstream is the fitted `pcmring-b745` seed 1, kept untracked in `work/diagnostics/pcmring-b745/`, also in `../tau-alpha-boost/work/...`). Build only the firmware: `bash fw2/build.sh`.

## 4. The old player: what was found and what is still open (branch `main`, `../tau-alpha-boost`)
* **Ring + gapless (DEV 123 to 128):** the FLAC-through-the-ring cost bug (cold hooks in the hot per-pair put, about 300 cycles a pair) is fixed with a hot inline fast path; Info/report fixes (full values, 16 KB report record, `SR_T_TRACKINFO` tag 28 for the playing and previous track, `UND CTX`, `GP SWAP`, `GP AUDIT`, Info labels moved to the cold image); the old-track clock bug (counted one frame per UI tick) and its wrap on Stop fixed (DEV 128).
* **OPEN, cause unknown:** at a natural gapless boundary the tone test (`test music/gapless_tone/`, MP3 pair then FLAC pair) shows **MP3: a 6 ms fade then 41 ms of silence; FLAC: a 199 degree phase jump = about 2,053 pairs lost (the size of A's last partial FLAC frame)**. The host decode of the same files is exact. The instrument is the `GP AUDIT` Info row (pairs put vs expected per track) in DEV 127/128; **its screenshots have not been read yet** (the owner's shots after DEV 127/128 were not Info shots). Player 2 does not need the answer, but a one-line fix could ship in the old player first.
* Other open old-player items: seek is unreliable (ROADMAP row 20, to be rebuilt in Player 2 S6), HW GAIN and track-change clicks (recordings pending), issue 025 (`tau_details.pack` crashes on ReplayGain peak 2.0+), the embedded-JPEG fallback removal once Omega writes sidecars.

## 5. Findings worth keeping (also in the audit)
* A hard FLAC (decode barely faster than real time) underruns without the governor and survives with it **but leaves the UI about 3.5% of the time** (simulation, spec 9.2): the FLAC decoder's speed is the lever for UI on hard files.
* After a flush or at start the ring is empty so the governor passes through PANIC: the real transport needs a separate fill phase.
* **`RING_CTRL` read layout is `{present(31), hold(3), busy(2), empty(1), enable(0)}`**; hold is clear at reset (B-756 first said the opposite; corrected in B-759).
* **The 32-bit cycle counter wraps every 64.4 s at 66.667 MHz**: `(cycles()-start)/CLK_HZ` is wrong after the first wrap (UPTIME showed 12 after 76 s); use `monoclock_core.h`.
* The dependency checker caught a layering mistake of my own on its first run; keep `tools/check_deps.py` in the loop.

## 6. Next steps (in order)
1. **Read the DEV 133 report**: with the card mounted, find the newest screenshots in `/Volumes/Pock/Memories/Screenshots`, run `python3 tools/decode_tau_suite.py --grid <shot.png> --table`. If the grid does not decode, debug the mailbox pixel writes (`fw2/diagnostics/report_page.c`, a 30 ms settle before the writes, buffer 0 only). Confirm `UPTIME` passes 64 s.
2. Optionally exercise X (flush), B (hold) and Y (44.1/48 kHz) with one START-report shot each.
3. **S1** (probes; the CAPS/underrun-counter fit can run on the VM in parallel) then **S4** (stream + queue: one target request in flight with generations, read-ahead into the 24 KB compressed ring, library queue; host-tested against a card-timing replay including the 1,074 ms slot-settle outlier). S6 brings MP3 and the real gapless-with-accounting test; S8 the new FLAC decoder (last, gated on CPU idle).
4. Read the old player's `GP AUDIT` shots (DEV 128) if the owner takes them.
5. Housekeeping: nothing is pushed (about 40 local commits across `main` and `player2`); merge `player2` into `main` only when the owner asks; push only when asked.

## 7. Traps
* Work in the worktree, not the primary checkout; run `git branch --show-current` before every commit (another session shares the repo).
* `fw/build.sh` flagged builds and unflagged checks can clobber each other: package with `--build-flags` (the packager builds right before packaging). Player 2 builds with `fw2/build.sh` and needs no flags (any KEY=VAL is accepted to trigger the build).
* `make test-host` passes in the worktree; it also builds `fw2` and checks the baseline.
* The Pocket screenshot stops/pauses the old player; Player 2's tone keeps running.
* Audio test files for Player 2 are generated on the device (no media needed); the old tone album is `test music/gapless_tone/Gapless Tone Test` (already synced to TAU's media on the card).
