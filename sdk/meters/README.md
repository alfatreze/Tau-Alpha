# tau-alpha-meters

The seven meters that ship with Tau, as SDK projects: Winamp Bars, Winamp Scope, VU Master, Chladni, Layered Wave, Waterfall Grid, Sound Map. Each folder builds with the SDK (`tau-meter build <folder>`) into `<id>.tmeter`; the export also publishes the built packages in `dist/` with `SHA256SUMS.txt` (the packages the SDK's clang recipe makes; the release of Tau ships the ones the project's own GCC makes: same ids, same descriptors, identical draw calls, see the SDK's `docs/TOOLCHAIN.md`).

| Meter | id |
|---|---|
| WINAMP BARS | `7dddfe63121e904843088c075cc28ce9` |
| WINAMP SCOPE | `143acb80cbcd1b08b84c2661eeab3414` |
| MASTER VU | `0d8c03d3a2770c1aac2dc6abbc222afd` |
| CHLADNI | `7471cc214987407c0b03917f27923179` |
| LAYERED WAVE | `5812803445b81ffae735527099ef6793` |
| WATERFALL | `17e607764b83b9372452eda124ae763f` |
| SOUND MAP | `0c82d9f07f73f25d90ca54c8d39173ce` |

**These are the player's own sources:** `src/fw2/ui/meter_pack_<key>.c` redirects the player's drawing calls to the host table and includes the meter; the player headers they use are copied under `src/` in the player's folder layout. They show how the real meters are written. **A new meter should start from the `template/` folder of `tau-alpha-meter-sdk` and `tau_meter.h`**, not from these.

`INDEX_DRAFT.md` is a design note for the future catalogue index; no catalogue exists. A meter runs with full access to the player (the SDK's `docs/TRUST.md`). MIT licence.
