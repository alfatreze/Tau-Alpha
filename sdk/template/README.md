# Tau Alpha meter template

A minimal, working meter for the Tau music player on the Analogue Pocket: sixteen bars in the theme's accent colour, two parameters (gain, bar gap), three presets, a list preview, a test and a CI file. Copy it, change the id, change the drawing.

```
git clone https://github.com/alfatreze/tau-alpha-meter-sdk      # once, next to this folder
tau-meter id new                      # paste the result into meter.json "id"
make                                  # builds dist/<id>.tmeter
make test                             # the conformance suite + this meter's own test + preview.png
```
(`make` finds the SDK in `../tau-alpha-meter-sdk`, or `$TAU_METER_SDK`, or `tau-meter` on PATH. You need the LLVM compiler: `brew install llvm lld` or `sudo apt install clang lld`; `tau-meter doctor` says what is missing.)

| File | What |
|---|---|
| `meter.json` | the manifest: id, name, version, author, licence, ABI, parameters, limits ([MANIFEST.md](../tau-alpha-meter-sdk/docs/MANIFEST.md)) |
| `src/meter.c` | the meter: only `tau_meter.h` (the host table); `TAU_METER(draw)` is the entry point |
| `thumb.json` | the 56 x 32 preview the METERS list shows (`make thumb` regenerates it from your meter) |
| `presets.json` | factory presets |
| `test/test_meter.py` | this meter's own tests, driving the SDK's host runner |
| `.github/workflows/build.yml` | CI, written but manual-only until you add triggers |
| `LICENSE` | MIT; put your own name in it |

Start with the SDK's `docs/GETTING_STARTED.md`. A meter runs with full access to the player: read `docs/TRUST.md` before you share one. MIT licence.
