# Testing with `tau-meter test`

```
tau-meter test dist/<id>.tmeter [--preview preview.png] [--quick] [--previous OLD.tmeter] [--json]
```
Exit code **0**: nothing failed (warnings are allowed). **1**: a check failed; the package would be refused by the installer or would misbehave in the player. **2**: the input cannot be tested at all (no such file, no toolchain). Every line has a stable **check id**; failures say what is wrong and what to do. `--quick` runs three scenarios instead of six and skips the parameter sweep (for a fast loop); run the full suite before you publish. `--json` prints the same results for a CI job. `--profile shipped` is for the project's own meters, which read a few hardware registers directly (see below); an ordinary meter never needs it.

## What it does
It reads the package without trusting it, checks it against the rules the player enforces, scans its code, and **runs it** on a small RISC-V computer that has the player's memory map and host table (`rv32run`, built once with your clang and cached). It replays the draw calls on a model of the draw engine to check where they land and to make the preview picture.

## The checks
| Id | Fails when | What to do |
|---|---|---|
| `PKG-ZIP` | the file is not a readable zip | rebuild with `tau-meter build` |
| `PKG-SIZE` | over 4 MiB compressed or declared | drop big files from `source/` |
| `PKG-PATH` | a path escapes the package (`..`, absolute, backslash) or appears twice | never hand-edit a package |
| `PKG-FILES` | a file is missing (`meter.json`, `meter.bin`, `README.md`, `LICENSE`), not allowed, or not pinned | rebuild |
| `PKG-PIN` | a file does not match its SHA-256 in `meter.json`: the package was changed after it was built | rebuild; never edit a `.tmeter` |
| `PKG-SIG` | `signature` is not null (signatures are reserved) | leave it null |
| `PKG-PACK` | `meter.bin` is not a valid pack (header, CRC, window fit with state, origin, ABI vs manifest, preview) | rebuild; see the detail lines |
| `PKG-MANIFEST` | a `meter.json` field breaks a rule (id, name, version, abi, tau_min, params_rev, author, licence) | [MANIFEST.md](MANIFEST.md) |
| `PKG-PLACEHOLDER` | (warning) `LICENSE`, `README.md` or `author` still says "Your Name" (the template's placeholder) | put your own text in |
| `PKG` | pass line: all the above are fine | |
| `LOAD-E21` to `LOAD-E29` | the player's loader would refuse the pack with that code (21 header, 22 ABI, 24 origin, 25 size or state, 26 CRC, 27 entry, 28 scratch origin, 29 scratch capacity) | [TROUBLESHOOTING.md](TROUBLESHOOTING.md) |
| `LOAD`, `LOAD-TAU` | pass lines; `LOAD-TAU` warns when `tau_min` is older than 0.7 | |
| `DESC-PARSE` | the descriptor cannot be read | rebuild |
| `DESC-PARAMS` | a parameter has min above max, a default out of range, a bad choice count or `when`, colliding key hashes | [PARAMETERS.md](PARAMETERS.md) |
| `DESC-LIMITS` | (warning) `cmds` or `max_cycles` is 0: the player cannot protect itself | set both |
| `DESC-PREVIEW` | the list preview is not 56 x 32 pixels in 1 KiB | `tau-meter thumb` |
| `PRESET` | a factory preset file is wrong: ids, names, CRC, a key the meter lacks, a value out of range | fix `presets.json` |
| `PARAMS-REV` | (with `--previous`) the parameters changed but `params_rev` did not; warns for the reverse | raise `params_rev` |
| `STATIC-INSN` | the reachable code has a system call, a CSR or `fence.i` instruction, or one a RV32IM core lacks | a meter may call only the host table |
| `STATIC-ADDR` | the code forms an absolute address that is not its own memory, or a hardware register | use the host table |
| `STATIC-HW`, `STATIC-FLOW` | information: first-party profile; instructions reached and indirect jumps left to the run | |
| `RUN-HW` | the meter read or wrote a hardware register | the host table has no registers: use `in->` fields |
| `RUN-INPUT-WRITE` | it wrote into the inputs (`in`, `spec`, `role`, `params`) | they are read-only: copy first |
| `RUN-MEM-WRITE`, `RUN-MEM-READ` | it touched memory that is not its own (outside its window, its own `.data` / `.bss`, the stack, the inputs) | a wild pointer, a bad index |
| `RUN-JUMP` | it jumped or called outside its own code and the host table | a bad function pointer |
| `RUN-INSN` | it executed a system call, CSR, compressed or illegal instruction | build with the SDK's flags |
| `RUN-ABI-CALL` | it called a host-table entry newer than the ABI it declares | declare `"abi": "1.2"` for `scroll_left` |
| `RUN-ABI-FIELDS` | it read `mtr_in_t` fields newer than the ABI it declares | declare `"abi": "1.1"` |
| `RUN-PSTATE-WORD` | (warning) byte or halfword access to `TAU_PSTATE` memory | use `uint32_t` and word loops |
| `RUN-HANG` | a tick did not return: the watchdog would cut it off (4 x `max_cycles`, 2 M to 20 M cycles) and strike the meter out at once | find the endless loop |
| `RUN-OVERRUN` | a drawn tick (not a forced repaint) used more estimated cycles than `max_cycles` | cheaper tick or a higher, honest limit |
| `RUN-STRIKES` | the player's strike model would switch the meter off in the 'music' scenario | as above |
| `RUN-CMDS` | a drawn tick issued more draw commands than `cmds` | fewer, bigger commands. Forced repaints are not judged by the player or the check; they are printed so you can see them: the `cmds` ceiling is for an ordinary picture, and a forced picture may use more |
| `RUN-BOX` | a draw call landed outside the meter's rectangle (the player's three boxes) | clip to `in->x, y, w, h` |
| `RUN-BOX-SMALL` | (warning) the same, in a 120 x 40 or 16 x 8 box | size from `in->w`, `in->h` |
| `RUN-STACK` | stack use above 3,584 bytes (warning above 2,048) | smaller locals |
| `RUN-COUNT` | (warning) the entry's return value is not the number of draw commands | use `TAU_METER()` |
| `RUN-HOSTONLY` | pass line: every run stayed inside the host table and the meter's own memory | |
| `RUN-DETERMINISM` | the same inputs gave a different call trace the second time (the second run starts with the stack full of garbage) | an uninitialised variable, or a read of memory that is not yours |
| `RUN-PARAMS` | a parameter at its minimum or maximum, or a factory preset, breaks the meter | check your ranges |
| `PREVIEW` | the preview image was made (or could not be) | |
| `INPUT`, `RUN` | the file is missing (exit 2) / a run step was skipped or the runner failed | |

## Driving the runner from Python (your own tests)
```python
sys.path.insert(0, SDK); from tau_meter import runner; from conformance import suite
with runner.Runner("meter.bin", params=[100, 1], limit=400000, roles=suite.ROLES_DARK) as r:     # meter.bin is inside the .tmeter zip; params in meter.json order
    t = r.tick(frame, dt_ms, (x, y, w, h), bg, force, paused, peak_l, peak_r, degrade, spec16bytes)
```
`t.status` is `"ok"`, `"cut"` (the watchdog would cut it) or `"fault"` (`t.fault["msg"]`); `t.calls` is the list of host-table calls in order, each a tuple of strings: `('rect', x, y, w, h, colour)`, `('bar', x, y, w, h, lit, fg, bg)`, `('text', x, y, hex-of-the-characters-plus-'.', fg, bg)`, `('scroll_left', ...)` and so on (arguments as in [HOST_TABLE.md](HOST_TABLE.md); pointers are left out); `('rect_stash', x, y, w, h, colour)`, `('blit', sx, sy, dx, dy, w, h)`, `('scroll_down' / 'scroll_left', x, y, w, h, d)` (every call lists its arguments in the order of its signature in HOST_TABLE.md, all as decimal strings); `t.cmds`, `t.cycles` (estimate), `t.instr`, `t.stack`. State persists from tick to tick like in the player. The suite uses the reference theme `suite.ROLES_DARK` (accent `0xFD20`, `MTR_ROLE_BASE` `0x0841`) and `suite.BG = 0x0841` as `in->bg`; pass `roles=suite.ROLES_DARK` or the runner uses arbitrary colours. `dt_ms` is 26 in the suite. `suite.BOXES` holds the boxes. `suite.descriptor(open('meter.bin','rb').read())` returns the descriptor: `d['params']` is a list of dicts with `label`, `type`, `min`, `max`, `step`, `vals`, `def` (the default) and `key` (the key hash); `d['cmds']`, `d['max_cycles']`, `d['name']`. To see pixels, replay `t.calls` on `conformance.canvas.Canvas` (`cv.apply(call)`, `cv.crop_rgb(x, y, w, h)`).

## Scenarios
`music` (a forced first picture, loud music, silence, a forced repaint), `pause`, `move` (Now Playing box, Configure box, fullscreen box, back), `loud` (every band at 255), `degrade` (levels 1 and 2), `tiny` (the robustness boxes). `suite.ROLES_DARK` is a list indexed by the `MTR_ROLE_*` numbers (`ROLES_DARK[MTR_ROLE_ACCENT]`, with `MTR_ROLE_BG_TOP` 0, `BG_BOTTOM` 1, `SURFACE` 2, `SURFACE_TRACK` 3, `TEXT_PRIMARY` 4, `TEXT_SECONDARY` 5, `ACCENT` 6, ... as in [HOST_TABLE.md](HOST_TABLE.md)). A tick's `text` calls are `('text', x, y, '504155534544.', fg, bg)`: the characters in hex followed by a `.` terminator. **Text size:** the suite models a character as 7 pixels wide and 16 high; the player's font may differ (8 x 16 or the theme font), so erase text with a margin and keep it away from the box edge. **A stale list preview is not detected:** the suite only checks that `thumb.json` is a valid 56 x 32 picture; run `tau-meter thumb` after you change the drawing. The preview image shows the `music` scenario only, never the paused path: test that in `test/test_meter.py`.

The spectrum is a fixed integer pattern, so a run is repeatable. Statistics inputs (`rms`, `corr`, ...) are set when you declare ABI 1.1 or later.

## The preview image
`--preview` writes a 2 x 2 grid of the Now Playing box (368 x 122, shown at 2x) after pictures 1, 10, 20 and 40 of the `music` scenario, in that order: picture 1 and 10 are loud music, picture 20 is in the four silent pictures (so a held peak or a fading trail shows), picture 40 is music again. Text is drawn as blocks.

## What the checks are not
- **They do not know what your meter is meant to look like.** A pass says the meter obeys the rules and does not crash or overrun; whether it holds a peak for a second or draws the right colour is yours to test (`test/test_meter.py`, above).
- **Cycles are an estimate** ([PERFORMANCE.md](PERFORMANCE.md)); a meter that passes can still be slow on the Pocket.
- **The static check is a recursive descent** from the entry point; code reachable only through a jump through a register is covered by the run, on the paths the scenarios take.
- A passing meter **follows the rules**; it is not proven **safe** ([TRUST.md](TRUST.md)).
- The preview draws text as blocks (the lab's model has no font).

## The first-party profile
The seven shipped meters read a few registers directly (the Scope's capture block, the cycle counter, the PSRAM status words). `--profile shipped` lets exactly those registers through, modelled as the project's own host tests model them (the cycle counter reads 0). A new meter should use none.

## The negative fixtures and the mutants
The suite is tested by `sim/test_sdk_conformance.py` in the Tau Alpha repository: 30 broken meters (`conformance/fixtures/`), each with the exact set of failing ids it must produce (`fixtures.json`): an out-of-bounds write, a forbidden call, a hardware register, a wild jump, a hang, an overrun, too many commands, an oversized state, an oversized scratch, a bad ABI, a bad parameter, a bad hash, a tampered file, a path escape, an extra file, an oversized package, a bad id, a bad origin, a wrong CRC, an uninitialised variable, a stack hog, a byte access to PSRAM state; the template and the seven shipped meters are the positive fixtures; and 19 mutations of the checks themselves (a bound relaxed, a limit made unreachable, the hang verdict removed, the runner's write protection removed, the pin check inverted) must each change a fixture's result.
