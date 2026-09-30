# Session handoff -- Cymo audio engine, branch `cymo` (2026-09-29/30)

Merge-safe: the branch only adds docs, `tools/lab/cymo_loopback.py` and `sim/` files; no firmware or RTL changed.

## What exists
- `docs/features/CYMO_AUDIO_ENGINE.md` (audit, plan C0-C8, Bluetooth cart design, collision register, skill review) and `docs/features/CYMO_AUDIO_ENGINE_REVIEW.md`. Owner scope: pitch-preserving tempo only; everything else parked.
- `tools/lab/cymo_loopback.py` (`gen` / `analyze` / `compare` / `selftest`), `sim/test_cymo_loopback.py` (in `make test-host`).
- `sim/tb_cymo_i2s_rate.v` + `sim/test_cymo_i2s_rate.py`: real `pcm_fifo` -> `sound_i2s` in iverilog; not in `make test-host` (needs iverilog, ~2 min). `--altera-mf <path to Quartus eda/sim_lib/altera_mf.v>` swaps in Intel's real `dcfifo` model (never committed; licensed).
- Audit entries B-417..B-432 in `docs/AUDIT_TRAIL.md`.

## The open finding (B-430/B-431, hardware + simulation)
Owner's analog loopback recordings (48 kHz capture, EQ FLAT, one gain for all): 48 kHz source is clean (52.6 dB SINAD, capture-noise limited). Every other rate is far worse than a plain hold predicts:
- 44.1 kHz: 10.8 dB SINAD (model 27.7), flat click-like spurs at 1 kHz + n x 3.9 kHz, tone 3.7 dB low.
- 24 kHz (2:1, fixed phase): image at 23 kHz -6.5 dBc (model -27.8). 32 kHz: -8.9 / -10.2 dBc (model -31.9). Level normal at both.
- No underruns. The simulation with my behavioural `dcfifo` gives exactly the modelled 27.7 dB, so the RTL logic around the FIFO is not the cause; the sliding-phase hypothesis is not supported (24 kHz fails too).
- Separate known item: `sound_i2s.v` sends `{a15, a15..a1}`, a 6 dB / 1 bit loss (finding F2), same at every rate.

## Next step (not done)
Run the simulation with the real Altera model on the Quartus VM (repo copy via `git archive` over the `-p 2222` SSH port, see `tools/vm_fit.py`):
`python3 sim/test_cymo_i2s_rate.py --altera-mf $(find / -name altera_mf.v -path "*sim_lib*" | head -1)`.
Still ~1 dB clean -> look after the serialiser / the recording path; much worse with flat spurs -> the real FIFO timing is the cause. Other pending owner items: none of the C0 firmware work (headroom metric etc.) was started.

## Merge note
`CLAUDE.md` and `docs/AUDIT_TRAIL.md` are append-only logs; expect a trivial conflict at the end of each if main also appended. Keep both sides.
