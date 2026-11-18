# FAQ

**Do I need a Pocket to develop a meter?** No. `tau-meter test` runs it on your computer and draws a preview. You do need a Pocket to be sure: the host cannot model the draw engine's timing.

**Which language?** C (GNU C11), freestanding: no libc, no C++, no floating point (the core is RV32IM). Assembly and other languages that compile to a RISC-V ELF would work in principle; the SDK is built and tested for C.

**Why is there no `printf`?** There is no operating system and no console. Draw text with `mtr_text`, and debug with the preview and by drawing values as numbers.

**Can I use floats?** Not sensibly: no FPU, and a soft-float library is not provided. Use integers and fixed point.

**How many meters can be installed?** Up to 64 in one bin, one 64 KiB window, one meter in memory at a time.

**Can two meters share state?** No. A meter loses its state when it is switched away; nothing persists except what the player saves (the selected meter and the user's tuning of its parameters).

**Can my meter save its own settings?** Not by itself. Declare parameters and presets; the player remembers the user's tuning of them.

**My meter needs the SDRAM / the audio samples.** The host table gives you what the player offers: the spectrum, peaks, RMS and statistics, and the mailbox and engine calls. Anything more is a firmware change (the ABI grows by appending: ask the Tau Alpha project).

**What is the difference between the template and `tau-alpha-meters`?** The template is the starting point for a new meter (it uses `tau_meter.h`). `tau-alpha-meters` is the collection of the seven meters that ship with Tau, built from the player's own source with its internal headers; read them for ideas, do not start from them.

**What is `compose`?** It makes the user bin `tau-meters.bin` from packages. Tau Omega is the planned tool for this; `tau-meter compose` does it by hand today.

**Why does my meter work in the suite and fail on the Pocket?** The suite estimates cycles (floor, not forecast) and models the engine's calls, not its timing. Measure on the Pocket and set `max_cycles` from that.

**Does a passing suite mean the meter is safe?** No ([TRUST.md](TRUST.md)).

**Can I sell a meter?** The licence is yours to choose (the template defaults to MIT). Tau's own licence is MIT; a meter is your code.

**Can I copy code from another meter?** Only under its licence. Do not copy copyleft (GPL) code into an MIT meter.

**I changed my parameters and users lost their settings.** That is `params_rev`: raise it when the set changes; the player drops a tuning that does not match.

**The id in the template is a placeholder, can I keep it?** No: every meter needs its own id (`tau-meter id new`); two meters with one id are one meter to the player.
