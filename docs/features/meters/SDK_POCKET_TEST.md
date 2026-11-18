# SDK meter on a Pocket: the prepared test (not run)

**What this proves when it passes:** a meter built by the SDK's clang recipe (not the project's GCC) from the template loads, draws and configures on a real Pocket, next to the seven shipped meters. The host side is proven (`sim/test_sdk_toolchain.py`: identical draw calls and pictures for the seven shipped meters built both ways); the Pocket has not seen an SDK-built pack. **Status: prepared, NOT run. Nothing has been copied to a card.**

**Files** (made by `python3 tools/export_sdk.py`, in `work/sdk-export/pocket-test/`): `tau-meters.bin` (eight rows: the seven shipped meters in the order Winamp Bars, Scope, VU Master, Chladni, Layered Wave, Waterfall Grid, Sound Map, then **SDK TEST BARS**, id `5eed0c0de0c0de5eed0c0de0c0de0001`, the template's bars) and `SHA256SUMS.txt`. The seven in it are **built by the SDK's clang recipe**, so the whole bin tests the recipe.
**Firmware:** any DEV core with `PACKS=1` that reads a user bin (DEV 225 or newer); the Diagnostic Build shows more rows. **A card write needs the owner's approval** and follows `docs/CARD_INSTALL_PROCEDURE.md`; the bin goes to `Assets/tau/common/tau-meters.bin` (the folder is shared by every Tau core) and is removed afterwards (the core's own seven are used again).

| Step | What you do | What you should see |
|---|---|---|
| 1 | Start the core with the bin in place. About > ON THE CARD > METERS | `8 METERS` (no `BIN REFUSED`); Report: `PACK FILE 0`, `PACKS SEEN 8`, `PACK SRC 1`, `PACKS STRUCK 0`, `PACK ERR 0` |
| 2 | X through all eight, slowly, then quickly | each draws and moves; **SDK TEST BARS** is sixteen orange bars in its box; no freeze, no stray picture |
| 3 | METER list | eight rows with previews; SDK TEST BARS shows bars |
| 4 | Configure SDK TEST BARS: Gain, Bar gap, presets NORMAL / TIGHT / LOUD | the bars change; the three presets are listed |
| 5 | Pick SDK TEST BARS, wait 10 s, Quit, restart | back on SDK TEST BARS |
| 6 | Pick Layered Wave and Chladni (the biggest, built by clang) for a minute each, loud music | both draw as they did with the GCC build; `PACKS STRUCK 0`; Diagnostic Build: `METER MAX` for each far under its limit (Layered Wave 15 M) |
Bad signs: `PACK ERR` other than 0 (look it up in `sdk/docs/TROUBLESHOOTING.md`), `PACKS STRUCK` above 0, a meter that differs visibly from the shipped GCC one, a hang. Layered Wave's stack is 3.3 KiB with clang against 3.0 KiB with GCC: a reset or a stray picture on Layered Wave is the thing to report first.
**When done:** tell the claude session; delete `Assets/tau/common/tau-meters.bin` (with approval). Record the result in `docs/AUDIT_TRAIL.md` as HARDWARE evidence.
