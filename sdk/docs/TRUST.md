# Trust: what "a meter runs with full access" means

A meter is **native code**. The player loads it into memory and calls it; there is no sandbox, no memory protection, no permission system. In plain words:

**What a meter can do**
- Read and write the player's memory, including the audio buffers and the player's own variables. It can crash the player or make it misbehave.
- Read and write the Pocket's hardware registers: the audio output, the draw engine, the timers.
- Reach anything else the processor can reach. Whether a hostile meter could reach the SD card through the hardware bridge is **unverified** (the project planned a hostile test pack on the host simulator and has not shown it either way).

**What protects you from a meter that is merely broken or slow** (these make "does not work" and "is slow" safe)
- A damaged or truncated file never runs (CRC); a meter built for another ABI or a newer Tau is refused and greyed.
- A cycle limit with strikes: three overruns switch the meter off for the session.
- A hardware watchdog: a meter that never returns is cut off after at most 300 ms, struck out at once, and replaced by the built-in Winamp Bars. The music keeps playing.
- Safe boot: if the core resets or the watchdog fires while a meter was active, the next boot starts on Winamp Bars.
- A newly installed meter is never selected automatically.

**What does not protect you:** none of the above stops a *hostile* meter. `tau-meter test` checks that a meter follows the rules; **a passing meter is not a safe meter.** The suite runs your meter on a model with a fixed input; a deliberately malicious program can behave well in the suite and badly on the Pocket.

**So:** install meters from people you trust or whose source you have read; prefer a package that includes its source (it is marked, and the source is in the `.tmeter`); check the hashes; know that "independent: not reviewed" means exactly that. A verified catalogue (reviewed, built from source by CI, signed index) is planned for people who want one; it is not built, and even then "verified" means "the signed index lists this exact package", never "safe".

**For authors:** you are handing people code that runs with full access. Say what the meter does, publish the source, never ship a binary you cannot rebuild from it, and do not ask people to install anything else.
