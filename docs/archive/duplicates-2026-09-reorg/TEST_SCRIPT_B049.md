# Test script: Phase G cold code (Diagnostic Build, cold-code variant)

Two stages. Stage 1 can run **now** on the existing P4 bitstream (fail-safe path). Stage 2 needs the G3 bitstream (instruction fetch) once it is built, gated and approved.

## Stage 1: `TAU PSRAM 14` on the OLD P4 bitstream (packaged, NOT installed)
Diagnostic Build with `TAU_COLD=1 TAU_COLD_CODE=1` (ROM `1f8dd65a...`, 169,348 B, heap gap 10,064 B), `Assets/tau_psram_14/common/tau-cold.bin` (12,040 B of cold code, data slot 6). The bitstream has no instruction-fetch feature.
Predictions [EST]: the cold image loads and verifies (size, layout id, CRC, PSRAM proof) in about 15-30 ms, then the boot refuses cold code because the bitstream does not report the feature; **no cold instruction is ever fetched**.
| # | Do | Expect |
|---|---|---|
| 1 | Start 14 (any media) | Boots as the normal Diagnostic Build |
| 2 | Menu > Settings > Info (S) | COLD IMAGE row `OFF E18` (cold image loaded, code refused: no instruction fetch in the bitstream) |
| 3 | Menu > Settings > Diagnostics > Tests > COLD CODE TEST (A) (S) | `NOT LOADED E18` |
| 4 | Tests > WINDOW TEST, READ/WRITE CYCLES | Same values as before (PASS 89; 48/56/335 and 31/38/344): the extra firmware changes nothing else |
Fail signs: a hang or reboot at start (would mean cold code was fetched: report immediately), any other E-code.

## Stage 2: the same firmware on the G3 bitstream (needs the seed pick, gates and approval)
Predictions [EST] (simulation numbers, `docs/AUDIT_TRAIL.md` B-047): Info `COLD IMAGE 12040 B <ms> MS CODE`; COLD CODE TEST `PASS 31.x C/W` (31.6 in simulation; 30-34 expected on the Pocket, the SDRAM-side load does not touch PSRAM); the fetch counters read non-zero; WINDOW TEST, READ/WRITE CYCLES, STRESS R1-R3 with late underruns 0 and worst window access 373 as before (the SDRAM-unchanged gate), a 30 minute soak with COLD CODE TEST repeated between passes (0 failures).
Order: (1) Info and COLD CODE TEST; (2) SDRAM-unchanged gate (Tests, Stress R1-R3); (3) 30 minute in-menu soak plus repeated COLD CODE TEST; (4) normal listening for 10 minutes; (5) decision on G4 (moving real cold code).
