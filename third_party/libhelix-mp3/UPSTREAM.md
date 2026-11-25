# libhelix-mp3: pristine upstream plus one patch

Everything in this directory except the three Tau files named below is **byte-identical to upstream** (checked file by file, SHA-256, by `sim/test_helix_pristine.py`).

| | |
|---|---|
| Upstream | <https://github.com/ultraembedded/libhelix-mp3> (a RISC-V port of the RealNetworks Helix fixed-point MP3 decoder) |
| Pinned commit | `0a0e0673f82bc6804e5a3ddb15fb6efdcde747cd` (upstream HEAD on 2026-11-25; pushed 2024-02-23) |
| Licence | RealNetworks RPSL 1.0 / RCSL 1.0 per each file header; "Copyright (c) 1995-2004 RealNetworks, Inc." (`LICENSE.txt`, `RPSL.txt`, `RCSL.txt` at this level and in `docs/`, `pub/`, `real/`, `testwrap/`) |
| Files | 70 (every file of the pinned commit); their SHA-256 are in `UPSTREAM.sha256` |

The `real/projects/**` project files are stored with their upstream CRLF line endings. (Before 2026-11-25 they were stored with LF; that was the only difference to upstream besides the five modified sources below.)

## Tau's changes (not in the pristine files)

| File | What | Why |
|---|---|---|
| `tau.patch` | one unified diff (`patch -p1`, applied inside this directory) against the pristine files: `mp3dec.c`, `real/coder.h`, `real/dct32.c`, `real/imdct.c`, `real/subband.c` | decoder stage-cost profiling macros (`MP3_PROFILE`, B-345; `mp3dec.c`, `imdct.c`) and the `TAU_POLY_FW` / `TAU_POLY_LOG` hooks that route the polyphase window to the hardware unit (`dct32.c`, `subband.c`, `coder.h`) |
| `../libhelix-mp3-tau/pub/mp3_profile.h` | new Tau file (installed as `pub/mp3_profile.h` in the build tree) | the profiling macros the patch refers to |
| `UPSTREAM.md`, `UPSTREAM.sha256` | this note and the hash list | provenance |

## How it is built

Nothing compiles this directory directly. `tools/helix_tree.py` assembles `build/helix-tau/` (git-ignored) = the pristine files + `tau.patch` + the Tau file above, with the same layout (`mp3dec.c`, `real/`, `pub/`), and prints its path. `fw2/build.sh`, `fw/build.sh`, `sim/build_fw.sh`, the host tests that compile Helix and `tools/gen_mp3_poly_rom.py` all call it. The compiled result is unchanged: `fw2/build.sh` DIAG=0 and DIAG=1 give the same `tau.rom` SHA-256 as before the split (B-1168).

## Updating the pin

Replace the pristine files with the new upstream commit (byte for byte), regenerate `UPSTREAM.sha256`, rebase `tau.patch` (`patch -p1 --dry-run` shows what no longer applies), change the commit above, run `sim/test_helix_pristine.py`.
