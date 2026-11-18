# tau-alpha-meter-sdk

The SDK for writing **meters** for Tau, the music player of the Analogue Pocket: a public header (the host table), a build driver (clang + lld, pinned), a package tool, a **conformance suite** that checks a meter against every rule the player enforces and draws a preview, and the documents.

```
python3 tau-meter doctor            # is LLVM installed? (macOS: brew install llvm lld; Linux: apt install clang lld)
python3 tau-meter id new            # a new meter id
python3 tau-meter build  DIR        # DIR/dist/<id>.tmeter       (start from a copy of template/)
python3 tau-meter test   X.tmeter --preview preview.png
python3 tau-meter compose tau-meters.bin X.tmeter share/*.tmeter
python3 tau-meter selftest          # the suite against its own broken-meter fixtures
```
Start with `docs/GETTING_STARTED.md`; `docs/README.md` lists the rest. Layout: `include/tau_meter.h` (the ABI), `ld/` (the window linker script), `runner/` (the host runner `rv32run.c` and the freestanding runtime), `tau_meter/` (the Python package; `_vendor/` holds byte copies of the Tau Alpha project's package, bin and descriptor tools), `conformance/` (the suite and its fixtures), `share/` (the seven shipped meters as packages: Winamp Bars is the player's fallback and `compose` adds it for you), `docs/`.

A meter runs with full access to the player: read `docs/TRUST.md`. MIT licence.
