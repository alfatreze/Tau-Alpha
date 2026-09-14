# Issue 009 — SDRAM diagnostic appears nonresponsive

**Status:** Root cause fixed; bounded Phase 1 acceptance passed
**Date:** 2026-09-14
**Evidence level:** **Pocket | host | code-review**

## Observed behavior

After the corrected diagnostic loaded, pressing A produced no visible change.
The user had Pocket developer diagnostics enabled. “Green” in the test
instruction meant the on-screen word `PASS`, not an LED or Pocket OS indicator;
the user did not report seeing that result.

## APF log evidence

The preserved log
`work/diagnostics/sdram/evidence/alfatreze.TAU_SDRAM_DIAG_20260914_073723.txt`
has SHA-256
`a750529af86d42c3f6004db6ea6832bb11ae2a0fd9f3fcb48d8f7cb5ab0bd4d9`.
It records Pocket OS 2.6 parsing all manifests, loading the bitstream, loading
the exact 4,872-byte ROM from `/Assets/tau_sdram_diag/common/tau.rom`, completing
the data slot, reaching `Run`, and successfully issuing Reset Exit. It contains
no APF load or setup error.

The line `Core says it is already running, even though it wasn't told to yet`
also occurs in the known-working normal TAU log, so it is not discriminating
evidence for this failure.

## Current diagnosis

APF setup is no longer the active blocker. The log cannot see firmware MMIO or
the SDRAM mailbox after reset exit.

`fw/sdram_diag.c` polls A only after `run_tests()` returns. If the mailbox stays
busy, each attempted operation waits up to 0.5 seconds, but the current code
does not abort the suite after the first timeout. A permanently busy mailbox
can therefore make roughly 247 attempted operations consume about two minutes,
during which A is intentionally not polled. This is a diagnostic-firmware
weakness whether or not it is the underlying SDRAM fault.

## Needed discriminator

Record the exact visible state before changing the ROM:

- black/no diagnostic UI;
- `STARTING`;
- `FIXED PATTERNS` or another progress label;
- `PASS`; or
- `FAIL` with its address/expected/actual fields.

A photo is preferable. If a progress label remains visible, replace the ROM
with a fail-fast build that aborts on first timeout and reports mailbox status.
If the display is black, investigate firmware execution/version interlock and
the framebuffer command path before interpreting SDRAM behavior.

## Fail-visible revision installed

The diagnostic was revised and installed on `/Volumes/Pock` after the black
screen report. It now draws its boot marker before the RTL version interlock,
shows expected/actual version values if that interlock fails, and stops the
test suite after the first timeout rather than accumulating approximately two
minutes of serial timeouts. The updated 5,568-byte ROM SHA-256 is:

`6ba3bbbdf0c730c522b6bb346440f87cb4bde2228f594bd175b77a7d83f062de`

The card-side ROM was byte-compared and flushed. A first write attempt was
blocked because the Pocket detached USB SD Access between mount check and copy;
it made no changes. The subsequent remount/copy/verification succeeded.

The revision adds a named version-mismatch framebuffer snapshot; the host
snapshot suite now contains 25 deterministic 400×360 RGB565 fixtures.

## Second discriminator revision — framebuffer warm-up and CPU heartbeat

The fail-visible ROM still produced a black Pocket screen after a verified
card remount and launch. This rules out the original two-minute mailbox wait
as the explanation for the observed black screen, but it does not prove an
SDRAM failure: the screen is drawn before any mailbox operation.

The next ROM waits 0.5 seconds from CPU reset before issuing any framebuffer
command. This is an evidence-backed hypothesis: the small diagnostic reaches
its first MMIO write much sooner than the normal player, whose larger startup
path naturally gives the SDRAM-backed framebuffer more time to initialise.
It then issues a read-only APF `GETFILE` request for its already loaded
firmware slot. Pocket developer logs record that target request, so it is a
CPU-execution heartbeat independent of visible video. The firmware does not
wait for the request, preventing a target bridge problem from hiding the UI.

The staged and card-byte-verified ROM SHA-256 is
`a34f3b994722ab7de216699f83a35bfba6f9b2cc857581f458669838d7bc169f`
(5,624 bytes). Its next launch has exactly two useful outcomes: a logged target
request with a black screen isolates the problem to framebuffer command/display
delivery; no request isolates it to firmware reset/execution before the test
itself.

## CPU execution confirmed; headless SDRAM preflight next

Two Pocket launches of the warm-up/heartbeat ROM remained black, but both new
developer logs contain `Target: New command [0190]` followed by the firmware
slot filename. This is **Pocket** evidence that the CPU executes after Reset
Exit and reaches the target-command MMIO bridge. Package selection, ROM load,
CPU reset, and the first instruction path are therefore no longer leading
causes of black output.

The next ROM deliberately sends no framebuffer commands. It emits up to four
ordered, read-only `0190` log checkpoints while doing one mailbox round-trip
at the safe 1 MiB address:

1. entered firmware `main`;
2. mailbox was initially idle;
3. `A55AA55A` write completed;
4. read completed and matched `A55AA55A`.

Its hash is `1d882cbda1958749f704e9f09bc354700a69b87b393e1ec725670fe2a4cbc249`
(672 bytes). This is intentionally headless; the next log, not the screen,
is the result. Four `0190` entries prove the smallest SDRAM mailbox
write/read. Fewer entries locate the first incomplete operation. Only after
that classification should the framebuffer/arbiter path be changed.

## Mailbox failure classified: arbitration acceptance deadlock

Two runs of the headless preflight generated exactly **two** `0190` entries:
firmware entry and initial mailbox-idle confirmation. Neither reached the
post-write checkpoint. This is **Pocket** evidence that the first mailbox
write remains busy; it is not a screen/UI observation.

Code review located the matching deadlock in `tau_sdram_arbiter.sv`.
`sdram_fb` defines `p0_available` as `state == IDLE && !port_req`, so it falls
in the same cycle the arbiter forwards a request. The arbiter nevertheless
used `p0_available` as both owner-latch and CPU-acceptance condition. The
controller queues the request, while the CPU bridge never leaves its request
state; it continuously forwards the request and cannot receive completion.

The correction makes the arbiter's owner latch the acceptance event,
independent of the controller's post-request availability. The regression test
now models the real `p0_available=0` condition while a request is present;
both SDRAM RTL tests pass. This change requires a fresh Quartus build and a
replacement RBF; no statement about Pocket success is valid until then.

The required clean Quartus build completed successfully on 2026-09-14. It
uses 5,832 / 18,480 ALMs (32%), 300 / 308 RAM blocks (97%), and has TNS 0 with
minimum setup slack 0.465 ns and minimum hold slack 0.120 ns. The new raw RBF
SHA-256 is `840f8d9187526521124864447d72619b533a1ca629d3a6168d003c4ee2082970`.
This clears the simulation/fit gate only; the headless Pocket preflight remains
the next required hardware gate. Its bit-reversed diagnostic package hash is
`49c1b6000fb88152ef6ee8aad4d3c4198039699f7ddb9bc0b68d3d35a2cd6340`;
that file and the unchanged headless ROM were byte-verified on `/Volumes/Pock`
after installation. The normal TAU core was not modified.

## Corrected-RBF Pocket result: mailbox preflight passes

Two Pocket runs of the corrected RBF generated all **four** ordered `0190`
checkpoints in logs `103939` and `104220`. This is **Pocket** evidence that
the diagnostic firmware entered, observed an idle mailbox, completed the safe
`A55AA55A` write, and completed a matching read. The arbiter acceptance fix
therefore resolves the previously observed first-write stall for this bounded
operation.

This is not yet full SDRAM acceptance: it does not cover the remaining data/
byte-lane patterns, soak behavior, or traffic concurrent with scanout/audio.
The headless preflight ROM is now superseded by the restored visible diagnostic
ROM, SHA-256
`8680a77ce89a20201d9d35470dc563d89bc6e54cf360a51798287012d366ae5d`
(5,688 bytes), byte-verified on the Pocket card. Its corrected RBF remains
unchanged. The next Pocket run should visibly show the diagnostic panel and
its pass/fail result.

## First visible bounded-suite pass

The restored diagnostic displayed `PASS` on Pocket with **183 readback
checks** and **0 failures**. The user-provided screen photo is retained as
`work/diagnostics/sdram/evidence/phase1-pass-pocket-photo.png`, SHA-256
`07fe7af04dcf473f2584ab07f2b9bb64d83f2f323070d1429c91acb57280e6ca`.
The matching Pocket log is retained as
`work/diagnostics/sdram/evidence/alfatreze.TAU_SDRAM_DIAG_20260914_105254.txt`,
SHA-256 `d6e8c7b0566700f8da99a70133772ff506d340d8878947095dae448b583cd881`.
It records the expected 5,688-byte ROM, Reset Exit, the post-reset heartbeat,
and clean unload.

This is run 1 of the ten-run acceptance matrix. It proves the bounded patterns,
including the safe-region byte-lane cases, on real hardware for one warm run;
it does not establish cold-boot reliability, soak behavior, or concurrent
framebuffer/audio contention.

## Bounded Phase 1 acceptance passed

The user completed the remaining four warm runs and five full power-off/cold
boot runs. Every reported the same `PASS`, 183 readback checks, and zero
failures; no display change or stall was observed. This completes the specified
five-warm/five-cold bounded Phase 1 acceptance matrix.

The latest finalised Pocket developer log is retained as
`work/diagnostics/sdram/evidence/alfatreze.TAU_SDRAM_DIAG_20260914_115930.txt`,
SHA-256 `d4db8dd0b27fa1135eb8d5052870b4a28659e2cee0af2d4309778b2c92dae902`.
APF logs establish the loaded 5,688-byte diagnostic, Reset Exit, heartbeat,
and clean unload, but do not count in-core A-triggered warm repetitions.
The ten-run screen results are therefore **Pocket user-observation evidence**,
not a claim inferred from the log count.

This closes only the bounded mailbox/byte-lane diagnostic gate. The next
separate gate is concurrent contention: sustained SDRAM traffic while high
bitrate MP3 playback and each visualizer run, with explicit audio-underrun and
framebuffer-stall evidence. No player data moves into SDRAM until that gate is
defined and passed.

## Impact

No normal player or SD content is affected. This run is not an SDRAM pass and
does not count toward the ten-run acceptance matrix.
