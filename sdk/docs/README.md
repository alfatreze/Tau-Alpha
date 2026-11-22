# Tau Alpha meter SDK: documents

A **meter** is the picture that dances to the music on the Analogue Pocket's Tau player. This SDK lets anyone write one in C, build it into a `.tmeter` package, test it on a computer against every rule the player enforces, and share it.

Read in this order the first time, **[GETTING_STARTED.md](GETTING_STARTED.md)** first (zero to a meter in one page).

| Document | What it answers |
|---|---|
| [GETTING_STARTED.md](GETTING_STARTED.md) | install, make a meter, build, test, see it |
| [TOOLCHAIN.md](TOOLCHAIN.md) | the pinned compiler (LLVM), install steps for macOS, Linux, Windows, what is not supported |
| [METER_MODEL.md](METER_MODEL.md) | what a meter is, when it loads, its memory, repaint and degrade |
| [LIMITS.md](LIMITS.md) | what a meter can know and cannot (no waveform today): read before designing |
| [SNIPPETS.md](SNIPPETS.md) | tested helpers: clamp, integer sine, RGB565 mix, a line from rectangles |
| [HOST_TABLE.md](HOST_TABLE.md) | every host-table call and every input field (generated from the header) |
| [MANIFEST.md](MANIFEST.md) | `meter.json`, the package, its rules and limits |
| [PARAMETERS.md](PARAMETERS.md) | parameters, descriptors, presets (`.tpi` / `.tpr`), the preview image |
| [IDS_AND_VERSIONS.md](IDS_AND_VERSIONS.md) | ids (`tau-meter id new`), versions, ABI, `params_rev` |
| [TESTING.md](TESTING.md) | `tau-meter test`: every check, its id, what to do when it fails |
| [PERFORMANCE.md](PERFORMANCE.md) | budgets, how the suite estimates, how to measure on a Pocket |
| [ON_A_POCKET.md](ON_A_POCKET.md) | compose `tau-meters.bin`, copy it, what the player shows |
| [SHARING.md](SHARING.md) | publishing: any URL, a GitHub release layout |
| [TRUST.md](TRUST.md) | what "a meter runs with full access" means, in plain words |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | build errors and the player's refusal codes, keyed by number |
| [FAQ.md](FAQ.md) | short answers |
| [COLD_START_REVIEW.md](COLD_START_REVIEW.md) | the review that made these documents work for a stranger |

What exists today and what does not: the SDK, the template, the test suite and the collection of the seven shipped meters exist. **Tau Omega** (the companion app) is the planned composer of the user bin; its meter library is Omega's own work and is not described here as done. A **verified catalogue** with a signed index and signing of single meters are future work (see [SHARING.md](SHARING.md)).
