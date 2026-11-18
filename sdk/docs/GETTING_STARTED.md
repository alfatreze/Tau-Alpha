# Getting started: from nothing to a meter in one page

You need a computer with Python 3.9 or newer (tested on 3.11) and the LLVM compiler ([TOOLCHAIN.md](TOOLCHAIN.md): one install command on macOS and Linux). No Pocket is needed until the last step.

```
git clone https://github.com/alfatreze/tau-alpha-meter-sdk      # the SDK (tau-meter, the header, the test suite) and template/, a working meter
cp -r tau-alpha-meter-sdk/template my-meter                      # your meter starts as a copy of the template, next to the SDK
cd my-meter
python3 ../tau-alpha-meter-sdk/tau-meter doctor                  # is the compiler found? (prints what to install if not)
```
(Put `tau-alpha-meter-sdk` on your PATH, or use its full path; below it is written `tau-meter`.)

**1. Give your meter an identity.** Every meter has a random 128-bit id, made once and never changed:
```
tau-meter id new                  # prints 32 hex digits
```
Edit `meter.json`: paste the id into `"id"`, set `"name"` (1 to 23 printable characters), `"author"` (your name), `"licence"` (a name such as `"MIT"`; the text in `LICENSE` is what binds, and it travels in the package), `"homepage"` (a link to your page, or `""` if you have none; it is shown, not followed). Edit `README.md` (describe your meter) and `LICENSE`: replace `Your Name` and keep or change the licence text; do not copy code under a licence you cannot meet. Then edit `params` and `presets.json` to your meter's settings ([PARAMETERS.md](PARAMETERS.md)); the template has two parameters and three presets.

**2. Build and test.**
```
tau-meter build                   # dist/<id>.tmeter
tau-meter test dist/*.tmeter --preview preview.png
```
The test prints one line per check (PASS / FAIL / warn) in plain words, and the exit code is 0 only when nothing failed. `preview.png` shows four frames of your meter in a simulated player. Open it.

**3. Change the drawing.** `src/meter.c` is the whole meter: one function `draw(const mtr_in_t *in)` called about 38 times a second, and the line `TAU_METER(draw)`. Draw only inside the rectangle `in->x, in->y, in->w, in->h`, with the calls in [HOST_TABLE.md](HOST_TABLE.md) (`mtr_rect`, `mtr_bar`, `mtr_text`, ...). The 16 numbers `in->spec[0..15]` are the spectrum (0 to 255); `mtr_param(i)` reads your parameters; `mtr_role(MTR_ROLE_ACCENT)` is the theme's colour. Rebuild and retest after every change; the test runs in a few seconds.

**4. Make the list preview** (after the drawing is done; the template ships a thumbnail of its own bars). The METERS list shows a 56 x 32 picture before your meter loads: `tau-meter thumb [DIR]` (default: the current folder) renders a frame of your meter and writes `thumb.json`, with `palette_roles` filled in for the colours that are theme colours ([PARAMETERS.md](PARAMETERS.md)). Rebuild afterwards.

**4b. Test what your meter is for.** The suite checks the rules, not your design. `test/test_meter.py` is an example of testing behaviour with the host runner: replace its checks with yours (for example "silence draws no lit bar", "the marker falls after a second"; [TESTING.md](TESTING.md), "Driving the runner").

**5. Declare your limits.** `meter.json` has `"p2": {"cmds": 24, "max_cycles": 400000}`: the most draw commands and CPU cycles one picture may use. `tau-meter test` prints what your meter used; set the limits a little above ([PERFORMANCE.md](PERFORMANCE.md)). A meter over its limit is slowed and, if it keeps at it, switched off by the player.

**6. Try it on a Pocket** (the one step that needs hardware): [ON_A_POCKET.md](ON_A_POCKET.md). In short: `tau-meter compose tau-meters.bin dist/*.tmeter ../tau-alpha-meters/dist/*.tmeter`, copy it to the card as `Assets/tau/common/tau-meters.bin`, start Tau, pick your meter from the METER list. **A meter built with this SDK has not yet been run on a Pocket by the project:** the host tests are strong but they are not the device; the document says what to look at.

**7. Share it** ([SHARING.md](SHARING.md)). Read [TRUST.md](TRUST.md) first: a meter runs with full access to the player.

When something fails: [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
