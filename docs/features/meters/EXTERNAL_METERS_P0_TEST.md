# External meters, phase P0: the Pocket test (pack-swap probe)

What it answers (plan section 3.3 and 5): can a different pack be loaded into ONE window at run time, with the music playing, without a dropout, a hang, wrong code (a stale instruction cache) or a visible glitch, and what does the load cost?

**Build:** `TAU DEV 920` (version text `0.7.1-dev.920+<commit>`), Player 2, Diagnostic Build (`DIAG=1`), `PACKS=1`, bitstream `wdog-w3` seed 1 (`4D50331B`, the one v0.7.0 and v0.7.1 ship). Its `tau-packs.bin` holds the seven normal packs and seven copies linked for the free pack slot 7 (ids `0x100 + meter`). Package: `python3 tools/package_dev_build.py --number 920 --variant player2 --build-flags PACKS=1 --pack-swap-probe --rbf <wdog-w3 ap_core_s1.rbf> --rbf-sha256 <hash>`. Install only with `tools/install_dev_core.py` (dry run first, owner's approval, `--card /Volumes/Pock`).

**What it does:** on every new track (right after the preset probe) it loads the next window pack from the bundle into slot 7 (read in 1 KB pieces from data slot 9), runs `fence.i`, runs one tick of it against stub host calls under the watchdog, compares what it did with what the same pack does from its own slot, then makes the live meter activate again (one forced repaint). It writes nothing to the card.

**Run (about 10 minutes):**
1. Boot `TAU DEV 920`, start a long playlist (a few albums), choose any meter, let it play.
2. Skip tracks with Right (or let short tracks end): about **30 new tracks** (every meter's window pack is tried 4 times or more). Keep the screen on Now Playing for a few tracks, then on the full-screen meter for a few.
3. Listen for dropouts or clicks at each track change and watch the meter for a visible glitch (one repaint at a track change is expected).
4. Open the Report (Select + Start) or the Info page and photograph it (pause first: a screenshot while playing costs one dropout).

**Read the `PS ...` rows:**
| Row | Good | What a bad value means |
|---|---|---|
| `PS FOUND` | 7 | the bundle has no window packs (wrong package) |
| `PS RUNS` | the number of tracks played | the probe did not run |
| `PS OK` | = `PS RUNS` (minus refused ones) | |
| `PS BAD` | **0** | a swapped-in pack did something different from itself in its own slot: **stale instruction cache** (the flush does not work) or wrong code |
| `PS HANG` | **0** | the watchdog cut a probe tick |
| `PS ERR`, `PS INST ERR` | 0, 0 | 1 no window packs, 2 the loader refused a pack (the code in `PS INST ERR`: 22 ABI, 24 origin, 25 sizes, 26 CRC), 3 no meter row |
| `PS INSTALL`, `PS INST MAX` | the load latency in cycles (66.667 MHz counter): the figure that decides whether loading at selection is acceptable (under about 20 ms = 1,300,000 cycles feels instant) | |
| `PS FLUSH` | a few cycles to a few hundred | |
| `PS RUN`, `PS RUN MAX` | one tick against stubs (cost of the pack's own code) | |
| `PS FILL MIN` | the audio ring fill when a probe started (48,672 pairs = 1.10 s is full) | |
| `PS DRY` | **0** | the audio ring ran dry during a probe: the load competes with the music stream |

**Decision rule:** the lazy-loading design goes on only if `PS BAD`, `PS HANG` and `PS DRY` stay 0 over 30 swaps and `PS INST MAX` is acceptable. A non-zero `PS BAD` with `fence.i` means the flush is not effective: the fallback is two alternating windows or a code sweep (plan 3.3) and the spike is repeated. A non-zero `PS DRY` means the load has to be scheduled around the music reads (or the pack read in smaller pieces).

**Host side (done, branch `external-meters`):** `sim/test_p2_pack_swap_probe.py` (the real loader and the real packs against a stubbed Pocket: scan, round robin, byte-for-byte install, the oracle, stale code caught on the first swap and later ones, a watchdog cut, a damaged pack, no meter row), `sim/test_p2_diag_split.py` (the probe is absent from the normal build), the shipped meter pack tests.
