# Cymo audio engine: independent design review

Status: **review only, 2026-09-29 (B-424).** Reviews `docs/features/CYMO_AUDIO_ENGINE.md` as it stood at commit `a3d9683`, from two angles: firmware
architecture (module boundaries, real-time budget, failure behaviour, testability) and embedded audio DSP (sample-rate conversion, gain staging,
quantisation, filters, clocking). Nothing was built or changed except two factual corrections applied to the main document (listed in section 8).

Evidence labels as in the main document: **[HW]** measured on a Pocket, **[READ]** read from source, **[MODEL]** host model, **[EST]** estimate, **[OPEN]** unknown.
For this review I re-checked claims against source rather than against my own document. Every finding names the code or document it rests on.

## 1. Verdict

The direction is sound: one output stage after decode, a real resampler, RTL gain and dither, and pitch-preserving tempo are the right
things to build, and the plan's discipline (probe-gated, host model first, mutation tests, collision register) is what this project should keep.
But the plan is **too broad for its stated purpose, has one architectural flaw in its clocking, understates two features, and misses several
things an audiobook player needs.** Seven findings should change the plan before any work starts; they are marked **Change**.

| Severity | Count | Meaning |
|---|---|---|
| Change (design flaw or wrong claim) | 7 | Alter the plan before starting |
| Tighten (missing spec or risk) | 8 | Fix while writing the phase's spec |
| Note | 4 | Keep in view |

## 2. Findings that should change the plan

### C1. The 48 kHz tick must be pulled by the DAC's frame strobe, not free-run in `clk_sys` (audio architecture)

The main document says `cymo_resamp` "becomes the single owner of the 48 kHz tick", implying a divider or accumulator in `clk_sys`. That repeats the weakness
the plan itself identified in `eq_biquad`'s free-running tick, only more visibly. **[READ]** `sound_i2s.v` latches a sample at each LRCK from `clk_74a`; `clk_sys`
(66.667 MHz) and `clk_74a` are separate PLL domains (the skill: "NOT phase aligned; treat as asynchronous"). Any residual frequency difference between a
`clk_sys` 48 kHz tick and the real LRCK, even a few ppm, makes the output drop or repeat a sample every few seconds. That is a periodic click, and nothing in
the FIFO absorbs it because the I2S side reads whatever is newest.

**Recommendation:** make the resampler *pull-driven*. Cross the single-bit LRCK frame strobe from `clk_74a` into `clk_sys` (a one-bit synchroniser is legitimate
here, unlike a bus, KB-009) and use it as the output tick. Per output tick, advance the input phase by a fixed-point ratio `r = file_rate x speed / 48000`
(an exact rational computed by firmware) and consume input samples when the phase overflows. Consequences, all good:

- Output is locked to the actual DAC clock by construction: no drift, no periodic slip, and the EQ runs on the same strobe.
- The FIFO drain no longer depends on `CLK_HZ`. `pcm_fifo`'s `rate_inc = rate x 2^32 / CLK_HZ` and the `CLK60`/`CLK66` interlock (a silent mis-pace hazard the
  RTL comments already warn about) disappear for audio.
- `sample_tick` for the spectrum and level blocks becomes "an input sample was consumed", the same source-rate semantics they have now (collision K2 stays resolved).
- It generalises to a second sink: each sink supplies its own frame strobe (12.2 in the main document should say so).

### C2. Gapless is not "small after the buffer"; it is its own project (wrong claim)

The main document (section 5) says gapless "needs the deeper buffer first, then it is small". That is wrong. **[READ]**:

- Audio comes from **one data slot**, `MP3_SLOT_ID 2` (`player.c:5724`), reopened by name per track. The skill and the code comments both record that reading a different
  file on the streaming slot corrupts its fragment cache (`player.c:7311-7320`). Gapless needs the next file open while the current one plays, so it needs a **second declared
  audio slot** (`core.json`, packager, and `CROSS_PROJECT_INTERFACE.md`, hence Tau Omega).
- `load_track()` is **synchronous and blocking** (`AUDIO_FIRST_TRACK_LOAD_SPEC.md`): the decoder cannot feed the FIFO while it runs, so a buffer only helps for the part of the load
  that is shorter than the buffer.
- It needs decoder re-initialisation without a flush, encoder delay and padding trimming (the LAME tag is parsed only for its name today, `player.c:977`), and exact sample counts for FLAC.

A deeper buffer is useful on its own (it absorbs stalls), but it is not the gate for gapless. Re-rank gapless as a separate, large item and do not promise it from C4.

### C3. The plan serves audiobooks in its headline but omits what audiobooks need (product fit)

You told me the pitch and speed work is mainly for audiobooks. Measured against that, the plan has gaps that matter more than several planned RTL blocks:

| Audiobook need | State | Evidence |
|---|---|---|
| Resume where you stopped | **Removed.** The RESUME feature and its persist words were retired (`SW_RETIRED_RESUME`, `player.c:1133`); a resume point cannot survive a relaunch today. | [READ] |
| Speed remembered per book | Speed is deliberately not persisted (`player.c:838`). | [READ] |
| Book/chapter structure | Library is folders and files; no chapter marks (ID3 CHAP) | [READ] |
| Common format | Many audiobooks are **M4B (AAC)**. The format survey rated AAC "low priority for this audience", which is the opposite for this use. | `AUDIO_FORMAT_SUPPORT_RESEARCH.md` |
| Speech clarity | Presets are music-shaped; no speech/voice preset, no high-pass for rumble, no loudness levelling | [READ] `EQ_DESIGN.md` |

Persist has 11 free words, so per-book positions do not fit in it, and writing a nonvolatile slot has a history of hanging the Pocket and destroying libraries in this
project (B-216 discussion). A realistic design is one small "last book + position + speed" record (3-4 words), not a bookmark database. **Decide these before C7**, because
they change what "audiobook mode" is. This is a product decision, not a DSP one.

### C4. The deeper buffer is coupled to hard-coded depth constants

Changing `AW` from 11 to 13 is not a one-line RTL change. **[READ]**:

- `pcm_fifo.v:85` sets `PRIME = DEPTH >> 1`. At `AW=13` that is 93 ms of silence before each track starts, not 23 ms. `PRIME` must become an independent parameter (about 20 ms).
- `player.c:5773-5774` hard-code the depth for `METER_STOP`/`METER_GO` (`2048u / 6u`, `2048u / 3u`). The comment says so; at 8192 the meter yield thresholds are wrong by 4x, which would
  silently change when meters are throttled to protect audio.
- `FADE_SAMPLES` (2048) is sized to the FIFO; and everything that reads `pcm_level()` (`min_level`, the stress statistics) assumes the old scale.
- Any pre-FIFO processing (tempo, ReplayGain in firmware) takes effect a buffer-length late, up to 186 ms. Post-FIFO controls (EQ, volume) are immediate. The UI must not imply otherwise.

**Recommendation:** first make depth a single named constant shared by RTL and firmware (a status bit or a `CYMO_CAPS` field reporting depth), then change `AW`.

### C5. Limiter design is underspecified and a zero-lookahead limiter will not work

Section 6.2 proposes a "one-multiplier peak follower with fast attack and slow release" replacing the EQ's clamp. A follower with no look-ahead reacts after the peak has
already passed, so it clips the first samples of every transient, which is the case it exists for. Two workable options:

1. **Soft clipper** (a small waveshaping ROM in the output path): no state, no lookahead, gentle on the 3.6 dB worst-case overshoot the loudness-matched presets can produce. Recommended.
2. **True limiter with lookahead:** a 1-2 ms delay line (about 50-100 samples per channel, small enough for MLAB) so the gain is already reduced when the peak arrives.

Also: dither must be applied at the **true final word width**. If the F2 finding stands (the DAC stage passes `{sign, audio[15:1]}`), the real quantisation point is the 15-bit word, so the
dither amplitude and the rounding must be placed there, not at 16 bits. Order the work so F2 is resolved before the dither is specified.

### C6. Graphic EQ headroom and topology are not validated

Option B (ten gain-indexed bands) has three hazards the document does not cost:

1. **Runtime preamp.** The shipped presets carry a precomputed, loudness-matched preamp. User sliders do not, so the engine needs a preamp computed from the slider set
   (for example minus the largest positive band gain) or it clips on any boost. That is another ROM and a rule.
2. **State width.** The documented margin is about 18 dB above the worst observed cascade (`eq_biquad.v` header: 36-bit state, worst case 33 bits, measured with the five-band presets).
   Ten overlapping bands at +12 dB can exceed that. Re-run the `tools/eq_model.py` width sweep for the new topology **before** fixing widths (`CW`, `SW`, `AW`).
3. **Band interaction.** Fixed-Q peaking filters overlap, so the drawn slider curve is not the resulting response. Either the UI shows the computed response (the project already
   generates response curves, `gen_eq_coeffs.py --curves`) or the sliders are labelled as approximate.

### C7. The phase plan is too broad for the stated purpose; there is a much shorter path to the audiobook value

The main document lists C0 to C8 plus X0 to X4. For audiobooks, most of the value comes from features that need **no new bitstream at all**:

| Step | Needs RTL? | Why it is first |
|---|---|---|
| Real headroom metric (C0a) | no | everything else is judged by it |
| One `cymo_push()` (C1) | no | prerequisite for stretch and for any output-stage change |
| **Firmware WSOLA tempo + pause shortening** | **no** | the headline audiobook feature; ships on today's proven bitstream, which removes fit and timing risk entirely |
| Resume/last-position record and per-book speed | no | without it the feature is unusable across sessions |
| Resampler, **up-conversion only** (C2, reduced) | yes | 22.05/24 kHz speech is the worst case for nearest-neighbour imaging |
| Speech EQ preset | small RTL (ROM) | a new preset is a coefficient-ROM change and a fit |
| Deeper buffer, dither, gain ramps (C3/C4) | yes | quality and robustness, not audiobook-critical |
| Programmable EQ, gapless, Bluetooth | yes/large | later |

This ordering also reduces exposure to the project's scarcest resources (ALM headroom, hold slack, M10K) until the audiobook feature has proven itself.

## 3. Findings to tighten

### T1. Correlation method and arithmetic for the firmware stretch

The main document budgets multiply-accumulates. For a firmware search, prefer **sum of absolute differences (SAD/AMDF)** over cross-correlation: no multiplies (the core is `rv32im`, multiplier
latency is not stated anywhere I found **[OPEN]**), no overflow analysis, and it is robust to level. Use 8-bit decimated samples, 32-bit sums. State the numeric format of the overlap window
(a 16-bit sine or Hann table, Q15 multiply) and rounding rules, as the project does for the EQ. Mono files (the common audiobook case) run natively at half the work; for stereo, search on the
mid channel and apply the same splice to both.

### T2. Real-time budget and a degradation ladder

The plan has estimates but no worst-case execution budget per hop and no policy for overrun. Meters already have one (`meter_afford()`), and audio deserves the same. Define, per decode-plus-stretch pass:
worst-case cycles (decode frame + refill stalls + stretch hop + UI), and a ladder when headroom is short: first drop pause shortening, then reduce the search range, then fall back to varispeed, and
report each step on the Info page. Without this, a slow SD card or a heavy meter shows up as stutter rather than a graceful loss of a feature.

### T3. Cold code and the instruction cache

The plan proposes the stretch as cold code (fetched from PSRAM through the I-cache). **[READ]** `PHASE_G_SPEC.md`: the I-cache is 4 KiB (128 lines), a line fill costs about 253 cycles, and loops smaller
than the cache run at full speed. That supports a small inner loop. Two things are **[OPEN]**: whether code executing from on-chip RAM also goes through that cache (if it does, the decoder's large
loops already thrash it and a new hot loop competes), and the measured cost of a *tight inner loop* run as cold code, since the existing figure (about 1.7% of a frame) is for meter draw calls. Add a
Check test for it before committing to firmware-in-cold-code.

### T4. Resampler scope is too wide for a first version

Section 6.1 proposes cubic plus a 16-tap, 32-phase sinc "with a bank selected by ratio". For a downsampling ratio the kernel must lengthen with the ratio to keep the same quality, so a fixed
16-tap bank does not deliver the promised anti-alias at 2x-2.5x, and continuous speeds (0.05 steps) times four source rates is a large bank set. With audiobook tempo handled by WSOLA, the drain ratio
stays at or below 1 (44.1 to 48 is 0.919; 22.05 to 48 is 0.459), so a **first version can be up-conversion only** with one kernel and phase interpolation between the tabulated phases. Defer
decimation (varispeed above 1.09x, 88.2/96 kHz FLAC) to a later, separately-specified block. This also shrinks the ROM and the verification matrix.

### T5. Verification lacks an analog-domain measurement and acceptance thresholds

The plan verifies against a host model, then listens. The tone-SNR table in the main document is a model, and the ranking (nearest < linear < cubic < sinc) is trustworthy but the absolute numbers are
not what a listener hears. There is **no measurement of the Pocket's analog output anywhere in the project.** Add a loopback capture (line or headphone out into a sound card) with fixed test tones and
a swept sine, computing SINAD/THD+N and image levels, run on the current build as a baseline **before** any Cymo RTL, then on each phase. Define acceptance numbers up front (for example: no image above
a stated dB at 1, 5 and 10 kHz; no measurable change in level for the FLAT path unless F2 is deliberately changed). For the stretch, add a pitch-preservation check (estimated fundamental before and
after within 1%) and an owner listening pass on the LibriVox clip; there is no reliable objective score for naturalness.

### T6. Firmware structure: build Cymo as a real module, not another `.inc`

**[READ]** `player.c` is 9,738 lines and the `.inc` files "are not modules: they share one namespace and can read and write every global" (`FIRMWARE_MODULARIZATION_PLAN.md`). The project has a better
pattern already: a host-testable portable core plus a thin firmware shim (`chladni_core.h`, `meter_core.h`, `timg_core.h`, `library_core.h`, `flac_lpc_hw.h`). Put the stretch, pause detector,
soft clipper model and gain-ramp maths in `cymo_core.h` with an explicit state struct and a small API (`cymo_push_block`, `cymo_flush`, `cymo_set_tempo`, `cymo_afford`), compile it on the host, and check
it against golden vectors, exactly as the LPC and Chladni work did. Register writes for multi-field targets (gain target plus ramp step) need an atomic commit (shadow registers latched on one write) so
audio never sees half an update.

### T7. Seek, pause, track change and speed change semantics are not specified

Each of these must reset or ramp the stretch state and the FIFO consistently with the existing `fade_left` and `pcm_flush()` logic (`player.c:1008`, `6301`), and the late/early underrun classification the
Check depends on must still mean the same thing once samples pass through a stretch buffer. The Check's speed and stress tests need a tempo-mode variant. Position and elapsed time are derived from file
position (`player.c:859-866`), which stays correct; the remaining-time display under pause shortening was already flagged.

### T8. Bluetooth sink details that change the design

- Give each sink its own **frame strobe** (see C1). With the FPGA as I2S master (v1), the ESP receives clocks and must absorb its own crystal drift. With the ESP as master, the FPGA-side resampler is
  pulled by the ESP's LRCK. A pull architecture supports both; a shared free-running tick supports only the first.
- ESP32 A2DP examples I found read I2S mostly as master or from a codec; slave-mode capture from an external master is documented less **[OPEN, verify on a bench]**.
- Keep Bluetooth entirely off the critical path of the audiobook MVP. It is a second bitstream to fit, and every fit is expensive. The powered-cartridge questions (voltage selection, adapter-ID check, power
  budget) still gate any hardware test.

## 4. Things in the plan that hold up

- Fixing the resampler first, and treating it as mandatory (confirmed by the Pocket documentation: exactly 48 kHz).
- Not merging the three multiplier units, and unifying only the register convention.
- Probe-gating every RTL feature and reporting presence through a capabilities register instead of bumping `CORE_VERSION`.
- The collision register (MMIO partition with `test/720`, resource ledger), and the correction that correlation windows must be on-chip.
- Refusing to build an FFT-based stretcher, a linear-phase FIR EQ or 44.1 kHz output to the DAC.
- Bluetooth as a separate core package with the main cores' cart power left off.
- Recording every estimate as an estimate and listing what would replace it.

## 5. Risk register (top items)

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Free-running 48 kHz tick slips against LRCK (C1) | high if built as written | periodic click | pull-driven design |
| Firmware stretch overruns the CPU at high tempo with a large meter or slow SD | medium | audible stutter | degradation ladder (T2), headroom metric first |
| Stretch quality on non-speech or noisy recordings | medium | artifacts | speech-only scope, Off setting, listening pass |
| Buffer depth change silently breaks meter yield thresholds and start-up latency (C4) | high if done casually | wrong throttling, slower starts | shared depth constant, independent `PRIME` |
| Users expect resume and chapters (C3) | high | feature feels unfinished | decide scope before C7 |
| Thin hold slack on new RTL near `clk_sys` | medium | failed fit | three seeds, small blocks, audiobook MVP avoids RTL first |
| Cold-code inner loop slower than estimated (T3) | medium | tempo unaffordable | measure before committing |
| Analog output never measured, so improvements are asserted, not shown (T5) | high | wasted RTL | baseline capture first |

## 6. Recommended revised order

1. **C0**: real headroom metric; FLAC and MP3 stage readings; analog-output baseline capture; the 15-bit slot A/B; decide the audiobook scope (C3).
2. **C1**: `cymo_core.h` plus a single `cymo_push()`; size-neutral; host golden vectors.
3. **Firmware tempo and pause shortening** on the current bitstream, with a degradation ladder and a resume/last-position record.
4. **Resampler v1** (up-conversion, LRCK-pulled) as the first RTL block, then speech EQ preset.
5. Buffer depth (with `PRIME` and constants decoupled), output stage (gain ramp, soft clip, dither at the true final width).
6. Programmable EQ, then Bluetooth, then decimation and hi-res FLAC. Gapless as a separate large item.

## 7. Questions this review adds for the owner

1. **Audiobook scope:** are resume, per-book speed and chapter marks in this effort? Is M4B/AAC in scope or explicitly out?
2. **Order:** approve the revised order in section 6 (firmware tempo before any new RTL)?
3. **Analog capture:** can a loopback measurement of the Pocket's line or headphone output be made (or is there an existing capture)? It is the only way to demonstrate the resampler's benefit.
4. **Limiter:** soft clipper (recommended) or a lookahead limiter?

## 8. Corrections applied to the main document

1. Section 5: the gapless row no longer says "then it is small" (C2).
2. Section 6.3: notes that `PRIME` and the firmware depth constants must be decoupled before the depth changes (C4).

The other findings are recorded here, not silently folded into the main document, so the owner can accept or reject them first.

## 9. Owner decisions on the review (2026-09-29, B-425)

1. **Audiobook scope is minimal: pitch correction only.** That means pitch-preserving tempo change (the speed-up that keeps the narrator's voice natural). Resume, per-book speed, chapter marks,
   M4B/AAC, a speech EQ preset and **pause shortening** are all deferred to "much later". (Pause shortening was approved earlier; this decision supersedes it for now, its spec stays in the main document.)
2. **The minimal path is therefore:** the real headroom metric (C0a), one shared `cymo_push()` (C1), then the firmware tempo stretch. Nothing in it needs a new bitstream.
3. **Analog loopback measurement:** the owner will try; a how-to and test files are to be provided (not yet built).
4. **Soft clipper vs limiter:** explained to the owner; it belongs to the later output stage, so no decision is needed for the minimal path.

Everything else in the Cymo plan (resampler, output stage, deeper buffer, EQ, gapless, Bluetooth) is unchanged but parked behind this minimal path, to be scheduled by the owner.
